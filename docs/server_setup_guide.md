# 在线更新服务器部署指引（阿里云 ECS + 宝塔面板）

> 配套代码：`docs/online_update_plan.md`（方案）、`scripts/publish.ps1`（本机发布）
> 适用环境：**已安装宝塔面板、且已在托管其它站点**的 Linux ECS

## 〇、与「裸装 Nginx」的区别（重要）

目标服务器是**宝塔面板**环境，与方案早期假设的裸装 Nginx 有本质差异：

| 项目 | 裸装 Nginx | 宝塔面板（实际情况） |
|---|---|---|
| 站点配置 | `/etc/nginx/conf.d/` | `/www/server/panel/vhost/nginx/` |
| Nginx 可执行 | 在 PATH 中 | `/www/server/nginx/sbin/nginx` |
| 运行用户 | nginx / www-data | `www` |
| 生效方式 | 手工 `nginx -t && reload` | 保存配置时宝塔自动校验并 reload |

> **`scripts/setup_server.sh` 是裸装版本，在宝塔环境不要执行。**
> 它写 `/etc/nginx/conf.d/`、调用不在 PATH 的 `nginx`，会失败或干扰现有站点。
> 宝塔环境请按本文用面板界面操作。

## 一、整体步骤与验证点

| 步骤 | 做什么 | 怎么算完成 | 状态 |
|---|---|---|---|
| 1 | 阿里云安全组放行 8080 | 入方向存在 `自定义 TCP 8080/8080` | ✅ 已完成 |
| 2 | **firewalld / 宝塔面板放行 8080** | `firewall-cmd --query-port=8080/tcp` 返回 yes | ✅ 已完成 |
| 3 | 建 **HTML 项目** 并改监听端口为 8080 | 站点配置里 `listen 8080;`，且无 `proxy_pass` | ✅ 已完成 |
| 4 | 建目录、传清单与签名 | 目录里能看到 `latest.json`、`latest.json.sig` | ⬜ 待做 |
| 5 | 服务器本地 `curl` 自测 | 返回 200 且带 `Cache-Control: no-cache` | ⬜ 待做 |
| 6 | **手机流量**访问 | 返回 JSON 内容 | ✅ 链路已验证（当前 404，仅缺文件） |
| 7 | 确认 `UPDATE_BASE_URL` | 客户端已内置真实地址并编译 | ✅ 已完成 |

第 6 步是路线 A 的前提：手机流量不通说明端口被拦，路线 A 不成立。

## 二、第 1 步：阿里云安全组

控制台：**ECS → 实例 → 安全组 → 配置规则 → 入方向 → 手动添加**

| 字段 | 值 |
|---|---|
| 授权策略 | 允许 |
| 优先级 | 1 |
| **协议类型** | **自定义 TCP**（⚠️ 不要选「Web HTTP 流量访问」，它把端口锁死为 80） |
| **端口范围** | `8080/8080` |
| 访问来源 | `0.0.0.0/0` |
| 描述 | `dsh 在线更新` |

现有规则（80/443/8888/22）**不要改动**。有 IPv6 公网地址时再补一条来源 `::/0` 的同端口规则。

## 三、第 2 步：系统防火墙（实测踩坑点）

**这一层极易被漏掉**。本机实测时安全组已放行，但 `firewalld` 的放行列表里没有 8080，表现就是「服务器本地通、手机端 `ERR_ADDRESS_UNREACHABLE`」。

```bash
firewall-cmd --list-ports          # 先确认 8080 是否在列表里
firewall-cmd --permanent --add-port=8080/tcp
firewall-cmd --reload
firewall-cmd --query-port=8080/tcp # 应返回 yes
```

宝塔用户还需在**面板 → 安全**里再放行一次 8080（宝塔防火墙与系统防火墙是两层）。

> 排查陷阱：`iptables -S INPUT` 只显示 `-P INPUT ACCEPT` **不代表没有拦截**。firewalld 使用自己的链（`IN_public` 等），必须用 `firewall-cmd --list-ports` 核对。

## 四、第 3 步：建站点（HTML 项目）

1. 面板 → **网站** → 顶部标签选 **HTML项目** → **添加项目**
2. 填写：

   | 字段 | 值 |
   |---|---|
   | 项目名称 | `qttools_updates` |
   | 域名 | `39.107.30.119`（公网 IP；不接受 IP 时先填占位域名，建好后改） |
   | 根目录 | `/www/wwwroot/qttools_updates` |
   | FTP | 不创建 |
   | 数据库 | 不创建 |
   | PHP 版本 | 纯静态 |

