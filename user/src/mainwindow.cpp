/**
 * @file        mainwindow.cpp
 * @brief       主窗口实现：界面搭建、仓库表格/日志展示与操作入口
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.8
 * @date        20261113
 * @note        扫描与拉取逻辑在 GitManager 中异步执行，日志、代次、进度与勾选状态统一维护
 *
 * @copyright   Copyright (c) 2026 by OLE, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261106    <td>1.0       <td>lch       <td>创建
 * <tr><td>20261106    <td>1.1       <td>lch       <td>标题用 APP_FULL_TITLE；新增关于/更新说明/退出；扫描改异步流式填表
 * <tr><td>20261108    <td>1.2       <td>lch       <td>按代码审查完善日志、代次、筛选、进度和复选框状态
 * <tr><td>20261108    <td>1.3       <td>lch       <td>优化浅色界面、工具栏布局和仓库表格自适应列宽
 * <tr><td>20261108    <td>1.4       <td>lch       <td>移除重复应用标题，名称改为 Git 批量拉取工具
 * <tr><td>20261108    <td>1.5       <td>lch       <td>移除设置持久化，默认扫描 E 盘、无 E 盘用 C 盘
 * <tr><td>20261108    <td>1.6       <td>lch       <td>表格支持拖动多选，右键菜单新增选择/取消选择
 * <tr><td>20261113    <td>1.7       <td>lch       <td>扫描完成后按落后数自动勾选，无变化的取消勾选
 * <tr><td>20261113    <td>1.8       <td>lch       <td>状态栏新增当前仓库进度与拉取流量，流量按 k / M 双单位显示
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
#include <QDir>
#include <QStringList>
#include <QMessageBox>
#include <QApplication>
#include <QTimer>
#include <QStatusBar>
#include <QMenu>
#include <QDesktopServices>
#include <QClipboard>
#include <QItemSelectionModel>
#include <QFont>
namespace {

// 日志最大段落数，防止大批量扫描/拉取无限增长
constexpr int kMaxLogBlocks = 2000;

/**
 * @brief       计算启动时使用的默认扫描根目录
 * @return      优先返回 E 盘根目录；E 盘不存在时返回 C 盘根目录
 * @details     每次启动都重新扫描，不读取任何历史记录，因此这里只按盘符存在性选择
 * @doxygen_sig_fp 5c1a0b7f2e6d4a91
 */
QString default_scan_root()
{
    // 候选盘符按优先级排列：E 盘优先，其次 C 盘
    const QStringList candidates = { QStringLiteral("E:/"), QStringLiteral("C:/") };
    for (const QString &path : candidates) {
        if (QDir(path).exists())
            return QDir::toNativeSeparators(path);
    }
    // 极端情况（连 C 盘都不可用）退回系统根目录，保证输入框有合法路径
    return QDir::rootPath();
}

/**
 * @brief       判断仓库是否可安全进入拉取队列
 * @param[in]   {repo} const RepoInfo& 已采集的仓库状态
 * @return      已采集且可安全拉取时返回 true
 * @doxygen_sig_fp 1cb356930c994aaa
 */
bool is_pullable(const RepoInfo &repo)
{
    return repo.scanned && repo.pullable && !repo.detached && !repo.inProgress &&
           !repo.noUpstream && !repo.dirty && repo.ahead == 0;
}

/**
 * @brief       生成工具栏按钮使用的字体
 *
 * @return      QFont 与应用默认字体同字族、字号放大的字体
 * @retval      none
 * @details     字族直接取自 QApplication::font()，与「扫描目录」等 QLabel 完全一致，
 *              避免在样式表里硬编码字体名；字号在默认值上只加大 1pt，
 *              保证比正文略突出但不显臃肿。
 *              默认字体以磅值描述时走 setPointSizeF，以像素描述时走 setPixelSize，
 *              两种取值方式都兼容（pointSizeF 返回 -1 表示未按磅设置）。
 * @doxygen_sig_fp 2f8b41c6d95a370e
 */
QFont tool_bar_button_font()
{
    QFont font = QApplication::font();
    if (font.pointSizeF() > 0)
        font.setPointSizeF(font.pointSizeF() + 1.0);
    else
        font.setPixelSize(font.pixelSize() + 1);
    return font;
}

/**
 * @brief       将累计流量格式化为 k 与 M 两种单位同时显示的文本
 *
 * @param[in]   bytes 累计传输字节数
 * @return      形如 "流量 1536k / 1.50M" 的展示文本
 * @details     k 为 KiB 取整显示，M 为 MiB 保留两位小数；两者表示同一个数值，便于同时看清小流量与大流量
 * @doxygen_sig_fp ada6f3f77f2a6eb9
 */
