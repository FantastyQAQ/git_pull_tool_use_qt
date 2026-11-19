/**
 * @file update_verifier.cpp
 * @brief 更新包校验实现：清单 RSA-2048 验签（Windows BCrypt）+ 文件 SHA256
 *
 * 为什么用 BCrypt 而不用 OpenSSL：
 *  - HTTP 明文传输无法保证内容可信，清单签名是整条更新通道的安全核心；
 *  - Windows 自带 CNG，不需要往工程里引入 OpenSSL 运行库，分发体积更小。
 *
 * 对应的服务端签名命令（PKCS#1 v1.5 + SHA-256，与 OpenSSL 默认输出一致）：
 *   openssl dgst -sha256 -sign update_sign_private.pem -out latest.json.sig latest.json
 */

#include "update_verifier.h"

#include <QCryptographicHash>
#include <QFile>

#include <windows.h>
#include <bcrypt.h>

#include <cstring>
#include <vector>

namespace {

// ===== 内置 RSA 公钥（指数固定 65537，模数 2048 位）=====
// 模数来自 `openssl rsa -pubin -in update_sign_public.pem -noout -modulus` 的输出，
// 只保存公钥：私钥仅留在开发者本机与离线介质，绝不进工程目录。
const char kRsaModulusHex[] =
    "A6E2CD2B6734138F1AD0B38E8AF57B013A7C8694846A156750FCE809DFCCC144"
    "402C9FF78307090A794E068387486E8DAB2F23ABAF0A9FEC55F5D4DA4E87B720"
    "1DE151FDD0BEC287052BEE16FC75F5439218367F395490E474DE0991B2A9966D"
    "5A9173535C01ECC13CDBB2C4593A1304772A006536DCB98FC1271E2B09D8DF03"
    "2FD6A894177C66D0028A1737B9F441A55D70BD48B079A46244C35A1DA236B4D1"
    "DC635715D59BCFFA5F7DB7BAB559E7EF6911EE0033A488DEC0427302203F9566"
    "5E921654AA850FAB272B4380D720605C904B93DA9DF1BA16B6F5BA65AE743B5A"
    "E660C372F7A9D7E95A999BC31FCCB0CBE86B5FEEBEEC1FF3E1CB325BDD497479";

// 公钥指数固定 65537，用大端三字节表示（BCRYPT_RSAKEY_BLOB 要求大端）
const unsigned char kRsaPublicExp[] = {0x01, 0x00, 0x01};

// RSA-2048 的模数与签名长度都是 256 字节
const int kRsaBytes = 256;

// ===== BCrypt 句柄的轻量 RAII 守卫 =====
// CNG 的句柄必须成对释放（算法提供者 / 密钥），任何一条失败路径漏放都会泄漏。
// 这里用一个小结构体在析构时释放，避免在多层 if 判断里重复写释放代码。
/// @brief BCrypt 句柄守卫：析构时依次销毁密钥与算法提供者
struct CngHandles
{
    BCRYPT_ALG_HANDLE alg = nullptr;  ///< 算法提供者句柄
    BCRYPT_KEY_HANDLE key = nullptr;  ///< 导入后的公钥句柄

    /**
     * @brief       析构时依次释放 BCrypt 密钥与算法提供者句柄
     *
     * @return      none（构造/析构函数，无返回值）
     * @retval      none
     * @details     先销毁密钥句柄，再关闭算法提供者；句柄为空时跳过，保证多层失败路径下都不泄漏。
     * @doxygen_sig_fp 26ccecca13da8754
     */
    ~CngHandles()
    {
        if (key)
            BCryptDestroyKey(key);
        if (alg)
            BCryptCloseAlgorithmProvider(alg, 0);
    }
};

/**
 * @brief 把十六进制模数字符串解析成字节数组
 * @param hex 大端十六进制字符串
 * @return 解析出的字节；长度或字符不合法时返回空数组
 * @doxygen_sig_fp f6c8ce5684bd7e6d
 */
QByteArray get_modulus_bytes(const QByteArray &hex)
{
    if (hex.size() != kRsaBytes * 2)
        return QByteArray();
    const QByteArray bytes = QByteArray::fromHex(hex);
    if (bytes.size() != kRsaBytes)
        return QByteArray();
    return bytes;
}

} // namespace