3. 建好后 → 站点 **设置** → **配置文件**，改两处：

   **a. 改监听端口**

   ```nginx
   listen 8080;          # 原来是 listen 80;
   ```

   若原来是 `listen 80 default_server;`，**必须去掉 `default_server`**，否则与同端口已有站点冲突，`nginx -t` 会报重复默认服务器。

   **b. 加两段缓存规则**（放在 `location` 块之前）

   站点根目录下按应用 ID 分子目录（`dsh_plugin_updater`、`git_pull_tool` 等），所以规则用 `[a-z0-9_]+` 匹配任意应用，**新增应用不用再改 Nginx**：

   ```nginx
   # 清单必须每次取最新
   location ~ ^/[a-z0-9_]+/latest\.json(\.sig)?$ {
       add_header Cache-Control "no-cache";
   }

   # 版本目录一经发布不再修改，可以长期缓存
   location ~ ^/[a-z0-9_]+/releases/ {
       add_header Cache-Control "public, max-age=31536000";
   }
   ```

4. 保存。宝塔会自动跑 `nginx -t` 并 reload；失败会拒绝保存，现有站点不受影响。

> 不要删除文件里 `#REWRITE-START`、`#SSL-START` 这类宝塔自己的锚点注释。

### 关键判定：配置里不能有 proxy_pass

HTML 项目只有 `root` 与 `location`。**出现 `proxy_pass` 说明选成了「反向代理」**，此时 8080 返回 **502 Bad Gateway**（因为找不到上游服务）。必须删掉重建为 HTML 项目。

## 五、第 4 步：目录与上传

### 目标结构

站点根目录名（`qttools_updates`）不出现在 URL 里，URL 只认它下面的一级子目录名，即 `<APP_ID>`：

```
/www/wwwroot/qttools_updates/                 # 站点根，URL 中不可见
├── dsh_plugin_updater/                       # http://39.107.30.119:8080/dsh_plugin_updater/
│   ├── latest.json
│   ├── latest.json.sig
│   └── releases/
│       └── SV01.011/
│           └── dsh_plugin_updater_SV01.011.exe
├── git_pull_tool/                         # 规划中，结构同上
├── opencode_proxy_tool/
├── esp32_s3_tool/
├── vscode_remote_fixer/
└── protocol_analyzer/
```

**URL 路径 = 站点根下一级目录名**，改名站点根不影响已发布客户端的地址。

exe 文件名**原样使用打包产物的名字**（如 `dsh_plugin_updater_SV01.011.exe`），`publish.ps1` 会把它写进清单的 `url` 字段，上传时不用改名。

### 上传方式（注意权限）

`/www` 属性是 `drwx------`、属主 `www`，**普通账号（如 `lch`）用 SFTP/Xftp 无法进入**，会报「无法显示远程文件夹」。三种可行方式：

| 方式 | 操作 |
|---|---|
| **宝塔文件管理器（推荐）** | 面板 → 文件 → 路径填 `/www/wwwroot/qttools_updates` → 上传 |
| root 登录 SFTP | Xftp 连接账号改 `root`（22 端口对公网开放时风险较高，建议改密钥登录） |
| 中转 | 先传到 `/home/<user>`，再 `sudo cp` + `chown` |

### 修正属主与权限（必须做）

宝塔的 Nginx 以 `www` 用户运行，属主或读权限不对会返回 **403**：

```bash
sudo chown -R www:www /www/wwwroot/qttools_updates
sudo chmod -R a+rX /www/wwwroot/qttools_updates
```

### 站点根目录改名 / 增加应用（多应用扩展）

**站点根目录名不进入 URL**，URL 只认根目录下的一级子目录名（`<APP_ID>`）。因此把站点根从 `dsh_updates` 改成 `qttools_updates` 时，已发布客户端的地址 `http://39.107.30.119:8080/dsh_plugin_updater/latest.json` **完全不受影响**，客户端代码无需改动。

改名步骤：

1. `mv /www/wwwroot/dsh_updates /www/wwwroot/qttools_updates`
2. 宝塔 → 网站 → 该站点 → 设置 → **网站目录** → 根目录改成 `/www/wwwroot/qttools_updates` → 保存。
   面板会重写站点配置里的 `root` 和 `.user.ini` 里的 `open_basedir`，并自动 `nginx -t` + reload。
3. 若手工改配置，只改 `/www/server/panel/vhost/nginx/html_39.107.30.119.conf` 里的 `root` 一行，然后 `nginx -t && nginx -s reload`；**漏改这一行会导致全站 404**。
4. 验证：`curl -I http://127.0.0.1:8080/dsh_plugin_updater/latest.json` 返回 200。

