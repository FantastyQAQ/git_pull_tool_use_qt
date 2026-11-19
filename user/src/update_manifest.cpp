/**
 * @file update_manifest.cpp
 * @brief 在线更新清单解析：字段校验、版本号比较
 */

#include "update_manifest.h"

#include "update_config.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

/**
 * @brief       解析并校验清单的原始 JSON 字节
 *
 * 依次检查：JSON 可解析、schema 为 1、app 等于 UPDATE_APP_ID、
 * version/sha256/url/size 四项必填且格式正确。任一不符即写入 error 并返回 false，
 * 调用方据此拒绝后续下载，避免被伪造或放错的清单带到错误地址。
 * url 必须是相对路径且位于 releases/ 下，不允许绝对地址、反斜杠或 ..。
 * 注意：本函数只做「格式可信」层面的校验，内容可信依赖调用方先用公钥验签。
 * @param[in]   {raw} const QByteArray& 清单原始字节
 * @param[out]  {out} UpdateManifest* 解析结果
 * @return      校验通过返回 true
 * @doxygen_sig_fp c5d3681d92e98d0a
 */
bool parse_manifest(const QByteArray &raw, UpdateManifest *out)
{
    if (!out)
        return false;
    *out = UpdateManifest();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        out->error = QStringLiteral("清单不是合法的 JSON");
        return false;
    }

    const QJsonObject obj = doc.object();

    // schema 只认 1：将来格式变更时老客户端应明确拒绝，而不是猜着读
    out->schema = obj.value(QStringLiteral("schema")).toInt(-1);
    if (out->schema != 1) {
        out->error = QStringLiteral("清单格式版本不受支持（schema=%1）").arg(out->schema);
        return false;
    }

    // app 必须精确匹配，避免把别的应用的清单当成自己的
    out->appId = obj.value(QStringLiteral("app")).toString();
    if (out->appId != QLatin1String(UPDATE_APP_ID)) {
        out->error = QStringLiteral("更新清单不属于本程序（app=%1）").arg(out->appId);
        return false;
    }

    out->version = obj.value(QStringLiteral("version")).toString().trimmed();
    out->sha256 = obj.value(QStringLiteral("sha256")).toString().trimmed().toLower();
    out->url = obj.value(QStringLiteral("url")).toString().trimmed();
    out->publishedAt = obj.value(QStringLiteral("publishedAt")).toString().trimmed();
    out->notes = obj.value(QStringLiteral("notes")).toString();

    // size 用 toDouble 读再转整型：JSON 数字在 Qt 里是 double，直接 toInt 有截断风险
    out->size = static_cast<qint64>(obj.value(QStringLiteral("size")).toDouble(0));

    if (out->version.isEmpty()) {
        out->error = QStringLiteral("清单缺少 version");
        return false;
    }
    int major = 0;
    int minor = 0;
    if (!parse_version(out->version, &major, &minor)) {
        out->error = QStringLiteral("清单版本号格式无法识别：%1").arg(out->version);
        return false;
    }
    if (out->size <= 0) {
        out->error = QStringLiteral("清单 size 无效");
        return false;
    }
    // SHA256 必须是 64 位十六进制，否则后面的比对永远失败
    static const QRegularExpression shaRe(QStringLiteral("^[0-9a-f]{64}$"));
    if (!shaRe.match(out->sha256).hasMatch()) {
        out->error = QStringLiteral("清单 sha256 格式无效");
        return false;
    }
    // url 只允许 releases/ 下的相对路径：绝对地址、反斜杠与 .. 一律拒绝
    if (out->url.isEmpty() || out->url.contains(QStringLiteral(".."))
        || out->url.contains(QLatin1Char('\\')) || !QUrl(out->url).isRelative()
        || !out->url.startsWith(QStringLiteral("releases/"))) {
        out->error = QStringLiteral("清单 url 无效");
        return false;
    }

    return true;
}

/**
 * @brief       解析版本号的主版本与序号
 *
 * 版本号严格要求 SVnn.nnn 格式（如 SV01.011）：SV 之后是主版本，点号之后是序号。
 * 格式不符（包括缺少 SV、多出段落）一律返回 false，调用方应拒绝该版本，
 * 而不是猜测它的大小，避免 SV02.001 被误判成比 SV01.010 旧。
 * @param[in]   {version} const QString& 版本号字符串
 * @param[out]  {major} int* 主版本号
 * @param[out]  {minor} int* 序号
 * @return      格式符合返回 true
 * @doxygen_sig_fp 455d68f9b1ed12bb
 */
bool parse_version(const QString &version, int *major, int *minor)
{
    static const QRegularExpression re(QStringLiteral("^SV(\\d+)\\.(\\d+)$"));
    const QRegularExpressionMatch m = re.match(version.trimmed());
    if (!m.hasMatch())
        return false;
    if (major)
        *major = m.captured(1).toInt();
    if (minor)
        *minor = m.captured(2).toInt();
    return true;
}
