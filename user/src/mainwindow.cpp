/**
 * @file        mainwindow.cpp
 * @brief       主窗口实现：界面搭建、仓库表格/日志展示与操作入口
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.1
 * @date        20261106
 * @note        扫描与拉取逻辑在 GitManager 中后台异步执行，结果经信号回传本类展示
 *
 * @copyright   Copyright (c) 2026 by OLE, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261106    <td>1.0       <td>lch       <td>创建
 * <tr><td>20261106    <td>1.1       <td>lch       <td>标题用 APP_FULL_TITLE；新增关于/更新说明/退出；扫描改异步流式填表
 * </table>
 */
#include "mainwindow.h"

#include "appinfo.h"
#include "aboutdialog.h"
#include "updatenotesdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QHeaderView>
#include <QFileDialog>
#include <QSettings>
#include <QMessageBox>
#include <QApplication>
#include <QTimer>
#include <QStatusBar>
#include <QMenu>
#include <QDesktopServices>
#include <QClipboard>
// 不使用 QtConcurrent，拉取流程走 QTimer 驱动队列；扫描走 GitManager 内部后台线程

// ═══════════════════════════════════════════════════════════════════════
// 构造 / 析构
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief       构造主窗口：搭建界面、连接 GitManager 信号并加载设置
 *
 * @param[out]  {parent} QWidget* Qt 父窗口指针
 * @return      none（构造/析构函数，无返回值）
 * @retval      none
 * @details     GitManager 以本窗口为父对象，随窗口销毁自动释放；
 *              窗口标题取自 appinfo.h 的 APP_FULL_TITLE（名称+版本+构建日期）
 * @doxygen_sig_fp 755a2b28c15df9d3
 */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_appDir = QCoreApplication::applicationDirPath();
    setWindowTitle(QStringLiteral(APP_FULL_TITLE));
    resize(1100, 700);
    setup_ui();

    // Git 扫描/拉取功能对象，随窗口销毁自动释放
    m_gitManager = new GitManager(this);
    // GitManager 的日志信号直接转发到日志面板（签名与 append_log 匹配）
    connect(m_gitManager, &GitManager::log_message, this, &MainWindow::append_log);
    // 扫描流程：骨架列表建表 → 单仓结果流式回填 → 收尾恢复按钮与汇总
    connect(m_gitManager, &GitManager::scan_list_ready,
            this, &MainWindow::on_scan_list);
    connect(m_gitManager, &GitManager::scan_result,
            this, &MainWindow::on_scan_result);
    connect(m_gitManager, &GitManager::scan_finished,
            this, &MainWindow::on_scan_finished);
    // 拉取进度回传到状态栏进度条（总数已在 pull_repos 里设置，无需重复）
    connect(m_gitManager, &GitManager::pull_progress, this,
            [this](int done, int /*total*/) { m_progress->setValue(done); });
    // 单仓库结果写入表格对应行
    connect(m_gitManager, &GitManager::repo_result,
            this, &MainWindow::on_repo_result);
    // 队列结束后恢复按钮并延时重扫
    connect(m_gitManager, &GitManager::pull_finished,
            this, &MainWindow::on_pull_finished);

    load_settings();
}

/**
 * @brief       析构主窗口：退出前保存持久化设置
 *
 * @return      none（构造/析构函数，无返回值）
 * @retval      none
 * @details     none
 * @doxygen_sig_fp a84ef10cd3716d34
 */
MainWindow::~MainWindow() { save_settings(); }

// ═══════════════════════════════════════════════════════════════════════
// UI 搭建
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief 搭建主窗口整体界面框架
 *
 * 布局自上而下：
 *   1. 顶栏行：关于 / 更新说明 / 退出 三个带边框按钮（参考欧力小工具）
 *   2. 工具栏行：扫描/拉取/停止按钮与全选复选框（见 setup_tool_bar）
 *   3. 扫描根目录行：路径输入框 + 浏览按钮
 *   4. 中间分栏：左侧仓库表格 + 右侧日志（QSplitter 可拖拽）
 *   5. 图例行：状态颜色说明
 *   6. 状态栏：摘要标签 + 进度条（见 setup_status_bar）
 * @return      none
 * @retval      none
 * @details     none
 * @doxygen_sig_fp 86fc58ce8be6edb3
 */
