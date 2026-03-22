/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <filesystem>
#include <string>
#include <QColor>
#include <QPalette>
#include <QString>

class QScrollArea;
class QWidget;
class QPixmap;
class QGraphicsEffect;
class VGMItem;

QScrollArea* getContainingScrollArea(const QWidget* widget);
void applyEffectToPixmap(QPixmap& src, QPixmap& tgt, QGraphicsEffect* effect, int extent = 0);
QString cssColor(const QColor &color);
QColor blendColors(const QColor &foreground, const QColor &background, qreal foregroundWeight);
bool isDarkPalette(const QPalette &palette);

std::filesystem::path openSaveDirDialog();
std::filesystem::path openSaveFileDialog(const std::filesystem::path& suggested_filename, const std::string& extension);
std::filesystem::path openFolderDialog(const std::filesystem::path& suggestedPath, std::string_view reason);
