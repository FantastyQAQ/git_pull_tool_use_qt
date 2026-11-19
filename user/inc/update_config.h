#ifndef UPDATE_CONFIG_H
#define UPDATE_CONFIG_H

/*
 * 在线更新的集中配置（只放可调参数，具体逻辑见 update_* 系列文件）。
 * ------------------------------------------------------------------
 * 说明：
 *   UPDATE_BASE_URL 里的 IP 与端口一旦发布就不能再改：已分发的老版本
 *   客户端内置的就是这个地址，改了等于让老版本无法获取更新。
 */

// 应用标识：清单里的 app 字段必须与之一致，防止拿错清单
#define UPDATE_APP_ID            "git_pull_tool"

// 更新源基址（末尾必须带斜杠，清单与版本目录都相对它解析）
// 已定稿为线上实际地址：端口 8080、非标准端口 + HTTP，不备案（路线 A）
#define UPDATE_BASE_URL          "http://39.107.30.119:8080/git_pull_tool/"

// 检查清单（latest.json 与 latest.json.sig）的网络超时（毫秒）
#define UPDATE_CHECK_TIMEOUT_MS  10000

// 清单与签名文件的字节上限：正常清单只有几百字节，超过即视为异常内容
#define UPDATE_MANIFEST_MAX_BYTES 65536

// 下载过程中连续无数据的停滞超时（毫秒）；Qt 5.12 没有 setTransferTimeout，需自行计时
#define UPDATE_STALL_TIMEOUT_MS  30000

#endif // UPDATE_CONFIG_H
