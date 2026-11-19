/**
 * @file online_update_dialog.cpp
 * @brief 在线更新窗口实现：检查版本、下载、校验、启动新版的完整状态机
 *
 * 流程顺序（与方案第二节一致）：
 *   确认 → 在窗口内下载（显示进度）→ 校验通过 → 启动新版本 → 退出当前程序
 * 新版本以「发布文件名」独立落盘（如 git_pull_tool_SV01.014.exe），
 * 不覆盖也不改名正在运行的 exe，因此不会被文件占用卡住；
 * 下载失败或校验失败时旧程序仍可用，不会出现「程序已退出但新版没装好」的窘境。
 */

#include "online_update_dialog.h"

#include "app_info.h"
#include "update_applier.h"
#include "update_config.h"
#include "update_downloader.h"
#include "update_verifier.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const int kDialogWidth = 900;      ///< 窗口宽度
const int kNotesMinHeight = 120;   ///< 更新说明区最小高度

/**
 * @brief 把字节数格式化成可读文本
 * @param bytes 字节数
 * @return 形如 "8.1 MB" 的文本
 * @doxygen_sig_fp 420a460aedd08173
 */
QString format_size(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
    return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
}

} // namespace

/**
 * @brief       构造在线更新窗口
 *
 * @param[in]   {check_busy} std::function<bool()> 「仓库扫描或拉取任务是否在跑」回调
 * @param[out]  {parent} QWidget* 父窗口
 * @return      none（构造/析构函数，无返回值）
 * @doxygen_sig_fp b657f4b7de990937
 */
OnlineUpdateDialog::OnlineUpdateDialog(std::function<bool()> check_busy, QWidget *parent)
    : QDialog(parent)
    , m_checkBusy(std::move(check_busy))
{
    // 去掉 Windows 默认的问号帮助按钮
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(QStringLiteral("在线更新"));
    setWindowIcon(QIcon(QStringLiteral(":/icon/app_256.ico")));

    m_manager = new QNetworkAccessManager(this);

    // 两个请求的超时看门狗：Qt 5.12 的 QNetworkRequest 没有超时设置，只能自己计时。
    // 超时后先给 reply 打上 timedOut 标记再 abort，这样 finished 里能区分
    // 「服务器慢/不可达」与「用户主动取消」，给出更准确的提示。
    m_manifestTimer = new QTimer(this);
    m_manifestTimer->setSingleShot(true);
    m_manifestTimer->setInterval(UPDATE_CHECK_TIMEOUT_MS);
    connect(m_manifestTimer, &QTimer::timeout, this, [this]() {
        if (m_manifestReply) {
            m_manifestReply->setProperty("timedOut", true);
            m_manifestReply->abort();
        }
    });

    m_signatureTimer = new QTimer(this);
    m_signatureTimer->setSingleShot(true);
    m_signatureTimer->setInterval(UPDATE_CHECK_TIMEOUT_MS);
    connect(m_signatureTimer, &QTimer::timeout, this, [this]() {
        if (m_signatureReply) {
            m_signatureReply->setProperty("timedOut", true);
            m_signatureReply->abort();
        }
    });

    m_downloader = new UpdateDownloader(this);
    connect(m_downloader, &UpdateDownloader::progress_changed,
            this, &OnlineUpdateDialog::on_download_progress);
    connect(m_downloader, &UpdateDownloader::download_finished,
            this, &OnlineUpdateDialog::on_download_finished);

    setup_ui();
    apply_state(StateIdle);
}

/**
 * @brief       析构函数（子对象由 Qt 父子关系释放）
 *
 * @return      none（构造/析构函数，无返回值）
 * @doxygen_sig_fp 4d7259c70418e6fa
 */
OnlineUpdateDialog::~OnlineUpdateDialog() = default;

/**
 * @brief       搭建窗口界面
 *
 * 布局与方案 6.2 节的示意图对应：版本信息在上，更新说明居中，
 * 进度条与状态文案在下，三个按钮靠右。
 * @return      none
 * @doxygen_sig_fp 04dcfa6acc062a1d
 */
