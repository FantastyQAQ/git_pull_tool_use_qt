/**
 * @file        gitmanager.cpp
 * @brief       Git 仓库扫描与批量拉取功能实现
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.5
 * @date        20261113
 * @note        扫描在独立 QThread 中并发执行；拉取由异步子进程串行驱动，失败/超时/中止均能收尾
 *
 * @copyright   Copyright (c) 2026 by OLE, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261106    <td>1.0       <td>lch       <td>创建
 * <tr><td>20261106    <td>1.1       <td>lch       <td>扫描移入后台线程；去重 remote 查询；fetch 加 --no-tags
 * <tr><td>20261108    <td>1.2       <td>lch       <td>按代码审查完善中止保护、结果判定、超时与队列收尾
 * <tr><td>20261108    <td>1.3       <td>lch       <td>单仓 git 调用由 6 次合并为 2 次，扫描改为多线程并发采集
 * <tr><td>20261113    <td>1.4       <td>lch       <td>扫描先 fetch 再统计差异，脏仓库同样显示落后/领先数
 * <tr><td>20261113    <td>1.5       <td>lch       <td>拉取改为 --progress 实时解析当前阶段百分比与传输流量
 * </table>
 */
#include "gitmanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <functional>

namespace {

// 各类 git 调用的超时时间（毫秒），集中定义便于统一调整
constexpr int kQuickTimeoutMs  = 3000;    // 本地元数据查询（remote、rev-parse 等）
constexpr int kStatusTimeoutMs = 5000;    // status（含分支/上游/ahead-behind）
constexpr int kFetchTimeoutMs  = 120000;  // 网络 fetch
constexpr int kPullTimeoutMs   = 180000;  // 网络 pull（超时后强杀子进程）
constexpr int kKillWaitMs      = 3000;    // 强杀后等待进程退出的时间
constexpr int kMaxScanDepth    = 6;       // 扫描根目录下的最大递归层数
constexpr int kMaxErrorChars   = 120;     // 日志中单条错误信息的最大字符数
constexpr int kScanThreadCount = 4;       // 扫描并发线程数（每线程一个仓库，受磁盘与 CPU 制约）

/**
 * @brief       用固定数量的线程并发执行一批按下标划分的任务
 *
 * @param[in]   taskCount 任务总数，任务以下标 [0, taskCount) 标识
 * @param[in]   abortFlag 中止标记，为 1 时各线程尽快停止领取新任务
 * @param[in]   body 任务体，参数为任务下标；调用方需保证不同下标不写同一份数据
 * @return      none
 * @details     用于扫描阶段并发执行 git 子进程：git 调用是阻塞式 waitForFinished，
 *              单线程串行会让每个仓库的进程启动开销累加，改成多线程后总耗时接近
 *              单仓耗时；线程数固定，避免仓库很多时创建大量线程；
 *              本函数会阻塞到所有线程结束，只应在扫描工作线程中调用
 * @doxygen_sig_fp 4b7f0c1d9e2a6b31
 */
void run_parallel(int taskCount, const QAtomicInt &abortFlag, const std::function<void(int)> &body)
{
    if (taskCount <= 0)
        return;

    // 任务数少于线程数时按任务数开线程，避免空转线程
    const int threadCount = qMin(kScanThreadCount, taskCount);

    // 原子游标把下标分发给各线程，天然实现动态负载均衡（快的线程多领任务）
    std::atomic<int> cursor(0);
    QVector<QThread *> threads;
    threads.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i) {
        QThread *thread = QThread::create([&]() {
            for (;;) {
                // 每领任务前检查中止标记，用户点停止后不再启动新的 git 子进程
                if (abortFlag.load())
                    return;
                const int index = cursor.fetch_add(1);
                if (index >= taskCount)
                    return;
                body(index);
            }
        });
        threads.append(thread);
        thread->start();
    }

    // 等全部线程收尾后再返回，保证回传信号不会晚于 scan_finished
    for (QThread *thread : threads) {
        thread->wait();
        delete thread;
    }
}

/**
 * @brief       创建禁用交互输入和分页器的 Git 子进程环境
 * @return      配置好的子进程环境
 * @doxygen_sig_fp b92254a09521c2e3
 */
QProcessEnvironment make_git_environment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_PAGER"), QStringLiteral("cat"));
    return env;
}

/**
 * @brief       截取适合表格和日志展示的 Git 错误信息
 * @param[in]   {result} const GitResult& 待提取的 Git 执行结果
 * @return      截断后的错误文本
 * @doxygen_sig_fp bd34838b1485e3d2
 */
QString short_error(const GitResult &result)
{
    QString message = result.err.trimmed();
    if (message.isEmpty())
        message = result.out.trimmed();
    if (message.isEmpty())
        message = QStringLiteral("退出码 %1").arg(result.exitCode);
    if (message.length() > kMaxErrorChars)
        message = message.left(kMaxErrorChars) + QStringLiteral("…");
    return message;
}

/**
 * @brief       根据领先/落后提交数生成展示用的同步差异文本
 *
 * @param[in]   ahead 领先上游的提交数
 * @param[in]   behind 落后上游的提交数
 * @return      差异文本；同时领先和落后时显示为分叉
 * @details     仅负责文案，不做拉取判定；拉取资格由调用方根据 ahead/behind 决定
 * @doxygen_sig_fp 68ccd7e5866822f6
 */
QString describe_sync_state(int ahead, int behind)
{
    if (ahead > 0 && behind > 0)
        return QStringLiteral("⇅ 分叉：领先 %1 / 落后 %2").arg(ahead).arg(behind);
    if (behind > 0)
        return QStringLiteral("↓ 落后 %1 个提交").arg(behind);
    if (ahead > 0)
        return QStringLiteral("↑ 领先 %1 个提交").arg(ahead);
    return QStringLiteral("✓ 已是最新");
}

/**
 * @brief       status --porcelain=v2 --branch 解析结果
 */
