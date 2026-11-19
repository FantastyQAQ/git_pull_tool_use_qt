# Qt 桌面应用在线更新通用方案

> 状态：P0 试点已落地（dsh_plugin_updater，落地方式见 3.1），P1 起待实施
> 适用范围：`E:\code\qt` 下的 Qt 桌面工具（见第一节）
> 参考实现：`docs/online_update_plan.md`（dsh_plugin_updater 的具体方案）
> 目标：一套服务器、一套客户端模块，多个应用共用，每个应用只做少量接入工作

## 〇、本文与具体方案的关系

- `docs/online_update_plan.md` 是 **dsh_plugin_updater 的落地方案**，包含具体端口、目录与代码细节。
- 本文是 **通用方案**，把可复用的部分抽出来：服务器布局、清单格式、签名密钥、共享客户端模块、接入清单、发布流程。
- 实施顺序上，dsh_plugin_updater 作为第一个试点，跑通后再接入其它应用。

## 一、适用应用现状盘点

| 应用 | TARGET（建议作为 APP_ID） | 当前版本 | 网络模块 | 源码编码 | 接入需注意 |
|---|---|---|---|---|---|
| DSH 插件更新工具 | `dsh_plugin_updater` | SV01.011 | 已有 `network` | UTF-8 | 试点应用（P0 已完成）；EVB 单 exe 分发；已有插件更新任务 |
| Git 批量拉取工具 | `git_pull_tool` | SV01.013 | 需新增 `network` | 待检查 | 已有 `UpdateNotesDialog`（更新说明） |
| OpenCode 代理配置工具 | `opencode_proxy_tool` | SV01.002 | 已有 `network` | 待检查 | 已有 `UpdateDialog` 类（更新说明），新类名需避开 |
| VS Code 远程连接修复器 | `vscode_remote_fixer` | SV01.001 | 已有 `network` | 待检查 | 已有 `UpdateNotesDialog`；有 SSH 长任务 |
| ESP32-S3 工具 | `esp32_s3_tool` | SV01.006 | 已有 `network` | **GBK** | `.pro` 指定 `-finput-charset=GBK`；Qt 最低 5.12；无 `.rc` 版本资源 |
| 协议分析工具（中文名待确认） | `protocol_analyzer` | 待确认 | 待确认 | 待确认 | 尚未接入，接入前先补全本表 |

**共同点**：

- 全部使用 `user/inc/app_info.h` 集中管理元信息，版本号格式统一为 `SVxx.NNN`。
- 全部使用 `user/inc` + `user/src` 目录结构，`.pro` 写法相近。
- 全部为 Qt 5.12.8 + MinGW 32 位（ESP32 工程最低要求 5.12.0）。
- 不需要安装，直接运行 exe。

**不纳入**：`QWidgetDemo` 是多个示例的 subdirs 工程，不属于发布的应用。

**需要提前检查**：所有应用的源文件编码（见第七节 7.4）。ESP32 工程已确认是 GBK，其它五个未确认。

## 二、设计原则

1. **一台服务器、一个端口、多个应用**：通过目录区分应用，清单按应用隔离。
2. **每个应用一对签名密钥**：公钥编进各自 exe，某个应用的私钥泄露不影响其它应用。
3. **客户端核心逻辑与界面分离**：核心模块不含界面和中文文案，中文文案由各应用提供。
4. **更新前后由应用决定何时可以切换版本**：每个应用通过回调声明「当前是否忙」「退出前做什么」。
5. **共享代码只维护一份**：通过 qmake `.pri` 引用共享目录，不复制粘贴。
6. **发布流程脚本化**：一个发布脚本适用于所有应用，参数只有应用 ID、版本与路径。
7. **沿用现有工程约定**：`release/` 构建目录、`app_info.h` 三宏、Doxygen 注释、中文文档等，遵循各工程的 AGENTS.md。
8. **命名统一**：新增的源文件名与函数名使用 snake_case（如 `update_manifest.cpp`、`cleanup_leftovers()`），类型名保持 PascalCase（如 `OnlineUpdateService`）。

## 三、总体架构

