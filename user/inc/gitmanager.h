/**
 * @file        gitmanager.h
 * @brief       Git 仓库扫描与批量拉取功能类声明（RepoInfo 与 GitManager）
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.1
 * @date        20261106
 * @note        扫描/拉取均在工作线程异步执行，日志、进度与结果经信号回传，UI 展示由 MainWindow 负责
 *
 * @copyright   Copyright (c) 2026 by OLE, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261106    <td>1.0       <td>lch       <td>创建
 * <tr><td>20261106    <td>1.1       <td>lch       <td>扫描改后台线程异步执行，去重 remote 查询
 * </table>
 */
#ifndef GITMANAGER_H
#define GITMANAGER_H

#include <QObject>
#include <QProcess>
#include <QVector>
#include <QStringList>
#include <QColor>
#include <QAtomicInt>
#include <QMetaType>

class QThread;

/**
 * @brief 单个 Git 仓库的扫描结果与状态信息
 */
struct RepoInfo
{
    QString path;          // 相对于扫描根的路径
    QString absolutePath;  // 绝对路径
    QString remoteUrl;     // origin 地址
    QString branch;        // 当前分支
    QString upstream;      // 上游分支 @{u}
    QString status;        // "待扫描" / "已是最新" / "落后 X" / "分离HEAD" / ...
    int     behind;        // 落后提交数
    int     ahead;         // 领先提交数
    bool    dirty;         // 工作区脏
    bool    detached;      // 分离 HEAD
    bool    inProgress;    // merge/rebase 进行中
    bool    noUpstream;    // 没上游
    bool    isGitHub;      // 是 github.com
};

// RepoInfo 经跨线程 queued 信号传递，需注册元类型
Q_DECLARE_METATYPE(RepoInfo)

/**
 * @brief Git 仓库扫描与批量拉取功能类（与 UI 解耦）
 *
 * 职责：
 *   1. 异步递归扫描根目录下的 GitHub 仓库并采集各仓库状态（start_scan）；
 *   2. 按索引队列串行执行 git pull --ff-only（start_pull）。
 * 扫描在独立 QThread 中执行（run_git 同步阻塞只占用工作线程，不卡 UI）；
 * 拉取在对象所属主线程用 QTimer + 异步 QProcess 驱动。
 * 过程日志、进度与结果均通过信号回传，由上层 UI 类负责展示与表格刷新。
 */
class GitManager : public QObject
{
    Q_OBJECT

public:
    explicit GitManager(QObject *parent = nullptr);
    ~GitManager();

    void start_scan(const QString &rootDir);                                  // 后台线程异步扫描根目录下的 GitHub 仓库
    void start_pull(const QVector<int> &indices, const QVector<RepoInfo> &repos); // 按索引队列串行拉取
    void request_abort();                                  // 请求中止当前扫描/拉取

signals:
    void log_message(const QString &text, const QColor &color); // 过程日志（文本+颜色）
    void scan_list_ready(QVector<RepoInfo> repos);              // 预检完成：GitHub 仓库骨架列表（状态"扫描中…"）
    void scan_result(int repoIndex, RepoInfo info);             // 单仓库状态采集完成
    void scan_finished(bool aborted);                           // 扫描结束（aborted=true 表示被用户中止）
    void pull_progress(int done, int total);                    // 拉取进度：已完成数/总数
    void repo_result(int repoIndex, const QString &status, const QColor &color, bool behindCleared); // 单仓库拉取结果
    void pull_finished();                                       // 拉取队列结束（含中止）

private:
    void scan_worker(const QString &rootDir);                                // 扫描线程主体：预检 + 逐仓采集
    RepoInfo make_skeleton(const QString &repoPath, const QString &rootDir, const QString &remoteUrl); // 由预检结果构造骨架信息
    QStringList find_git_repos(const QString &rootDir, int maxDepth);        // BFS 查找 .git 目录
    RepoInfo examine_repo(const QString &repoPath, const QString &rootDir, const QString &remoteUrl); // 采集单个仓库状态（复用预检 url）
    QString run_git(const QString &workDir, const QStringList &args, int timeoutMs); // 同步执行 git 命令
    void handle_pull_result(QProcess *proc, int repoIndex, int exitCode);    // 解析单仓库 pull 输出并回传结果

    QAtomicInt m_abortFlag; // 0 = 正常执行，1 = 用户请求中止
    QThread   *m_scanThread = nullptr; // 当前扫描工作线程（未运行时为悬空前一轮，start_scan 内先清理）
};

#endif // GITMANAGER_H