QString format_traffic(qint64 bytes)
{
    const double kib = bytes / 1024.0;
    const double mib = bytes / (1024.0 * 1024.0);
    return QStringLiteral("流量 %1k / %2M")
        .arg(kib, 0, 'f', 0)
        .arg(mib, 0, 'f', 2);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════
// 构造 / 析构
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief       构造主窗口：搭建界面并连接 GitManager 信号
 *
 * @param[out]  {parent} QWidget* Qt 父窗口指针
 * @return      none（构造/析构函数，无返回值）
 * @retval      none
 * @details     GitManager 以本窗口为父对象，随窗口销毁自动释放；
 *              窗口标题取自 appinfo.h 的 APP_FULL_TITLE（名称+版本+构建日期）；
 *              不读取任何历史记录，扫描根目录每次启动按盘符可用性重新决定
 * @doxygen_sig_fp 755a2b28c15df9d3
 */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral(APP_FULL_TITLE));
    // 工具栏放大后略宽于默认尺寸，窗口适当加宽，避免右侧「关于/更新说明/退出」被挤压
    resize(2600, 1500);
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
    // 当前仓库阶段百分比与本轮累计流量回传到状态栏
    connect(m_gitManager, &GitManager::pull_repo_progress, this,
            [this](int percent) { m_repoProgress->setValue(percent); });
    connect(m_gitManager, &GitManager::pull_traffic, this,
            [this](qint64 bytes) { m_trafficLabel->setText(format_traffic(bytes)); });
    // 单仓库结果写入表格对应行
    connect(m_gitManager, &GitManager::repo_result,
            this, &MainWindow::on_repo_result);
    // 队列结束后恢复按钮并延时重扫
    connect(m_gitManager, &GitManager::pull_finished,
            this, &MainWindow::on_pull_finished);
}

/**
 * @brief       析构主窗口
 *
 * @return      none（构造/析构函数，无返回值）
 * @retval      none
 * @details     不做任何持久化保存：每次启动都是全新扫描
 * @doxygen_sig_fp a84ef10cd3716d34
 */
MainWindow::~MainWindow() = default;

// ═══════════════════════════════════════════════════════════════════════
// UI 搭建
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief 搭建主窗口整体界面框架
 *
 * 布局自上而下：
 *   1. 工具栏行：扫描/拉取/停止、选择选项，右侧为关于 / 更新说明 / 退出
 *   2. 扫描目录行：路径输入框 + 浏览按钮
 *   3. 中间分栏：左侧仓库表格 + 右侧日志（QSplitter 可拖拽，带面板标题）
 *   4. 图例行：状态颜色说明
 *   5. 状态栏：摘要标签 + 进度条（见 setup_status_bar）
 *   浅色样式集中设置在中央部件，所有表格列均支持自由拖拽调宽。
 * @return      none
 * @retval      none
 * @details     none
 * @doxygen_sig_fp 86fc58ce8be6edb3
 */