void MainWindow::setup_ui()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *mainVBox = new QVBoxLayout(central);
    mainVBox->setContentsMargins(8, 8, 8, 8);
    mainVBox->setSpacing(6);

    // ── 顶行：关于 / 更新说明 / 退出（带边框按钮靠左，位于窗口标题下、工具栏行上） ──
    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(6);

    auto *aboutBtn = new QPushButton(QStringLiteral("关于"), this);
    aboutBtn->setCursor(Qt::PointingHandCursor);
    connect(aboutBtn, &QPushButton::clicked, this, &MainWindow::on_about);
    titleRow->addWidget(aboutBtn);

    auto *updateBtn = new QPushButton(QStringLiteral("更新说明"), this);
    updateBtn->setCursor(Qt::PointingHandCursor);
    connect(updateBtn, &QPushButton::clicked, this, &MainWindow::on_update_notes);
    titleRow->addWidget(updateBtn);

    auto *exitBtn = new QPushButton(QStringLiteral("退出"), this);
    exitBtn->setCursor(Qt::PointingHandCursor);
    connect(exitBtn, &QPushButton::clicked, this, &MainWindow::on_exit);
    titleRow->addWidget(exitBtn);

    titleRow->addStretch(1); // 按钮靠左，剩余空间留白
    mainVBox->addLayout(titleRow);

    // ── 工具栏行：扫描/拉取/停止/全选（嵌入主布局，必须位于关于行之下） ──
    setup_tool_bar(mainVBox);

    // ── 第一行：扫描根目录 ──
    auto *topRow = new QHBoxLayout;
    topRow->addWidget(new QLabel(QStringLiteral("扫描根目录：")));
    m_rootEdit = new QLineEdit("E:\\code");
    m_rootEdit->setMinimumWidth(300);
    connect(m_rootEdit, &QLineEdit::textChanged, this, &MainWindow::on_scan_root_changed);
    topRow->addWidget(m_rootEdit, 1);

    m_browseBtn = new QPushButton(QStringLiteral("浏览…"));
    connect(m_browseBtn, &QPushButton::clicked, this, &MainWindow::on_browse);
    topRow->addWidget(m_browseBtn);
    mainVBox->addLayout(topRow);

    // ── 中间：表格 + 日志 左右分栏 ──
    auto *splitter = new QSplitter(Qt::Horizontal);

    // 左：表格
    m_table = new QTableWidget(0, 6);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral(""),                // 复选框
        QStringLiteral("仓库路径"),
        QStringLiteral("分支"),
        QStringLiteral("状态"),
        QStringLiteral("落后"),
        QStringLiteral("远程地址")
    });
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    // 全部列用 Interactive 模式，宽度可自由拖拽调整（此处只给初始宽度）
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->setColumnWidth(0, 36);   // 复选框
    m_table->setColumnWidth(1, 300);  // 仓库路径
    m_table->setColumnWidth(2, 110);  // 分支
    m_table->setColumnWidth(3, 130);  // 状态
    m_table->setColumnWidth(4, 60);   // 落后
    m_table->setColumnWidth(5, 240);  // 远程地址（完整地址见单元格 tooltip）

    // 右键菜单：打开所在目录 / 复制远程地址
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &MainWindow::on_table_context_menu);
    splitter->addWidget(m_table);

    // 右：日志（不限最大宽度，由 splitter 自由分栏）
    m_log = new QTextEdit;
    m_log->setReadOnly(true);
    m_log->setFont(QFont("Consolas", 9));
    splitter->addWidget(m_log);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({700, 400}); // 初始分栏比例约 3:2
    mainVBox->addWidget(splitter, 1);

    // ── 图例行：状态颜色说明 ──
    auto *legend = new QLabel(
        QStringLiteral(
            "<span style='color:#009600;'>● 已是最新</span>&nbsp;&nbsp;"
            "<span style='color:#c85000;'>● 落后可更新</span>&nbsp;&nbsp;"
            "<span style='color:#c89600;'>● 工作区有改动</span>&nbsp;&nbsp;"
            "<span style='color:#808080;'>● 分离/无上游</span>"));
    legend->setTextFormat(Qt::RichText);
    mainVBox->addWidget(legend);

    // 状态栏（QMainWindow 独立区域；工具栏行已在上方插入主布局）
    setup_status_bar();
}

