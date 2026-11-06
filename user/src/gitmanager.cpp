/**
 * @file        gitmanager.cpp
 * @brief       Git 仓库扫描与批量拉取功能实现
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.1
 * @date        20261106
 * @note        扫描在独立 QThread 同步执行（不卡 UI）；拉取由 QTimer 驱动串行异步队列，结果经信号回传
 *
 * @copyright   Copyright (c) 2026 by OLE, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261106    <td>1.0       <td>lch       <td>创建
 * <tr><td>20261106    <td>1.1       <td>lch       <td>扫描移入后台线程；去重 remote 查询；fetch 加 --no-tags
 * </table>
 */
#include "gitmanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QThread>
#include <QTimer>
#include <QRegularExpression>

// ═══════════════════════════════════════════════════════════════════════
// 构造 / 析构
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief       构造 GitManager，初始化中止标记并注册跨线程元类型
 *
 * @param[out]  {parent} QObject* Qt 父对象，用于自动释放
 * @return      none（构造/析构函数，无返回值）
 * @retval      none
 * @details     RepoInfo 及其容器经 queued 信号跨线程传递，必须先注册元类型，
 *              否则运行期报「Cannot queue arguments of type」
 * @doxygen_sig_fp f5e45c2cd3bbfea9
 */
GitManager::GitManager(QObject *parent)
    : QObject(parent)
    , m_abortFlag(0) // 0 = 正常执行，1 = 用户请求中止
{
    // 扫描信号从工作线程发往主线程 UI，走 queued 连接，需要注册自定义类型
    qRegisterMetaType<RepoInfo>("RepoInfo");
    qRegisterMetaType<QVector<RepoInfo>>("QVector<RepoInfo>");
}

/**
 * @brief       析构 GitManager：若扫描线程仍在运行，请求中止并等待其退出
 *
 * @return      none（构造/析构函数，无返回值）
 * @retval      none
 * @details     防止主窗口关闭时工作线程还在向已销毁对象发信号导致崩溃
 * @doxygen_sig_fp c2533acf15b1d336
 */
