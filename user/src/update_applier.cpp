/**
 * @file update_applier.cpp
 * @brief 更新包落地实现：路径解析、可写性检查、启动新版、清理残留
 *
 * 为什么不再「重命名自替换」：
 *  - 老做法是把运行中的 exe 改名成 .old、再把下载的 .new 改名回原名。运行中的
 *    exe 虽然允许改名，但只要安全软件、资源管理器缩略图、云同步或备份工具
 *    短暂持有文件句柄，改名就会失败，用户最终只拿到一句报错和一个 .new 残骸；
 *  - 现在改为「按发布文件名直接落盘」（如 git_pull_tool_SV01.014.exe），
 *    校验通过后直接启动这个新文件并退出当前进程。整个过程完全不碰正在运行的
 *    exe，既没有改名动作，也不会被文件占用卡住；
 *  - 代价是旧版本文件会留在原目录（与历史版本并排），由用户自行删除。
 */

#include "update_applier.h"

#include "app_info.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

/// 诊断用环境变量：EVB 打包下若自动推断失败，可手动指定真实 exe 路径
const char kExePathEnvVar[] = "GIT_PULL_TOOL_EXE";

/**
 * @brief 判断路径是否落在系统临时目录下
 *
 * EVB（Enigma Virtual Box）打包运行时，进程映像可能被映射到临时目录，
 * 此时 applicationFilePath() 返回的是临时文件路径而不是用户手里的 exe，
 * 需要换用别的来源推断。
 * @param path 待判断的绝对路径
 * @return 位于临时目录返回 true
 * @doxygen_sig_fp 88b18792bc66216d
 */
bool check_in_temp_dir(const QString &path)
{
    QString tempRoot = QDir::fromNativeSeparators(QDir::tempPath()).toLower();
    if (tempRoot.isEmpty())
        return false;
    // 末尾补上 /，避免 ...\Temp 误匹配 ...\Temp Tools\ 之类同前缀的目录
    tempRoot += QLatin1Char('/');
    return QDir::fromNativeSeparators(path).toLower().startsWith(tempRoot);
}

} // namespace

/**
 * @brief       取当前 exe 的真实路径
 *
 * 路径来源按优先级：
 *  1. 环境变量 GIT_PULL_TOOL_EXE（仅诊断/救急用，正常分发不设置）；
 *  2. QCoreApplication::applicationFilePath()，且不在系统临时目录下；
 *  3. 若上一步落在临时目录（EVB 把映像映射到了临时文件），退回
 *     「当前工作目录 + 原始文件名」，并要求该文件确实存在。
 *
 * 之所以要第 3 步：更新文件放错目录会让用户找不到新版程序，宁可退回工作目录
 * 也不能拿临时文件当落地位置。
 * @return      exe 绝对路径（正斜杠形式）；全部推断失败时返回applicationFilePath()
 * @doxygen_sig_fp d48ef81eea095933
 */
QString UpdateApplier::get_exe_path()
{
    // ① 环境变量优先，便于现场排查
    const QString override =
        QProcessEnvironment::systemEnvironment().value(QLatin1String(kExePathEnvVar)).trimmed();
    if (!override.isEmpty())
        return QDir::fromNativeSeparators(override);

    // ② 常规情况：进程映像就是用户双击的那个 exe
    const QString appPath = QDir::fromNativeSeparators(QCoreApplication::applicationFilePath());
    if (!check_in_temp_dir(appPath))
        return appPath;

    // ③ EVB 映射到临时文件：退回工作目录找同名文件
    const QString candidate =
        QDir::fromNativeSeparators(QDir::current().filePath(QLatin1String(APP_ORIGINAL_FILENAME)));
    if (QFile::exists(candidate))
        return candidate;

    return appPath;
}

/**
 * @brief       判断当前 exe 路径是否可信（可安全用于定位落地位置）
 *
 * 若 exe 路径落在系统临时目录，说明程序经 EVB 等打包工具映射运行，
 * 这个路径随时可能被系统清理，往它旁边放新版本等于把文件写进垃圾区。
 * 这种情况下一律不自替换，改为「另存到桌面 + 手动运行」的兜底流程。
 * @return      路径可信返回 true
 * @doxygen_sig_fp a67ce74aded7fa57
 */
bool UpdateApplier::check_exe_path_trusted()
{
    return !check_in_temp_dir(get_exe_path());
}

/**
 * @brief       取 exe 所在目录
 *
 * @return      目录绝对路径（正斜杠形式）
 * @doxygen_sig_fp 874ce10be1600343
 */