struct StatusSnapshot
{
    QString branch;    // 当前分支名；分离 HEAD 时为 "(detached)"
    QString oid;       // HEAD 提交哈希（空仓库为 "(initial)"）
    QString upstream;  // 上游分支名，未配置时为空
    int     ahead = 0; // 领先上游提交数
    int     behind = 0;// 落后上游提交数
    bool    hasCounts = false; // 是否解析到 branch.ab（上游引用缺失时为 false）
    bool    dirty = false;     // 已跟踪文件是否有改动
};

/**
 * @brief       解析 git status --porcelain=v2 --branch 的输出
 *
 * @param[in]   out 命令标准输出
 * @param[out]  snap 解析结果
 * @return      true 表示解析到分支信息（命令输出合法）
 * @details     v2 格式以 "# " 开头的为分支头信息，其余非空行均为文件状态；
 *              branch.ab 缺失或含 "?" 表示上游引用已失效，交由调用方判为统计失败
 * @doxygen_sig_fp 7d3b1c5a82f04e6d
 */
bool parse_status_v2(const QString &out, StatusSnapshot &snap)
{
    bool sawBranch = false;

    // 逐行扫描：头信息取值，其余非空行说明工作区/索引有改动
    const QStringList lines = out.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QLatin1String("# branch.head "))) {
            snap.branch = line.mid(14);
            sawBranch = true;
        } else if (line.startsWith(QLatin1String("# branch.oid "))) {
            snap.oid = line.mid(13);
        } else if (line.startsWith(QLatin1String("# branch.upstream "))) {
            snap.upstream = line.mid(18);
        } else if (line.startsWith(QLatin1String("# branch.ab "))) {
            // 形如 "+2 -3"，分别表示领先与落后提交数
            const QStringList parts = line.mid(12).split(QLatin1Char(' '), QString::SkipEmptyParts);
            if (parts.size() == 2 && parts[0].startsWith(QLatin1Char('+')) &&
                parts[1].startsWith(QLatin1Char('-'))) {
                bool aheadOk = false;
                bool behindOk = false;
                snap.ahead = parts[0].mid(1).toInt(&aheadOk);
                snap.behind = parts[1].mid(1).toInt(&behindOk);
                snap.hasCounts = aheadOk && behindOk;
            }
        } else if (!line.startsWith(QLatin1Char('#'))) {
            snap.dirty = true;
        }
    }
    return sawBranch;
}

/**
 * @brief       检查仓库是否处于 merge / cherry-pick / revert / bisect / rebase 过程中
 *
 * @param[in]   repoPath 仓库根目录
 * @return      true 表示有未完成的操作
 * @details     纯文件存在性判断，不启动 git 子进程
 * @doxygen_sig_fp 1c8e5f4a93b7d02e
 */