```
共享目录（一份）                              各应用（接入）
E:\code\qt\_shared\online_update\             E:\code\qt\<应用>\
  ├─ online_update.pri                  ─►     .pro 中 include 上述 .pri
  ├─ src/ inc/  （核心模块，UI 无关）         user/src/update_app.cpp（应用 ID、地址、公钥）
  ├─ dialog/   （通用更新窗口，文案外置）      user/src/main_window.cpp（按钮与回调）
  ├─ tools/    （发布脚本、密钥脚本）
  └─ selftest/ （自测程序，验证验签与清单解析）

服务器（阿里云 ECS，一台，已装宝塔面板）
  Nginx :8080
  /www/wwwroot/qttools_updates/        # 站点根，URL 中不可见
    ├─ dsh_plugin_updater/   latest.json  latest.json.sig  releases/<版本>/...
    ├─ git_pull_tool/     ...
    ├─ opencode_proxy_tool/  ...
    ├─ esp32_s3_tool/        ...
    ├─ vscode_remote_fixer/  ...
    └─ protocol_analyzer/    ...
```

> 站点根目录名只影响服务器上的物理路径，不进入 URL；客户端地址始终是
> `http://39.107.30.119:8080/<APP_ID>/`，所以重命名站点根目录对已发布客户端无影响。

> **实测环境差异**：目标服务器是宝塔面板环境（Nginx 由面板管理，运行用户 `www`，
> 站点配置在 `/www/server/panel/vhost/nginx/`），因此实际根目录为
> `/www/wwwroot/qttools_updates/`，而非早期假设的 `/srv/updates/`。
> 多应用共用时仍在同一根目录下按 `<APP_ID>` 分目录即可，不影响本方案结构。
> 部署细节见 `server_setup_guide.md`；裸装 Nginx 的 `setup_server.sh` 在宝塔环境不适用。

**客户端数据流**（与 dsh_plugin_updater 方案一致）：

1. 请求 `<base>/<APP_ID>/latest.json` 与 `.sig`。
2. 用本应用公钥验签清单原始字节。
3. 检查 `app == APP_ID`、`schema == 1`，比较版本号。
4. 用户确认后下载 exe，显示进度条。
5. 大小与 SHA256 校验通过。
6. 调用应用的 `can_replace_now` 回调，忙则提示等待。
7. 调用应用的 `prepare_for_exit` 回调，保存状态、停止后台任务。
8. 新版本按发布文件名落到同目录，启动新版，退出旧进程（不改名、不覆盖运行中的 exe）。

### 3.1 落地方式（P0 试点定稿，各应用统一照此实现）

**结论：不做「重命名替换运行中的 exe」，改为「新版本按发布文件名独立落盘 + 直接启动」。**

做法：

1. 下载目标路径 = `<应用 exe 所在目录>/<TARGET>_<清单版本>.exe`，例如
   `dsh_plugin_updater_SV01.011.exe`。文件名中的版本段一律取清单里的 `version`，
   并先做 `[A-Za-z0-9._-]` 白名单校验（防清单塞路径分隔符或 `..`）。
2. 校验通过后 `QProcess::startDetached(新版本路径, {}, exe目录)` 启动新版本。
3. 启动成功则退出旧进程；旧版本文件**保留在原目录**，由用户自行删除。
4. exe 目录不可写、或 exe 路径落在系统临时目录（打包工具把映像映射到临时文件）时，
   改把新版本另存到桌面（同样是正式文件名），由用户手动运行。
5. 启动时清理只管老方案残留：`.old` 在正式 exe 缺失时改回原名，`.new` 直接删除。

**为什么统一采用这一条**：P0 试点中，原「改名替换」方案在真实升级里第一步就失败了——
运行中的 exe 被安全软件/资源管理器/云同步短暂持有句柄，改名报 `Access denied`，
用户最终只拿到一个 `.new` 残骸和一句报错。而「按发布文件名落盘」全程不碰运行中的
exe，只要求目录可写，失败时也能留下一个可直接双击运行的正式版本文件。

**对共享模块的约束**：

- 文件名推导只实现一次（`OnlineUpdateApplier::get_publish_name()/get_publish_path()`），
  各应用不得自行拼文件名。