void OnlineUpdateDialog::setup_ui()
{
    setMinimumWidth(kDialogWidth);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);

    // ---- 版本信息 ----
    m_currentLabel = new QLabel(QStringLiteral("当前版本：%1").arg(QStringLiteral(APP_VERSION)), this);
    layout->addWidget(m_currentLabel);

    m_latestLabel = new QLabel(QStringLiteral("最新版本：未知"), this);
    layout->addWidget(m_latestLabel);

    auto *notesTitle = new QLabel(QStringLiteral("更新说明："), this);
    notesTitle->setObjectName(QStringLiteral("panelTitle"));
    layout->addWidget(notesTitle);

    m_notesEdit = new QTextEdit(this);
    m_notesEdit->setReadOnly(true);
    m_notesEdit->setMinimumHeight(kNotesMinHeight);
    m_notesEdit->setPlaceholderText(QStringLiteral("点击「检查更新」获取最新版本说明"));
    m_notesEdit->setStyleSheet(QStringLiteral(
        "QTextEdit { background: #ffffff; border: 1px solid #d7dee8; border-radius: 4px;"
        "            color: #344256; }"));
    layout->addWidget(m_notesEdit, 1);

    // ---- 进度 ----
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setVisible(false);
    layout->addWidget(m_progress);

    m_progressLabel = new QLabel(QString(), this);
    m_progressLabel->setVisible(false);
    m_progressLabel->setStyleSheet(QStringLiteral("color: #7c8798;"));
    layout->addWidget(m_progressLabel);

    // ---- 状态文案 ----
    m_statusLabel = new QLabel(QStringLiteral("尚未检查更新"), this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    // ---- 按钮行 ----
    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);

    m_checkBtn = new QPushButton(QStringLiteral("检查更新"), this);
    m_checkBtn->setCursor(Qt::PointingHandCursor);
    connect(m_checkBtn, &QPushButton::clicked, this, &OnlineUpdateDialog::on_check);
    btnRow->addWidget(m_checkBtn);

    m_installBtn = new QPushButton(QStringLiteral("立即更新"), this);
    m_installBtn->setCursor(Qt::PointingHandCursor);
    // 主按钮配色沿用主窗口的蓝色主按钮
    m_installBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #2f73d9; border-color: #2f73d9; color: #ffffff; }"
        "QPushButton:hover { background: #2363c5; border-color: #2363c5; color: #ffffff; }"
        "QPushButton:disabled { background: #f0f2f5; border-color: #d7dee8; color: #a2acb9; }"));
    connect(m_installBtn, &QPushButton::clicked, this, &OnlineUpdateDialog::on_install);
    btnRow->addWidget(m_installBtn);

    m_cancelBtn = new QPushButton(QStringLiteral("关闭"), this);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(m_cancelBtn, &QPushButton::clicked, this, &OnlineUpdateDialog::on_cancel_or_close);
    btnRow->addWidget(m_cancelBtn);

    layout->addLayout(btnRow);

    resize(kDialogWidth, 500);
}

/**
 * @brief       按状态刷新控件可用性与文案
 *
 * 状态机共 9 个状态（见头文件枚举），这里集中处理「哪个按钮可用、
 * 进度条怎么显示、状态文案是什么」，避免各流程各写一遍。
 * @param[in]   {state} State 目标状态
 * @return      none
 * @doxygen_sig_fp f40fa4065adf7bf9
 */