/**
 * @brief       创建主工具栏按钮行（嵌入主布局，位于关于行之下）
 *
 * 按钮行承载全部操作按钮：开始扫描、一键拉取全部、拉取选中、停止，
 * 末尾附加「全选」复选框。直接嵌入主垂直布局——若用 QMainWindow 的
 * QToolBar 停靠区，其显示在 central widget 之上，会跑到关于行的上面，
 * 打破「标题 → 关于行 → 工具栏行」的既定顺序。
 * 按钮统一加大高度与内边距，便于点击；「开始扫描」额外加粗突出主操作。
 *
 * @param[out]  {mainVBox} QVBoxLayout* 主窗口主布局，按钮行插入其尾部
 * @return      none
 * @retval      none
 * @details     none
 * @doxygen_sig_fp 3e7a7e9914390443
 */
void MainWindow::setup_tool_bar(QVBoxLayout *mainVBox)
{
    // 工具栏行容器：普通 QWidget 嵌入主布局，确保显示顺序为 关于行 → 工具栏行 → 扫描根目录行
    auto *bar = new QWidget(this);
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(6);

    // 主按钮统一样式：加大内边距与最小高度，视觉上更大更易点击
    const QString btnStyle =
        "QPushButton { padding:4px 18px; min-height:24px; }";

    // 扫描按钮（加粗突出主操作）
    m_scanBtn = new QPushButton(QStringLiteral("🔍 开始扫描"), bar);
    m_scanBtn->setStyleSheet("QPushButton { font-weight:bold; padding:6px 22px; min-height:24px; }");
    connect(m_scanBtn, &QPushButton::clicked, this, &MainWindow::on_scan);
    lay->addWidget(m_scanBtn);

    // 拉取全部：仅扫描到仓库后可用
    m_pullAllBtn = new QPushButton(QStringLiteral("⬇️ 一键拉取全部"), bar);
    m_pullAllBtn->setStyleSheet(btnStyle);
    m_pullAllBtn->setEnabled(false);
    connect(m_pullAllBtn, &QPushButton::clicked, this, &MainWindow::on_pull_all);
    lay->addWidget(m_pullAllBtn);

    // 拉取选中：仅扫描到仓库后可用
    m_pullSelectedBtn = new QPushButton(QStringLiteral("⬇️ 拉取选中"), bar);
    m_pullSelectedBtn->setStyleSheet(btnStyle);
    m_pullSelectedBtn->setEnabled(false);
    connect(m_pullSelectedBtn, &QPushButton::clicked, this, &MainWindow::on_pull_selected);
    lay->addWidget(m_pullSelectedBtn);

    // 停止：扫描/拉取进行中可用
    m_stopBtn = new QPushButton(QStringLiteral("⏹ 停止"), bar);
    m_stopBtn->setStyleSheet(btnStyle);
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::on_stop);
    lay->addWidget(m_stopBtn);

    lay->addSpacing(12); // 与全选复选框之间留出分隔间距

    // 全选复选框：批量勾选/取消表格首列
    m_selectAllCb = new QCheckBox(QStringLiteral("全选"), bar);
    connect(m_selectAllCb, &QCheckBox::stateChanged, this, &MainWindow::on_select_all);
    lay->addWidget(m_selectAllCb);

    lay->addStretch(1); // 按钮靠左，剩余空间留白
    mainVBox->addWidget(bar);
}

