/**
 * @file        main.cpp
 * @brief       程序入口：创建应用与主窗口
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.1
 * @date        20261108
 *
 * @copyright   Copyright (c) 2026 by OLE, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261106    <td>1.0       <td>lch       <td>创建
 * <tr><td>20261108    <td>1.1       <td>lch       <td>使用 Fusion 基础风格统一浅色界面外观
 * </table>
 */
#include <QApplication>
#include "mainwindow.h"

/**
 * @brief       程序入口：创建应用与主窗口并进入事件循环
 *
 * @param[in]   {argc} int 命令行参数个数
 * @param[in]   {argv} char*[] 命令行参数数组
 * @return      应用事件循环退出码
 * @retval      none
 * @details     none
 * @doxygen_sig_fp f4b9f73e14950a17
 */
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    // 统一基础控件外观，避免不同 Windows 主题影响浅色界面样式
    a.setStyle(QStringLiteral("Fusion"));
    MainWindow w;
    w.show();
    return a.exec();
}