void OnlineUpdateDialog::apply_state(State state)
{
    m_state = state;

    // 默认全部复位，再按状态单独打开
    m_checkBtn->setEnabled(false);
    m_installBtn->setEnabled(false);
    m_cancelBtn->setText(QStringLiteral("关闭"));
    m_progress->setVisible(false);
    m_progress->setRange(0, 100);
    m_progressLabel->setVisible(false);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #536174;"));

    switch (state) {
    case StateIdle:
        m_checkBtn->setEnabled(true);
        m_statusLabel->setText(QStringLiteral("点击「检查更新」查询服务器上的最新版本"));
        break;

    case StateChecking:
        m_cancelBtn->setText(QStringLiteral("取消"));
        m_progress->setVisible(true);
        m_progress->setRange(0, 0);   // 忙碌态
        m_statusLabel->setText(QStringLiteral("正在检查…"));
        break;

    case StateUpToDate:
        m_checkBtn->setEnabled(true);
        m_statusLabel->setStyleSheet(QStringLiteral("color: #159447;"));
        m_statusLabel->setText(QStringLiteral("已是最新版本"));
        break;

    case StateAvailable:
        m_checkBtn->setEnabled(true);
        m_installBtn->setEnabled(true);
        m_statusLabel->setStyleSheet(QStringLiteral("color: #d56a16;"));
        m_statusLabel->setText(QStringLiteral("发现新版本，点击「立即更新」开始下载"));
        break;

    case StateDownloading:
        m_cancelBtn->setText(QStringLiteral("取消下载"));
        m_progress->setVisible(true);
        m_progress->setRange(0, 100);
        m_progressLabel->setVisible(true);
        m_statusLabel->setText(QStringLiteral("正在下载…"));
        break;

    case StateVerifying:
        m_progress->setVisible(true);
        m_progress->setRange(0, 0);
        m_statusLabel->setText(QStringLiteral("正在校验文件完整性…"));
        break;

    case StateApplying:
        m_progress->setVisible(true);
        m_progress->setRange(0, 0);
        m_statusLabel->setStyleSheet(QStringLiteral("color: #159447;"));
        m_statusLabel->setText(QStringLiteral("校验通过，正在启动新版本…"));
        break;

    case StateManualInstall:
        m_checkBtn->setEnabled(true);
        m_cancelBtn->setText(QStringLiteral("关闭"));
        m_statusLabel->setStyleSheet(QStringLiteral("color: #d56a16;"));
        break;

    case StateError:
        m_checkBtn->setEnabled(true);
        m_statusLabel->setStyleSheet(QStringLiteral("color: #d0021b;"));
        break;
    }
}

/**
 * @brief       发起一次带超时的 GET 请求
 *
 * 统一关闭缓存（AlwaysNetwork）：清单与签名每次都必须取最新内容，
 * 命中本地缓存会导致版本判断滞后。
 * @param[in]   {relativePath} const QString& 相对 UPDATE_BASE_URL 的路径
 * @param[out]  {timeoutTimer} QTimer** 该请求对应的超时定时器
 * @return      新建的请求对象（失败时可能为空，由调用方检查）
 * @doxygen_sig_fp 7dc943f17d076e1f
 */
QNetworkReply *OnlineUpdateDialog::start_request(const QString &relativePath, QTimer *timeoutTimer)
{
    const QUrl base(QString::fromLatin1(UPDATE_BASE_URL));
    const QUrl url = base.resolved(QUrl(relativePath));

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);

    if (timeoutTimer)
        timeoutTimer->start();
    return m_manager->get(request);
}

/**
 * @brief       开始检查服务器上的最新版本
 *
 * 先取清单、再取签名，两步都成功后才验签与解析：顺序不可颠倒，
 * 因为验签必须针对清单的原始字节。
 * @return      none
 * @doxygen_sig_fp 06f2d236490b9cec
 */
void OnlineUpdateDialog::on_check()
{
    if (m_state == StateChecking || m_state == StateDownloading
        || m_state == StateVerifying || m_state == StateApplying)
        return;

    release_replies();
    m_manifestRaw.clear();
    m_manifest = UpdateManifest();

    apply_state(StateChecking);
    m_latestLabel->setText(QStringLiteral("最新版本：查询中…"));

    m_manifestReply = start_request(QStringLiteral("latest.json"), m_manifestTimer);
    connect(m_manifestReply, &QNetworkReply::finished,
            this, &OnlineUpdateDialog::on_manifest_reply_finished);
}

/**
 * @brief       清单请求完成：保存原始字节并继续取签名
 *
 * @return      none
 * @doxygen_sig_fp acf7fd569e9b5b7a
 */