/**
 * @brief 创建状态栏
 *
 * 左侧为摘要标签（仓库统计），右侧为拉取进度条（永久部件）。
 * 进度条默认隐藏，进入拉取流程时显示。
 * @return      none
 * @retval      none
 * @details     none
 * @doxygen_sig_fp ae25a04a32918f73
 */
void MainWindow::setup_status_bar()
{
    m_summaryLabel = new QLabel(this);
    statusBar()->addWidget(m_summaryLabel);

    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    statusBar()->addPermanentWidget(m_progress);
}

/**
 * @brief 表格右键菜单：打开所在目录 / 复制远程地址
 * @param pos 视口内坐标（由 customContextMenuRequested 信号给出）
 *
 * 仅当右键落在有效数据行上时弹出菜单；
 * 动作直接作用于该行对应的仓库。
 * @return      none
 * @retval      none
 * @details     右键落在空白区域时不弹菜单
 * @doxygen_sig_fp 5509136069130d53
 */
void MainWindow::on_table_context_menu(const QPoint &pos)
{
    // 行号无效（空白区域或超出数据范围）则不弹菜单
    int row = m_table->rowAt(pos.y());
    if (row < 0 || row >= m_repos.size())
        return;

    QMenu menu(this);
    QAction *openDirAct = menu.addAction(QStringLiteral("打开所在目录"));
    QAction *copyUrlAct = menu.addAction(QStringLiteral("复制远程地址"));

    QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (chosen == openDirAct) {
        // 用系统资源管理器打开仓库目录
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_repos[row].absolutePath));
    } else if (chosen == copyUrlAct) {
        // 复制 origin 远程地址到剪贴板
        QApplication::clipboard()->setText(m_repos[row].remoteUrl);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// 设置持久化
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief       从 QSettings 读取扫描根目录并填入输入框
 *
 * @return      none
 * @retval      none
 * @details     无历史记录时使用默认值 E:\code
 * @doxygen_sig_fp 1302fd73883bb048
 */
void MainWindow::load_settings()
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                "github_pull_tool", "Settings");
    QString root = s.value("scanRoot", "E:\\code").toString();
    m_rootEdit->setText(root);
}

/**
 * @brief       把当前扫描根目录写回 QSettings
 *
 * @return      none
 * @retval      none
 * @details     none
 * @doxygen_sig_fp e207d972b95dd305
 */
void MainWindow::save_settings()
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                "github_pull_tool", "Settings");
    s.setValue("scanRoot", m_rootEdit->text());
}

/**
 * @brief       扫描根目录文本变化时立即保存设置
 *
 * @return      none
 * @retval      none
 * @details     输入框随时可编辑，路径修改即时持久化
 * @doxygen_sig_fp 907df523c9127ad6
 */
void MainWindow::on_scan_root_changed() { save_settings(); }

/**
 * @brief       弹出系统目录选择对话框，更新扫描根目录
 *
 * @return      none
 * @retval      none
 * @details     用户取消选择（目录为空）时不改动输入框
 * @doxygen_sig_fp f6740b4f6a3a3003
 */
void MainWindow::on_browse()
{
    QString dir = QFileDialog::getExistingDirectory(this,
        QStringLiteral("选择扫描根目录"), m_rootEdit->text());
    if (!dir.isEmpty()) m_rootEdit->setText(dir);
}

// ═══════════════════════════════════════════════════════════════════════
// 日志
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief       向日志面板追加一行带颜色的文本
 *
 * @param[in]   {text} const QString& 日志内容（自动换行）
 * @param[in]   {color} const QColor& 文本颜色
 * @return      none
 * @retval      none
 * @details     签名与 GitManager::log_message 信号匹配，可直接连接
 * @doxygen_sig_fp 7460c47fa016223c
 */
void MainWindow::append_log(const QString &text, const QColor &color)
{
    m_log->moveCursor(QTextCursor::End);
    m_log->setTextColor(color);
    m_log->insertPlainText(text + "\n");
    m_log->moveCursor(QTextCursor::End);
}