- 各应用打包产物文件名必须遵循 `<TARGET>_<版本>.exe`；若产物不带版本号，
  客户端仍会生成带版本号的新文件，与服务器文件名不一致但不影响更新（校验用的是
  清单里的 size/sha256，不比对文件名）。
- 旧版本文件会随升级次数累积（与各应用已有的 `_SV01.009.exe`、`_SV01.010.exe`
  形态一致）。自动清理历史版本属后续优化项，本期统一不做。

## 四、服务器（一台 ECS 托管所有应用）

### 4.1 端口与备案

- 沿用 dsh_plugin_updater 方案的路线 A：**非标准端口 + HTTP，不做 ICP 备案**。
- 所有应用共用一个端口（示例 8080），阶段 0 实测确认可达即可用于全部应用。
- 端口与公网 IP 一经确定不要更改。已分发的任何应用版本都内置了这个地址，变更会导致全部老版本失效。建议绑定弹性公网 IP。

### 4.2 目录与应用 ID 规则

- 应用 ID 只允许 `[a-z0-9_]+`，与 `.pro` 的 `TARGET` 一致，一经确定不再修改。
- 每个应用一个子目录，结构相同：

```
/www/wwwroot/qttools_updates/<APP_ID>/
├── latest.json
├── latest.json.sig
└── releases/
    ├── SV01.009/<打包产物文件名>
    └── SV01.010/<打包产物文件名>
```

- 版本目录一经发布不再修改。
- 旧版本保留策略：每个应用保留最近 3 个版本；更早版本的目录由发布脚本清理，清理前需确认没有清单仍指向它们。

### 4.3 Nginx 配置（单份，覆盖所有应用）

```nginx
server {
    listen 8080;                       # 宝塔 HTML 项目；不要带 default_server
    server_name 39.107.30.119;

    root /www/wwwroot/qttools_updates;  # 站点根名不出现在 URL 里
    autoindex off;

    # 清单每次必须取最新
    location ~ ^/[a-z0-9_]+/latest\.json(\.sig)?$ {
        add_header Cache-Control "no-cache";
    }

    # 版本目录不可变，可长期缓存
    location ~ ^/[a-z0-9_]+/releases/ {
        add_header Cache-Control "public, max-age=31536000";
    }

    location / {
        return 404;
    }
}
```

新增应用时无需修改 Nginx，只需创建对应目录。

### 4.4 回滚

客户端禁止降级（远端版本号不高于本地则不提示），因此回滚的做法是：**发布一个更高版本号，内容为旧版本的修复版**。不能通过把清单改回老版本来回滚。这一点需要写入发布手册。

## 五、清单格式（所有应用统一）

```json
{
  "schema": 1,
  "app": "git_pull_tool",
  "version": "SV01.014",
  "size": 15728640,
  "sha256": "（exe 的小写 SHA256）",
  "url": "releases/SV01.014/git_pull_tool_SV01.014.exe",
  "publishedAt": "2026-11-20T10:00:00+08:00",
  "notes": "- 修复若干问题\n- 新增 xxx"
}
```

字段含义与 dsh_plugin_updater 方案第 5.6 节一致。通用约定：

- `app` 必须等于客户端内置的 `APP_ID`，防止把 A 应用的清单喂给 B 应用。
- `version` 必须匹配 `SVxx.NNN`，客户端取末尾数字比较。
- `url` 为相对路径，相对于 `<base>/<APP_ID>/`。
- `notes` 以纯文本显示，不使用 HTML。

## 六、签名密钥

### 6.1 每个应用一对密钥

| 应用 | 私钥位置（仅本机，不入工程） | 公钥位置（入工程） |
|---|---|---|
| dsh_plugin_updater | `E:\keys\online_update\dsh_plugin_updater\private.pem` | 生成的公钥源文件 |
| 其它应用 | `E:\keys\online_update\<APP_ID>\private.pem` | 同上 |

- 私钥目录不放在任何工程目录内，不上传服务器，离线备份。
- 公钥由脚本从私钥导出，输出为 ASCII 源文件（见 6.3），提交进工程，不属于机密。

### 6.2 密钥生成（脚本化，每应用执行一次）

```bash
openssl genrsa -out private.pem 2048
openssl rsa -in private.pem -pubout -out public.pem
openssl rsa -pubin -in public.pem -noout -modulus
```