void OnlineUpdateDialog::on_manifest_reply_finished()
{
    if (!m_manifestReply)
        return;
    m_manifestTimer->stop();

    QNetworkReply *reply = m_manifestReply;
    m_manifestReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        // 超时与真正的网络错误分开报，用户才知道该查网络还是查服务器
        const QString err = reply->property("timedOut").toBool()
            ? QStringLiteral("检查更新超时（%1 秒），请确认服务器地址与网络")
                  .arg(UPDATE_CHECK_TIMEOUT_MS / 1000)
            : QStringLiteral("无法获取更新清单：%1").arg(reply->errorString());
        reply->deleteLater();
        show_error(err);
        return;
    }
    m_manifestRaw = reply->readAll();
    reply->deleteLater();

    if (m_manifestRaw.isEmpty()) {
        show_error(QStringLiteral("更新清单内容为空"));
        return;
    }
    if (m_manifestRaw.size() > UPDATE_MANIFEST_MAX_BYTES) {
        show_error(QStringLiteral("更新清单超过 %1 字节上限，已拒绝").arg(UPDATE_MANIFEST_MAX_BYTES));
        return;
    }

    // 继续取签名：验签要用到清单原始字节，两者必须成对
    m_signatureReply = start_request(QStringLiteral("latest.json.sig"), m_signatureTimer);
    connect(m_signatureReply, &QNetworkReply::finished,
            this, &OnlineUpdateDialog::on_signature_reply_finished);
}

/**
 * @brief       签名请求完成：验签 + 解析 + 比较版本
 *
 * 验签失败时直接拒绝，不解析清单内容——否则被篡改的清单依然可能
 * 指向恶意地址。
 * @return      none
 * @doxygen_sig_fp 0677e9038be2b271
 */
void OnlineUpdateDialog::on_signature_reply_finished()
{
    if (!m_signatureReply)
        return;
    m_signatureTimer->stop();

    QNetworkReply *reply = m_signatureReply;
    m_signatureReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        const QString err = reply->property("timedOut").toBool()
            ? QStringLiteral("获取清单签名超时（%1 秒），请确认服务器地址与网络")
                  .arg(UPDATE_CHECK_TIMEOUT_MS / 1000)
            : QStringLiteral("无法获取清单签名：%1").arg(reply->errorString());
        reply->deleteLater();
        show_error(err);
        return;
    }
    const QByteArray signature = reply->readAll();
    reply->deleteLater();

    if (signature.size() > UPDATE_MANIFEST_MAX_BYTES) {
        show_error(QStringLiteral("清单签名超过 %1 字节上限，已拒绝").arg(UPDATE_MANIFEST_MAX_BYTES));
        return;
    }
    if (!UpdateVerifier::check_signature(m_manifestRaw, signature)) {
        show_error(QStringLiteral("更新源校验失败：清单签名无效，已拒绝本次更新"));
        return;
    }

    handle_manifest_verified();
}

/**
 * @brief       清单验签通过后的版本比较
 *
 * 先比主版本号，主版本相同再比序号；远端必须严格大于本地才提示更新（不做降级）。
 * @return      none
 * @doxygen_sig_fp 24b736fe2a9f49f6
 */
void OnlineUpdateDialog::handle_manifest_verified()
{
    UpdateManifest manifest;
    if (!parse_manifest(m_manifestRaw, &manifest)) {
        show_error(QStringLiteral("更新清单格式不符：%1").arg(manifest.error));
        return;
    }

    int localMajor = 0;
    int localMinor = 0;
    if (!parse_version(QStringLiteral(APP_VERSION), &localMajor, &localMinor)) {
        show_error(QStringLiteral("本地版本号格式无法识别：%1").arg(QStringLiteral(APP_VERSION)));
        return;
    }
    // 远端版本号的格式已在 parse_manifest 中校验过，这里直接取值
    int remoteMajor = 0;
    int remoteMinor = 0;
    parse_version(manifest.version, &remoteMajor, &remoteMinor);
    const bool hasNewer = remoteMajor > localMajor
        || (remoteMajor == localMajor && remoteMinor > localMinor);

    m_manifest = manifest;

    QString published = m_manifest.publishedAt;
    if (!published.isEmpty()) {
        // 只取到「日」这一级，完整时间戳对用户没有意义
        published = published.left(10);
        m_latestLabel->setText(QStringLiteral("最新版本：%1    发布于 %2")
                                   .arg(m_manifest.version, published));
    } else {
        m_latestLabel->setText(QStringLiteral("最新版本：%1").arg(m_manifest.version));
    }

    if (!hasNewer) {
        m_notesEdit->setPlainText(QStringLiteral("当前已是最新版本，无需更新。"));
        apply_state(StateUpToDate);
        return;
    }

    m_notesEdit->setPlainText(m_manifest.notes.isEmpty()
                                  ? QStringLiteral("（本次更新没有提供说明）")
                                  : m_manifest.notes);
    m_notesEdit->verticalScrollBar()->setValue(0);
    apply_state(StateAvailable);
}

