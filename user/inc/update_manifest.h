#ifndef UPDATE_MANIFEST_H
#define UPDATE_MANIFEST_H

#include <QByteArray>
#include <QString>

/**
 * @brief 在线更新清单（latest.json）解析结果
 *
 * 只承载「解析并校验通过」的数据；解析失败时通过 error 返回原因，
 * valid 保持 false，调用方据此拒绝后续流程。
 */
struct UpdateManifest
{
    int schema = 0;          ///< 清单格式版本，目前只认 1
    QString appId;           ///< 应用标识，必须等于 UPDATE_APP_ID
    QString version;         ///< 显示版本号（如 SV01.010）
    qint64 size = 0;         ///< exe 字节数
    QString sha256;          ///< exe 的 SHA256（小写十六进制）
    QString url;             ///< exe 的相对路径（相对 UPDATE_BASE_URL）
    QString publishedAt;     ///< 发布时间（仅用于展示）
    QString notes;           ///< 更新说明（纯文本，\n 换行）
    QString error;           ///< 校验失败原因（valid 为 false 时有效）

    /// @brief 清单是否已通过基本校验
    bool check_valid() const { return error.isEmpty(); }
};

/**
 * @brief 解析并校验清单的原始 JSON 字节
 * @param raw 清单原始字节（必须与签名时的字节完全一致）
 * @param out 解析结果
 * @return 解析并校验通过返回 true
 */
bool parse_manifest(const QByteArray &raw, UpdateManifest *out);

/**
 * @brief 解析版本号（SV01.011 → 主版本 1、序号 11）
 * @param version 版本号字符串，必须严格形如 SVnn.nnn
 * @param major 输出主版本号
 * @param minor 输出序号
 * @return 格式符合返回 true
 */
bool parse_version(const QString &version, int *major, int *minor);

#endif // UPDATE_MANIFEST_H