// ═══════════════════════════════════════════════════════════════════════
// 扫描（异步：GitManager 后台线程执行，结果经信号回传）
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief       启动异步扫描：复位 UI 后交由 GitManager 后台线程执行
 *
 * @return      none
 * @retval      none
 * @details     扫描期间禁用扫描/拉取按钮、启用停止按钮（可中止扫描）；
 *              骨架表格、单行结果、收尾汇总分别由
 *              on_scan_list / on_scan_result / on_scan_finished 处理
 * @doxygen_sig_fp f8a8d2b569608d7f
 */
void MainWindow::on_scan()
{
    // 切换到扫描状态：禁用相关按钮，停止按钮对扫描同样生效
    m_scanBtn->setEnabled(false);
    m_pullAllBtn->setEnabled(false);
    m_pullSelectedBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    m_selectAllCb->setChecked(false);
    m_table->setRowCount(0);
    m_repos.clear();
    m_log->clear();
    m_summaryLabel->clear();

    append_log(QStringLiteral("═══ 开始扫描 %1 …").arg(m_rootEdit->text()),
              QColor(0, 100, 200));

    // 扫描在 GitManager 内部后台线程执行，本函数立即返回，UI 保持响应
    m_gitManager->start_scan(m_rootEdit->text().trimmed());
}

/**
 * @brief       GitManager 预检完成：按骨架列表建表（状态显示"扫描中…"）
 *
 * @param[in]   {repos} QVector<RepoInfo> GitHub 仓库骨架列表
 * @return      none
 * @retval      none
 * @details     骨架行先上屏让用户立刻看到仓库清单，详细状态随后逐行回填
 * @doxygen_sig_fp 77c821abbb0df98c
 */
void MainWindow::on_scan_list(QVector<RepoInfo> repos)
{
    // 表格已被新一轮扫描重置（行数不符）则忽略本轮结果
    if (m_table->rowCount() != 0 || !m_repos.isEmpty())
        return;

    m_repos = repos;
    m_table->setRowCount(m_repos.size());
    for (int i = 0; i < m_repos.size(); ++i)
        append_repo_row(i, m_repos[i]);
}

/**
 * @brief       GitManager 回传的单仓库状态回填表格对应行
 *
 * @param[in]   {repoIndex} int 仓库在 m_repos/表格中的行号
 * @param[in]   {info} RepoInfo 采集完成的仓库状态
 * @return      none
 * @retval      none
 * @details     行号超出当前表格范围（期间表格被重置）则忽略本条结果
 * @doxygen_sig_fp 948385a2cf8816a8
 */
void MainWindow::on_scan_result(int repoIndex, RepoInfo info)
{
    // 行号无效（新一轮扫描已重置表格）则忽略
    if (repoIndex < 0 || repoIndex >= m_table->rowCount() ||
        repoIndex >= m_repos.size())
        return;

    m_repos[repoIndex] = info;
    update_repo_row(repoIndex, info);
}

/**
 * @brief       扫描结束：恢复按钮、输出汇总日志与状态栏摘要
 *
 * @param[in]   {aborted} bool true 表示扫描被用户中止
 * @return      none
 * @retval      none
 * @details     统计数据直接遍历 m_repos 得出；中止时表格保留已采集的部分结果
 * @doxygen_sig_fp 08ce95f39b2f6834
 */
