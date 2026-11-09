/**
 * @file        gitmanager.h
 * @brief       Git 仓库扫描与批量拉取功能类声明（RepoInfo 与 GitManager）
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.3
 * @date        20261108
 * @note        扫描在后台线程并发执行，拉取由异步子进程串行执行，结果经信号回传 UI
 *
 * @copyright   Copyright (c) 2026 by OLE, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261106    <td>1.0       <td>lch       <td>创建
 * <tr><td>20261106    <td>1.1       <td>lch       <td>扫描改后台线程异步执行，去重 remote 查询
 * <tr><td>20261108    <td>1.2       <td>lch       <td>按代码审查完善扫描、拉取和进程生命周期管理
 * <tr><td>20261108    <td>1.3       <td>lch       <td>单仓 git 调用合并为 2 次，扫描改为并发采集
 * </table>
 */
#ifndef GITMANAGER_H
#define GITMANAGER_H

#include <QAtomicInt>
#include <QColor>
#include <QMetaType>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVector>

class QThread;
class QTimer;

/** 单个 Git 仓库的扫描结果与状态信息 */
struct RepoInfo
{
    QString path;                  // 相对于扫描根的路径
    QString absolutePath;          // 绝对路径
    QString remoteUrl;             // 远端地址（优先 origin，否则第一个远端）
    QString branch;                // 当前分支
    QString upstream;              // 上游分支 @{u}
    QString status;                // 当前展示状态
    int     behind = 0;            // 落后提交数
    int     ahead = 0;             // 领先提交数
    bool    scanned = false;       // 已完成状态采集（未采集的骨架行不允许拉取）
    bool    pullable = false;      // 全部检查通过且仅允许 fast-forward 时为 true
    bool    dirty = false;         // 工作区脏
    bool    detached = false;      // 分离 HEAD
    bool    inProgress = false;    // merge/rebase 进行中
    bool    noUpstream = false;    // 没上游
};

// RepoInfo 经跨线程 queued 信号传递，需注册元类型
Q_DECLARE_METATYPE(RepoInfo)

/** 一次 git 子进程调用的结果 */
struct GitResult
{
    QString out;              // 标准输出（UTF-8）
    QString err;              // 标准错误（UTF-8）
    int     exitCode = -1;    // 退出码，超时或启动失败时为 -1
    bool    ok = false;       // 是否成功（正常退出且退出码为 0）
};

/** Git 仓库扫描与批量拉取功能类（与 UI 解耦） */
class GitManager : public QObject
{
    Q_OBJECT

public:
    explicit GitManager(QObject *parent = nullptr);
    ~GitManager();

    int  start_scan(const QString &rootDir, bool fetchRemote); // 启动扫描并返回本轮代次
    void start_pull(const QVector<int> &indices, const QVector<RepoInfo> &repos); // 串行拉取指定仓库
    void request_abort();                                      // 中止扫描或立即终止当前拉取

signals:
    void log_message(const QString &text, const QColor &color); // 过程日志（文本+颜色）
    void scan_list_ready(int scanId, QVector<RepoInfo> repos);  // 指定代次的仓库骨架列表
    void scan_result(int scanId, int repoIndex, RepoInfo info); // 指定代次的单仓库状态
    void scan_finished(int scanId, bool aborted);               // 扫描结束
    void pull_progress(int done, int total);                    // 拉取进度：已完成数/总数
    void repo_result(int repoIndex, const QString &status, const QColor &color, bool behindCleared); // 单仓库拉取结果
    void pull_finished(bool aborted);                           // 拉取队列结束（含中止）

private:
    void scan_worker(int scanId, const QString &rootDir, bool fetchRemote); // 扫描线程主体
    QString pick_remote_url(const QString &repoPath);                        // 选取远端地址
    RepoInfo make_skeleton(const QString &repoPath, const QString &rootDir, const QString &remoteUrl); // 构造骨架信息
    QStringList find_git_repos(const QString &rootDir, int maxDepth);        // BFS 查找 .git 目录
    RepoInfo examine_repo(const QString &repoPath, const QString &rootDir,
                          const QString &remoteUrl, bool fetchRemote);       // 采集单个仓库状态
    QString relative_display_path(const QString &repoPath, const QString &rootDir); // 计算展示路径
    GitResult run_git(const QString &workDir, const QStringList &args, int timeoutMs); // 同步执行 git 命令
    void process_next_pull();                                                 // 启动下一项或结束队列
    void finish_pull_process(int repoIndex, int exitCode, QProcess::ExitStatus exitStatus,
                             const QString &startError);                      // 统一回收拉取进程
    void finish_pull_item(int repoIndex, const GitResult &res);               // 回传单项结果并推进队列
    void kill_current_pull();                                                 // 强制结束当前拉取进程

    QAtomicInt m_abortFlag;                 // 0 = 正常执行，1 = 用户请求中止
    int        m_scanId = 0;                // 扫描代次，每次 start_scan 递增
    QThread   *m_scanThread = nullptr;      // 上一轮扫描线程对象，start_scan 中会先清理

    QVector<int>      m_pullQueue;          // 待拉取仓库在仓库快照中的索引
    QVector<RepoInfo> m_pullRepos;          // 拉取时的仓库快照
    int               m_pullCursor = 0;     // 当前处理到的队列位置
    QProcess         *m_pullProc = nullptr; // 当前正在运行的 git pull 进程
    QTimer           *m_pullWatchdog = nullptr; // 单次看门狗：pull 超时则强杀
    QString           m_headBefore;         // 当前仓库 pull 前的 HEAD 哈希
    bool              m_pullTimedOut = false; // 当前 pull 是否因超时被终止
};

#endif // GITMANAGER_H