void MainWindow::setup_ui()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralWidget"));
    setCentralWidget(central);
    auto *mainVBox = new QVBoxLayout(central);
    mainVBox->setContentsMargins(14, 10, 14, 8);
    mainVBox->setSpacing(10);

    // 应用名称仅显示在系统标题栏，窗口操作按钮放在工具栏右侧
    setup_tool_bar(mainVBox);
    auto *rootRow = new QHBoxLayout;
    rootRow->setSpacing(8);
    auto *rootLabel = new QLabel(QStringLiteral("扫描目录"), central);
    rootLabel->setObjectName(QStringLiteral("fieldLabel"));
    rootRow->addWidget(rootLabel);
    m_rootEdit = new QLineEdit(default_scan_root(), central);
    m_rootEdit->setMinimumWidth(300);
    m_rootEdit->setClearButtonEnabled(true);
    rootRow->addWidget(m_rootEdit, 1);

    m_browseBtn = new QPushButton(QStringLiteral("浏览…"), central);
    connect(m_browseBtn, &QPushButton::clicked, this, &MainWindow::on_browse);
    rootRow->addWidget(m_browseBtn);
    mainVBox->addLayout(rootRow);

    // 仓库和日志均带小标题，分栏可拖动且不会压缩到完全不可见
    auto *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setChildrenCollapsible(false);
    auto *repoPanel = new QWidget(splitter);
    auto *repoLayout = new QVBoxLayout(repoPanel);
    repoLayout->setContentsMargins(0, 0, 0, 0);
    repoLayout->setSpacing(6);
    auto *repoTitle = new QLabel(QStringLiteral("仓库列表"), repoPanel);
    repoTitle->setObjectName(QStringLiteral("panelTitle"));
    repoLayout->addWidget(repoTitle);

    m_table = new QTableWidget(0, 6, repoPanel);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral(""), QStringLiteral("仓库路径"), QStringLiteral("分支"),
        QStringLiteral("状态"), QStringLiteral("落后"), QStringLiteral("远程地址")
    });
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(34);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    // 扩展选择：按住左键从某行向下/向上拖动即可连续选中整行范围，配合 Ctrl/Shift 也可增量选择
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // 右键菜单由 on_table_context_menu 自行弹出，需显式开启自定义菜单策略
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &MainWindow::on_table_context_menu);
    // 首列勾选状态改为表格项自带（不再是 QCheckBox 控件），
    // 这样整行任意位置都能起拖选中；勾选变化统一在此同步全选框
    connect(m_table, &QTableWidget::itemChanged,
            this, &MainWindow::on_table_item_changed);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // 全部列均可自由拖拽调整，以下宽度仅作为首次显示的初始值
    auto *header = m_table->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->setColumnWidth(0, 38);
    m_table->setColumnWidth(1, 300);
    m_table->setColumnWidth(2, 150);
    m_table->setColumnWidth(3, 250);
    m_table->setColumnWidth(4, 200);
    m_table->setColumnWidth(5, 800);
    repoLayout->addWidget(m_table, 1);
    splitter->addWidget(repoPanel);

    auto *logPanel = new QWidget(splitter);
    auto *logLayout = new QVBoxLayout(logPanel);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->setSpacing(6);
    auto *logTitle = new QLabel(QStringLiteral("运行日志"), logPanel);
    logTitle->setObjectName(QStringLiteral("panelTitle"));
    logLayout->addWidget(logTitle);
    m_log = new QTextEdit(logPanel);
    m_log->setReadOnly(true);
    m_log->setFont(QFont(QStringLiteral("Consolas"), 9));
    m_log->document()->setMaximumBlockCount(kMaxLogBlocks);
    logLayout->addWidget(m_log, 1);
    splitter->addWidget(logPanel);
    splitter->setStretchFactor(0, 7);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({750, 320});
    repoPanel->setMinimumWidth(480);
    logPanel->setMinimumWidth(240);
    mainVBox->addWidget(splitter, 1);

    auto *legend = new QLabel(QStringLiteral(
        "<span style='color:#159447;'>● 已是最新</span>&nbsp;&nbsp;&nbsp;"
        "<span style='color:#d56a16;'>● 落后可更新</span>&nbsp;&nbsp;&nbsp;"
        "<span style='color:#bf8b09;'>● 工作区有改动</span>&nbsp;&nbsp;&nbsp;"
        "<span style='color:#7c8798;'>● 分离/无上游</span>"), central);
    legend->setTextFormat(Qt::RichText);
    legend->setContentsMargins(2, 0, 0, 0);
    mainVBox->addWidget(legend);

    // 使用轻量浅色样式统一各控件；只作用于主窗口，不影响独立对话框
    central->setStyleSheet(QStringLiteral(R"(
        QWidget#centralWidget { background: #f5f7fa; }
        QLabel { color: #536174; }
        QLabel#fieldLabel, QLabel#panelTitle { color: #344256; font-weight: 600; }
        QPushButton {
            background: #ffffff; color: #344256; border: 1px solid #d7dee8;
            border-radius: 4px; padding: 6px 13px; min-height: 20px;
        }
        /* 工具栏整行按钮统一放大：加高、加宽内边距，字族与字号由 setup_tool_bar
           统一设置为与「扫描目录」相同的字体（不在此处写死字体名） */
        QWidget#toolBar QPushButton {
            padding: 7px 16px; min-height: 22px; border-radius: 5px;
        }
        QWidget#toolBar QPushButton#navButton,
        QWidget#toolBar QPushButton#navAboutButton,
        QWidget#toolBar QPushButton#navUpdateButton,
        QWidget#toolBar QPushButton#navExitButton {
            padding: 7px 10px; min-height: 22px;
        }
        /* 右侧三个窗口操作按钮用不同浅色底区分：
           关于=浅蓝（信息）、更新说明=浅绿（内容）、退出=浅红（结束程序） */
        QWidget#toolBar QPushButton#navAboutButton {
            background: #eaf1fc; border: 1px solid #c3d8f5; color: #2166c1;
        }
        QWidget#toolBar QPushButton#navAboutButton:hover {
            background: #dbe8fb; border-color: #9dc0ee;
        }
        QWidget#toolBar QPushButton#navUpdateButton {
            background: #e9f7ef; border: 1px solid #c2e6d1; color: #1d7a45;
        }
        QWidget#toolBar QPushButton#navUpdateButton:hover {
            background: #d9f0e4; border-color: #9bd4b4;
        }
        QWidget#toolBar QPushButton#navExitButton {
            background: #fdecec; border: 1px solid #f4c7c7; color: #b93a3a;
        }
        QWidget#toolBar QPushButton#navExitButton:hover {
            background: #fbdada; border-color: #eda4a4;
        }
        /* 复选框文字字号由 setup_tool_bar 用 setFont 统一设置（与按钮同字族同字号），
           这里只负责方框尺寸与文字间距；样式表一旦写 font-size 会覆盖 setFont，故不写 */
        QWidget#toolBar QCheckBox { spacing: 7px; }
        QWidget#toolBar QCheckBox::indicator { width: 24px; height: 24px; }
        QPushButton:hover { background: #f0f5ff; border-color: #82a9e8; color: #2166c1; }
        QPushButton:pressed { background: #e3edfc; }
        QPushButton:disabled { background: #f0f2f5; color: #a2acb9; border-color: #e2e6ec; }
        QPushButton#primaryButton { background: #2f73d9; color: #ffffff; border-color: #2f73d9; font-weight: 600; }
        QPushButton#primaryButton:hover { background: #2766c4; border-color: #2766c4; }
        QPushButton#primaryButton:disabled { background: #a9c3e9; color: #edf3fc; border-color: #a9c3e9; }
        /* 右侧窗口操作按钮的样式见下方 toolBar 内 navAboutButton / navUpdateButton /
           navExitButton 规则（各自浅色底 + 同色系方框边框） */
        QWidget#toolBar { background: #ffffff; border: 1px solid #e1e7ef; border-radius: 5px; }
        QLineEdit {
            background: #ffffff; color: #2f3b4c; border: 1px solid #d7dee8;
            border-radius: 4px; padding: 6px 9px; min-height: 20px;
            selection-background-color: #bcd5f7;
        }
        QLineEdit:focus { border-color: #5791df; }
        QCheckBox { color: #536174; spacing: 6px; }
        QTableWidget, QTextEdit {
            background: #ffffff; alternate-background-color: #f8fafc;
            color: #344256; border: 1px solid #dde4ed; border-radius: 4px;
            selection-background-color: #e4efff; selection-color: #243447;
        }
        QTableWidget { gridline-color: #edf1f5; }
        QTableWidget::item { padding: 4px 7px; border-bottom: 1px solid #f0f2f5; }
        /* 上面给 ::item 设了样式后单元格改由样式表绘制，必须显式给出选中配色，
           否则左键拖动多选时整行高亮不可见 */
        QTableWidget::item:selected { background: #e4efff; color: #243447; }
        QHeaderView::section {
            background: #f0f3f7; color: #536174; padding: 8px 7px;
            border: none; border-right: 1px solid #e3e8ef; border-bottom: 1px solid #dce3ec;
        }
        QTextEdit { padding: 6px; }
        QStatusBar { background: #edf1f6; color: #536174; }
        QStatusBar::item { border: none; }
        QProgressBar { background: #dce4ef; border: none; border-radius: 4px; max-height: 8px; }
        QProgressBar::chunk { background: #2f73d9; border-radius: 4px; }
        QSplitter::handle:horizontal { width: 4px; background: #e0e6ee; }
    )"));
    setup_status_bar();
}

/**
 * @brief       创建主工具栏按钮行
 *
 * 按钮行左侧承载开始扫描、一键拉取全部、拉取选中、停止，
 * 以及「全选」与「扫描时同步远程」复选框，右侧为关于 / 更新说明 / 退出。
 * 直接嵌入主垂直布局，使用独立浅色容器区分操作区与内容区。
 * 按钮样式由主窗口统一提供，「开始扫描」使用蓝色突出主操作。
 *
 * @param[out]  {mainVBox} QVBoxLayout* 主窗口主布局，按钮行插入其尾部
 * @return      none
 * @retval      none
 * @details     none
 * @doxygen_sig_fp 3e7a7e9914390443
 */
void MainWindow::setup_tool_bar(QVBoxLayout *mainVBox)
{
    // 工具栏作为独立浅色卡片，集中放置仓库操作与选择选项
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("toolBar"));
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(8);

    // 工具栏内所有按钮（含右侧关于/更新说明/退出）统一使用与「扫描目录」相同的字族，
    // 并在默认字号上放大，保证整行文字大小一致
    const QFont buttonFont = tool_bar_button_font();
    const auto apply_button_font = [buttonFont](QPushButton *button) {
        button->setFont(buttonFont);
    };
    // 复选框（全选 / 扫描时同步远程）与按钮同字族、同字号，整行文字视觉一致
    const auto apply_check_box_font = [buttonFont](QCheckBox *checkBox) {
        checkBox->setFont(buttonFont);
    };

    // 仅扫描按钮使用强调色，其余操作保持次要按钮样式
    m_scanBtn = new QPushButton(QStringLiteral("开始扫描"), bar);
    m_scanBtn->setObjectName(QStringLiteral("primaryButton"));
    apply_button_font(m_scanBtn);
    connect(m_scanBtn, &QPushButton::clicked, this, &MainWindow::on_scan);
    lay->addWidget(m_scanBtn);

    // 拉取全部：仅扫描到仓库后可用
    m_pullAllBtn = new QPushButton(QStringLiteral("一键拉取全部"), bar);
    m_pullAllBtn->setEnabled(false);
    apply_button_font(m_pullAllBtn);
    connect(m_pullAllBtn, &QPushButton::clicked, this, &MainWindow::on_pull_all);
    lay->addWidget(m_pullAllBtn);

    // 拉取选中：仅扫描到仓库后可用
    m_pullSelectedBtn = new QPushButton(QStringLiteral("拉取选中"), bar);
    m_pullSelectedBtn->setEnabled(false);
    apply_button_font(m_pullSelectedBtn);
    connect(m_pullSelectedBtn, &QPushButton::clicked, this, &MainWindow::on_pull_selected);
    lay->addWidget(m_pullSelectedBtn);

    // 停止：扫描/拉取进行中可用
    m_stopBtn = new QPushButton(QStringLiteral("停止"), bar);
    m_stopBtn->setEnabled(false);
    apply_button_font(m_stopBtn);
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::on_stop);
    lay->addWidget(m_stopBtn);

    lay->addSpacing(8); // 与全选复选框之间留出分隔间距

    // 全选复选框：批量勾选/取消表格首列
    m_selectAllCb = new QCheckBox(QStringLiteral("全选"), bar);
    // 三态用于表示全部/部分/未勾选；批量操作时由同步标记避免递归
    m_selectAllCb->setTristate(true);
    apply_check_box_font(m_selectAllCb);
    connect(m_selectAllCb, &QCheckBox::stateChanged, this, &MainWindow::on_select_all);
    lay->addWidget(m_selectAllCb);

    // 默认勾选同步：扫描前先 fetch，远程跟踪引用才是最新，落后提交数才准确；取消勾选则只读本地缓存
    m_fetchRemoteCb = new QCheckBox(QStringLiteral("扫描时同步远程"), bar);
    m_fetchRemoteCb->setToolTip(QStringLiteral("勾选后扫描会执行 git fetch --prune --no-tags，更新远程跟踪引用"));
    m_fetchRemoteCb->setChecked(true);
    apply_check_box_font(m_fetchRemoteCb);
    lay->addWidget(m_fetchRemoteCb);

    lay->addStretch(1); // 仓库操作靠左，窗口操作靠右

    // 次要窗口操作集中放在工具栏右侧，避免额外占用一行标题空间；
    // 三个按钮各自带不同底色，便于与左侧白色操作按钮区分开
    const auto add_nav_button = [this, lay, apply_button_font](const QString &text,
                                                              const QString &objectName) {
        auto *button = new QPushButton(text, this);
        button->setObjectName(objectName);
        button->setCursor(Qt::PointingHandCursor);
        apply_button_font(button);
        lay->addWidget(button);
        return button;
    };
    auto *aboutBtn = add_nav_button(QStringLiteral("关于"), QStringLiteral("navAboutButton"));
    auto *updateBtn = add_nav_button(QStringLiteral("更新说明"), QStringLiteral("navUpdateButton"));
    auto *exitBtn = add_nav_button(QStringLiteral("退出"), QStringLiteral("navExitButton"));
    connect(aboutBtn, &QPushButton::clicked, this, &MainWindow::on_about);
    connect(updateBtn, &QPushButton::clicked, this, &MainWindow::on_update_notes);
    connect(exitBtn, &QPushButton::clicked, this, &MainWindow::on_exit);
    mainVBox->addWidget(bar);
}

/**
 * @brief 创建状态栏
 *
 * 左侧为摘要标签（仓库统计），右侧依次为总进度条、当前仓库进度条（带百分比）、拉取流量（永久部件）。
 * 三个进度控件默认隐藏，进入拉取流程时显示；流量标签在拉取结束后保留最终值。
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
    m_progress->setFixedWidth(160);
    m_progress->setTextVisible(false);
    m_progress->setVisible(false);
    statusBar()->addPermanentWidget(m_progress);

    // 当前仓库进度显示百分比文字，范围固定为 0-100
    m_repoProgress = new QProgressBar(this);
    m_repoProgress->setFixedWidth(160);
    m_repoProgress->setRange(0, 100);
    m_repoProgress->setFormat(QStringLiteral("%p%"));
    m_repoProgress->setVisible(false);
    statusBar()->addPermanentWidget(m_repoProgress);

    // 流量文本长度随数值变化，给一个最小宽度避免状态栏跳动
    m_trafficLabel = new QLabel(this);
    m_trafficLabel->setMinimumWidth(200);
    m_trafficLabel->setVisible(false);
    statusBar()->addPermanentWidget(m_trafficLabel);
}

/**
 * @brief 表格右键菜单：选择 / 取消选择 / 打开所在目录 / 复制远程地址
 * @param pos 视口内坐标（由 customContextMenuRequested 信号给出）
 *
 * 「选择」「取消选择」作用于当前高亮的多行选区（左键拖动或 Ctrl/Shift 选出的范围）；
 * 「打开所在目录」「复制远程地址」只作用于右键所在的那一行。
 * @return      none
 * @retval      none
 * @details     右键落在空白区域时不弹菜单；右键按在未高亮的行上时，
 *              Qt 已在按下阶段把选区收缩为该行，因此这里直接读选区即可
 * @doxygen_sig_fp 5509136069130d53
 */
void MainWindow::on_table_context_menu(const QPoint &pos)
{
    // 行号无效（空白区域或超出数据范围）则不弹菜单
    const int clickedRow = m_table->rowAt(pos.y());
    if (clickedRow < 0 || clickedRow >= m_repos.size())
        return;

    // 收集当前高亮选中的行；左键拖动多选后这里会是连续的一段行
    QVector<int> rows;
    const QModelIndexList selected = m_table->selectionModel()->selectedRows();
    for (const QModelIndex &index : selected)
        rows.append(index.row());
    // 兜底：选区为空时只对右键所在行生效
    if (rows.isEmpty())
        rows.append(clickedRow);

    QMenu menu(this);
    QAction *selectAct = menu.addAction(QStringLiteral("选择"));
    QAction *unselectAct = menu.addAction(QStringLiteral("取消选择"));
    menu.addSeparator();
    QAction *openDirAct = menu.addAction(QStringLiteral("打开所在目录"));
    QAction *copyUrlAct = menu.addAction(QStringLiteral("复制远程地址"));

    QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (chosen == selectAct || chosen == unselectAct) {
        // 批量修改首列勾选状态，作用范围即当前高亮选区
        set_rows_checked(rows, chosen == selectAct);
    } else if (chosen == openDirAct) {
        // 用系统资源管理器打开右键所在行的仓库目录
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_repos[clickedRow].absolutePath));
    } else if (chosen == copyUrlAct) {
        // 复制右键所在行的 origin 远程地址到剪贴板
        QApplication::clipboard()->setText(m_repos[clickedRow].remoteUrl);
    }
}

/**
 * @brief       判断指定行的首列复选框是否已勾选
 *
 * @param[in]   {row} int 目标行号
 * @return      已勾选返回 true；行无效或该项不存在返回 false
 * @retval      false 行号越界或单元格缺失
 * @details     勾选状态存在表格项里（见 append_repo_row），不是独立控件
 * @doxygen_sig_fp 4f2b6d8c1e35a970
 */
bool MainWindow::check_row_checked(int row) const
{
    const QTableWidgetItem *item = m_table->item(row, 0);
    return item && item->checkState() == Qt::Checked;
}

/**
 * @brief       批量设置指定行首列复选框的勾选状态
 *
 * @param[in]   {rows} const QVector<int>& 目标行号列表（与 m_repos 索引一致）
 * @param[in]   {checked} bool true 为勾选，false 为取消勾选
 * @return      none
 * @retval      none
 * @details     批量期间用 m_syncingSelectAll 抑制单行变化反复重算全选框，
 *              批量结束后统一同步一次，避免中途出现半选闪烁
 * @doxygen_sig_fp be9a39ff3f259e57
 */
void MainWindow::set_rows_checked(const QVector<int> &rows, bool checked)
{
    const Qt::CheckState state = checked ? Qt::Checked : Qt::Unchecked;
    m_syncingSelectAll = true;
    for (int row : rows) {
        QTableWidgetItem *item = m_table->item(row, 0);
        if (item)
            item->setCheckState(state);
    }
    m_syncingSelectAll = false;
    update_select_all_state();
}

/**
 * @brief       表格项变化处理：用户勾选/取消勾选首列时同步全选框
 *
 * @param[in]   {item} QTableWidgetItem* 发生变化的表格项
 * @return      none
 * @retval      none
 * @details     只关心首列（勾选列）；批量修改期间由 m_syncingSelectAll 抑制
 * @doxygen_sig_fp 2c7e19ab54d03f86
 */
void MainWindow::on_table_item_changed(QTableWidgetItem *item)
{
    if (m_syncingSelectAll || !item || item->column() != 0)
        return;
    update_select_all_state();
}

// ═══════════════════════════════════════════════════════════════════════
// 扫描根目录
// ═══════════════════════════════════════════════════════════════════════

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
    // 手动扫描开启新日志；拉取后的自动重扫走 begin_scan(false) 保留拉取结果
    begin_scan(true);
}

/**
 * @brief       GitManager 预检完成：按骨架列表建表（状态显示"扫描中…"）
 *
 * @param[in]   {repos} QVector<RepoInfo> 带远端的仓库骨架列表
 * @return      none
 * @retval      none
 * @details     骨架行先上屏让用户立刻看到仓库清单，详细状态随后逐行回填
 * @doxygen_sig_fp abc2d8b0c21b4e28
 * @param[in]   {scanId} int 回传扫描代次
 */
void MainWindow::on_scan_list(int scanId, QVector<RepoInfo> repos)
{
    // 只有当前代次的骨架允许建表，旧线程残留信号直接丢弃
    if (scanId != m_scanId)
        return;

    m_syncingSelectAll = true;
    m_repos = repos;
    m_table->setRowCount(m_repos.size());
    for (int i = 0; i < m_repos.size(); ++i)
        append_repo_row(i, m_repos[i]);
    m_syncingSelectAll = false;
    update_select_all_state();
}

/**
 * @brief       GitManager 回传的单仓库状态回填表格对应行
 *
 * @param[in]   {repoIndex} int 仓库在 m_repos/表格中的行号
 * @param[in]   {info} RepoInfo 采集完成的仓库状态
 * @return      none
 * @retval      none
 * @details     行号超出当前表格范围（期间表格被重置）则忽略本条结果
 * @doxygen_sig_fp 3c8ba4be443a7202
 * @param[in]   {scanId} int 回传扫描代次
 */
void MainWindow::on_scan_result(int scanId, int repoIndex, RepoInfo info)
{
    // 代次和行号都必须匹配，防止旧扫描结果覆盖新表格
    if (scanId != m_scanId || repoIndex < 0 || repoIndex >= m_table->rowCount() ||
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
 * @details     统计数据直接遍历 m_repos 得出；未中止时按落后数自动勾选（有落后提交的勾选，
 *              其余取消）；中止时表格保留已采集的部分结果且保持原勾选
 * @doxygen_sig_fp 0909728d5cb376a6
 * @param[in]   {scanId} int 回传扫描代次
 */
void MainWindow::on_scan_finished(int scanId, bool aborted)
{
    // 旧代次的完成信号不得恢复新一轮扫描的按钮状态
    if (scanId != m_scanId)
        return;

    m_stopBtn->setEnabled(false);
    m_scanBtn->setEnabled(true);
    m_pullAllBtn->setEnabled(!m_repos.isEmpty());
    m_pullSelectedBtn->setEnabled(!m_repos.isEmpty());
    update_select_all_state();

    // 未中止时按落后数自动勾选：有落后提交（含分叉）的仓库勾选，其余取消勾选；
    // 中止时保留扫描前的勾选状态，避免未采集的行被误改
    if (!aborted) {
        QVector<int> changedRows;
        QVector<int> unchangedRows;
        for (int i = 0; i < m_repos.size(); ++i) {
            if (m_repos[i].scanned && m_repos[i].behind > 0)
                changedRows.append(i);
            else
                unchangedRows.append(i);
        }
        set_rows_checked(changedRows, true);
        set_rows_checked(unchangedRows, false);
    }

    // 统计只依据已采集状态；未采集的骨架行不会计入可更新数量
    int behindCount = 0;
    int dirtyCount = 0;
    int detachCount = 0;
    for (const RepoInfo &repo : m_repos) {
        if (repo.dirty)
            ++dirtyCount;
        else if (repo.detached)
            ++detachCount;
        else if (repo.scanned && repo.behind > 0)
            ++behindCount;
    }

    append_log(aborted ? QStringLiteral("─── 扫描已中止 ───")
                       : QStringLiteral("─── 扫描完成 ───"),
               aborted ? QColor(200, 150, 0) : QColor(0, 120, 0));
    append_log(QStringLiteral("带远端的仓库：%1 个").arg(m_repos.size()));
    if (behindCount > 0)
        append_log(QStringLiteral("有更新可拉：%1 个").arg(behindCount), QColor(200, 80, 0));
    if (dirtyCount > 0)
        append_log(QStringLiteral("工作区脏  ：%1 个（跳过）").arg(dirtyCount), QColor(200, 150, 0));
    if (detachCount > 0)
        append_log(QStringLiteral("分离 HEAD ：%1 个（跳过）").arg(detachCount), QColor(180, 100, 0));

    m_summaryLabel->setText(QStringLiteral("共 %1 个带远端的仓库，%2 个可更新")
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
    // 复选框 (col 0)：默认勾选，便于一键拉取；用表格项自带勾选状态，
    // 这样整行任意位置都能作为左键拖选的起点
    auto *checkItem = new QTableWidgetItem;
    checkItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    checkItem->setCheckState(Qt::Checked);
    m_table->setItem(row, 0, checkItem);

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

    // 状态 (col 3)：按状态着色；未完成采集或检查失败统一用灰色，避免误报为最新
    auto *stItem = m_table->item(row, 3);
    stItem->setText(r.status);
    if (!r.scanned) {
        stItem->setForeground(Qt::gray);
    } else if (r.dirty) {
        stItem->setForeground(QColor(200, 150, 0));
    } else if (r.detached) {
        stItem->setForeground(QColor(180, 100, 0));
    } else if (r.inProgress) {
        stItem->setForeground(QColor(200, 100, 0));
    } else if (r.noUpstream || !r.pullable) {
        stItem->setForeground(Qt::gray);
    } else if (r.behind > 0) {
        stItem->setForeground(QColor(200, 80, 0));
    } else if (r.ahead > 0) {
        stItem->setForeground(QColor(180, 100, 0)); // 本地领先需人工确认，不能显示为绿色最新
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
        if (is_pullable(m_repos[i]))
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
        if (check_row_checked(i) && is_pullable(m_repos[i]))
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
 * @details     扫描在当前仓库采集完成后退出循环；拉取立即终止当前子进程，不再启动后续仓库
 * @doxygen_sig_fp 4580e21835f4d255
 */
void MainWindow::on_stop()
{
    // 扫描在当前仓库采集后退出；拉取立即终止当前子进程并收尾
    m_stopBtn->setEnabled(false);
    m_gitManager->request_abort();
    append_log(QStringLiteral("⏹ 已请求停止，正在结束当前任务…"), QColor(200, 150, 0));
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

    // 当前仓库进度与流量从本轮开始重新计数
    m_repoProgress->setValue(0);
    m_repoProgress->setVisible(true);
    m_trafficLabel->setText(format_traffic(0));
    m_trafficLabel->setVisible(true);

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
    // 行号无效时忽略；拉取期间表格不会被重扫，当前代次结果可直接回写
    if (repoIndex < 0 || repoIndex >= m_table->rowCount() || repoIndex >= m_repos.size())
        return;

    m_repos[repoIndex].status = status;
    m_table->item(repoIndex, 3)->setText(status);
    m_table->item(repoIndex, 3)->setForeground(color);
    if (behindCleared) {
        // 同步内部快照与表格，避免去掉自动重扫后仍使用旧 behind 值
        m_repos[repoIndex].behind = 0;
        m_table->item(repoIndex, 4)->setText("0");
    }
}

/**
 * @brief 拉取队列结束（含中止）：隐藏进度条、恢复工具栏按钮并延时重扫
 * @return      none
 * @retval      none
 * @details     none
 * @doxygen_sig_fp c01ec34fecea40b6
 * @param[in]   {aborted} bool 是否由用户中止
 */
void MainWindow::on_pull_finished(bool aborted)
{
    // 流量标签保留最终值，直到下一轮拉取开始
    m_progress->setVisible(false);
    m_repoProgress->setVisible(false);
    m_scanBtn->setEnabled(true);
    m_pullAllBtn->setEnabled(!m_repos.isEmpty());
    m_pullSelectedBtn->setEnabled(!m_repos.isEmpty());
    m_stopBtn->setEnabled(false);

    // 正常完成后自动重扫刷新状态，但保留本次拉取日志；中止后由用户决定何时刷新
    if (!aborted) {
        append_log(QStringLiteral("─── 保留拉取日志，500ms 后刷新仓库状态 ───"), QColor(0, 100, 200));
        QTimer::singleShot(500, this, [this]() {
            begin_scan(false);
        });
    }
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
    // 程序同步全选框状态时不反向修改行，避免半选状态被意外清除
    if (m_syncingSelectAll)
        return;

    // 半选状态由用户点击时按全选处理，避免一次点击反而清空所有选择
    const bool checked = (state != Qt::Unchecked);
    m_syncingSelectAll = true;
    const Qt::CheckState checkState = checked ? Qt::Checked : Qt::Unchecked;
    for (int i = 0; i < m_repos.size(); ++i) {
        QTableWidgetItem *item = m_table->item(i, 0);
        if (item)
            item->setCheckState(checkState);
    }
    m_syncingSelectAll = false;
    update_select_all_state();
}

// ═══════════════════════════════════════════════════════════════════════
// 扫描 / 全选状态同步
// ═══════════════════════════════════════════════════════════════════════

/**
 * @brief       启动一轮扫描并统一初始化界面状态
 *
 * @param[in]   clearLog true 为手动扫描清空旧日志，false 为拉取后刷新保留日志
 * @return      none
 * @details     代次由 GitManager 分配并立即保存；所有扫描回传均需匹配该代次，
 *              远程同步选项默认关闭，只有用户勾选后才传给扫描线程
 * @doxygen_sig_fp 6605958a2047b142
 */
void MainWindow::begin_scan(bool clearLog)
{
    // 切换到扫描状态：禁用相关按钮，停止按钮对扫描同样生效
    m_scanBtn->setEnabled(false);
    m_pullAllBtn->setEnabled(false);
    m_pullSelectedBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);

    // 批量清空表格期间禁止复选框信号递归修改全选状态
    m_syncingSelectAll = true;
    m_table->setRowCount(0);
    m_repos.clear();
    m_selectAllCb->setCheckState(Qt::Unchecked);
    m_syncingSelectAll = false;
    if (clearLog)
        m_log->clear();
    m_summaryLabel->clear();

    append_log(QStringLiteral("═══ 开始扫描 %1 …").arg(m_rootEdit->text()),
               QColor(0, 100, 200));

    // 保存 GitManager 返回的代次，后续所有结果均按它过滤
    m_scanId = m_gitManager->start_scan(m_rootEdit->text().trimmed(),
                                        m_fetchRemoteCb->isChecked());
}

/**
 * @brief       根据表格各行勾选状态同步全选复选框
 *
 * @return      none
 * @details     全部勾选为 Checked，全部未选为 Unchecked，部分勾选为 PartiallyChecked；
 *              批量更新时通过 m_syncingSelectAll 抑制递归
 * @doxygen_sig_fp 5d8cd862211f5de9
 */
void MainWindow::update_select_all_state()
{
    int checkedCount = 0;
    for (int i = 0; i < m_repos.size(); ++i) {
        if (check_row_checked(i))
            ++checkedCount;
    }

    Qt::CheckState state = Qt::Unchecked;
    if (checkedCount == m_repos.size() && checkedCount > 0)
        state = Qt::Checked;
    else if (checkedCount > 0)
        state = Qt::PartiallyChecked;

    // 修改全选框也会触发 on_select_all，必须临时屏蔽该反向同步
    m_syncingSelectAll = true;
    m_selectAllCb->setCheckState(state);
    m_syncingSelectAll = false;
}

/**
 * @brief       模态弹出「关于」对话框
 *
 * @return      none
 * @details     对话框内容取自 appinfo.h 宏与 Qt 运行时信息
 * @doxygen_sig_fp e119773229ac67b1
 */
// ═══════════════════════════════════════════════════════════════════════
// 关于 / 更新说明 / 退出
// ═══════════════════════════════════════════════════════════════════════

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
