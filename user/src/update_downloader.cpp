/**
 * @file update_downloader.cpp
 * @brief 更新包下载实现：QNetworkAccessManager + 停滞超时 + 速度采样
 *
 * Qt 5.12 的 QNetworkRequest 没有 setTransferTimeout（5.15 才有），
 * 因此这里用一个单次 QTimer 自己实现「连续无数据超时」：
 * 每收到一块数据就重置定时器，到期仍未收到数据则主动 abort。
 */

#include "update_downloader.h"

#include "update_config.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

/**
 * @brief       构造下载器：建网络管理器与两个定时器
 *
 * 定时器只在下载期间启动，构造后处于停止状态，不占资源。
 * @param[out]  {parent} QObject* 父对象
 * @return      none（构造/析构函数，无返回值）
 * @doxygen_sig_fp 97489a54f99d0461
 */
UpdateDownloader::UpdateDownloader(QObject *parent)
    : QObject(parent)
{
    m_manager = new QNetworkAccessManager(this);

    // 停滞定时器：单次触发，收到数据就重置，超时说明连接已经卡死
    m_stallTimer = new QTimer(this);
    m_stallTimer->setSingleShot(true);
    m_stallTimer->setInterval(UPDATE_STALL_TIMEOUT_MS);
    connect(m_stallTimer, &QTimer::timeout, this, [this]() {
        if (!m_reply)
            return;
        // 主动放弃这次请求，abort 之后 QNetworkReply 会发出 finished 信号；
        // 打标记是为了在 finished 里给出「网络停滞」而不是笼统的 Operation canceled
        m_reply->setProperty("stalled", true);
        m_reply->abort();
    });

    // 速度采样定时器：500 ms 一次，用两次采样之间的字节差计算瞬时速度
    m_speedTimer = new QTimer(this);
    m_speedTimer->setInterval(500);
    connect(m_speedTimer, &QTimer::timeout, this, &UpdateDownloader::update_speed);
}

/**
 * @brief       析构函数：确保请求与文件句柄都已收尾
 *
 * 直接销毁正在跑的 reply 会触发它自身的 finished，但那时对象已部分析构，
 * 因此先断开并 abort，不再走 finish 的回调路径。
 * 若销毁时仍在下载，目标文件是残缺的，这里直接删除，避免留下半截新版本文件。
 * @return      none（构造/析构函数，无返回值）
 * @doxygen_sig_fp a371f111030e7f2d
 */
UpdateDownloader::~UpdateDownloader()
{
    // 记下是否还在下载：abort 之后 m_reply 会被清空，但文件仍需删除
    const bool wasActive = m_reply != nullptr;
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    // 先关闭文件句柄，再删除目标文件（Windows 下打开的文件无法删除）
    delete m_file;
    m_file = nullptr;

    if (wasActive && !m_targetPath.isEmpty())
        QFile::remove(m_targetPath);
}

/**
 * @brief       开始下载到指定路径
 *
 * 目标路径应位于 exe 同目录（保证后续重命名不跨卷）。调用前调用方应已确认
 * 该目录可写；这里仍会检查文件能否打开，失败直接通过 download_finished 报错。
 * @param[in]   {url} const QUrl& 下载地址
 * @param[in]   {targetPath} const QString& 落盘的临时文件绝对路径
 * @param[in]   {expectedSize} qint64 清单声明的大小，仅用于进度显示
 * @return      none
 * @doxygen_sig_fp 9ddc9bac0600fbe0
 */
void UpdateDownloader::start(const QUrl &url, const QString &targetPath, qint64 expectedSize)
{
    if (check_active()) {
        emit download_finished(false, QStringLiteral("已有一个下载任务在进行中"));
        return;
    }

    m_targetPath = targetPath;
    m_expectedSize = expectedSize;
    m_lastBytes = 0;

    // 以「写 + 截断」打开：上一次中断残留的同名文件直接覆盖掉
    m_file = new QFile(targetPath);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString err = QStringLiteral("无法写入新版本文件（同名文件可能正被占用）：%1")
                                .arg(m_file->errorString());
        finish(false, err, true);
        return;
    }

    QNetworkRequest request(url);
    // 清单与 exe 都不允许走本地缓存，否则可能拿到旧内容导致哈希校验失败
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);

    m_reply = m_manager->get(request);
    attach_reply(m_reply);

    // 停滞超时从「发出请求」就开始计时，而不是等首块数据：
    // 服务器完全不响应（连接被丢弃、中间设备挂住）时也必须能超时退出，
    // 否则界面会永久停在「正在下载」。
    m_stallTimer->start();
}

/**
 * @brief       取消下载并删除未完成的临时文件
 *
 * abort 之后 reply 仍会发出 finished，届时走统一收尾；这里只负责触发。
 * @return      none
 * @doxygen_sig_fp fd4a14854da0648c
 */
void UpdateDownloader::cancel()
{
    if (!m_reply)
        return;
    // 用属性标记「用户主动取消」，避免 finished 里把它当成网络错误再报一次
    m_reply->setProperty("canceledByUser", true);
    m_reply->abort();
}

/**
 * @brief       当前是否有下载任务在跑
 *
 * @return      存在未结束的请求返回 true
 * @doxygen_sig_fp 40931084ad1e7d6e
 */
bool UpdateDownloader::check_active() const
{
    return m_reply != nullptr;
}

