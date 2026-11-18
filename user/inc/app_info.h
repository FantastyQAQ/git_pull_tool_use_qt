#ifndef APPINFO_H
#define APPINFO_H

/*
 * 应用元信息集中定义（方便随时修改，改这里即可，无需改动业务代码）。
 * ------------------------------------------------------------------
 * 说明：
 *   工具名 / 版本号 / 作者 / 邮箱均以宏表示，被主窗口标题、"关于"对话框、
 *   Windows VERSIONINFO 资源共同引用，避免多处硬编码、改一处漏一处。
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 用户可见的品牌信息 ===== */

// 工具名称（最终用户在窗口标题、exe 属性、About 对话框都看到这个名字）
#define APP_NAME            "Git 批量拉取工具"

// 原始文件名（VERSIONINFO OriginalFilename，必须与最终 exe 名字一致，
// 否则 SmartScreen / 资源管理器会把改动认作「可疑行为」加分）
#define APP_ORIGINAL_FILENAME  "git_pull_tool.exe"

// 发布者（VERSIONINFO CompanyName / LegalCopyright 用）
//   写公司名或个人开发者名都行；写「发布者未知」反而让 SmartScreen 更怀疑。
//   如果你后面有正式公司名，改这里一处即可。
#define APP_AUTHOR_NAME     "lch"

// 邮箱
#define APP_AUTHOR_MAIL     "fantasylch@foxmail.com"

// 版权年份
#define APP_COPYRIGHT_YEAR  "2026"

// 显示用的简短版本号字符串（窗口标题、关于对话框都用这个）
#define APP_VERSION         "SV01.013"

// 数字版本号（exe 属性里的「文件版本 / 产品版本」用，四段：主.次.补丁.构建）
//   必须与上面的 APP_VERSION 同步：SV01.013 对应 1,0,0,13
//   （末三位 "013" 即第四段，前三段固定为 1,0,0）
#define APP_VERSION_NUM     1,0,0,13

// 构建日期标签（形如 260914，即 26 年 09 月 14 日）
#define APP_BUILD_DATE      "261113"

/* ===== 完整标题 ===== */
#define APP_FULL_TITLE      APP_NAME " " APP_VERSION " [" APP_BUILD_DATE "]"

/* ===== exe 版本资源用的派生宏 ===== */
// 产品版本带构建日期标签（如 "SV01.003 [261108]"），文件版本只留版本号。
// git_pull_tool_resource.rc 直接引用 APP_VERSION、APP_VERSION_NUM 与本宏，
// windres 的预处理器支持字符串拼接与逗号展开，无需在 rc 里再写一份硬编码。
#define APP_PRODUCT_VERSION APP_VERSION " [" APP_BUILD_DATE "]"

#ifdef __cplusplus
}
#endif

#endif // APPINFO_H