### 6.3 公钥源文件

由 `tools/gen_pubkey.ps1` 生成 `user/src/update_public_key.cpp`，内容仅包含 256 字节模数的字节数组和指数，ASCII 编码，避免编码问题。

### 6.4 私钥泄露处理

- 只影响对应应用。
- 处理步骤：生成新密钥对，发布一个携带新公钥的版本。
- 老版本无法自动升级到新公钥，需要用户手动安装一次。这一代价需在告知用户时说明。

## 七、共享客户端模块

### 7.1 目录与引用方式

共享目录放在 `E:\code\qt\_shared\online_update\`，各应用的 `.pro` 通过 `.pri` 引用：

```
# 各应用 .pro 中
include($$PWD/../_shared/online_update/online_update.pri)
```

`.pri` 负责：`QT += network`、`LIBS += -lbcrypt`、`SOURCES`、`HEADERS`、`INCLUDEPATH`。

**不采用复制**：复制会导致各应用的副本逐渐分叉，修一个 bug 要改五份。

**不采用 git submodule**：部分工程不是 git 仓库，统一性难以保证。

### 7.2 核心模块职责（UI 无关）

| 类 | 职责 |
|---|---|
| `OnlineUpdateConfig` | 应用侧传入的配置结构：APP_ID、当前版本、基址、公钥、回调 |
| `OnlineUpdateManifest` | 解析清单，校验 `schema` 与 `app`，版本号解析与比较 |
| `OnlineUpdateVerifier` | RSA 验签（BCrypt），文件 SHA256 |
| `OnlineUpdateDownloader` | 下载、进度信号、停滞超时、取消、可写性预检 |
| `OnlineUpdateApplier` | 新版本文件名与落地路径（`get_publish_name/get_publish_path`）、启动新版、清理旧版 `.old` / `.new` 残留 |
| `OnlineUpdateService` | 对外的入口，串起以上模块，发出状态与进度信号 |

### 7.3 应用侧接口（示意）

```cpp
// 应用侧提供的配置，由 main_window 在打开更新窗口时构造
struct OnlineUpdateConfig {
    QString appId;                      // 清单 app 字段，如 "git_pull_tool"
    QString currentVersion;             // APP_VERSION，如 "SV01.013"
    QString baseUrl;                    // 如 "http://<IP>:8080/"
    std::function<bool(QString *)> can_replace_now;  // 忙则返回 false 并给出原因
    std::function<void()> prepare_for_exit;          // 停止后台任务、保存状态
};