QString UpdateApplier::get_exe_dir()
{
    return QFileInfo(get_exe_path()).absolutePath();
}

/**
 * @brief       判断目录是否可写
 *
 * 用「实际创建再删除一个探针文件」判断，比查文件属性可靠：
 * Program Files 下的目录属性显示可写，但普通权限实际无法创建文件。
 * @param[in]   {dirPath} const QString& 待检查的目录
 * @return      能成功创建文件返回 true
 * @doxygen_sig_fp fa6b82449b58c0bb
 */
bool UpdateApplier::check_dir_writable(const QString &dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists())
        return false;

    QFile probe(dir.filePath(QStringLiteral(".update_write_probe")));
    if (!probe.open(QIODevice::WriteOnly))
        return false;
    probe.close();
    probe.remove();
    return true;
}

/**
 * @brief       生成新版本文件的正式文件名
 *
 * 以当前 exe 的文件名为基准，先去掉末尾的版本段，再拼上清单里的新版本号，
 * 例如 git_pull_tool_SV01.013.exe + SV01.014 → git_pull_tool_SV01.014.exe。
 * 这样落盘的名字与服务器上发布的文件名完全一致，用户下载后能直接认出是哪个版本。
 *
 * version 来自网络清单，虽然已验签，这里仍做一次白名单校验，
 * 防止出现路径分隔符或 .. 之类把文件写到别处。
 * @param[in]   {version} const QString& 新版本号（如 SV01.011）
 * @return      文件名；版本号不合法时返回空字符串
 * @doxygen_sig_fp 9d634a79a1ec429e
 */
QString UpdateApplier::get_publish_name(const QString &version)
{
    static const QRegularExpression validVersion(QStringLiteral("^[A-Za-z0-9._-]+$"));
    if (version.isEmpty() || !validVersion.match(version).hasMatch())
        return QString();

    // 去掉当前文件名末尾的版本段，得到不含版本号的基础名
    QString baseName = QFileInfo(get_exe_path()).completeBaseName();
    static const QRegularExpression versionSuffix(QStringLiteral("[_-]?SV\\d+\\.\\d+$"));
    baseName.remove(versionSuffix);
    if (baseName.isEmpty())
        baseName = QFileInfo(QLatin1String(APP_ORIGINAL_FILENAME)).completeBaseName();

    return baseName + QLatin1Char('_') + version + QStringLiteral(".exe");
}

/**
 * @brief       exe 同目录下的新版本文件路径
 *
 * 与当前 exe 放同一目录，用户升级后就在同一个文件夹里看到新旧两个版本。
 * @param[in]   {version} const QString& 新版本号
 * @return      绝对路径；版本号不合法时返回空字符串
 * @doxygen_sig_fp d2396f79ab5d8cb7
 */
QString UpdateApplier::get_publish_path(const QString &version)
{
    const QString name = get_publish_name(version);
    if (name.isEmpty())
        return QString();
    return QDir(get_exe_dir()).filePath(name);
}

/**
 * @brief       系统临时目录的下载路径
 *
 * 仅用于 exe 所在目录不可写时的兜底：先把包下载到临时目录，
 * 校验通过后再复制到桌面（同目录放不下，也不能污染目标目录）。
 * @param[in]   {version} const QString& 新版本号
 * @return      系统临时目录下的下载路径；版本号不合法时返回空字符串
 * @doxygen_sig_fp b099557eac013ed6
 */
QString UpdateApplier::get_fallback_temp_path(const QString &version)
{
    const QString name = get_publish_name(version);
    if (name.isEmpty())
        return QString();
    return QDir(QDir::tempPath()).filePath(name);
}

/**
 * @brief       桌面兜底文件路径
 *
 * 用于 exe 所在目录不可写（如 Program Files）时另存新版本，
 * 由用户关闭程序后直接运行桌面上的这个文件。
 * @param[in]   {version} const QString& 新版本号（如 SV01.011）
 * @return      桌面上的目标路径；版本号不合法时返回空字符串
 * @doxygen_sig_fp 03b17572842f26eb
 */
QString UpdateApplier::get_desktop_path(const QString &version)
{
    const QString name = get_publish_name(version);
    if (name.isEmpty())
        return QString();

    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    return QDir(desktop).filePath(name);
}