增加应用：在站点根下按 `<APP_ID>` 建目录，放入 `latest.json`、`latest.json.sig`、`releases/<版本>/<打包产物文件名>`，属主改 `www:www`。**Nginx 不用改**（缓存规则已用 `[a-z0-9_]+` 通配）。

> 清单已签名，改名或换目录后**不要重新生成或编辑 `latest.json`**，只需保证 `url` 字段的相对路径在服务器上真实存在。

## 六、第 5～6 步：验证

### 服务器本地自测

```bash
curl -I http://127.0.0.1:8080/dsh_plugin_updater/latest.json
curl -s  http://127.0.0.1:8080/dsh_plugin_updater/latest.json
```

期望 `HTTP/1.1 200` 且响应头含 `Cache-Control: no-cache`。

### 一键诊断

```bash
echo "=== 8080 监听 ==="; ss -lntp | grep 8080
echo "=== 本地直连 ==="; curl -sS -I --max-time 5 http://127.0.0.1:8080/
echo "=== 站点配置列表 ==="; ls -1 /www/server/panel/vhost/nginx/
echo "=== 8080 配置内容 ==="; grep -rn -A 15 "listen 8080" /www/server/panel/vhost/nginx/ 2>/dev/null
```

### 外网验证（必须用手机流量）

**关闭 Wi-Fi**，手机浏览器打开：

```
http://39.107.30.119:8080/dsh_plugin_updater/latest.json
```

## 七、状态码对照表

**先明确一件事：判断外网可达性只能用两个来源** —— 服务器本地 `127.0.0.1` 测试、手机流量测试。开发者办公网络的代理会对不可达端口返回假的 `502 Bad Gateway`（无 `Server` 头、`Content-Length: 0`），实测中确实踩过这个坑。

同理，在服务器上 `curl` 自己的**公网 IP** 可能超时（ECS 公网 IP 为 NAT 映射，通常不支持 hairpin 回环），这属正常；而 `curl` 自己的**私网 IP** 会走 loopback，即使防火墙拦着也能返回 200。两者都不能用来判断端口是否放行。

| 现象 | 含义 | 处理 |
|---|---|---|
| **502 Bad Gateway**（无 `Server` 头） | 多半是本地网络代理的假响应 | 改用服务器本地 + 手机流量复测 |
| **502 Bad Gateway**（有 `Server: nginx`） | 站点被配成反向代理，找不到上游 | 重建为 HTML 项目，确认配置无 `proxy_pass` |
| **403 Forbidden** | 属主/读权限不对 | `chown -R www:www` + `chmod -R a+rX` |
| **404 Not Found** | 文件没上传；`root` 指向别的目录；漏建 `<APP_ID>` 子目录；服务器文件名与清单 `url` 里的不一致 | 核对目录结构与文件名，清单 `url` 是相对路径 |
| **手机端 `ERR_ADDRESS_UNREACHABLE` / 超时** | 安全组、firewalld 或宝塔防火墙未放行 | 三层逐一核对，重点看 `firewall-cmd --list-ports` |
| 运营商提示页 | 端口被运营商拦截 | 换高位端口（8081/9000）重测 |
| 客户端「更新源校验失败」 | 清单被改过哪怕一个空格 | 重新跑 `publish.ps1` 生成签名 |
| 客户端「清单格式不符」 | `schema` ≠ 1 或 `app` 字段错 | 检查清单内容 |

## 八、本机发布流程

```powershell
# 1. 升版本号（仅发布时）：user/inc/app_info.h 三个宏
# 2. 纯 cmd 环境编译（见 AGENTS.md）→ release/dsh_plugin_updater.exe
# 3. EVB 命令行打包（输出名由 pack_cfg.evb 的 <OutputFile> 决定，带版本号）
#    "D:\SW\Enigma Virtual Box\enigmavbconsole.exe" pack\pack_cfg.evb
#    → pack\dsh_plugin_updater_SV01.011.exe
# 4. 生成并签名清单（私钥默认用 update_keys/ 下的相对路径，无需传 -PrivateKey）
powershell -ExecutionPolicy Bypass -File scripts/publish.ps1 `
    -Exe pack\dsh_plugin_updater_SV01.011.exe `
    -Version SV01.011 `
    -Notes "- 新增在线更新功能`n- 修复若干界面问题"
