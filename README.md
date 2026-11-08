# Git 批量拉取工具

基于 Qt Widgets 的 Windows 桌面工具：递归扫描指定根目录下的 Git 仓库（最多 6 层），筛选出所有带远端的仓库，展示分支、工作区状态与落后提交数，支持一键或勾选执行 `git pull --ff-only`。每次启动都是全新扫描，不保留历史记录。

## 功能特性

- 递归扫描根目录，自动识别带远端的 Git 仓库；启动时默认扫描 E 盘，E 盘不存在时用 C 盘
- 表格展示仓库路径、分支、状态（最新 / 落后 / 工作区有改动 / 分离 HEAD / 无上游）、落后提交数与远程地址
- 一键拉取全部或仅拉取勾选仓库，自动跳过脏工作区、分离 HEAD、合并/变基进行中的仓库
- 扫描与拉取均在后台异步执行，界面不卡顿；「停止」按钮可随时中止
- 状态颜色图例、摘要统计与进度条；右键菜单支持打开所在目录、复制远程地址
- 左上角提供「关于」「更新说明」「退出」入口

## 使用方法

1. 在「扫描根目录」输入或浏览选择目录，点击「开始扫描」
2. 扫描完成后表格列出全部带远端的 Git 仓库及其状态
3. 点击「一键拉取全部」或勾选后「拉取选中」，全程可点「停止」中止

## 构建环境

- Qt 5.12.8 + MinGW 7.3 32 位（`D:\sw\Qt5.12.8\5.12.8\mingw73_32`），C++17，qmake 构建
- 生成 Makefile：`qmake git_pull_tool.pro -spec win32-g++`
- 编译（Release，产物在 `release\`）：`mingw32-make release`

---

# Git Batch Pull Tool (English)

A Qt Widgets desktop tool for Windows: recursively scans a given root directory (up to 6 levels) for Git repositories that have a remote, shows branch, working-tree status and how many commits each repo is behind, and runs `git pull --ff-only` on all or selected repos with one click. Every launch starts from a fresh scan; no history is stored.

## Features

- Recursive scan with auto detection of repos that have a remote; defaults to the E: drive, falling back to C: when E: is unavailable
- Table view of repo path, branch, status (up to date / behind / dirty / detached HEAD / no upstream), behind count and remote URL
- Pull all or pull selected; dirty, detached, in-progress-merge and upstream-less repos are skipped automatically
- Scanning and pulling run asynchronously in the background — the UI never freezes; a Stop button aborts at any time
- Status color legend, summary and progress bar; context menu to open the folder or copy the remote URL
- "About", "Update Notes" and "Exit" buttons at the top-left corner

## Usage

1. Type or browse the scan root directory, then click "Start Scan"
2. After scanning, all Git repos with a remote and their statuses are listed in the table
3. Click "Pull All" or check rows and click "Pull Selected"; press "Stop" to abort anytime

## Build

- Qt 5.12.8 + MinGW 7.3 32-bit, C++17, qmake
- Generate Makefile: `qmake git_pull_tool.pro -spec win32-g++`
- Build (Release, output in `release\`): `mingw32-make release`

---

## License / 许可证

本项目以 MIT 许可证发布（以下为英文原文，具有法律效力；中文仅供参考）。
This project is released under the MIT License (the English text below is the authoritative version; the Chinese note is for reference only — roughly: anyone may use, copy, modify and distribute this software, provided the copyright notice is preserved, and the authors accept no liability).

```text
MIT License

Copyright (c) 2026 lch (fantasylch@foxmail.com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

# 更新记录 / Change Log

完整修改记录见 [docs/README.zh_CN.md](docs/README.zh_CN.md)（程序内「更新说明」窗口同源显示）。
Full change log: [docs/README.zh_CN.md](docs/README.zh_CN.md) (same source as the in-app "Update Notes" window).