/**
 * @brief       开始下载新版本
 *
 * 下载前先做可写性预检：目录可写就按「发布文件名」下到 exe 同目录，
 * 用户升级后在同一文件夹里就能看到新版程序；不可写则下到系统临时目录，
 * 校验通过后再另存到桌面，避免白下载两次。
 * 下载前先确认没有扫描或拉取任务在跑：否则会白下载一个包，校验通过后才被拒绝。
 * @return      none
 * @doxygen_sig_fp 373049a35e6e11ae
 */
void OnlineUpdateDialog::on_install()
{
    if (m_state != StateAvailable)
        return;

    // 扫描或拉取任务在跑时保持「发现新版本」状态，用户等它结束后可直接再点
    if (m_checkBusy && m_checkBusy()) {
        m_statusLabel->setStyleSheet(QStringLiteral("color: #d0021b;"));
        m_statusLabel->setText(QStringLiteral("仓库扫描或拉取任务正在进行，请等待其完成后再更新程序"));
        return;
    }

    const QString exeDir = UpdateApplier::get_exe_dir();
    // 只有「目录可写 + 路径可信」才下到 exe 同目录，否则下到系统临时目录
    if (UpdateApplier::check_dir_writable(exeDir) && UpdateApplier::check_exe_path_trusted())
        m_downloadPath = UpdateApplier::get_publish_path(m_manifest.version);
    else
        m_downloadPath = UpdateApplier::get_fallback_temp_path(m_manifest.version);

    if (m_downloadPath.isEmpty()) {
        show_error(QStringLiteral("版本号不合法，无法确定新版本文件名"));
        return;
    }

    const QUrl base(QString::fromLatin1(UPDATE_BASE_URL));
    const QUrl url = base.resolved(QUrl(m_manifest.url));

    apply_state(StateDownloading);
    m_progress->setValue(0);
    m_progressLabel->setText(QString());
    m_downloader->start(url, m_downloadPath, m_manifest.size);
}

/**
 * @brief       刷新进度条与速度显示
 *
 * total 为 -1（服务器没有返回长度）时把进度条切成忙碌态，
 * 但仍然显示已下载的字节数，让用户知道还在动。
 * @param[in]   {received} qint64 已下载字节数
 * @param[in]   {total} qint64 总字节数（-1 表示未知）
 * @param[in]   {bytesPerSec} double 瞬时速度（字节/秒）
 * @return      none
 * @doxygen_sig_fp 36c0be609fff2b72
 */
void OnlineUpdateDialog::on_download_progress(qint64 received, qint64 total, double bytesPerSec)
{
    if (total > 0) {
        m_progress->setRange(0, 100);
        m_progress->setValue(static_cast<int>(received * 100 / total));
        m_progressLabel->setText(QStringLiteral("已下载 %1 / %2    %3/s")
                                     .arg(format_size(received), format_size(total),
                                          format_size(static_cast<qint64>(bytesPerSec))));
    } else {
        m_progress->setRange(0, 0);
        m_progressLabel->setText(QStringLiteral("已下载 %1    %2/s")
                                     .arg(format_size(received),
                                          format_size(static_cast<qint64>(bytesPerSec))));
    }
}