```

> 升版后要同步改 `pack_cfg.evb` 里的 `<OutputFile>` 文件名，否则会覆盖上一个版本的打包产物。

脚本会：校验版本号与 `app_info.h` 一致 → 对**打包后 exe** 算 size/SHA256 → 生成无 BOM 的 `publish_info/latest.json` → 签名 → 用配对公钥自检（失败中止）。

### 上传（先 exe，后清单）

上传时**保持文件名不变**（`dsh_plugin_updater_SV01.011.exe`），脚本已把它写进清单的 `url` 字段：

```bash
ssh root@39.107.30.119 "mkdir -p /www/wwwroot/qttools_updates/dsh_plugin_updater/releases/SV01.011"
scp pack/dsh_plugin_updater_SV01.011.exe root@39.107.30.119:/www/wwwroot/qttools_updates/dsh_plugin_updater/releases/SV01.011/dsh_plugin_updater_SV01.011.exe
scp publish_info/latest.json publish_info/latest.json.sig root@39.107.30.119:/www/wwwroot/qttools_updates/dsh_plugin_updater/
ssh root@39.107.30.119 "chown -R www:www /www/wwwroot/qttools_updates && chmod -R a+rX /www/wwwroot/qttools_updates"
```

顺序不能颠倒：否则用户可能在 exe 未就位时就检查到了新版本。

## 九、私钥保管（重要）

**私钥位置**：`update_keys\`（工程根目录下，相对路径）

| 文件 | 用途 |
|---|---|
| `update_keys/update_sign_private.pem` | 私钥，签名清单用。**绝不外传** |
| `update_keys/update_sign_public.pem` | 公钥，发布脚本用来自检签名；对应模数已硬编码在 `user/src/update_verifier.cpp` |

发布脚本默认就使用该相对路径，调用时**无需再传 `-PrivateKey`**。

⚠️ **`.gitignore` 已排除整个 `update_keys/` 目录**（不只靠 `*.pem`，避免将来放入其它后缀文件时漏网）。提交前建议执行 `git status` 确认该目录不在待提交列表中。

已完成：

1. ✅ 密钥已就位于工程内 `update_keys/`
2. ⬜ **待办：备份到离线介质**（U 盘 / 加密压缩包），本机损坏则无法再发布更新
3. ✅ 已加入 `.gitignore`，`git check-ignore` 验证通过

私钥泄露 = 他人可推送任意更新，只能换密钥并让用户手动装一次新版。
私钥丢失 = 无法再发布更新，只能换密钥对并让用户手动装一次新版。

> 注意：密钥放在工程目录内，任何对该目录的打包、同步、备份、云盘动作都可能把私钥带走。若将来工程要开源或共享，务必先确认 `update_keys/` 未被包含。

## 十、线上地址（已定稿）

| 项目 | 值 |
|---|---|
| 公网 IP | `39.107.30.119` |
| 端口 | `8080` |
| 协议 | HTTP（路线 A，不备案） |
| `UPDATE_BASE_URL` | `http://39.107.30.119:8080/dsh_plugin_updater/` |
| 服务器根目录 | `/www/wwwroot/qttools_updates`（站点根名不进 URL） |

**地址一经随客户端分发就不能再改**：老版本内置的就是它。

## 十一、当前进度

| 项目 | 状态 |
|---|---|
| 客户端代码（验签/下载/校验/落盘启动） | ✅ 已完成并本地实测通过 |
| `UPDATE_BASE_URL` 写入真实地址 | ✅ 已完成，编译通过 |
| 服务器 8080 静态站点 | ✅ 已就绪（`listen 8080`、`root /www/wwwroot/qttools_updates`、无 `proxy_pass`） |
| 端口可达性（安全组 + firewalld） | ✅ 已完成，外网返回 `Server: nginx` 正常响应 |
| EVB 打包 | ✅ 已完成：`pack/dsh_plugin_updater_SV01.011.exe`（22,580,224 字节） |
| EVB 下 `applicationFilePath()` | ✅ 已实测：返回真实目录，可就地落盘新版本 |
| 缓存规则（`Cache-Control`） | ⬜ 待加到站点配置 |
| 文件上传（清单 + 签名 + exe） | 🔄 已上传，待归位到 `<APP_ID>/` 子目录并统一 exe 文件名 |
| 首次发布演练 | ⬜ 待做 |

尚未实测（需真实环境）：

- 杀毒软件占用文件时的重命名重试；
- 更新时 Windows 是否对程序自己写入的新 exe 弹 SmartScreen；
- 真实客户端完成一次完整的「检查 → 下载 → 落盘 → 启动」闭环。