/**
 * @brief       启动已下载并校验通过的新版本
 *
 * 只做「启动 + 报告结果」，不做任何文件改名或替换：
 * 新版本是以正式文件名独立存在的完整程序，旧版本继续留在原目录，
 * 因此不存在「旧程序被占用导致改名失败」的问题。
 * 工作目录设为 exe 所在目录，与用户手动双击的行为保持一致。
 * @param[in]   {newExePath} const QString& 新版本文件绝对路径（已通过大小与 SHA256 校验）
 * @param[out]  {error} QString* 失败原因
 * @return      启动成功返回 true（成功后调用方应立即退出当前进程）
 * @doxygen_sig_fp ecb4526ee4ef7870
 */
bool UpdateApplier::start_new_version(const QString &newExePath, QString *error)
{
    if (newExePath.isEmpty() || !QFile::exists(newExePath)) {
        if (error)
            *error = QStringLiteral("未找到已下载的新版本文件");
        return false;
    }

    if (!QProcess::startDetached(newExePath, QStringList(), get_exe_dir())) {
        if (error)
            *error = QStringLiteral("新版本已下载但启动失败，请关闭本程序后手动运行：\n%1")
                         .arg(QDir::toNativeSeparators(newExePath));
        return false;
    }

    return true;
}

/**
 * @brief       启动时清理旧版替换方案留下的残留文件
 *
 * 必须在构建界面之前调用（main.cpp 里越早越好）。
 * 现行方案不再产生 .old / .new，这里只为兼容老版本可能留下的残骸：
 *  - .old 存在且正式 exe 也在 → 上次替换已完成，删掉 .old；
 *  - .old 存在但正式 exe 不在 → 上次替换中途失败或断电，把 .old 改回原名；
 *  - 同目录的 .new 与临时目录的 .new → 上次下载未完成，直接删除。
 *
 * 删除失败一律忽略：文件可能仍被安全软件占用，下次启动会再试。
 * @return      none
 * @doxygen_sig_fp 720012e055db45d2
 */
void UpdateApplier::cleanup_leftovers()
{
    const QString exePath = get_exe_path();
    const QString oldPath = exePath + QStringLiteral(".old");

    if (QFile::exists(oldPath)) {
        if (QFile::exists(exePath)) {
            QFile::remove(oldPath);
        } else {
            // 正式 exe 不见了，说明替换过程被打断，恢复旧版优先
            QFile::rename(oldPath, exePath);
        }
    }

    // 同目录与临时目录的残留下载文件都清掉（老版本以 .new 结尾）
    QFile::remove(exePath + QStringLiteral(".new"));
    QFile::remove(QDir(QDir::tempPath())
                      .filePath(QLatin1String(APP_ORIGINAL_FILENAME) + QStringLiteral(".new")));
}

/**
 * @brief       把下载好的新版本另存到桌面（不可写目录兜底）
 *
 * exe 所在目录不可写时无法把新版本放到程序旁边：新版本先下载到系统临时目录，
 * 校验通过后复制到桌面（保持正式文件名），用户关闭本程序后直接运行即可。
 * @param[in]   {sourcePath} const QString& 已校验的新版本文件路径
 * @param[in]   {version} const QString& 新版本号
 * @param[out]  {outPath} QString* 另存后的桌面文件路径
 * @param[out]  {error} QString* 失败原因
 * @return      另存成功返回 true
 * @doxygen_sig_fp 6b8be0a6737026c8
 */
bool UpdateApplier::save_to_desktop(const QString &sourcePath, const QString &version,
                                    QString *outPath, QString *error)
{
    const QString desktopPath = get_desktop_path(version);
    if (desktopPath.isEmpty()) {
        if (error)
            *error = QStringLiteral("版本号不合法，无法确定新版本文件名");
        return false;
    }

    if (!QFile::exists(sourcePath)) {
        if (error)
            *error = QStringLiteral("未找到已下载的新版本文件");
        return false;
    }

    // 桌面已有同名旧文件时先删掉：QFile::copy 不会覆盖已存在的目标
    if (QFile::exists(desktopPath) && !QFile::remove(desktopPath)) {
        if (error)
            *error = QStringLiteral("桌面上的同名旧文件无法删除，请先手动移除");
        return false;
    }

    if (!QFile::copy(sourcePath, desktopPath)) {
        if (error)
            *error = QStringLiteral("复制到桌面失败，请检查桌面目录权限");
        return false;
    }

    // 复制完成后临时文件就没有用了，删掉避免临时目录一直堆版本
    QFile::remove(sourcePath);

    if (outPath)
        *outPath = desktopPath;
    return true;
}