bool check_repo_in_progress(const QString &repoPath)
{
    static const QStringList markers = {
        QStringLiteral("MERGE_HEAD"), QStringLiteral("CHERRY_PICK_HEAD"),
        QStringLiteral("REVERT_HEAD"), QStringLiteral("BISECT_LOG"),
        QStringLiteral("rebase-merge"), QStringLiteral("rebase-apply")
    };

    const QString gitDir = repoPath + QStringLiteral("/.git");
    for (const QString &marker : markers) {
        if (QFile::exists(gitDir + QLatin1Char('/') + marker))
            return true;
    }
    return false;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════
// 构造 / 析构
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief       构造 GitManager：注册跨线程元类型并初始化拉取看门狗
 *
 * @details     RepoInfo 及其容器经 queued 信号跨线程传递，必须先注册元类型；
 *              看门狗超时强杀当前拉取进程，finished 回调将超时结果计入队列推进
 * @param[out]  {parent} QObject* Qt 父对象，负责生命周期管理
 * @doxygen_sig_fp f5e45c2cd3bbfea9
 */
GitManager::GitManager(QObject *parent)
    : QObject(parent)
    , m_abortFlag(0)
{
    // 扫描信号从工作线程发往主线程 UI，走 queued 连接，需要注册自定义类型
    qRegisterMetaType<RepoInfo>("RepoInfo");
    qRegisterMetaType<QVector<RepoInfo>>("QVector<RepoInfo>");

    // 拉取看门狗：单个 pull 超时则强杀子进程，让队列能继续推进
    m_pullWatchdog = new QTimer(this);
    m_pullWatchdog->setSingleShot(true);
    connect(m_pullWatchdog, &QTimer::timeout, this, [this]() {
        // 超时强杀：kill 后 finished 信号仍会触发，标记超时便于结果判定
        if (m_pullProc) {
            m_pullTimedOut = true;
            kill_current_pull();
        }
    });
}

/**
 * @brief       析构 GitManager：结束拉取进程并等待扫描线程退出
 *
 * @details     防止窗口关闭时工作线程还在向已销毁对象发信号导致崩溃；
 *              正在运行的 pull 子进程显式结束，避免孤儿 git 进程
 * @doxygen_sig_fp c2533acf15b1d336
 */
GitManager::~GitManager()
{
    // 结束正在运行的拉取子进程（QProcess 析构也会杀进程，这里显式处理更清晰）
    m_abortFlag.store(1);
    if (m_pullProc) {
        m_pullProc->kill();
        m_pullProc->waitForFinished(kKillWaitMs);
        m_pullProc->deleteLater();
        m_pullProc = nullptr;
    }

    if (m_scanThread) {
        // 线程仍在跑：中止标记让其尽快退出，再同步等待，确保不再有信号发出
        if (m_scanThread->isRunning())
            m_scanThread->wait();
        delete m_scanThread;
        m_scanThread = nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════════════
// 扫描
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief       启动后台线程异步扫描根目录下的带远端 Git 仓库
 *
 * @param[in]   rootDir 扫描根目录
 * @param[in]   fetchRemote 是否在采集状态前同步远程引用
 * @return      本轮扫描代次，调用方用它过滤过期回传
 * @details     每次进入先递增扫描代次，使上一轮残留信号失效；
 *              再等待并清理上一轮线程，实际扫描逻辑在 scan_worker 中
 * @doxygen_sig_fp a85617b7b9bb7f9a
 */
int GitManager::start_scan(const QString &rootDir, bool fetchRemote)
{
    // 递增代次：上一轮线程若仍有残留信号，其 scanId 与新代次不符会被主线程丢弃
    ++m_scanId;

    // 复位中止标记：扫描与拉取共用一个标记，进入前必须清零
    m_abortFlag.store(0);

    // 上一轮扫描线程若仍在收尾，先请求中止并等待退出，避免两个扫描线程并发
    if (m_scanThread) {
        if (m_scanThread->isRunning()) {
            m_abortFlag.store(1);
            m_scanThread->wait();
            m_abortFlag.store(0);
        }
        delete m_scanThread;
        m_scanThread = nullptr;
    }

    // QThread::create 包一个 lambda 作为扫描线程主体，捕获本轮代次和同步选项
    const int scanId = m_scanId;
    m_scanThread = QThread::create(
        [this, scanId, rootDir, fetchRemote]() { scan_worker(scanId, rootDir, fetchRemote); });
    m_scanThread->start();
    return scanId;
}

/**
 * @brief       扫描线程主体：BFS 找 .git、并发预检远端、并发采集状态
 *
 * @param[in]   scanId 本轮扫描代次，所有回传信号都携带它
 * @param[in]   rootDir 扫描根目录
 * @param[in]   fetchRemote 是否执行网络 fetch
 * @return      none
 * @details     在工作线程执行，run_git 的同步阻塞只占用本线程不卡 UI；
 *              目录遍历串行（I/O 顺序性更好），仓库级的 git 调用按 kScanThreadCount
 *              并发执行，单仓只起 2 个（fetch 勾选时 3 个）子进程；
 *              所有并发线程结束后才发 scan_finished，保证结果先于结束信号到达 UI
 * @doxygen_sig_fp 3e39441d576c4b74
 */
void GitManager::scan_worker(int scanId, const QString &rootDir, bool fetchRemote)
{
    // 根目录为空或不存在时直接结束，错误信息经信号回传给 UI
    if (rootDir.isEmpty()) {
        emit scan_finished(scanId, false);
        return;
    }
    QDir dir(rootDir);
    if (!dir.exists()) {
        emit log_message(QStringLiteral("❌ 目录不存在：%1").arg(rootDir), Qt::red);
        emit scan_finished(scanId, false);
        return;
    }

    // 第一步：BFS 找出根目录下全部 .git 目录（纯文件系统遍历）
    const QStringList gitDirs = find_git_repos(rootDir, kMaxScanDepth);
    emit log_message(QStringLiteral("发现 .git 目录：%1 个").arg(gitDirs.size()), Qt::black);

    // 第二步：并发读取每个仓库的远端地址，筛出带远端的仓库
    // 预分配定长容器并只写各自下标；先取一次 data() 指针，避免多线程同时触发
    // QVector 的隐式共享检查，写入互不重叠的位置不需要加锁
    const int candidateCount = gitDirs.size();
    QVector<QString> repoPaths(candidateCount);
    QVector<QString> remoteUrls(candidateCount);
    QString *pathSlots = repoPaths.data();
    QString *urlSlots = remoteUrls.data();

    run_parallel(candidateCount, m_abortFlag, [&](int index) {
        // .git 的父目录即仓库根
        const QString repoPath = QFileInfo(gitDirs.at(index)).absolutePath();
        pathSlots[index] = repoPath;
        urlSlots[index] = pick_remote_url(repoPath);
    });

    // 用户已请求中止时不再继续采集，直接按中止收尾
    if (m_abortFlag.load()) {
        emit scan_finished(scanId, true);
        return;
    }

    // 按原顺序组装骨架列表，保持表格顺序稳定；无远端的本地仓库无法拉取，跳过
    QVector<RepoInfo> repos;
    repos.reserve(candidateCount);
    for (int i = 0; i < candidateCount; ++i) {
        if (remoteUrls.at(i).isEmpty())
            continue;
        repos.append(make_skeleton(repoPaths.at(i), rootDir, remoteUrls.at(i)));
    }
    emit log_message(QStringLiteral("带远端的仓库：%1 / 总计 %2")
        .arg(repos.size()).arg(candidateCount), Qt::black);

    // 把骨架列表先回传主线程建表，随后逐行回填
    emit scan_list_ready(scanId, repos);

    // 第三步：并发采集详细状态，每个仓库完成后立即回传对应行
    // 注意：repos 已在上方按值传给 scan_list_ready，多线程中只做只读访问（at 为 const，
    // 不会触发写时复制），因此各线程读同一容器是安全的；回传后本容器不再需要写回
    run_parallel(repos.size(), m_abortFlag, [&](int index) {
        const RepoInfo &skeleton = repos.at(index);
        const RepoInfo info = examine_repo(skeleton.absolutePath, rootDir,
                                          skeleton.remoteUrl, fetchRemote);
        // 逐仓回传：主线程按 repoIndex 回填对应行，完成一个就能看到一行
        emit scan_result(scanId, index, info);
        emit log_message(QStringLiteral("  ✔ %1  [%2]  %3")
            .arg(info.path, info.branch, info.status), Qt::black);
    });

    // 扫描收尾：aborted 标记告知主线程本次是否被中止
    emit scan_finished(scanId, m_abortFlag.load() != 0);
}

/**
 * @brief       由预检结果构造仓库骨架信息（未采集不可拉取）
 *
 * @param[in]   repoPath 仓库根目录
 * @param[in]   rootDir 扫描根目录，用于计算相对展示路径
 * @param[in]   remoteUrl 预检得到的远端地址
 * @return      填好路径/远程地址、scanned 为 false 的骨架结构体
 * @details     骨架先行回传让表格立刻可见，详细状态由 examine_repo 补齐；
 *              scanned 为 false 时拉取流程会跳过该仓库，防止拉取未检查过的脏仓库
 * @doxygen_sig_fp 77b25cc155afdd07
 */
RepoInfo GitManager::make_skeleton(const QString &repoPath, const QString &rootDir,
                                   const QString &remoteUrl)
{
    RepoInfo info;
    info.absolutePath = repoPath;
    info.path = relative_display_path(repoPath, rootDir);
    info.remoteUrl = remoteUrl;
    info.branch = QStringLiteral("…");
    info.status = QStringLiteral("扫描中…");
    // 其余字段使用结构体默认值；scanned=false 是中止场景下的安全兜底
    return info;
}

/**
 * @brief       选取仓库用于展示与判定的远端地址
 *
 * @param[in]   repoPath 仓库根目录
 * @return      远端地址（已 trim）；仓库没有任何远端时返回空串
 * @details     用一条 git config 命令取回全部远端 URL（形如 "remote.origin.url <地址>"），
 *              避免先 remote 再 remote get-url 的两次进程启动；
 *              优先使用名为 origin 的远端，不存在则取输出中的第一条；
 *              远端列表为空即视为无远端，调用方应跳过该仓库
 * @doxygen_sig_fp 2d38a546fd9a41de
 */
QString GitManager::pick_remote_url(const QString &repoPath)
{
    // --get-regexp 输出 "remote.<名字>.url <地址>"，名字含点号时也能正确解析
    const GitResult listed = run_git(
        repoPath, {QStringLiteral("config"), QStringLiteral("--get-regexp"),
                   QStringLiteral("^remote\\..*\\.url$")}, kQuickTimeoutMs);
    if (!listed.ok)
        return QString(); // 无远端时 config 返回 1，直接视为没有远端

    QString firstUrl;
    const QStringList lines = listed.out.split(QLatin1Char('\n'), QString::SkipEmptyParts);
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        const int spacePos = line.indexOf(QLatin1Char(' '));
        if (spacePos <= 0)
            continue; // 没有空格说明不是预期的 key value 结构，跳过

        const QString key = line.left(spacePos);
        const QString url = line.mid(spacePos + 1).trimmed();
        if (url.isEmpty())
            continue;

        // 键名形如 remote.origin.url，取中间一段与 origin 比较
        if (key.mid(7, key.length() - 7 - 4) == QLatin1String("origin"))
            return url;
        if (firstUrl.isEmpty())
            firstUrl = url;
    }
    return firstUrl;
}

/**
 * @brief       计算仓库相对扫描根目录的展示路径
 *
 * @param[in]   repoPath 仓库根目录（绝对路径）
 * @param[in]   rootDir 扫描根目录
 * @return      相对路径；仓库即根目录时返回 "."
 * @details     用 QDir::relativeFilePath 按路径语义计算，避免按字符长度截断前缀
 * @doxygen_sig_fp 70db4fe34b6fb2cd
 */
QString GitManager::relative_display_path(const QString &repoPath, const QString &rootDir)
{
    const QString rel = QDir::cleanPath(QDir(rootDir).relativeFilePath(QDir::cleanPath(repoPath)));
    return rel.isEmpty() ? QStringLiteral(".") : rel;
}

/**
 * @brief       BFS 查找根目录下 maxDepth 层内的全部 .git 目录
 *
 * @param[in]   rootDir 扫描起始根目录
 * @param[in]   maxDepth 最大递归深度（相对层数）
 * @return      命中的 .git 目录绝对路径列表
 * @details     不跟随符号链接与目录联接；按 canonicalFilePath 去重，避免重复或越界扫描；
 *              深度按 BFS 层级计算，逐层入队
 * @doxygen_sig_fp 5f6a891dd48a4728
 */
QStringList GitManager::find_git_repos(const QString &rootDir, int maxDepth)
{
    QStringList result;

    // 剪枝目录名：这些目录不可能是用户的工作仓库，直接跳过
    static const QStringList prune = {
        QStringLiteral(".git"), QStringLiteral(".svn"), QStringLiteral(".hg"),
        QStringLiteral(".bzr"), QStringLiteral("node_modules"), QStringLiteral(".venv"),
        QStringLiteral("venv"), QStringLiteral("__pycache__"), QStringLiteral(".idea"),
        QStringLiteral(".vs"), QStringLiteral(".gradle"), QStringLiteral(".m2"),
        QStringLiteral("site-packages"), QStringLiteral("dist-packages"),
        QStringLiteral(".dart_tool"), QStringLiteral(".conda")
    };

    // BFS 队列元素为 (目录路径, 当前层级)，层级从根目录的 0 开始
    struct Item { QString path; int depth; };
    QVector<Item> queue;
    queue.append({QDir::cleanPath(rootDir), 0});
    QSet<QString> visited;
    int head = 0;

    while (head < queue.size()) {
        const Item current = queue[head++];
        QDir d(current.path);
        if (!d.exists())
            continue;

        // canonicalPath 为空时退回 cleanPath，保证不存在的路径不会阻塞其它分支
        const QString canonical = d.canonicalPath();
        const QString visitKey = canonical.isEmpty() ? current.path : canonical;
        if (visited.contains(visitKey))
            continue;
        visited.insert(visitKey);

        // .git 在 Windows 上带隐藏属性，过滤器必须包含 Hidden；NoSymLinks 避免目录联接
        const QFileInfoList entries = d.entryInfoList(
            QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        for (const QFileInfo &entry : entries) {
            const QString name = entry.fileName();
            if (prune.contains(name)) {
                // .git 本身被剪枝不展开，但说明当前目录是仓库根，记录其路径
                if (name == QLatin1String(".git"))
                    result.append(entry.absoluteFilePath());
                continue;
            }
            // 子目录层级比当前目录深一层；超过上限则不再入队
            if (current.depth + 1 < maxDepth)
                queue.append({entry.absoluteFilePath(), current.depth + 1});
        }
    }
    return result;
}

/**
 * @brief       采集单个仓库的分支、状态与 ahead/behind 信息
 *
 * @param[in]   repoPath 仓库根目录
 * @param[in]   rootDir 扫描根目录，用于计算相对展示路径
 * @param[in]   remoteUrl 预检得到的远端地址
 * @param[in]   fetchRemote 是否先同步远程引用
 * @return      填充完成的仓库状态结构体（scanned 恒为 true）
 * @details     勾选同步时先 fetch，再执行 git status --porcelain=v2 --branch，
 *              保证 ahead/behind 基于最新远程引用；分支、上游、工作区脏也来自这次 status，
 *              单仓只起 2 个 git 子进程（勾选同步时再加 1 次 fetch）；
 *              先做纯文件系统的进行中检查，命中即提前返回，省掉子进程开销；
 *              工作区有改动的仓库同样显示差异，但 pullable 恒为 false；
 *              任何失败都明确反映在状态中，异常仓库 pullable 恒为 false
 * @doxygen_sig_fp 7c8a0f76d2348713
 */
RepoInfo GitManager::examine_repo(const QString &repoPath, const QString &rootDir,
                                  const QString &remoteUrl, bool fetchRemote)
{
    RepoInfo info;
    info.absolutePath = repoPath;
    info.path = relative_display_path(repoPath, rootDir);
    info.remoteUrl = remoteUrl;
    // 已采集但尚未通过全部检查，pullable 保持 false，异常状态不会进入拉取队列
    info.scanned = true;

    // 先同步远程引用：status 里的 ahead/behind 依赖远程跟踪引用，必须在 fetch 之后读取。
    // fetch 失败时仍继续读取本地信息，最终状态以 fetch 失败为准，不误报“已是最新”
    QString fetchError;
    if (fetchRemote) {
        fetchError = fetch_remote_refs(repoPath);
        if (!fetchError.isEmpty())
            emit log_message(QStringLiteral("  ❌ %1：fetch 失败：%2").arg(info.path, fetchError), Qt::red);
    }

    // 一次 status 调用同时拿到分支、上游、ahead/behind 与工作区脏状态
    // --untracked-files=no：未跟踪文件不影响 pull，避免大仓库遍历未跟踪目录
    const GitResult statusRes = run_git(
        repoPath, {QStringLiteral("status"), QStringLiteral("--porcelain=v2"),
                   QStringLiteral("--branch"), QStringLiteral("--untracked-files=no")},
        kStatusTimeoutMs);
    if (!statusRes.ok) {
        info.status = QStringLiteral("状态检查失败：%1").arg(short_error(statusRes));
        return info;
    }

    StatusSnapshot snap;
    if (!parse_status_v2(statusRes.out, snap)) {
        info.status = QStringLiteral("状态检查失败：输出格式异常");
        return info;
    }

    // 分离 HEAD：branch.head 为 (detached)，改用短哈希展示
    if (snap.branch == QLatin1String("(detached)")) {
        info.detached = true;
        info.branch = run_git(repoPath,
            {QStringLiteral("rev-parse"), QStringLiteral("--short"), QStringLiteral("HEAD")},
            kQuickTimeoutMs).out.trimmed();
        if (info.branch.isEmpty())
            info.branch = QStringLiteral("HEAD");
        info.status = QStringLiteral("分离 HEAD");
        return info;
    }
    info.branch = snap.branch;

    // merge/rebase 等进行中的仓库跳过（拉取会产生冲突）；纯文件检查，无需子进程
    if (check_repo_in_progress(repoPath)) {
        info.inProgress = true;
        info.status = QStringLiteral("合并/变基进行中");
        return info;
    }

    // 无上游分支的仓库无法 pull，同样跳过
    if (snap.upstream.isEmpty()) {
        info.noUpstream = true;
        info.status = QStringLiteral("无上游");
        return info;
    }
    info.upstream = snap.upstream;

    // branch.ab 只在本地存在上游引用时出现；缺失说明上游引用已被删除
    if (!snap.hasCounts) {
        info.status = QStringLiteral("统计失败：上游引用缺失");
        return info;
    }
    info.ahead = snap.ahead;
    info.behind = snap.behind;

    // fetch 失败时远程引用可能已过期：优先展示失败原因，避免误报“已是最新”或“可更新”
    if (!fetchError.isEmpty()) {
        info.status = QStringLiteral("fetch 失败：%1").arg(fetchError);
        return info; // pullable 保持 false
    }

    // 差异始终展示；工作区有改动的仓库只显示差异，不参与拉取（避免覆盖用户改动）
    const QString diffText = describe_sync_state(info.ahead, info.behind);
    if (snap.dirty) {
        info.dirty = true;
        info.status = QStringLiteral("工作区有改动 · %1").arg(diffText);
        return info;
    }
    info.status = diffText;

    // 本地领先或分叉时不自动 pull，避免产生合并；纯落后或已最新时可安全 fast-forward
    if (info.ahead > 0)
        return info;
    info.pullable = true;
    return info;
}

/**
 * @brief       同步仓库的远程跟踪引用（git fetch --prune --no-tags）
 *
 * @param[in]   repoPath 仓库根目录
 * @return      失败时返回截断后的错误信息；成功时返回空串
 * @details     只更新 refs/remotes 下的远程跟踪引用，不改动工作区与当前分支，
 *              因此有未提交改动的仓库也能安全同步；
 *              调用方必须在读取 ahead/behind 之前调用，否则统计仍沿用旧引用
 * @doxygen_sig_fp ca3ad17cb1460803
 */
QString GitManager::fetch_remote_refs(const QString &repoPath)
{
    const GitResult fetchRes = run_git(
        repoPath, {QStringLiteral("fetch"), QStringLiteral("--prune"),
                   QStringLiteral("--no-tags"), QStringLiteral("--quiet")}, kFetchTimeoutMs);
    return fetchRes.ok ? QString() : short_error(fetchRes);
}

/**
 * @brief       在指定目录同步执行 git 命令并返回完整结果
 *
 * @param[in]   workDir git 工作目录
 * @param[in]   args git 子命令及参数列表
 * @param[in]   timeoutMs 超时毫秒数，超时强杀进程
 * @return      GitResult，包含 stdout、stderr、退出码与是否成功
 * @details     已禁用交互式凭据输入与分页器；本函数只应在工作线程中调用；
 *              启动失败或超时时 ok 为 false，exitCode 为 -1
 * @doxygen_sig_fp 03700da56eb55b14
 */
GitResult GitManager::run_git(const QString &workDir, const QStringList &args, int timeoutMs)
{
    GitResult res;
    QProcess proc;
    proc.setProcessEnvironment(make_git_environment());
    proc.setWorkingDirectory(workDir);
    proc.start(QStringLiteral("git"), args);

    // 单独等待启动，区分 git 不在 PATH 与命令执行超时
    if (!proc.waitForStarted(kQuickTimeoutMs)) {
        res.err = QStringLiteral("无法启动 git，请确认已安装并加入 PATH");
        return res;
    }

    // 等待结束；超时则强杀，防止扫描被无响应的 git 子进程卡死
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(kKillWaitMs);
        res.err = QStringLiteral("git 命令超时");
        return res;
    }

    res.out = QString::fromUtf8(proc.readAllStandardOutput());
    res.err = QString::fromUtf8(proc.readAllStandardError());
    res.exitCode = proc.exitCode();
    res.ok = (proc.exitStatus() == QProcess::NormalExit && res.exitCode == 0);
    return res;
}

// ═══════════════════════════════════════════════════════════════════════
// 拉取（成员状态机：start_pull → process_next_pull → 进程回调 → finish_pull_item）
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief       保存拉取队列并启动串行拉取
 *
 * @param[in]   indices 待拉取仓库在 repos 中的索引队列
 * @param[in]   repos 仓库状态快照（调用方当前扫描结果）
 * @return      none
 * @details     队列与快照保存在成员变量中，由 process_next_pull 逐个推进；
 *              每项完成后统一发出进度，启动失败、超时和中止也会进入同一收尾路径
 * @doxygen_sig_fp 9e94f376270c63c6
 */
void GitManager::start_pull(const QVector<int> &indices, const QVector<RepoInfo> &repos)
{
    // 重置中止标记和本轮队列状态，避免上一轮拉取残留影响新任务
    m_abortFlag.store(0);
    m_pullQueue = indices;
    m_pullRepos = repos;
    m_pullCursor = 0;
    m_pullTimedOut = false;
    m_pullDoneBytes = 0;
    m_pullRepoBytes = 0;

    // 经事件循环投递，保证 start_pull 返回后才开始子进程操作
    QTimer::singleShot(0, this, &GitManager::process_next_pull);
}

/**
 * @brief       推进拉取队列：启动下一个仓库的 pull，或在队列结束/中止时收尾
 *
 * @return      none
 * @details     每次只有一个 pull 子进程在运行；启动前记录 HEAD 哈希，
 *              结束后比较哈希判断是否真的前进，未采集或索引无效的仓库直接跳过
 * @doxygen_sig_fp 1ca98a8a5900870c
 */
void GitManager::process_next_pull()
{
    // 队列处理完毕或用户请求中止：通知 UI 恢复，不在中止后启动新进程
    if (m_pullCursor >= m_pullQueue.size() || m_abortFlag.load()) {
        const bool aborted = m_abortFlag.load() != 0;
        emit log_message(aborted ? QStringLiteral("═══ 拉取已中止 ═══")
                                 : QStringLiteral("═══ 拉取结束 ═══"),
                         aborted ? QColor(200, 150, 0) : QColor(0, 120, 0));
        emit pull_finished(aborted);
        m_pullQueue.clear();
        m_pullRepos.clear();
        m_pullCursor = 0;
        return;
    }

    const int repoIndex = m_pullQueue[m_pullCursor];
    if (repoIndex < 0 || repoIndex >= m_pullRepos.size()) {
        // 防御性处理非法索引：不访问越界快照，记录后推进队列
        emit log_message(QStringLiteral("  ⚠ 跳过：仓库索引无效"), QColor(200, 150, 0));
        ++m_pullCursor;
        emit pull_progress(m_pullCursor, m_pullQueue.size());
        QTimer::singleShot(0, this, &GitManager::process_next_pull);
        return;
    }

    const RepoInfo &repo = m_pullRepos[repoIndex];
    emit log_message(QStringLiteral("[%1/%2] 拉取 %3…")
        .arg(m_pullCursor + 1).arg(m_pullQueue.size()).arg(repo.path), Qt::black);

    // 安全兜底：未采集或任何检查未通过的仓库不拉取，直接记为跳过
    if (!repo.scanned || !repo.pullable || repo.detached || repo.inProgress ||
        repo.noUpstream || repo.dirty || repo.ahead > 0) {
        emit log_message(QStringLiteral("  ⚠ 跳过：尚未完成状态检查"), QColor(200, 150, 0));
        emit repo_result(repoIndex, QStringLiteral("⚠ 未检查，已跳过"), QColor(200, 150, 0), false);
        ++m_pullCursor;
        emit pull_progress(m_pullCursor, m_pullQueue.size());
        QTimer::singleShot(0, this, &GitManager::process_next_pull);
        return;
    }

    // 记录拉取前的 HEAD，pull 结束后比较，决定是刚更新还是已是最新
    const GitResult before = run_git(
        repo.absolutePath, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}, kQuickTimeoutMs);
    if (!before.ok || before.out.trimmed().isEmpty()) {
        GitResult res;
        res.err = QStringLiteral("读取当前 HEAD 失败：%1").arg(short_error(before));
        finish_pull_item(repoIndex, res);
        return;
    }
    m_headBefore = before.out.trimmed();
    m_pullTimedOut = false;

    // 当前仓库的进度与流量从零开始统计，进度条先归零
    m_pullRepoBytes = 0;
    m_pullLineBytes.clear();
    m_pullErrText.clear();
    emit pull_repo_progress(0);

    // 异步启动 git pull --ff-only --progress；看门狗限制单次拉取耗时
    QProcess *proc = new QProcess(this);
    m_pullProc = proc;
    proc->setWorkingDirectory(repo.absolutePath);
    proc->setProcessEnvironment(make_git_environment());

    // 进度输出走 stderr，边运行边读取，避免管道写满导致 git 阻塞
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc]() {
        if (m_pullProc == proc)
            consume_pull_stderr(proc, false);
    });

    // 正常结束：统一交给 finish_pull_process 回收进程并推进队列
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, repoIndex](int exitCode, QProcess::ExitStatus status) {
        if (m_pullProc != proc)
            return;
        finish_pull_process(repoIndex, exitCode, status, QString());
    });

    // 启动失败时 finished 不一定发出，使用同一收尾入口兜底推进队列
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, repoIndex](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_pullProc != proc)
            return;
        finish_pull_process(repoIndex, -1, QProcess::CrashExit,
                            QStringLiteral("无法启动 git，请确认已安装并加入 PATH"));
    });

    proc->start(QStringLiteral("git"),
                {QStringLiteral("pull"), QStringLiteral("--ff-only"), QStringLiteral("--progress")});
    m_pullWatchdog->start(kPullTimeoutMs);
}