void MainWindow::on_scan_finished(bool aborted)
{
    m_stopBtn->setEnabled(false);
    m_scanBtn->setEnabled(true);
    m_pullAllBtn->setEnabled(m_repos.size() > 0);
    m_pullSelectedBtn->setEnabled(m_repos.size() > 0);
    m_selectAllCb->setChecked(true);

    // 遍历采集结果统计各状态数量（中止时未采集完的仍为"扫描中…"不计入）
    int behindCount = 0, dirtyCount = 0, detachCount = 0;
    for (const auto &r : m_repos) {
        if (r.dirty) dirtyCount++;
        else if (r.detached) detachCount++;
        else if (r.behind > 0) behindCount++;
    }

    if (aborted) {
        append_log(QStringLiteral("─── 扫描已中止 ───"), QColor(200, 150, 0));
    } else {
        append_log(QStringLiteral("─── 扫描完成 ───"), QColor(0, 120, 0));
    }
    append_log(QStringLiteral("GitHub 仓库：%1 个").arg(m_repos.size()));
    if (behindCount > 0)
        append_log(QStringLiteral("有更新可拉：%1 个").arg(behindCount), QColor(200, 80, 0));
    if (dirtyCount > 0)
        append_log(QStringLiteral("工作区脏  ：%1 个（跳过）").arg(dirtyCount), QColor(200, 150, 0));
    if (detachCount > 0)
        append_log(QStringLiteral("分离 HEAD ：%1 个（跳过）").arg(detachCount), QColor(180, 100, 0));

    m_summaryLabel->setText(QStringLiteral("共 %1 个 GitHub 仓库，%2 个可更新")
        .arg(m_repos.size()).arg(behindCount));
}

/**
 * @brief       为仓库在表格中建立行骨架（复选框/路径/占位状态）
 *
 * @param[in]   {row} int 目标行号（与 m_repos 索引一致）
 * @param[in]   {r} const RepoInfo& 仓库骨架信息
 * @return      none
 * @retval      none
 * @details     状态列显示"扫描中…"占位，详细内容由 update_repo_row 回填
 * @doxygen_sig_fp 212982a92376673f
 */
void MainWindow::append_repo_row(int row, const RepoInfo &r)
{
    // 复选框 (col 0)：默认全勾选，便于一键拉取
    auto *cb = new QCheckBox;
    cb->setChecked(true);
    m_table->setCellWidget(row, 0, cb);

    // 路径 (col 1)
    m_table->setItem(row, 1, new QTableWidgetItem(r.path));

    // 分支 (col 2)：占位，采集完成后回填
    m_table->setItem(row, 2, new QTableWidgetItem(r.branch));

    // 状态 (col 3)：占位
    m_table->setItem(row, 3, new QTableWidgetItem(r.status));

    // 落后 (col 4)：占位
    auto *beItem = new QTableWidgetItem("-");
    beItem->setTextAlignment(Qt::AlignCenter);
    m_table->setItem(row, 4, beItem);

    // 远程 (col 5)：单元格 tooltip 显示完整 URL
    auto *urlItem = new QTableWidgetItem(r.remoteUrl);
    urlItem->setForeground(Qt::gray);
    urlItem->setToolTip(r.remoteUrl);
    m_table->setItem(row, 5, urlItem);
}

/**
 * @brief       用采集完成的仓库状态回填表格单行内容与颜色
 *
 * @param[in]   {row} int 目标行号（与 m_repos 索引一致）
 * @param[in]   {r} const RepoInfo& 仓库完整状态
 * @return      none
 * @retval      none
 * @details     状态文本颜色按 工作区脏/分离HEAD/进行中/无上游/落后/最新 区分，
 *              与底部图例一致；分离或无上游时落后列显示 "-"
 * @doxygen_sig_fp 8df6b187b89a7745
 */
void MainWindow::update_repo_row(int row, const RepoInfo &r)
{
    // 分支 (col 2)
    m_table->item(row, 2)->setText(r.branch);

    // 状态 (col 3)：按状态着色（与图例颜色对应）
    auto *stItem = m_table->item(row, 3);
    stItem->setText(r.status);
    if (r.dirty) {
        stItem->setForeground(QColor(200, 150, 0));
    } else if (r.detached) {
        stItem->setForeground(QColor(180, 100, 0));
    } else if (r.inProgress) {
        stItem->setForeground(QColor(200, 100, 0));
    } else if (r.noUpstream) {
        stItem->setForeground(Qt::gray);
    } else if (r.behind > 0) {
        stItem->setForeground(QColor(200, 80, 0));
    } else {
        stItem->setForeground(QColor(0, 150, 0));
    }

    // 落后 (col 4)：分离/无上游显示 "-"，否则显示落后提交数
    auto *beItem = m_table->item(row, 4);
    if (!r.detached && !r.noUpstream) {
        beItem->setText(QString::number(r.behind));
        beItem->setForeground(m_table->palette().color(QPalette::WindowText));
    } else {
        beItem->setText("-");
        beItem->setForeground(Qt::gray);
    }

    // 远程 (col 5)：骨架阶段已填入，这里同步一次保持一致
    m_table->item(row, 5)->setText(r.remoteUrl);
}

