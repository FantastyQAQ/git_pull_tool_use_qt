#ifndef UPDATE_VERIFIER_H
#define UPDATE_VERIFIER_H

#include <QByteArray>
#include <QString>

/// @brief 更新包校验：清单 RSA 验签（Windows BCrypt）+ 文件 SHA256
class UpdateVerifier
{
public:
    /// @brief 用内置公钥校验清单签名的原始字节
    static bool check_signature(const QByteArray &data, const QByteArray &signature);

    /// @brief 分块计算文件 SHA256（小写十六进制），失败返回空串
    static QString get_file_sha256(const QString &path);
};

#endif // UPDATE_VERIFIER_H