/**
 * @brief       统一回收当前 pull 子进程并生成单项结果
 *
 * @param[in]   repoIndex 仓库在 m_repos 中的索引
 * @param[in]   exitCode 进程退出码
 * @param[in]   exitStatus 进程退出状态
 * @param[in]   startError 启动失败错误文本，正常结束为空
 * @return      none
 * @details     finished 与 errorOccurred 共用此入口；先读取输出再释放进程，
 *              通过 m_pullProc 判重，避免启动失败后重复推进队列
 * @doxygen_sig_fp 31acb429c69e3422
 */
void GitManager::finish_pull_process(int repoIndex, int exitCode,
                                     QProcess::ExitStatus exitStatus, const QString &startError)
{
    QProcess *proc = m_pullProc;
    if (!proc)
        return;

    m_pullWatchdog->stop();
    // 先吃掉残留的 stderr（含末尾无换行的最后一行），再组装结果
    consume_pull_stderr(proc, true);
    GitResult res;
    res.exitCode = exitCode;
    res.out = QString::fromUtf8(proc->readAllStandardOutput());
    res.err = m_pullErrText;
    if (!startError.isEmpty())
        res.err = startError;
    if (m_pullTimedOut)
        res.err = QStringLiteral("git pull 超时，已终止当前仓库");
    res.ok = startError.isEmpty() && !m_pullTimedOut &&
             exitStatus == QProcess::NormalExit && exitCode == 0;

    proc->deleteLater();
    m_pullProc = nullptr;
    finish_pull_item(repoIndex, res);
}

