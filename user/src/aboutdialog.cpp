/**
 * @file        aboutdialog.cpp
 * @brief       「关于」对话框实现：大标题 + 编译时间/Qt 版本/作者/邮箱信息列表
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.0
 * @date        20261106
 * @note        参考欧力小工具（protocol-analy）同名实现，信息源为 appinfo.h 宏与 Qt 运行时接口
 *
 * @copyright   Copyright (c) 2026 by OLE, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261106    <td>1.0       <td>lch       <td>创建
 * </table>
 */
#include "aboutdialog.h"

#include "appinfo.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtGlobal>

/**
 * @brief       构造「关于」对话框并填充信息
 *
 * @param[out]  {parent} QWidget* 父窗口（主窗口）
 * @return      none（构造/析构函数，无返回值）
 * @retval      none
 * @details     大标题取 APP_FULL_TITLE；编译时间取预定义宏 __DATE__/__TIME__，
 *              Qt 版本取 qVersion()，作者与邮箱取 appinfo.h 宏
 * @doxygen_sig_fp eb29c6acf32f318f
 */
AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("关于"));
    // 去掉对话框标题栏上的问号按钮（Windows 默认会加，观感突兀）
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    QVBoxLayout *pLay = new QVBoxLayout(this);
    pLay->setSpacing(12);
    pLay->setContentsMargins(32, 28, 32, 28);

    // 工具名 + 版本号（大标题）
    QLabel *pTitle = new QLabel(QString::fromUtf8(APP_FULL_TITLE), this);
    pTitle->setAlignment(Qt::AlignCenter);
    QFont fTitle = pTitle->font();
    fTitle.setPointSize(16);
    fTitle.setBold(true);
    pTitle->setFont(fTitle);
    pLay->addWidget(pTitle);
    pLay->addSpacing(8);

    // 信息列表：编译时间 / Qt 版本 / 作者 / 邮箱
    // 编译时间取自预定义宏 __DATE__ / __TIME__，Qt 版本取自 qVersion()。
    const QString qsBuildTime =
        QString::fromLatin1(__DATE__) + " " + QString::fromLatin1(__TIME__);

    QLabel *pBuild = new QLabel(tr("编译时间：") + qsBuildTime, this);
    QLabel *pQt =
        new QLabel(tr("Qt 版本：") + QString::fromLatin1(qVersion()), this);
    QLabel *pAuthor =
        new QLabel(tr("作    者：") + QString::fromUtf8(APP_AUTHOR_NAME), this);
    QLabel *pMail =
        new QLabel(tr("作者邮箱：") + QString::fromUtf8(APP_AUTHOR_MAIL), this);

    pBuild->setAlignment(Qt::AlignCenter);
    pQt->setAlignment(Qt::AlignCenter);
    pAuthor->setAlignment(Qt::AlignCenter);
    pMail->setAlignment(Qt::AlignCenter);

    pLay->addWidget(pBuild);
    pLay->addWidget(pQt);
    pLay->addWidget(pAuthor);
    pLay->addWidget(pMail);
    pLay->addSpacing(12);

    // 确定按钮（居中）
    QPushButton *pBtnOk = new QPushButton(tr("确定"), this);
    pBtnOk->setMinimumWidth(120);
    pBtnOk->setMinimumHeight(36);
    connect(pBtnOk, SIGNAL(clicked()), this, SLOT(accept()));

    QHBoxLayout *pBtnLay = new QHBoxLayout();
    pBtnLay->addStretch(1);
    pBtnLay->addWidget(pBtnOk);
    pBtnLay->addStretch(1);
    pLay->addLayout(pBtnLay);

    resize(360, 260);
}

/**
 * @brief       析构函数，界面控件由 Qt 父子关系自动释放
 *
 * @return      none（构造/析构函数，无返回值）
 * @retval      none
 * @details     none
 * @doxygen_sig_fp 83c2f035179f8598
 */
AboutDialog::~AboutDialog() {}
