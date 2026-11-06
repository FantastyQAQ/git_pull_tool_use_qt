/**
 * @file        main.cpp
 * @brief       程序入口：创建应用与主窗口
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.0
 * @date        20261106
 *
 * @copyright   Copyright (c) 2026 by OLE, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261106    <td>1.0       <td>lch       <td>创建
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
    MainWindow w;
    w.show();
    return a.exec();
}