/**
 * @brief       单仓库 pull 结束：判定结果、回传信号并推进到下一个仓库
 *
 * @param[in]   repoIndex 仓库在 m_repos 中的索引
 * @param[in]   res pull 的执行结果
 * @return      none
 * @details     成功时比较 pull 前后的 HEAD 哈希：哈希变化视为刚更新，否则为已是最新；
 *              不再依赖 git 的本地化输出文本，失败时展示截断后的错误原因
 * @doxygen_sig_fp 181aae20d079d5da
 */
void GitManager::finish_pull_item(int repoIndex, const GitResult &res)
{
    // 当前仓库已结束：流量并入本轮累计，进度条补满到 100%
    m_pullDoneBytes += m_pullRepoBytes;
    m_pullRepoBytes = 0;
    emit pull_repo_progress(100);
    emit pull_traffic(m_pullDoneBytes);

    if (res.ok) {
        // 比较拉取前后的 HEAD：只有提交真正前进才算刚更新
        const GitResult after = run_git(m_pullRepos[repoIndex].absolutePath,
            {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}, kQuickTimeoutMs);
        const QString headAfter = after.out.trimmed();
        if (after.ok && headAfter != m_headBefore) {
            emit log_message(QStringLiteral("  ✔ 更新成功"), QColor(0, 180, 0));
            emit repo_result(repoIndex, QStringLiteral("✔ 刚更新"), QColor(0, 180, 0), true);
        } else if (after.ok) {
            emit log_message(QStringLiteral("  ✓ 已是最新"), QColor(0, 150, 0));
            emit repo_result(repoIndex, QStringLiteral("✓ 已是最新"), QColor(0, 150, 0), true);
        } else {
            emit log_message(QStringLiteral("  ❌ 拉取后读取 HEAD 失败"), Qt::red);
            emit repo_result(repoIndex, QStringLiteral("✘ 状态未知"), Qt::red, false);
        }
    } else if (m_abortFlag.load()) {
        // 用户中止当前进程：状态明确标为中止，不把它误报为网络或认证失败
        emit log_message(QStringLiteral("  ⏹ 当前仓库已中止"), QColor(200, 150, 0));
        emit repo_result(repoIndex, QStringLiteral("⏹ 已中止"), QColor(200, 150, 0), false);
    } else {
        // 失败：优先展示 stderr，截断避免单条日志过长
        emit log_message(QStringLiteral("  ❌ 失败: %1").arg(short_error(res)), Qt::red);
        emit repo_result(repoIndex, QStringLiteral("✘ 拉取失败"), Qt::red, false);
    }

    // 进度按已完成数发出；队列收尾由 process_next_pull 统一处理
    ++m_pullCursor;
    emit pull_progress(m_pullCursor, m_pullQueue.size());
    QTimer::singleShot(100, this, &GitManager::process_next_pull);
}