GitManager::~GitManager()
{
    if (m_scanThread) {
        // 线程仍在跑：置位中止标记让其尽快退出，再同步等待，确保不再有信号发出
        if (m_scanThread->isRunning()) {
            m_abortFlag.store(1);
            m_scanThread->wait();
        }
        delete m_scanThread;
        m_scanThread = nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════════════
// 扫描
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief       启动后台线程异步扫描根目录下的 GitHub 仓库
 *
 * @param[in]   {rootDir} const QString& 扫描根目录
 * @return      none
 * @retval      none
 * @details     每次进入先复位中止标记（修复停止拉取后标志残留导致自动重扫被
 *              立即中止的问题），再清理上一轮已结束的线程对象；实际扫描逻辑
 *              在 scan_worker 中，过程与结果经信号回传主线程
 * @doxygen_sig_fp dedef3835e626d2b
 */
void GitManager::start_scan(const QString &rootDir)
{
    // 复位中止标记：扫描与拉取共用一个标记，进入前必须清零
    m_abortFlag.store(0);

    // 上一轮扫描线程若仍在收尾（理论上 UI 已禁用扫描按钮，防御性处理），
    // 先请求中止并等待退出，避免两个扫描线程并发
    if (m_scanThread) {
        if (m_scanThread->isRunning()) {
            m_abortFlag.store(1);
            m_scanThread->wait();
            m_abortFlag.store(0); // 等待结束后恢复标记，供新一轮扫描使用
        }
        m_scanThread->deleteLater();
        m_scanThread = nullptr;
    }

    // QThread::create 包一个 lambda 作为扫描线程主体；
    // 线程对象挂在 this 下，析构函数里统一等待并释放
    m_scanThread = QThread::create([this, rootDir]() { scan_worker(rootDir); });
    m_scanThread->start();
}

/**
 * @brief       扫描线程主体：BFS 找 .git、预检 GitHub、逐仓采集状态
 *
 * @param[in]   {rootDir} const QString& 扫描根目录
 * @return      none
 * @retval      none
 * @details     在工作线程执行，run_git 的同步阻塞只占用本线程不卡 UI；
 *              每仓只查一次 remote url（原实现预检+详查各查一次），全部
 *              过程经信号回传；循环内响应 m_abortFlag 中止请求
 * @doxygen_sig_fp 9d835f3762684542
 */
void GitManager::scan_worker(const QString &rootDir)
{
    bool aborted = false; // 是否因用户请求中止而提前结束

    // 根目录为空或不存在时直接结束，错误信息经信号回传给 UI
    if (rootDir.isEmpty()) {
        emit scan_finished(false);
        return;
    }
    QDir dir(rootDir);
    if (!dir.exists()) {
        emit log_message(QStringLiteral("❌ 目录不存在：%1").arg(rootDir), Qt::red);
        emit scan_finished(false);
        return;
    }

    // 第一步：BFS 找出根目录下全部 .git 目录（限 6 层，纯文件系统遍历）
    QStringList gitDirs = find_git_repos(rootDir, 6);
    emit log_message(QStringLiteral("发现 .git 目录：%1 个").arg(gitDirs.size()), Qt::black);

    // 第二步：预检每个 .git 目录的 origin 地址，筛出 GitHub 仓库并构造骨架信息
    //         （每仓只执行一次 remote get-url，替代原预检+详查各一次的重复调用）
    QVector<RepoInfo> repos;
    for (const QString &gitDir : gitDirs) {
        // 用户请求中止时提前退出预检循环
        if (m_abortFlag.load()) {
            aborted = true;
            break;
        }

        QString repoPath = QFileInfo(gitDir).absolutePath(); // .git 的父目录即仓库根
        QString url = run_git(repoPath, {"remote", "get-url", "origin"}, 3000);
        if (!url.contains("github.com", Qt::CaseInsensitive))
            continue; // 非 GitHub 仓库（gitee 等）跳过

        repos.append(make_skeleton(repoPath, rootDir, url.trimmed()));
    }
    emit log_message(QStringLiteral("GitHub 仓库：%1 / 总计 %2")
        .arg(repos.size()).arg(gitDirs.size()), Qt::black);

    // 把骨架列表先回传主线程建表（各行状态显示"扫描中…"），随后逐行回填
    emit scan_list_ready(repos);

    // 第三步：逐仓采集详细状态（分支/脏检查/fetch/落后数），结果逐个回传
    for (int i = 0; i < repos.size(); ++i) {
        // 用户请求中止时提前退出采集循环
        if (m_abortFlag.load()) {
            aborted = true;
            break;
        }

        repos[i] = examine_repo(repos[i].absolutePath, rootDir, repos[i].remoteUrl);
        emit scan_result(i, repos[i]);
        emit log_message(QStringLiteral("  ✔ %1  [%2]  %3")
            .arg(repos[i].path, repos[i].branch, repos[i].status), Qt::black);
    }

    // 扫描收尾：aborted 标记告知主线程本次是否被中止
    emit scan_finished(aborted);
}

/**
 * @brief       由预检结果构造仓库骨架信息（状态显示"扫描中…"）
 *
 * @param[in]   {repoPath} const QString& 仓库根目录
 * @param[in]   {rootDir} const QString& 扫描根目录，用于计算相对展示路径
 * @param[in]   {remoteUrl} const QString& 预检得到的 origin 地址（已 trim）
 * @return      填好路径/远程地址、其余字段为默认值的骨架结构体
 * @retval      none
 * @details     骨架先行回传让表格立刻可见，详细状态由 examine_repo 补齐
 * @doxygen_sig_fp 77b25cc155afdd07
 */
RepoInfo GitManager::make_skeleton(const QString &repoPath, const QString &rootDir,
                                   const QString &remoteUrl)
{
    RepoInfo info;
    info.absolutePath = repoPath;
    // path 为相对扫描根的展示路径，去掉根前缀与开头分隔符
    info.path = repoPath;
    info.path.remove(0, rootDir.length());
    while (info.path.startsWith('/') || info.path.startsWith('\\'))
        info.path.remove(0, 1);
    if (info.path.isEmpty()) info.path = ".";

    info.remoteUrl  = remoteUrl;
    info.isGitHub   = true; // 预检已确认 origin 指向 github.com
    info.branch     = QStringLiteral("…");
    info.status     = QStringLiteral("扫描中…");
    info.behind     = 0;
    info.ahead      = 0;
    info.dirty      = false;
    info.detached   = false;
    info.inProgress = false;
    info.noUpstream = false;
    return info;
}

/**
 * @brief       BFS 查找根目录下 maxDepth 层内的全部 .git 目录
 *
 * @param[in]   {rootDir} const QString& 扫描起始根目录
 * @param[in]   {maxDepth} int 最大递归深度（相对层数）
 * @return      命中的 .git 目录绝对路径列表
 * @retval      none
 * @details     命中剪枝名单的子目录不展开；.git 命中时记录其父目录为仓库根
 * @doxygen_sig_fp 5f6a891dd48a4728
 */
QStringList GitManager::find_git_repos(const QString &rootDir, int maxDepth)
{
    QStringList result;
    QStringList queue;
    queue.append(rootDir);

    // 剪枝目录名
    static const QStringList prune = {
        ".git", ".svn", ".hg", ".bzr", "node_modules", ".venv", "venv",
        "__pycache__", ".idea", ".vs", ".gradle", ".m2",
        "site-packages", "dist-packages", ".dart_tool", ".conda"
    };

    // BFS：从根目录逐层展开子目录，命中 .git 时记录其路径
    while (!queue.isEmpty()) {
        QString dir = queue.takeFirst();
        QDir d(dir);
        if (!d.exists()) continue;

        // .git 目录在 Windows 上带隐藏属性，过滤器必须包含 QDir::Hidden 才能列出
        QFileInfoList entries = d.entryInfoList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            QString name = entry.fileName();
            if (prune.contains(name)) {
                // .git 本身被剪枝不展开，但说明当前目录是仓库根，记录下来
                if (name == ".git") {
                    result.append(entry.absoluteFilePath());
                }
                continue;
            }
            // 深度由相对路径的目录分隔符计数控制，超过上限的子目录不再展开
            QString rel = entry.absoluteFilePath();
            rel.remove(0, rootDir.length());
            int depth = rel.count('/') + rel.count('\\');
            if (depth < maxDepth) {
                queue.append(entry.absoluteFilePath());
            }
        }
    }
    return result;
}

/**
 * @brief       采集单个仓库的分支、状态与 ahead/behind 信息
 *
 * @param[in]   {repoPath} const QString& 仓库根目录
 * @param[in]   {rootDir} const QString& 扫描根目录，用于计算相对展示路径
 * @param[in]   {remoteUrl} const QString& 预检得到的 origin 地址，本函数不再重复查询
 * @return      填充完成的仓库状态结构体
 * @retval      none
 * @details     依次检查分离 HEAD、merge/rebase 进行中、无上游、工作区脏，命中即提前返回；
 *              否则 fetch（--no-tags 减少传输量）后统计 ahead/behind
 * @doxygen_sig_fp b483c16af54e03f2
 */
RepoInfo GitManager::examine_repo(const QString &repoPath, const QString &rootDir,
                                  const QString &remoteUrl)
{
    RepoInfo info;
    info.absolutePath = repoPath;
    // path 为相对扫描根的展示路径，去掉根前缀与开头分隔符
    info.path = repoPath;
    info.path.remove(0, rootDir.length());
    while (info.path.startsWith('/') || info.path.startsWith('\\'))
        info.path.remove(0, 1);
    if (info.path.isEmpty()) info.path = ".";

    // 远程地址复用预检结果，节省一次 git 子进程调用
    info.remoteUrl  = remoteUrl;
    info.isGitHub   = true;
    info.dirty      = false;
    info.detached   = false;
    info.inProgress = false;
    info.noUpstream = false;
    info.behind     = 0;
    info.ahead      = 0;

    // 分支名：返回 "HEAD" 说明处于分离 HEAD 状态，改用短哈希展示
    info.branch = run_git(repoPath, {"rev-parse", "--abbrev-ref", "HEAD"}, 3000).trimmed();
    if (info.branch == "HEAD") {
        info.detached = true;
        info.branch   = run_git(repoPath, {"rev-parse", "--short", "HEAD"}, 3000).trimmed();
        info.status   = QStringLiteral("分离 HEAD");
        return info;
    }

    // merge/rebase 进行中的仓库直接跳过（拉取会产生冲突）
    QStringList checkFiles = {
        "MERGE_HEAD", "CHERRY_PICK_HEAD", "REVERT_HEAD",
        "BISECT_LOG", "rebase-merge", "rebase-apply"
    };
    QString gitDir = repoPath + "/.git";
    for (const QString &f : checkFiles) {
        if (QFile::exists(gitDir + "/" + f)) {
            info.inProgress = true;
            info.status = QStringLiteral("合并/变基进行中");
            return info;
        }
    }

    // 无上游分支的仓库无法 pull，同样跳过
    info.upstream = run_git(repoPath, {"rev-parse", "--abbrev-ref", "@{u}"}, 5000).trimmed();
    if (info.upstream.isEmpty() || info.upstream.contains("fatal:")) {
        info.noUpstream = true;
        info.status = QStringLiteral("无上游");
        return info;
    }

    // 脏检查：工作区有改动的仓库不自动 pull（避免覆盖用户改动）
    QString statusOut = run_git(repoPath, {"status", "--porcelain", "--untracked-files=no"}, 5000);
    if (!statusOut.trimmed().isEmpty()) {
        info.dirty = true;
        info.status = QStringLiteral("工作区有改动");
        return info;
    }

    // fetch 后用 rev-list 统计本地相对上游的 ahead/behind
    // --no-tags：标签同步对本工具无意义，跳过可减少网络传输量
    run_git(repoPath, {"fetch", "--prune", "--no-tags", "--quiet", "origin"}, 120000);

    QString countStr = run_git(repoPath,
        {"rev-list", "--left-right", "--count",
         QString("HEAD...%1").arg(info.upstream)}, 10000).trimmed();

    // 输出形如 "0 3"（空格分隔的 ahead behind）
    QStringList parts = countStr.split(QRegularExpression("\\s+"));
    if (parts.size() >= 2) {
        info.ahead  = parts[0].toInt();
        info.behind = parts[1].toInt();
    }

    if (info.behind == 0)
        info.status = QStringLiteral("✓ 已是最新");
    else
        info.status = QStringLiteral("↓ 落后 %1 个提交").arg(info.behind);

    return info;
}

/**
 * @brief       在指定目录同步执行 git 命令并返回标准输出
 *
 * @param[in]   {workDir} const QString& git 工作目录
 * @param[in]   {args} const QStringList& git 子命令及参数列表
 * @param[in]   {timeoutMs} int 超时毫秒数，超时强杀进程
 * @return      git 标准输出（UTF-8 解码）
 * @retval      none
 * @details     已禁用交互式凭据输入与分页器，避免子进程挂起拖死扫描；
 *              本函数只应在工作线程中调用（同步阻塞等待子进程结束）
 * @doxygen_sig_fp ec916cf4fcd75af9
 */
QString GitManager::run_git(const QString &workDir, const QStringList &args, int timeoutMs)
{
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("GIT_TERMINAL_PROMPT", "0"); // 禁止交互式凭据输入，超时即失败
    env.insert("GIT_PAGER", "cat");         // 禁用分页器，避免子进程挂起等待输入
    proc.setProcessEnvironment(env);
    proc.setWorkingDirectory(workDir);
    proc.start("git", args);
    // 超时则强杀，防止扫描被无响应的 git 子进程卡死
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(3000);
    }
    return QString::fromUtf8(proc.readAllStandardOutput());
}