// 应用侧提供的界面文案，中文只出现在应用侧源文件中
struct OnlineUpdateTexts {
    QString windowTitle;
    QString checking;
    QString upToDate;
    QString available;
    QString downloading;
    QString applying;
    // ……其余文案
};
```

这样共享模块中没有任何中文字符串，应用的中文文案留在应用自己的源文件中，编码与该应用一致。

### 7.4 编码规则（重要）

- 共享模块的源文件与头文件一律采用 **ASCII 编码**，中文文案不出现在共享目录中。
- 注释语言：ESP32 工程使用 GBK 编译（`-finput-charset=GBK`），UTF-8 中文注释在 GBK 下可能报错。因此共享模块注释使用英文，这是对 AGENTS.md「注释用中文」的唯一例外（已确认，见第十四节）。
- 应用侧文件保持该应用的既有编码，不做转换。
- 接入前用脚本检查每个应用的源文件编码，把结果记入接入清单。

### 7.5 通用更新窗口

- 共享模块提供一个通用的 `OnlineUpdateDialog`，只使用普通控件（QLabel、QProgressBar、QPushButton、QTextBrowser）。
- 视觉风格由应用的全局 QSS 决定，共享模块不写死颜色。
- 按钮行为、状态机与 dsh_plugin_updater 方案第 6.2、6.3 节一致；校验通过后的落地与启动
  统一按本文 3.1 节执行（状态文案用「正在启动新版本…」，不再是「即将关闭并重启」）。

### 7.6 自测程序

共享目录提供 `selftest/`，一个控制台程序，用于：

- 用 `openssl dgst -sha256 -sign` 生成的签名验证 BCrypt 验签实现是否正确；
- 用篡改后的清单、篡改后的 exe 验证拒绝逻辑；
- 解析各种版本号格式，验证比较结果。

验签实现出错的代价很高（要么全部拒绝，要么接受恶意包），因此在接入任何应用前先通过自测。

## 八、各应用接入要点

### 8.1 通用接入清单（每个应用都要完成）

- [ ] `user/inc/app_info.h` 保持不变（已有 `APP_VERSION`，不新增版本宏）。
- [ ] 新增 `user/src/update_app.cpp`（应用侧配置：APP_ID、基址、回调、文案）与对应公钥文件。
- [ ] `.pro` 中 `include` 共享 `.pri`。
- [ ] `main.cpp` 启动时调用 `OnlineUpdateService::cleanup_leftovers()`。
- [ ] 主窗口工具栏「关于」右侧新增「在线更新」按钮（颜色与该应用现有按钮风格一致）。
- [ ] 实现 `can_replace_now`：检查应用内所有后台任务。
- [ ] 实现 `prepare_for_exit`：停止后台任务，保存必要状态。
- [ ] 检查新类名与已有类名不冲突（例如 OpenCode 工具已有 `UpdateDialog`）。
- [ ] 确认源文件编码，记录在接入报告中。
- [ ] 确认 `.pro` 的 `TARGET` 与 APP_ID 一致。
- [ ] 确认打包产物文件名遵循 `<TARGET>_<版本>.exe`，与共享模块推导出的落地文件名一致。
- [ ] 按 AGENTS.md 要求在该应用的更新记录中追加一条记录。

### 8.2 各应用的特定事项

| 应用 | 按钮位置 | `can_replace_now` 需检查的任务 | `prepare_for_exit` 需处理的内容 | 其它 |
|---|---|---|---|---|
| dsh_plugin_updater | 「关于」右侧 | `PluginManager` 的 `m_activeProc`、`m_queryProc` | 无 | 不可写目录走桌面另存兜底 |
| git_pull_tool | 「关于」右侧 | `GitManager` 正在执行的 git 任务 | 停止正在执行的 git 子进程 | 需新增 `network` 模块 |
| opencode_proxy_tool | 「关于」右侧 | `ProxyManager` 正在进行的代理切换或检测 | 确认代理状态已恢复（如已修改过的系统代理设置） | 已有 `UpdateDialog` 类，新类使用 `OnlineUpdate` 前缀 |
| vscode_remote_fixer | 「关于」右侧 | `SshRunner` 正在执行的远程命令、`Fixer` 修复任务、`Downloader` 下载 | 断开 SSH 会话，停止子进程 | 有 SSH 长任务，忙碌判断尤其重要 |
| esp32_s3_tool | 「关于」右侧 | `FtpClient` 传输中、`DtuChannel` 通信中 | 关闭串口/网络通道 | GBK 编码；Qt 最低 5.12；`.rc` 版本资源缺失 |

**说明**：

- 「按钮位置」以「关于」为参照，各应用主窗口布局不同，具体位置在接入时确认。
- `can_replace_now` 与 `prepare_for_exit` 的具体实现需要读对应应用源码后确定，本表只给出方向。

### 8.3 ESP32 工程的额外事项

- `.pro` 中已有 `-finput-charset=GBK -fexec-charset=UTF-8`，共享模块为 ASCII，不受影响。
- `esp32_s3_tool` 的 `APP_BUILD_DATE` 使用 `__DATE__`，与在线更新无关，不需要改。
- 工程只有 `RC_ICONS`，没有 `.rc` 版本资源，exe 属性中没有版本号。这不阻断更新，但与其它工程不一致；是否补齐 `.rc` 作为可选项，不纳入本次接入。

### 8.4 DSH 插件更新工具的额外事项

- 需要 `QT += network`、`LIBS += -lbcrypt`，按 EVB 打包要求把 `Qt5Network.dll` 放进打包目录（`pack/app_32/`）。
- 接入前需先完成 dsh_plugin_updater 的试点，验证通过后再复制经验到其它应用。

## 九、编译与分发

### 9.1 动态编译 + EVB 打包（沿用各应用现有分发方式）

- 所有应用只用到 core、gui、widgets、network，都属于 qtbase，按各应用既有方式动态编译。
- 单 exe 分发统一走 EVB（Enigma Virtual Box）：把 `release\<TARGET>.exe` 与运行库一起封成单个 exe，
  打包配置与产物命名沿用各应用现有 `pack/` 目录的做法（dsh_plugin_updater 的具体步骤见
  `docs/online_update_plan.md` 第七节）。
- 在线更新新增的依赖只有 `Qt5Network.dll`，需补进各应用的打包目录；平台插件 `qwindows.dll`、
  图标插件 `qico.dll` 已由 EVB 配置打包，`.pro` 中不需要 `QTPLUGIN` / `Q_IMPORT_PLUGIN`。

### 9.2 分发目标

- 每个应用发布的产物为单个 exe，文件名建议遵循 `<TARGET>_<版本>.exe`（如 `git_pull_tool_SV01.014.exe`）；发布脚本会把实际文件名写进清单 `url`，上传时无需改名。
- 客户端不按文件名下载，而是按清单版本号生成 `<TARGET>_<版本>.exe` 落到 exe 同目录（见 3.1），因此产物文件名带不带版本号都不影响更新；带版本号的好处是服务器、本地目录两处的名字保持一致，便于人工核对。
- 版本信息（exe 属性）由 `.rc` 文件的 `VERSIONINFO` 提供，沿用各应用现有写法。

## 十、发布流程（统一脚本）

共享目录 `tools/publish_update.ps1`，参数示例：

```powershell
publish_update.ps1 `
  -AppId git_pull_tool `
  -Version SV01.014 `
  -Exe release\git_pull_tool.exe `
  -NotesFile notes.txt `
  -KeyDir E:\keys\online_update\git_pull_tool `
  -Server user@<服务器IP>