/**
 * @brief       强制结束当前 pull 子进程（用于超时或中止）
 *
 * @return      none
 * @details     kill 后 finished 信号仍会触发（exitStatus 为 CrashExit），由其走统一收尾路径
 * @doxygen_sig_fp 22b2a6ee64291408
 */
void GitManager::kill_current_pull()
{
    if (m_pullProc)
        m_pullProc->kill();
}

/**
 * @brief       读取拉取进程的 stderr 并逐行解析进度输出
 *
 * @param[in]   proc 当前运行的 git pull 进程
 * @param[in]   flushTail true 表示进程已结束，残留内容需作为最后一行处理
 * @return      none
 * @details     git 的进度行用回车符原地刷新，因此回车与换行都视为行结束；
 *              未结束的尾巴保存在 m_pullLineBytes 中，等待下一次读取拼接，避免截断多字节字符；
 *              非进度行累积到 m_pullErrText，失败时用于展示真正的错误原因
 * @doxygen_sig_fp 7af15fc1807ac242
 */
void GitManager::consume_pull_stderr(QProcess *proc, bool flushTail)
{
    m_pullLineBytes.append(proc->readAllStandardError());

    // 进度行交给解析函数更新界面，其余行记入错误文本
    auto handle_line = [this](const QString &line) {
        if (!parse_pull_progress_line(line))
            m_pullErrText += line.trimmed() + QLatin1Char('\n');
    };

    // 扫描已有字节，遇到 \r 或 \n 就切出一整行
    int lineStart = 0;
    for (int i = 0; i < m_pullLineBytes.size(); ++i) {
        const char c = m_pullLineBytes.at(i);
        if (c != '\r' && c != '\n')
            continue;
        handle_line(QString::fromUtf8(m_pullLineBytes.mid(lineStart, i - lineStart)));
        lineStart = i + 1;
    }
    // 删除已处理的部分，只保留最后一段未结束的内容
    m_pullLineBytes.remove(0, lineStart);

    // 进程已结束时残留内容不会再有行结束符，直接当作完整的一行处理
    if (flushTail && !m_pullLineBytes.isEmpty()) {
        handle_line(QString::fromUtf8(m_pullLineBytes));
        m_pullLineBytes.clear();
    }
}