// ═══════════════════════════════════════════════════════════════════════
// 拉取
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief       按索引队列串行执行 git pull --ff-only
 *
 * @param[in]   {indices} const QVector<int>& 待拉取仓库在 repos 中的索引队列
 * @param[in]   {repos} const QVector<RepoInfo>& 仓库状态列表（调用方当前扫描结果）
 * @return      none
 * @retval      none
 * @details     QTimer 驱动队列逐仓库异步拉取，进度与结果经信号回传；请求中止后当前仓库完成时退出并发出 pull_finished
 * @doxygen_sig_fp 9e94f376270c63c6
 */
void GitManager::start_pull(const QVector<int> &indices, const QVector<RepoInfo> &repos)
{
    // 重置中止标记，开始新一轮拉取
    m_abortFlag.store(0);

    // 队列/仓库列表/游标都放到堆上，供异步 lambda 跨事件循环持有
    //（QTimer 回调触发时 start_pull 早已返回，栈上对象不可用）
    auto *queue = new QVector<int>(indices);
    auto *repoList = new QVector<RepoInfo>(repos);
    int *idx = new int(0);

    // 用单次触发 QTimer 驱动索引队列：每个仓库的 QProcess 结束后再次启动
    // timer，从而实现串行拉取（QtConcurrent 里跑 QProcess 有线程亲和性问题，故不用）
    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [=]() mutable {
        // 队列处理完毕或用户请求中止：清理堆资源并通知 UI 恢复
        if (*idx >= queue->size() || m_abortFlag.load()) {
            emit log_message(QStringLiteral("═══ 拉取结束 ═══"), QColor(0, 120, 0));
            emit pull_finished();
            delete queue;
            delete repoList;
            delete idx;
            timer->deleteLater();
            return;
        }

        int i = (*queue)[*idx];
        emit pull_progress(*idx + 1, queue->size()); // 进度：正在处理第 done 个仓库

        const RepoInfo &r = (*repoList)[i];
        emit log_message(QStringLiteral("[%1/%2] 拉取 %3…")
            .arg(*idx + 1).arg(queue->size()).arg(r.path), Qt::black);

        // 异步启动 git pull --ff-only，完成后在 finished 回调里推进队列
        QProcess *proc = new QProcess(this);
        proc->setWorkingDirectory(r.absolutePath);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("GIT_TERMINAL_PROMPT", "0"); // 禁止 git 弹出交互式凭据输入导致卡死
        proc->setProcessEnvironment(env);

        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [=](int exitCode, QProcess::ExitStatus) {
            // 解析输出并回传本仓库结果，然后推进到下一个仓库
            handle_pull_result(proc, i, exitCode);
            proc->deleteLater();
            (*idx)++;
            timer->start(100); // 稍作间隔后拉取下一个仓库
        });

        proc->start("git", {"pull", "--ff-only", "--quiet"});
    });

    timer->start(0); // 立即触发第一轮
}

