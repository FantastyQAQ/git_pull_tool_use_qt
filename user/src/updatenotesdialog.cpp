/**
 * @file        updatenotesdialog.cpp
 * @brief       更新说明窗口：非模态展示更新记录
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.1
 * @date        20261106
 * @note        内容来源为编译进 Qt 资源的 docs/README.zh_CN.md，修改记录后需重新构建；
 *              参考欧力小工具（protocol-analy）同名实现，配色改用本仓固定色值
 *
 * @copyright   Copyright (c) 2026 by OLE, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261106    <td>1.0       <td>lch       <td>创建
 * <tr><td>20261106    <td>1.1       <td>lch       <td>记录源改读 docs/README.zh_CN.md
 * </table>
 */
#include "updatenotesdialog.h"

#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringList>
#include <QTextBrowser>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {

// 更新记录展示用固定色值（本工程无 uitheme 体系，直接取主界面配色近似值）
const char *kTitleColor = "#1a56a0"; // 章节标题色
const char *kKeyColor   = "#2b6cb0"; // 行内代码色

/**
 * @brief       把反引号包裹的行内代码转为等宽着色片段
 *
 * @param[in]   {escapedLine} const QString& 已做 HTML 转义的文本
 * @return      转换后的 HTML 文本
 * @details     入参必须已转义，避免 README 内容中的尖括号被当作标签解析
 * @doxygen_sig_fp 9ba48dd206a0e160
 */
QString inlineCodeHtml(const QString &escapedLine) {
    static const QRegularExpression reCode(QStringLiteral("`([^`]*)`"));

    // 替换串中的 \\1 表示正则第一个捕获组（反引号里的内容）。
    const QString codeOpen = QStringLiteral("<span style=\"font-family:Consolas,'Courier New',monospace; color:")
                             + QString::fromLatin1(kKeyColor)
                             + QStringLiteral(";\">");
    const QString replacement = codeOpen + QStringLiteral("\\1</span>");

    QString result = escapedLine;
    result.replace(reCode, replacement);
    return result;
}

} // namespace

/**
 * @brief       构造更新说明窗口并加载 README 更新记录
 *
 * @param[in]   {parent} QWidget* 父窗口（主窗口）
 * @return      none（构造/析构函数，无返回值）
 * @details     非模态独立窗口，随父窗口销毁；读取失败时显示提示文本
 * @doxygen_sig_fp e2b3e5e74db5f0fc
 */
UpdateNotesDialog::UpdateNotesDialog(QWidget *parent)
    : QWidget(parent, Qt::Window), m_pBrowser(nullptr) {
    setWindowTitle(tr("更新说明"));

    QVBoxLayout *pLay = new QVBoxLayout(this);
    pLay->setSpacing(12);
    pLay->setContentsMargins(24, 20, 24, 20);

    // 顶部标题：与「关于」对话框的大标题保持一致的观感
    QLabel *pTitle = new QLabel(tr("更新记录"), this);
    pTitle->setAlignment(Qt::AlignCenter);
    QFont fTitle = pTitle->font();
    fTitle.setPointSize(16);
    fTitle.setBold(true);
    pTitle->setFont(fTitle);
    pLay->addWidget(pTitle);

    // 正文：只读浏览器，占据窗口剩余空间，可滚动查看全部更新条目
    m_pBrowser = new QTextBrowser(this);
    const QString qsNotes = loadUpdateNotes();
    if (qsNotes.isEmpty()) {
        m_pBrowser->setPlainText(tr("未找到更新记录"));
    } else {
        m_pBrowser->setHtml(notesToHtml(qsNotes));
        // setHtml 后光标可能停在文末，导致首次打开就滚到底部；显式回到开头
        m_pBrowser->moveCursor(QTextCursor::Start);
    }
    pLay->addWidget(m_pBrowser, 1);

    // 底部「关闭」按钮：靠右放置
    QPushButton *pBtClose = new QPushButton(tr("关闭"), this);
    pBtClose->setMinimumWidth(110);
    pBtClose->setMinimumHeight(34);
    connect(pBtClose, SIGNAL(clicked()), this, SLOT(close()));

    QHBoxLayout *pBtnLay = new QHBoxLayout();
    pBtnLay->addStretch(1);
    pBtnLay->addWidget(pBtClose);
    pLay->addLayout(pBtnLay);

    resize(640, 600);
    setMinimumSize(520, 400);
}

/**
 * @brief       析构函数，界面控件由 Qt 父子关系自动释放
 *
 * @return      none（构造/析构函数，无返回值）
 * @doxygen_sig_fp 93d42a5fba2838d7
 */
UpdateNotesDialog::~UpdateNotesDialog() {}

/**
 * @brief       读取资源中的 README 并截取更新记录章节
 *
 * @return      更新记录 Markdown 原文，未找到时为空串
 * @details     从「# 更新记录」标题行之后截取到文件末尾
 * @doxygen_sig_fp e2c10efa72eb87d0
 */
QString UpdateNotesDialog::loadUpdateNotes() {
    // 完整更新记录在 docs/README.zh_CN.md（根目录 README.md 只放索引）
    QFile file(QStringLiteral(":/docs/README.zh_CN.md"));
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    // README 源文件统一为 UTF-8，直接按 UTF-8 解码
    const QString qsText = QString::fromUtf8(file.readAll());
    file.close();

    // 逐行查找章节标题；docs/README.zh_CN.md 中该标题写作「# 更新记录」
    const QStringList lines = qsText.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).trimmed().startsWith(QStringLiteral("# 更新记录"))) {
            // 标题行本身由窗口顶部标题承担，这里只取后续条目
            if (i + 1 >= lines.size())
                return QString();
            return lines.mid(i + 1).join(QLatin1Char('\n'));
        }
    }

    return QString();
}

/**
 * @brief       把更新记录 Markdown 片段转为简单 HTML
 *
 * @param[in]   {markdown} const QString& 更新记录原始 Markdown 文本
 * @return      QTextBrowser 可显示的 HTML 文本
 * @details     仅处理标题行、编号条目和反引号行内代码，其余内容按普通段落原样输出
 * @doxygen_sig_fp a627d8f22c1b7bee
 */
QString UpdateNotesDialog::notesToHtml(const QString &markdown) {
    // 编号条目：以「数字.」开头的行，例如「01. 添加 : ...」
    static const QRegularExpression reItem(QStringLiteral("^\\d+\\."));

    QString html;
    // 顶部标题由窗口标签显示，正文不再重复 README 的「# 更新记录」标题行
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue; // 空行跳过，段落间距由标题标签自身的边距提供

        if (line.startsWith(QLatin1Char('#'))) {
            // 标题行：去掉行首所有 # 与其后空白，统一按二级标题渲染
            QString text = line;
            while (text.startsWith(QLatin1Char('#')))
                text.remove(0, 1);
            text = text.trimmed();
            html += QStringLiteral("<h4 style=\"color:") + QString::fromLatin1(kTitleColor)
                    + QStringLiteral(";\">") + inlineCodeHtml(text.toHtmlEscaped())
                    + QStringLiteral("</h4>");
            continue;
        }

        // 普通行与编号条目都保留原文，编号条目额外左缩进以便区分层级
        const QString body = inlineCodeHtml(line.toHtmlEscaped());
        if (reItem.match(line).hasMatch()) {
            html += QStringLiteral("<p style=\"margin-left:18px;\">") + body
                    + QStringLiteral("</p>");
        } else {
            html += QStringLiteral("<p>") + body + QStringLiteral("</p>");
        }
    }

    return html;
}
