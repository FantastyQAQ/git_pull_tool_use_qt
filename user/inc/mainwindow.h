/**
 * @file        mainwindow.h
 * @brief       主窗口类声明：UI 展示与操作入口
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.1
 * @date        20261106
 * @note        Git 扫描与拉取功能由 GitManager 后台异步提供，本类只负责展示与状态控制
 *
 * @copyright   Copyright (c) 2026 by OLE, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261106    <td>1.0       <td>lch       <td>创建
 * <tr><td>20261106    <td>1.1       <td>lch       <td>标题用 APP_FULL_TITLE；新增关于/更新说明/退出；适配异步扫描
 * </table>
 */
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QProgressBar>
#include <QCheckBox>
#include <QVector>

#include "gitmanager.h"

class QWidget;
class QVBoxLayout;

/**
 * @brief 主窗口：界面搭建、仓库表格/日志展示与操作入口
 *
 * Git 扫描与批量拉取的具体逻辑由 GitManager 承担（后台线程异步执行），
 * 本类只负责 UI 状态控制、展示 GitManager 回传的日志与结果。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_scan_root_changed();               // 扫描根目录输入变化
    void on_scan();                            // 启动异步扫描
    void on_scan_list(QVector<RepoInfo> repos); // 预检完成：按骨架建表
    void on_scan_result(int repoIndex, RepoInfo info); // 单仓库状态回填表格
    void on_scan_finished(bool aborted);       // 扫描结束：恢复按钮与汇总
    void on_pull_all();                        // 一键拉取全部可拉仓库
    void on_pull_selected();                   // 拉取勾选的仓库
    void on_stop();                            // 请求停止当前扫描/拉取队列
    void on_select_all(int state);             // 全选/全不选
    void on_browse();                          // 浏览选择扫描根目录
    void on_about();                           // 关于对话框
    void on_update_notes();                    // 更新说明窗口（非模态）
    void on_exit();                            // 退出程序
    void on_table_context_menu(const QPoint &pos); // 表格右键菜单
    void on_repo_result(int repoIndex, const QString &status, const QColor &color, bool behindCleared); // 单仓库拉取结果写入表格
    void on_pull_finished();                   // 拉取队列结束：恢复按钮并延时重扫

private:
    void setup_ui();         // 搭建整体界面框架（含顶部关于/更新说明/退出按钮行）
    void setup_tool_bar(QVBoxLayout *mainVBox); // 创建主工具栏按钮行（嵌主布局，位于关于行之下）
    void setup_status_bar(); // 创建状态栏（摘要 + 进度条）
    void append_repo_row(int row, const RepoInfo &r);   // 为仓库建立表格行骨架
    void update_repo_row(int row, const RepoInfo &r);   // 回填单行状态/分支/落后数与颜色
    void load_settings();    // 读取持久化设置
    void save_settings();    // 写回持久化设置
    void append_log(const QString &text, const QColor &color = Qt::black); // 追加一行日志
    void pull_repos(const QVector<int> &indices); // 切换 UI 到拉取状态后交由 GitManager 串行拉取

    QTableWidget *m_table;
    QTextEdit    *m_log;
    QLineEdit    *m_rootEdit;
    QPushButton  *m_scanBtn;
    QPushButton  *m_pullAllBtn;
    QPushButton  *m_pullSelectedBtn;
    QPushButton  *m_stopBtn;
    QPushButton  *m_browseBtn;
    QCheckBox    *m_selectAllCb;
    QProgressBar *m_progress;
    QLabel       *m_summaryLabel;
    QWidget      *m_updateNotesWnd = nullptr; // 更新说明窗口（判重前置用，WA_DeleteOnClose）

    QVector<RepoInfo> m_repos;  // 最近一次扫描的仓库列表（与表格行一一对应）
    GitManager *m_gitManager;   // Git 扫描与拉取功能类

    QString m_appDir; // 应用程序所在目录（扫描根默认值）
};

#endif // MAINWINDOW_H
