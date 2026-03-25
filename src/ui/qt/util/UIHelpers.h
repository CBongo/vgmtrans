/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <filesystem>
#include <string>
#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QString>

class QScrollArea;
class QAbstractScrollArea;
class QWidget;
class QPixmap;
class QGraphicsEffect;
class VGMItem;

QScrollArea* getContainingScrollArea(const QWidget* widget);
int horizontalScrollBarReservedHeight(const QAbstractScrollArea* area);
void applyEffectToPixmap(QPixmap& src, QPixmap& tgt, QGraphicsEffect* effect, int extent = 0);
QIcon stencilSvgIcon(const QString &iconPath, const QColor &color);
QIcon gradientStencilSvgIcon(const QString &iconPath, const QColor &startColor, const QColor &endColor,
                             int angleDegrees = 90);
QString cssColor(const QColor &color);
QColor blendColors(const QColor &foreground, const QColor &background, qreal foregroundWeight);
bool isDarkPalette(const QPalette &palette);
QString toolBarButtonStyle(const QPalette &palette, bool checkable = false);
QColor toolBarButtonIconColor(const QPalette &palette, bool enabled = true);

std::filesystem::path openSaveDirDialog();
std::filesystem::path openSaveFileDialog(const std::filesystem::path& suggested_filename, const std::string& extension);
std::filesystem::path openFolderDialog(const std::filesystem::path& suggestedPath, std::string_view reason);
