#ifndef UPDATE_APPLIER_H
#define UPDATE_APPLIER_H

#include <QString>

/// @brief 更新包落地：路径解析、可写性检查、启动新版、清理残留
class UpdateApplier
{
public:
    /// @brief 当前 exe 所在目录（EVB 打包下也用于定位真实文件）
    static QString get_exe_dir();

    /// @brief 当前 exe 的真实路径
    static QString get_exe_path();

    /// @brief 当前 exe 路径是否可信（EVB 打包下可能指向临时文件）
    static bool check_exe_path_trusted();

    /// @brief 目录是否可写（不可写时走「另存到桌面」兜底）
    static bool check_dir_writable(const QString &dirPath);

    /// @brief 新版本文件的正式文件名（与服务器上发布的文件名一致）
    static QString get_publish_name(const QString &version);

    /// @brief exe 同目录下的新版本文件路径
    static QString get_publish_path(const QString &version);

    /// @brief 系统临时目录的下载路径（exe 目录不可写时使用）
    static QString get_fallback_temp_path(const QString &version);

    /// @brief 桌面兜底文件路径（桌面 + 正式文件名）
    static QString get_desktop_path(const QString &version);

    /// @brief 启动已下载并校验通过的新版本
    /// @param newExePath 新版本文件的绝对路径
    /// @param error 失败原因
    /// @return 启动成功返回 true（成功后调用方退出当前进程）
    static bool start_new_version(const QString &newExePath, QString *error);

    /// @brief 启动时清理旧版替换方案留下的 .old / .new 残留
    static void cleanup_leftovers();

    /// @brief 把下载好的新版本另存到桌面（不可写目录兜底）
    static bool save_to_desktop(const QString &sourcePath, const QString &version,
                                QString *outPath, QString *error);
};

#endif // UPDATE_APPLIER_H