/**
 * @brief       下载结束：成功则进入校验，失败则报错
 *
 * @param[in]   {ok} bool 是否下载成功
 * @param[in]   {error} const QString& 失败原因
 * @return      none
 * @doxygen_sig_fp dbba3a09814228db
 */
void OnlineUpdateDialog::on_download_finished(bool ok, const QString &error)
{
    if (!ok) {
        // 用户主动取消不算错误，回到 Idle 即可（临时文件已由下载器删除）
        if (error == QStringLiteral("下载已取消")) {
            m_progressLabel->setText(QString());
            apply_state(StateIdle);
            m_statusLabel->setText(QStringLiteral("下载已取消"));
            return;
        }
        show_error(error);
        return;
    }

    m_progress->setValue(100);
    apply_state(StateVerifying);
    // 让界面先把「正在校验」画出来，再做可能耗时的哈希计算
    QTimer::singleShot(50, this, &OnlineUpdateDialog::verify_download);
}

/**
 * @brief       校验下载结果：大小与 SHA256
 *
 * 两项都通过才算成功；任一不符即删除下载的文件，避免残缺或篡改的文件
 * 被后续步骤误用。
 * @return      none
 * @doxygen_sig_fp 1cb9edf8223f9d48
 */
void OnlineUpdateDialog::verify_download()
{
    QFile file(m_downloadPath);
    const qint64 actualSize = file.exists() ? file.size() : -1;

    if (actualSize != m_manifest.size) {
        QFile::remove(m_downloadPath);
        show_error(QStringLiteral("文件大小校验失败（期望 %1 字节，实际 %2 字节）")
                       .arg(m_manifest.size)
                       .arg(actualSize));
        return;
    }

    const QString actualSha = UpdateVerifier::get_file_sha256(m_downloadPath);
    if (actualSha.isEmpty()) {
        show_error(QStringLiteral("无法读取下载的文件以计算校验值"));
        return;
    }
    if (actualSha.compare(m_manifest.sha256, Qt::CaseInsensitive) != 0) {
        QFile::remove(m_downloadPath);
        show_error(QStringLiteral("文件完整性校验失败（SHA256 不匹配），已删除下载的文件"));
        return;
    }

    apply_or_save();
}

/**
 * @brief       校验通过后决定「直接启动新版本」还是「另存到桌面」
 *
 * 启动前还要确认扫描或拉取任务没在跑：那类任务会长时间占用
 * git 子进程并改写仓库工作区，此时切到新版本容易留下半截状态。
 * @return      none
 * @doxygen_sig_fp 3217b6c7ae81b98f
 */
void OnlineUpdateDialog::apply_or_save()
{
    // ① 目录不可写，或 exe 路径不可信（EVB 打包下可能是临时文件）：
    //    一律走「另存到桌面」，旧程序保持运行
    const bool writable = UpdateApplier::check_dir_writable(UpdateApplier::get_exe_dir());
    if (!writable || !UpdateApplier::check_exe_path_trusted()) {
        QString desktopPath;
        QString error;
        if (!UpdateApplier::save_to_desktop(m_downloadPath, m_manifest.version,
                                            &desktopPath, &error)) {
            show_error(error);
            return;
        }

        apply_state(StateManualInstall);
        const QString reason = writable
            ? QStringLiteral("当前运行的是打包版（程序路径在临时目录），无法自动切换版本。")
            : QStringLiteral("当前程序所在目录不可写（可能是 Program Files 或只读介质）。");
        // 步骤写进状态栏（可换行），更新说明区保持显示本次更新内容
        m_statusLabel->setText(reason
                               + QStringLiteral("\n新版本已保存到：%1\n"
                                                "请关闭本程序后直接运行这个文件。")
                                     .arg(QDir::toNativeSeparators(desktopPath)));
        return;
    }

    // ② 扫描或拉取任务在跑：不允许切换版本，提示稍后再来
    if (m_checkBusy && m_checkBusy()) {
        // 新版本暂时不能启动，删除已下载的文件，避免它残留在程序目录里
        QFile::remove(m_downloadPath);
        show_error(QStringLiteral("仓库扫描或拉取任务正在进行，请等待其完成后再更新程序"));
        return;
    }

    // ③ 直接启动新版本：新文件是独立完整程序，不覆盖也不改名正在运行的 exe
    apply_state(StateApplying);
    QString error;
    if (!UpdateApplier::start_new_version(m_downloadPath, &error)) {
        show_error(error);
        return;
    }

    // 启动成功：让状态文案先刷新出来，再退出当前进程
    m_statusLabel->setText(QStringLiteral("已启动新版本 %1，本窗口即将关闭…")
                               .arg(m_manifest.version));
    QTimer::singleShot(300, qApp, &QApplication::quit);
}

