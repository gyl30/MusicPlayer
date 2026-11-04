#ifndef THEMED_ICON_H
#define THEMED_ICON_H

#include <QIcon>
#include <QString>

// 随主题变色的 SVG 图标：每次绘制时读取 qApp 的 darkMode 属性，
// 暗色主题下把为亮色背景设计的深色换成浅色，切换主题后控件重绘即生效。
QIcon themed_icon(const QString& resource);

#endif