// ═══════════════════════════════════════════════════════════════════════
// 拉取
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief       收集全部可拉取仓库的索引并启动拉取
 *
 * @return      none
 * @retval      none
 * @details     排除分离 HEAD、merge/rebase 进行中、无上游、工作区脏的仓库；无可拉取时仅提示
 * @doxygen_sig_fp cee779e1d3ada7b9
 */
void MainWindow::on_pull_all()
{
    QVector<int> indices;
    for (int i = 0; i < m_repos.size(); ++i) {
        if (!m_repos[i].detached && !m_repos[i].inProgress &&
            !m_repos[i].noUpstream && !m_repos[i].dirty)
            indices.append(i);
    }
    if (indices.isEmpty()) {
        append_log(QStringLiteral("⚠ 没有可拉取的仓库"), QColor(200, 150, 0));
        return;
    }
    pull_repos(indices);
}

/**
 * @brief       收集勾选且可拉取的仓库索引并启动拉取
 *
 * @return      none
 * @retval      none
 * @details     在 on_pull_all 排除条件基础上要求首列复选框勾选
 * @doxygen_sig_fp 7842f2728ba65b4e
 */
void MainWindow::on_pull_selected()
{
    QVector<int> indices;
    for (int i = 0; i < m_repos.size(); ++i) {
        auto *cb = qobject_cast<QCheckBox *>(m_table->cellWidget(i, 0));
        if (cb && cb->isChecked() &&
            !m_repos[i].detached && !m_repos[i].inProgress &&
            !m_repos[i].noUpstream && !m_repos[i].dirty)
            indices.append(i);
    }
    if (indices.isEmpty()) {
        append_log(QStringLiteral("⚠ 没有选中的可拉取仓库"), QColor(200, 150, 0));
        return;
    }
    pull_repos(indices);
}

/**
 * @brief       请求 GitManager 中止当前扫描或拉取队列
 *
 * @return      none
 * @retval      none
 * @details     扫描在当前仓库采集完成后退出循环；拉取在当前仓库完成后结束队列，不强行中断子进程
 * @doxygen_sig_fp 4580e21835f4d255
 */
void MainWindow::on_stop()
{
    // 只请求中止，GitManager 在当前仓库处理完成后结束扫描/拉取
    m_gitManager->request_abort();
    append_log(QStringLiteral("⏹ 已请求停止，正在等待当前仓库完成…"), QColor(200, 150, 0));
}

/**
 * @brief       切换 UI 到拉取状态并交由 GitManager 串行拉取
 *
 * @param[in]   {indices} const QVector<int>& 待拉取仓库在 m_repos 中的索引队列
 * @return      none
 * @retval      none
 * @details     禁用扫描/拉取按钮、显示进度条；具体拉取流程在 GitManager 内执行
 * @doxygen_sig_fp 7de96d95c4dbfba5
 */
void MainWindow::pull_repos(const QVector<int> &indices)
{
    // 切换到拉取状态：禁用扫描/拉取按钮、启用停止按钮、显示进度条
    m_scanBtn->setEnabled(false);
    m_pullAllBtn->setEnabled(false);
    m_pullSelectedBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    m_progress->setVisible(true);
    m_progress->setRange(0, indices.size());
    m_progress->setValue(0);

    append_log(QStringLiteral("═══ 开始拉取 %1 个仓库…").arg(indices.size()),
              QColor(0, 100, 200));

    // 串行拉取队列由 GitManager 驱动（QTimer + 异步 QProcess），
    // 进度经 pull_progress、结果经 repo_result 信号回传刷新 UI
    m_gitManager->start_pull(indices, m_repos);
}

