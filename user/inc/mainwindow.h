/**
 * @file        mainwindow.h
 * @brief       主窗口类声明：UI 展示与操作入口
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.5
 * @date        20261113
 * @note        Git 扫描与拉取功能由 GitManager 异步提供，本类负责展示与状态控制
 *
 * @copyright   Copyright (c) 2026 by OLE, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261106    <td>1.0       <td>lch       <td>创建
 * <tr><td>20261106    <td>1.1       <td>lch       <td>标题用 APP_FULL_TITLE；新增关于/更新说明/退出；适配异步扫描
 * <tr><td>20261108    <td>1.2       <td>lch       <td>按代码审查完善日志、代次、筛选、进度和复选框状态
 * <tr><td>20261108    <td>1.3       <td>lch       <td>移除设置持久化相关接口
 * <tr><td>20261108    <td>1.4       <td>lch       <td>新增批量设置行勾选状态的接口
 * <tr><td>20261113    <td>1.5       <td>lch       <td>状态栏新增当前仓库进度与拉取流量
 * </table>
 */
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QVector>

#include "gitmanager.h"

class QVBoxLayout;

/** 主窗口：界面搭建、仓库表格/日志展示与操作入口 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_scan();                            // 手动启动扫描（清空旧日志）
    void on_scan_list(int scanId, QVector<RepoInfo> repos); // 按指定代次骨架建表
    void on_scan_result(int scanId, int repoIndex, RepoInfo info); // 回填指定代次单行状态
    void on_scan_finished(int scanId, bool aborted); // 扫描结束：恢复按钮与汇总
    void on_pull_all();                        // 一键拉取全部可拉仓库
    void on_pull_selected();                   // 拉取勾选的仓库
    void on_stop();                            // 请求停止当前扫描/拉取队列
    void on_select_all(int state);             // 全选/全不选
    void on_browse();                          // 浏览选择扫描根目录
    void on_about();                           // 关于对话框
    void on_update_notes();                    // 更新说明窗口（非模态）
    void on_exit();                            // 退出程序
    void on_table_context_menu(const QPoint &pos); // 表格右键菜单
    void on_table_item_changed(QTableWidgetItem *item); // 首列勾选变化后同步全选框
    void on_repo_result(int repoIndex, const QString &status, const QColor &color, bool behindCleared); // 写入单仓库拉取结果
    void on_pull_finished(bool aborted);       // 拉取结束：恢复按钮并按需重扫

private:
    void setup_ui();         // 搭建整体界面框架
    void setup_tool_bar(QVBoxLayout *mainVBox); // 创建主工具栏按钮行
    void setup_status_bar(); // 创建状态栏（摘要 + 进度条）
    void append_repo_row(int row, const RepoInfo &r); // 为仓库建立表格行骨架
    void update_repo_row(int row, const RepoInfo &r); // 回填单行状态/分支/落后数与颜色
    void append_log(const QString &text, const QColor &color = Qt::black); // 追加一行日志
    void pull_repos(const QVector<int> &indices); // 切换 UI 到拉取状态并启动队列
    void begin_scan(bool clearLog);            // 启动扫描并决定是否清空日志
    void update_select_all_state();            // 根据各行勾选状态同步全选框
    void set_rows_checked(const QVector<int> &rows, bool checked); // 批量设置指定行勾选状态
    bool check_row_checked(int row) const;     // 查询指定行首列是否勾选

    QTableWidget *m_table;
    QTextEdit    *m_log;
    QLineEdit    *m_rootEdit;
    QPushButton  *m_scanBtn;
    QPushButton  *m_pullAllBtn;
    QPushButton  *m_pullSelectedBtn;
    QPushButton  *m_stopBtn;
    QPushButton  *m_browseBtn;
    QCheckBox    *m_selectAllCb;
    QCheckBox    *m_fetchRemoteCb;
    QProgressBar *m_progress;
    QProgressBar *m_repoProgress;   // 当前仓库所处阶段的进度（带百分比）
    QLabel       *m_trafficLabel;   // 本轮拉取累计流量（k 与 M 两种单位）
    QLabel       *m_summaryLabel;
    QWidget      *m_updateNotesWnd = nullptr; // 更新说明窗口（WA_DeleteOnClose）

    QVector<RepoInfo> m_repos;  // 最近一次扫描的仓库列表（与表格行一一对应）
    GitManager *m_gitManager;   // Git 扫描与拉取功能类
    int         m_scanId = 0;   // 当前扫描代次，用于忽略过期的扫描回传结果
    bool        m_syncingSelectAll = false; // 批量勾选期间抑制单行状态回写
};

#endif // MAINWINDOW_H