/**
 * @brief       解析单行 git pull 进度输出，更新当前阶段百分比与累计流量
 *
 * @param[in]   rawLine 一行原始输出（已按 \r 或 \n 切分）
 * @return      true 表示该行是进度/服务端统计信息，不应计入错误文本
 * @details     百分比取最新值，阶段切换时 git 会从 0 重新计数，直接展示即可；
 *              传输量取本仓库见过的最大值（字节数只增不减），
 *              Receiving 与 Unpacking 行会重复报告同一数值，取最大值即可去重
 * @doxygen_sig_fp d9130b1eaa8e1117
 */
bool GitManager::parse_pull_progress_line(const QString &rawLine)
{
    // 当前阶段百分比，如 "Receiving objects:  45% (45/100)"；远端阶段带 "remote: " 前缀
    static const QRegularExpression percentRe(
        QStringLiteral("^(?:remote: )?[^:]+:\\s+(\\d{1,3})%"));
    // 传输量，如 "Receiving objects: 100% (3/3), 1.20 MiB | 1.00 MiB/s, done."；小数据量时单位为 bytes
    static const QRegularExpression sizeRe(QStringLiteral(
        "^(?:Receiving|Unpacking) objects:.*\\(\\d+/\\d+\\),\\s*(\\d+(?:\\.\\d+)?)\\s*(bytes|KiB|MiB|GiB)\\b"));

    const QString line = rawLine.trimmed();
    if (line.isEmpty())
        return true;

    // remote: 开头的行是服务端的计数/压缩/传输统计，不属于错误原因
    const bool isRemoteLine = line.startsWith(QLatin1String("remote:"));

    const QRegularExpressionMatch percentMatch = percentRe.match(line);
    if (percentMatch.hasMatch())
        emit pull_repo_progress(qMin(100, percentMatch.captured(1).toInt()));

    const QRegularExpressionMatch sizeMatch = sizeRe.match(line);
    if (sizeMatch.hasMatch()) {
        // 把 git 的二进制单位换算为字节
        const QString unit = sizeMatch.captured(2);
        double multiplier = 1.0;
        if (unit == QLatin1String("KiB"))
            multiplier = 1024.0;
        else if (unit == QLatin1String("MiB"))
            multiplier = 1024.0 * 1024.0;
        else if (unit == QLatin1String("GiB"))
            multiplier = 1024.0 * 1024.0 * 1024.0;

        // 只有数值增长时才发出信号，减少界面刷新次数
        const qint64 bytes = static_cast<qint64>(sizeMatch.captured(1).toDouble() * multiplier);
        if (bytes > m_pullRepoBytes) {
            m_pullRepoBytes = bytes;
            emit pull_traffic(m_pullDoneBytes + m_pullRepoBytes);
        }
    }

    return isRemoteLine || percentMatch.hasMatch() || sizeMatch.hasMatch();
}

/**
 * @brief       请求中止当前扫描或拉取
 *
 * @return      none
 * @details     扫描在当前仓库采集完成后退出；拉取立即终止当前子进程，
 *              由 finished 回调收尾，不再启动后续仓库
 * @doxygen_sig_fp 6c115d4d666073fd
 */
void GitManager::request_abort()
{
    m_abortFlag.store(1);
    // 立即停止当前网络子进程，避免停止按钮只等待当前仓库自然结束
    kill_current_pull();
}
