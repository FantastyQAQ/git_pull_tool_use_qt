/**
 * @file        aboutdialog.h
 * @brief       「关于」对话框：展示工具名、版本、编译时间、Qt 版本、作者与邮箱
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.0
 * @date        20261106
 * @note        所有展示信息均取自 appinfo.h 中的宏或 Qt 运行时接口，方便统一修改
 *
 * @copyright   Copyright (c) 2026 by OLE, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261106    <td>1.0       <td>lch       <td>创建
 * </table>
 */
#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>

// 「关于」对话框：展示工具名、版本、编译时间、Qt 版本、作者与邮箱。
// 所有展示信息均取自 appinfo.h 中的宏或 Qt 运行时接口，方便统一修改。
class AboutDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent = nullptr);
    ~AboutDialog();
};

#endif // ABOUTDIALOG_H
