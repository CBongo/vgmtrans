/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include <QWidget>
#include "Metrics.h"

class TitleBar : public QWidget {
  Q_OBJECT

public:
  enum Button {
    NoButtons = 0x0,
    HideButton = 0x1,
    NewButton = 0x2,
  };
  Q_DECLARE_FLAGS(Buttons, Button)

  explicit TitleBar(const QString& title, Buttons buttons = NoButtons, QWidget *parent = nullptr);
  ~TitleBar() override = default;

  QSize sizeHint() const override {
    return QSize(200, Size::VTab);
  }
  QSize minimumSizeHint() const override {
    return QSize(0, Size::VTab);
  }

signals:
  void hideRequested();
  void addRequested();
};

Q_DECLARE_OPERATORS_FOR_FLAGS(TitleBar::Buttons)
