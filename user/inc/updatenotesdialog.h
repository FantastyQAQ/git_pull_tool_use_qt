/**
 * @file        updatenotesdialog.h
 * @brief       「更新说明」窗口：非模态显示 docs/README.zh_CN.md 中的「更新记录」章节
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.1
 * @date        20261106
 * @note        内容来源为编译进 Qt 资源的 docs/README.zh_CN.md，修改记录后需重新构建
 *
 * @copyright   Copyright (c) 2026 by OLE, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261106    <td>1.0       <td>lch       <td>创建
 * </table>
 */
#ifndef UPDATENOTESDIALOG_H
#define UPDATENOTESDIALOG_H

#include <QWidget>

class QTextBrowser;

// 「更新说明」窗口：非模态显示 README.md 中的「更新记录」章节。
// 以主窗口为父窗口，主窗口销毁时随之销毁；重复打开时由调用方前置已有窗口。
class UpdateNotesDialog : public QWidget {
    Q_OBJECT
  public:
    explicit UpdateNotesDialog(QWidget *parent = nullptr);
    ~UpdateNotesDialog();

  private:
    // 读取资源中的 README 并截取「更新记录」章节，返回原始 Markdown 文本。
    static QString loadUpdateNotes();
    // 把「更新记录」片段转换为 QTextBrowser 可显示的简单 HTML。
    static QString notesToHtml(const QString &markdown);

    QTextBrowser *m_pBrowser;
};

#endif // UPDATENOTESDIALOG_H