/**
 * @brief       用内置公钥校验清单签名的原始字节
 *
 * 流程（对应 BCrypt 的标准用法）：
 *  1. 打开 RSA 算法提供者；
 *  2. 按 BCRYPT_RSAPUBLIC_BLOB 组装公钥（头部 + 指数 + 模数，均大端）；
 *  3. 计算数据的 SHA256；
 *  4. 以 PKCS#1 v1.5 + SHA-256 验证签名。
 *
 * 签名字节序：BCryptVerifySignature 接受的就是 OpenSSL 输出的大端字节，
 * 可直接传入，无需反转（已实测：反转后返回 STATUS_INVALID_PARAMETER）。
 * @param[in]   {data} const QByteArray& 被签名的原始字节（清单文件内容）
 * @param[in]   {signature} const QByteArray& 签名文件内容（.sig 的原始字节）
 * @return      验签通过返回 true
 * @doxygen_sig_fp 225a6808b26e1a95
 */
bool UpdateVerifier::check_signature(const QByteArray &data, const QByteArray &signature)
{
    if (data.isEmpty() || signature.size() != kRsaBytes)
        return false;

    const QByteArray modulus = get_modulus_bytes(QByteArray(kRsaModulusHex));
    if (modulus.isEmpty())
        return false;

    CngHandles handles;

    // ① 打开 RSA 算法提供者
    if (BCryptOpenAlgorithmProvider(&handles.alg, BCRYPT_RSA_ALGORITHM, nullptr, 0) != 0)
        return false;

    // ② 组装 BCRYPT_RSAPUBLIC_BLOB：定长头 + 指数 + 模数
    BCRYPT_RSAKEY_BLOB header;
    header.Magic = BCRYPT_RSAPUBLIC_MAGIC;
    header.BitLength = kRsaBytes * 8;
    header.cbPublicExp = sizeof(kRsaPublicExp);
    header.cbModulus = kRsaBytes;
    header.cbPrime1 = 0;
    header.cbPrime2 = 0;

    std::vector<unsigned char> blob(sizeof(header) + sizeof(kRsaPublicExp) + kRsaBytes);
    unsigned char *cursor = blob.data();
    memcpy(cursor, &header, sizeof(header));
    cursor += sizeof(header);
    memcpy(cursor, kRsaPublicExp, sizeof(kRsaPublicExp));
    cursor += sizeof(kRsaPublicExp);
    memcpy(cursor, modulus.constData(), kRsaBytes);

    if (BCryptImportKeyPair(handles.alg, nullptr, BCRYPT_RSAPUBLIC_BLOB, &handles.key,
                            blob.data(), static_cast<ULONG>(blob.size()), 0)
        != 0)
        return false;

    // ③ 计算被签名数据的 SHA256
    const QByteArray digest = QCryptographicHash::hash(data, QCryptographicHash::Sha256);

    // ④ 按 PKCS#1 v1.5 + SHA-256 验证签名（签名字节直接使用，不做字节序转换）
    BCRYPT_PKCS1_PADDING_INFO padInfo;
    memset(&padInfo, 0, sizeof(padInfo));
    padInfo.pszAlgId = BCRYPT_SHA256_ALGORITHM;

    QByteArray signatureCopy(signature);   // BCrypt 的入参非 const，复制一份避免强转
    const NTSTATUS status = BCryptVerifySignature(
        handles.key, &padInfo,
        reinterpret_cast<PUCHAR>(const_cast<char *>(digest.constData())),
        static_cast<ULONG>(digest.size()),
        reinterpret_cast<PUCHAR>(signatureCopy.data()),
        static_cast<ULONG>(signatureCopy.size()), BCRYPT_PAD_PKCS1);

    return status == 0;
}

/**
 * @brief       分块计算文件 SHA256
 *
 * 按 1 MB 分块读取，避免把十几兆的 exe 整个读进内存。
 * 结果统一转小写十六进制，便于与清单里的 sha256 字段直接做字符串比较。
 * @param[in]   {path} const QString& 待校验文件的绝对路径
 * @return      小写十六进制 SHA256；文件打不开或读失败返回空串
 * @doxygen_sig_fp d399df74b24b8cac
 */
QString UpdateVerifier::get_file_sha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();

    return QString::fromLatin1(hash.result().toHex());
}