/**
 * @brief       显示错误并回到可重试状态
 *
 * @param[in]   {message} const QString& 错误说明
 * @return      none
 * @doxygen_sig_fp 6c0d29d737283006
 */
void OnlineUpdateDialog::show_error(const QString &message)
{
    release_replies();
    apply_state(StateError);
    m_statusLabel->setText(message);
}

/**
 * @brief       释放两个网络请求与超时定时器
 *
 * 检查更新失败、重新检查、窗口关闭前都要走这里，避免遗留的 reply
 * 在对象销毁后再回调回来。
 * @return      none
 * @doxygen_sig_fp dc74a3b74d7db467
 */
void OnlineUpdateDialog::release_replies()
{
    m_manifestTimer->stop();
    m_signatureTimer->stop();

    if (m_manifestReply) {
        QNetworkReply *reply = m_manifestReply;
        m_manifestReply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (m_signatureReply) {
        QNetworkReply *reply = m_signatureReply;
        m_signatureReply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

/**
 * @brief       取消下载或关闭窗口
 *
 * 下载中按钮是「取消下载」，只中止下载、窗口留着；
 * 其它状态下按钮是「关闭」，直接关窗。
 * @return      none
 * @doxygen_sig_fp 53dc8d799ede9933
 */
void OnlineUpdateDialog::on_cancel_or_close()
{
    if (m_state == StateDownloading) {
        m_downloader->cancel();
        return;
    }
    if (m_state == StateChecking) {
        release_replies();
        apply_state(StateIdle);
        m_statusLabel->setText(QStringLiteral("已取消检查"));
        return;
    }
    close();
}

/**
 * @brief       拦截窗口关闭
 *
 * 下载、校验、启动阶段不允许通过关闭按钮或窗口的 X 退出窗口：
 * 中途退出会留下半截下载文件，甚至出现「新版本已启动、旧窗口还在」的尴尬状态。
 * Esc 键走 reject()，见 reject() 的注释。
 * @param[out]  {event} QCloseEvent* 关闭事件
 * @return      none
 * @doxygen_sig_fp 1272a00194c2cf9b
 */
void OnlineUpdateDialog::closeEvent(QCloseEvent *event)
{
    // 任务阶段直接忽略关闭请求，窗口保持打开
    if (check_task_running()) {
        event->ignore();
        return;
    }

    m_downloader->cancel();
    release_replies();
    QDialog::closeEvent(event);
}

/**
 * @brief       Esc 键或 reject() 路径的关闭入口
 *
 * Qt 5.12 中 QDialog::reject() 直接隐藏窗口，不会触发 closeEvent，
 * 所以 closeEvent 里的保护对 Esc 无效。这里补上同样的判断：
 * 任务阶段直接返回，窗口保持打开；其余情况先做清理再交给 QDialog::reject()。
 * @return      none
 * @doxygen_sig_fp 12cd03239f7e2966
 */
void OnlineUpdateDialog::reject()
{
    if (check_task_running())
        return;

    m_downloader->cancel();
    release_replies();
    QDialog::reject();
}

/**
 * @brief       当前是否处于下载、校验或启动阶段
 *
 * 这三个阶段不允许中途关闭窗口，否则会留下半截文件或新旧版本并存的状态。
 * @return      处于上述阶段返回 true
 * @doxygen_sig_fp 06e1bd7b2602d505
 */
bool OnlineUpdateDialog::check_task_running() const
{
    return m_state == StateDownloading || m_state == StateVerifying || m_state == StateApplying;
}