/**
 * @brief       解析单仓库 pull 输出并发送日志与结果信号
 *
 * @param[in]   {proc} QProcess* 已结束的 git pull 进程，用于读取输出
 * @param[in]   {repoIndex} int 仓库在表格/队列中的索引
 * @param[in]   {exitCode} int git pull 退出码，0 表示成功
 * @return      none
 * @retval      none
 * @details     成功时区分已最新/有更新/无典型输出三种；失败时错误信息截断到 120 字符
 * @doxygen_sig_fp 669a5788f9ef54a4
 */
void GitManager::handle_pull_result(QProcess *proc, int repoIndex, int exitCode)
{
    // 读取 pull 的标准输出/错误输出，用于结果判定与失败原因提示
    QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
    QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();

    if (exitCode == 0) {
        // 输出包含 Updating/Fast-forward 视为有实际更新落盘
        bool isUpdated = out.contains("Updating") ||
                        (out.contains("Fast-forward") && !out.contains("Already up"));
        if (out.contains("Already up")) {
            // 远端无新提交
            emit log_message(QStringLiteral("  ✓ 已是最新"), QColor(0, 150, 0));
            emit repo_result(repoIndex, QStringLiteral("✓ 已是最新"), QColor(0, 150, 0), true);
        } else if (isUpdated) {
            // 本次确实拉下来了新提交
            emit log_message(QStringLiteral("  ✔ 更新成功"), QColor(0, 180, 0));
            emit repo_result(repoIndex, QStringLiteral("✔ 刚更新"), QColor(0, 180, 0), true);
        } else {
            // 退出码为 0 但无典型输出（如空输出），按完成处理
            emit log_message(QStringLiteral("  ✓ 完成 (exit 0)"), QColor(0, 150, 0));
            emit repo_result(repoIndex, QStringLiteral("✓ 完成"), QColor(0, 150, 0), true);
        }
    } else {
        // 失败：优先展示 stderr，截断到 120 字符避免单条日志过长
        QString msg = err.isEmpty() ? out : err;
        if (msg.length() > 120) msg = msg.left(120) + "…";
        emit log_message(QStringLiteral("  ❌ 失败: %1").arg(msg), Qt::red);
        emit repo_result(repoIndex, QStringLiteral("✘ 拉取失败"), Qt::red, false);
    }
}

/**
 * @brief       置位中止标记，请求停止当前扫描或拉取
 *
 * @return      none
 * @retval      none
 * @details     实际退出点在扫描循环与拉取队列回调中检查标记
 * @doxygen_sig_fp 6c115d4d666073fd
 */
void GitManager::request_abort()
{
    // 只置位标记，实际退出点在扫描循环与拉取队列的超时回调里检查
    m_abortFlag.store(1);
}