/**
 * @brief GitManager 回传的单仓库拉取结果写入表格对应行
 * @param repoIndex     仓库在 m_repos/表格中的行号
 * @param status        要显示的状态文本
 * @param color         状态文本颜色
 * @param behindCleared true 表示拉取成功，落后数列清零
 * @return      none
 * @retval      none
 * @details     none
 * @doxygen_sig_fp 4241db00aa134639
 */
void MainWindow::on_repo_result(int repoIndex, const QString &status,
                              const QColor &color, bool behindCleared)
{
    // 行号超出当前表格范围（期间表格被重置）则忽略本条结果
    if (repoIndex >= m_table->rowCount()) return;

    m_table->item(repoIndex, 3)->setText(status);
    m_table->item(repoIndex, 3)->setForeground(color);
    if (behindCleared)
        m_table->item(repoIndex, 4)->setText("0"); // 拉取成功后落后数清零
}

/**
 * @brief 拉取队列结束（含中止）：隐藏进度条、恢复工具栏按钮并延时重扫
 * @return      none
 * @retval      none
 * @details     none
 * @doxygen_sig_fp 71a8b366654e9d36
 */
void MainWindow::on_pull_finished()
{
    m_progress->setVisible(false);
    m_scanBtn->setEnabled(true);
    m_pullAllBtn->setEnabled(true);
    m_pullSelectedBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);

    // 延时 500ms 重扫一次，刷新各仓库的最新状态
    QTimer::singleShot(500, this, [this]() {
        on_scan();
    });
}

/**
 * @brief       按状态批量勾选/取消全部仓库行的复选框
 *
 * @param[in]   {state} int 全选复选框状态，Qt::Checked 表示全选
 * @return      none
 * @retval      none
 * @details     none
 * @doxygen_sig_fp f668811c3afa5d92
 */
void MainWindow::on_select_all(int state)
{
    bool checked = (state == Qt::Checked);
    for (int i = 0; i < m_repos.size(); ++i) {
        auto *cb = qobject_cast<QCheckBox *>(m_table->cellWidget(i, 0));
        if (cb) cb->setChecked(checked);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// 关于 / 更新说明 / 退出
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief       模态弹出「关于」对话框
 *
 * @return      none
 * @retval      none
 * @details     对话框内容取自 appinfo.h 宏与 Qt 运行时信息
 * @doxygen_sig_fp e119773229ac67b1
 */
void MainWindow::on_about()
{
    AboutDialog dlg(this);
    dlg.exec();
}

/**
 * @brief       非模态打开「更新说明」窗口
 *
 * @return      none
 * @retval      none
 * @details     窗口挂在本窗口下并 WA_DeleteOnClose，关闭即销毁；
 *              已打开过则直接前置，避免同一时刻弹出多个相同窗口
 * @doxygen_sig_fp 5c3241c9b94a7b48
 */
void MainWindow::on_update_notes()
{
    // 已打开则前置已有窗口，避免重复弹出
    if (m_updateNotesWnd) {
        m_updateNotesWnd->show();
        m_updateNotesWnd->raise();
        m_updateNotesWnd->activateWindow();
        return;
    }

    // 非模态独立窗口：弹出后主窗口仍可正常操作
    m_updateNotesWnd = new UpdateNotesDialog(this);
    m_updateNotesWnd->setAttribute(Qt::WA_DeleteOnClose);
    // 关闭时清空指针，下次点击可重新创建
    connect(m_updateNotesWnd, &QObject::destroyed, this, [this]() {
        m_updateNotesWnd = nullptr;
    });
    m_updateNotesWnd->show();
    m_updateNotesWnd->raise();
    m_updateNotesWnd->activateWindow();
}

/**
 * @brief       退出整个程序
 *
 * @return      none
 * @retval      none
 * @details     连同所有已弹出的子窗口一起关闭
 * @doxygen_sig_fp 6ac6ca72c74e2d7b
 */
void MainWindow::on_exit()
{
    qApp->quit();
}