```

脚本步骤：

1. 校验 `Version` 格式与 `app_info.h` 中的 `APP_VERSION` 一致，不一致则中止。
2. 计算 exe 的 `size` 与 `sha256`。
3. 生成 `latest.json`（`notes` 从 `NotesFile` 读取，转义换行）。
4. 用该应用的私钥签名清单，得到 `latest.json.sig`。
5. 本地用公钥验证签名，失败则中止。
6. 上传：先 `releases/<版本>/`，后清单与签名。
7. 远程验证：下载清单并验签，下载 exe 比对 SHA256。
8. 清理超过保留数量的旧版本目录（需二次确认）。

**升版规则**（沿用各应用 AGENTS.md）：仅在明确发版时升版；小改动不升版。发布脚本不负责升版本号，只校验一致性。

## 十一、实施路线

| 阶段 | 内容 | 验收 |
|---|---|---|
| P0 | dsh_plugin_updater 试点（已有方案） | 完整跑通一次在线更新。**已完成**；落地方式按 3.1 改为发布文件名落地并编译通过 |
| P1 | 抽取共享模块到 `_shared/online_update`，按 7.4 编码规则编写；完成 selftest | selftest 全部通过 |
| P2 | 接入 opencode_proxy_tool（依赖最少，已有 network） | 完成 8.1 清单，实机更新一次 |
| P3 | 接入 git_pull_tool（需新增 network） | 同上 |
| P4 | 接入 vscode_remote_fixer（有 SSH 长任务，忙碌判断重点验证） | 同上，且忙碌时切换版本被正确阻止 |
| P5 | 接入 esp32_s3_tool（GBK，Qt 5.12.0+） | 同上，且 GBK 工程编译无告警 |
| P6 | 接入 protocol_analyzer（盘点信息待补全） | 同上 |
| P7 | 发布脚本与手册定稿 | 每个应用都完成一次发布演练 |

**顺序依据**：从依赖最少、忙碌逻辑最简单的应用开始，逐步增加复杂度。顺序可以按实际情况调整。

## 十二、测试

### 12.1 共享模块自测

- selftest 覆盖：验签通过与失败、清单字段错误、版本号比较、篡改 exe 的 SHA256 不符。
- 接入任何应用前必须通过 selftest。

### 12.2 每个应用的冒烟测试

- 无网络、服务器 404、超时；
- 清单签名错误、`app` 字段错误；
- 远端版本相同或更低；
- 下载中途断网、取消；
- 应用忙碌时切换版本被阻止；
- 目录不可写时进入桌面另存流程；
- 新版落盘后能正常启动，旧进程退出；
- 更新后同目录保留旧版本 exe，确认可正常运行、可手动删除。

### 12.3 测试矩阵

每个应用至少跑一遍冒烟测试；共享模块的改动需要对所有已接入应用回归。

## 十三、风险与对策

| 风险 | 对策 |
|---|---|
| 共享模块改动影响多个应用 | 每次改动回归测试所有已接入应用；版本化共享模块（在 `.pri` 旁记录版本号） |
| 应用忙碌判断遗漏，切换版本时打断任务 | 每个应用的 `can_replace_now` 逐一列出后台任务，接入报告中逐项确认 |
| 某应用私钥泄露 | 每应用独立密钥，影响范围仅限该应用 |
| 单个端口或公网 IP 变化 | 绑定弹性公网 IP；端口一经确定不改 |
| ESP32 工程 GBK 编码导致编译问题 | 共享模块保持 ASCII，注释英文 |
| 类名冲突（如 `UpdateDialog`） | 共享模块统一使用 `OnlineUpdate` 前缀 |
| 各应用 `.pro` 写法差异导致 `.pri` 集成失败 | 先在 dsh_plugin_updater 验证 `.pri`，再逐个接入 |
| 老版本无法自动升级到新公钥 | 密钥轮换前评估影响，必要时提前手动升级 |

## 十四、已确认决策

| 序号 | 事项 | 结论 | 影响 |
|---|---|---|---|
| 1 | 共享模块接入方式 | 通过 `.pri` 引用共享目录，不复制、不使用 git submodule | 各应用 `.pro` 只需一行 `include`，修改只需维护一份 |
| 2 | 注释语言 | 共享模块注释使用英文 | 兼容 ESP32 工程的 GBK 编译；这是对 AGENTS.md「注释用中文」的唯一例外 |
| 3 | 密钥策略 | 每个应用一对密钥 | 某应用私钥泄露只影响该应用 |
| 4 | 端口 | 全部应用共用一个端口 | 只需一个 Nginx 站点、一个安全组规则 |
| 5 | 接入顺序 | 按 P2 → P6 推进：opencode_proxy_tool → git_pull_tool → vscode_remote_fixer → esp32_s3_tool → protocol_analyzer | 先接入依赖最少、忙碌逻辑最简单的应用 |
| 6 | 共享目录位置 | `E:\code\qt\_shared\online_update\` | 与 snake_case 命名规则一致，P1 阶段创建 |
| 7 | 站点根目录 | `/www/wwwroot/qttools_updates`（原 `dsh_updates`） | 根目录名不进入 URL，改名不影响已发布客户端 |
| 8 | 更新落地方式 | 新版本按发布文件名 `<TARGET>_<版本>.exe` 落到 exe 同目录，校验通过后直接启动，不改名、不覆盖运行中的 exe（见 3.1） | 各应用共享同一套文件名推导与启动逻辑；旧版本文件保留在原目录，自动清理留作后续优化 |

以上决策已记录，后续实施按此执行。

## 修订记录

- v1：通用方案草案，覆盖服务器布局、清单、密钥、共享模块、接入清单、发布流程与实施路线。
- v2：第十四节 6 项待拍板事项全部确认并记录；统一为 snake_case 命名，共享目录改为 `online_update`。
- v3：站点根目录改用 `/www/wwwroot/qttools_updates`，按 6 个应用 ID 划分一级子目录；缓存规则改为 `[a-z0-9_]+` 通配；补入 protocol_analyzer 待接入项。
- v4（当前）：P0 试点落地。新增 3.1 节「落地方式（P0 试点定稿）」，把「重命名替换运行中的 exe」统一改为「新版本按发布文件名落盘 + 直接启动」；同步修订核心模块职责、通用窗口说明、接入清单、分发目标、冒烟测试与实施路线；第九节编译分发由「静态 Qt」改为「动态编译 + EVB 打包」；第十四节新增第 8 项决策。
