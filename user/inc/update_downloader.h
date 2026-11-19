#ifndef UPDATE_DOWNLOADER_H
#define UPDATE_DOWNLOADER_H

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QUrl>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/// @brief 更新包下载：进度/速度信号 + 停滞超时 + 可取消（同一时间只允许一个任务）
class UpdateDownloader : public QObject
{
    Q_OBJECT

public:
    explicit UpdateDownloader(QObject *parent = nullptr);
    ~UpdateDownloader() override;

    /// @brief 开始下载到指定路径；成功与否由 download_finished 通知
    void start(const QUrl &url, const QString &targetPath, qint64 expectedSize);

    /// @brief 取消下载并删除未完成的文件
    void cancel();

    /// @brief 当前是否有下载任务在跑
    bool check_active() const;

signals:
    /// @brief 进度变化（total 为 -1 表示服务器未给长度，界面应切忙碌态）
    void progress_changed(qint64 received, qint64 total, double bytesPerSec);
    /// @brief 下载结束（ok 为 false 时 error 说明原因，已下载的文件会被删除）
    void download_finished(bool ok, const QString &error);

private:
    /// @brief 连接 QNetworkReply 的各路信号
    void attach_reply(QNetworkReply *reply);
    /// @brief 收尾：关闭文件、清理定时器、删除半成品（removeFile 为 true 时）
    void finish(bool ok, const QString &error, bool removeFile);
    /// @brief 收到数据后重置停滞定时器
    void reset_stall_timer();
    /// @brief 定时刷新下载速度
    void update_speed();
    /// @brief 立即上报一次进度（首块数据到达时用，避免小文件看不到进度）
    void report_progress(double bytesPerSec);

private:
    QNetworkAccessManager *m_manager = nullptr;  ///< 网络管理器（窗口生命周期内复用）
    QNetworkReply *m_reply = nullptr;            ///< 当前请求（同一时间只有一个）
    QFile *m_file = nullptr;                     ///< 落盘的目标文件
    QTimer *m_stallTimer = nullptr;              ///< 停滞超时定时器（每次收数据重置）
    QTimer *m_speedTimer = nullptr;              ///< 速度采样定时器（500 ms）

    QString m_targetPath;      ///< 目标文件绝对路径
    qint64 m_expectedSize = 0; ///< 清单声明的大小（用于进度与完整性校验）
    qint64 m_lastBytes = 0;    ///< 上次采样的已下载字节数
    QElapsedTimer m_lastTick;  ///< 上次采样的时刻
};

#endif // UPDATE_DOWNLOADER_H
