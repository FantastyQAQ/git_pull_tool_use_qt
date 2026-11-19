#ifndef ONLINE_UPDATE_DIALOG_H
#define ONLINE_UPDATE_DIALOG_H

#include <QByteArray>
#include <QDialog>
#include <QString>

#include <functional>

#include "update_manifest.h"

class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QTextEdit;
class QTimer;
class UpdateDownloader;

/// @brief 在线更新窗口：检查版本 → 下载 → 校验 → 启动新版的完整状态机
class OnlineUpdateDialog : public QDialog
{
    Q_OBJECT

public:
    /// @brief 构造窗口
    /// @param check_busy 回调：返回 true 表示当前有仓库扫描或拉取任务在跑，不允许切换版本
    /// @param parent 父窗口
    explicit OnlineUpdateDialog(std::function<bool()> check_busy, QWidget *parent = nullptr);
    ~OnlineUpdateDialog() override;

protected:
    /// @brief 下载/校验/启动过程中拦截关闭，避免半途被打断
    void closeEvent(QCloseEvent *event) override;
    /// @brief Esc 等 reject 路径同样拦截（Qt 的 reject 不会触发 closeEvent）
    void reject() override;

private slots:
    // 按钮
    /// @brief 开始检查服务器上的最新版本
    void on_check();
    /// @brief 开始下载新版本
    void on_install();
    /// @brief 取消下载或关闭窗口
    void on_cancel_or_close();

    // 网络
    /// @brief 清单下载完成
    void on_manifest_reply_finished();
    /// @brief 签名下载完成
    void on_signature_reply_finished();

    // 下载器
    /// @brief 刷新进度条与速度显示
    void on_download_progress(qint64 received, qint64 total, double bytesPerSec);
    /// @brief 下载结束（成功则进入校验）
    void on_download_finished(bool ok, const QString &error);

private:
    /// @brief 状态机状态
    enum State {
        StateIdle,        ///< 打开窗口，等待检查
        StateChecking,    ///< 正在请求清单与签名
        StateUpToDate,    ///< 已是最新版本
        StateAvailable,   ///< 发现新版本，可下载
        StateDownloading, ///< 正在下载
        StateVerifying,   ///< 正在校验哈希
        StateApplying,    ///< 正在启动新版本
        StateManualInstall, ///< 目录不可写，已另存到桌面
        StateError        ///< 出错
    };

    // UI 构建
    /// @brief 搭建窗口界面
    void setup_ui();
    /// @brief 按状态刷新控件可用性与文案
    void apply_state(State state);

    // 网络请求
    /// @brief 发起一次带超时的 GET 请求（超时到点会 abort 该请求）
    QNetworkReply *start_request(const QString &relativePath, QTimer *timeoutTimer);
    /// @brief 清单验签通过后比较版本
    void handle_manifest_verified();
    /// @brief 进入校验阶段
    void verify_download();
    /// @brief 决定直接启动新版本还是另存到桌面
    void apply_or_save();
    /// @brief 显示错误并回到可重试状态
    void show_error(const QString &message);
    /// @brief 释放两个网络请求与超时定时器
    void release_replies();
    /// @brief 当前是否处于下载、校验或启动阶段（不允许关闭窗口）
    bool check_task_running() const;

private:
    // ---- UI 成员 ----
    QLabel *m_currentLabel = nullptr;    ///< 当前版本
    QLabel *m_latestLabel = nullptr;     ///< 最新版本与发布时间
    QLabel *m_statusLabel = nullptr;     ///< 状态文案
    QTextEdit *m_notesEdit = nullptr;    ///< 更新说明
    QProgressBar *m_progress = nullptr;  ///< 下载进度
    QLabel *m_progressLabel = nullptr;   ///< 已下载量与速度
    QPushButton *m_checkBtn = nullptr;   ///< 检查更新
    QPushButton *m_installBtn = nullptr; ///< 立即更新
    QPushButton *m_cancelBtn = nullptr;  ///< 取消下载 / 关闭

    // ---- 逻辑成员 ----
    std::function<bool()> m_checkBusy;   ///< 「仓库扫描或拉取任务是否在跑」回调
    UpdateDownloader *m_downloader = nullptr;       ///< 下载器
    QNetworkAccessManager *m_manager = nullptr;     ///< 网络管理器
    QNetworkReply *m_manifestReply = nullptr;       ///< 清单请求
    QNetworkReply *m_signatureReply = nullptr;      ///< 签名请求
    QTimer *m_manifestTimer = nullptr;              ///< 清单请求超时
    QTimer *m_signatureTimer = nullptr;             ///< 签名请求超时

    State m_state = StateIdle;    ///< 当前状态
    UpdateManifest m_manifest;    ///< 验签并解析通过的清单
    QByteArray m_manifestRaw;     ///< 清单原始字节（验签对象）
    QString m_downloadPath;       ///< 本次下载落盘的新版本文件路径
};

#endif // ONLINE_UPDATE_DIALOG_H