/**
 * @brief       连接 QNetworkReply 的信号
 *
 * 只连一次：readyRead 负责写盘并重置停滞超时，
 * finished 负责统一收尾（成功、失败、超时、取消都走这里）。
 * @param[out]  {reply} QNetworkReply* 新建的请求对象
 * @return      none
 * @doxygen_sig_fp 1f2d62bce4635e84
 */
void UpdateDownloader::attach_reply(QNetworkReply *reply)
{
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        if (reply != m_reply || !m_file)
            return;
        const QByteArray chunk = reply->readAll();
        if (chunk.isEmpty())
            return;
        if (m_file->write(chunk) != chunk.size()) {
            // 磁盘满或介质被拔出：直接放弃，由 finish 删除残缺文件
            reply->setProperty("writeFailed", true);
            reply->abort();
            return;
        }
        reset_stall_timer();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply != m_reply)
            return;

        const bool canceled = reply->property("canceledByUser").toBool();
        const bool stalled = reply->property("stalled").toBool();
        const bool writeFailed = reply->property("writeFailed").toBool();
        const QNetworkReply::NetworkError netError = reply->error();
        const qint64 received = m_file ? m_file->size() : 0;

        // 用户取消：静默清理，不算错误
        if (canceled) {
            finish(false, QStringLiteral("下载已取消"), true);
            return;
        }
        if (stalled) {
            finish(false, QStringLiteral("下载停滞超过 %1 秒，已中止（请检查网络后重试）")
                               .arg(UPDATE_STALL_TIMEOUT_MS / 1000),
                   true);
            return;
        }
        if (writeFailed) {
            finish(false, QStringLiteral("写入临时文件失败（磁盘空间不足或无权限）"), true);
            return;
        }
        if (netError != QNetworkReply::NoError) {
            // 停滞超时也会走到这里（abort 产生 OperationCanceledError）
            finish(false, QStringLiteral("下载失败：%1").arg(reply->errorString()), true);
            return;
        }
        // 大小不符说明传输被截断或服务器内容与清单不一致
        if (m_expectedSize > 0 && received != m_expectedSize) {
            finish(false, QStringLiteral("下载大小不符（期望 %1 字节，实际 %2 字节）")
                               .arg(m_expectedSize)
                               .arg(received),
                   true);
            return;
        }
        finish(true, QString(), false);
    });
}

/**
 * @brief       统一收尾：停定时器、关文件、删残留、发结果
 *
 * 所有结束路径（成功 / 网络错误 / 超时 / 取消 / 写盘失败）都汇聚到这里，
 * 保证 m_reply 与 m_file 一定被释放，界面不会卡在「正在下载」状态。
 * @param[in]   {ok} bool 是否成功
 * @param[in]   {error} const QString& 失败原因
 * @param[in]   {removeFile} bool 是否删除目标文件（失败与取消时为 true）
 * @return      none
 * @doxygen_sig_fp 97f43962163a61e9
 */
void UpdateDownloader::finish(bool ok, const QString &error, bool removeFile)
{
    m_stallTimer->stop();
    m_speedTimer->stop();

    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->deleteLater();
    }

    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }

    // 失败时删掉半成品：留着会让下次下载看到一个可疑的旧文件
    if (removeFile && !m_targetPath.isEmpty())
        QFile::remove(m_targetPath);

    emit download_finished(ok, error);
}

/**
 * @brief       重置停滞超时定时器
 *
 * @return      none
 * @doxygen_sig_fp 9a96343d018b858f
 */
void UpdateDownloader::reset_stall_timer()
{
    m_stallTimer->start();
    if (!m_speedTimer->isActive()) {
        // 首块数据到达时启动速度采样，并记录基准时刻
        m_lastBytes = m_file ? m_file->size() : 0;
        m_lastTick.start();
        m_speedTimer->start();
        // 小文件可能在 500 ms 采样前就下载完，这里先立即上报一次，
        // 保证界面无论如何都能看到进度反馈
        report_progress(0.0);
    }
}

/**
 * @brief       采样并上报下载速度
 *
 * 用两次采样之间的字节差除以实际经过的毫秒数，得到瞬时速度。
 * 服务器未给 Content-Length 时用清单里的 size 作为总长，保证进度条仍能前进。
 * @return      none
 * @doxygen_sig_fp 190e0a3b67f14f34
 */
void UpdateDownloader::update_speed()
{
    if (!m_reply || !m_file)
        return;

    const qint64 now = m_file->size();
    const qint64 elapsed = m_lastTick.restart();
    double speed = 0.0;
    if (elapsed > 0)
        speed = static_cast<double>(now - m_lastBytes) * 1000.0 / static_cast<double>(elapsed);
    m_lastBytes = now;

    report_progress(speed);
}

/**
 * @brief       上报一次下载进度
 *
 * 服务器未给 Content-Length 时用清单里的 size 作为总长，
 * 保证进度条仍能正常前进而不是一直停在忙碌态。
 * @param[in]   {bytesPerSec} double 瞬时速度（字节/秒），未知时传 0
 * @return      none
 * @doxygen_sig_fp 2917abee5964b7f3
 */
void UpdateDownloader::report_progress(double bytesPerSec)
{
    if (!m_reply || !m_file)
        return;

    const qint64 received = m_file->size();
    const qint64 total = m_reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    const qint64 reportTotal = (total > 0) ? total : m_expectedSize;
    emit progress_changed(received, reportTotal, bytesPerSec);
}
