/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "TitleBar.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QStyle>
#include <QToolButton>
#include "Metrics.h"

TitleBar::TitleBar(const QString& title, Buttons buttons, QWidget *parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  QHBoxLayout *titleLayout = new QHBoxLayout(this);
  titleLayout->setContentsMargins(Margin::HCommon, 5, Margin::HCommon, 5);
  titleLayout->setSpacing(4);
  QLabel *titleLabel = new QLabel(title);
  titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
  titleLayout->addWidget(titleLabel);
  titleLayout->addStretch(1);

  QFont labelFont("Arial", -1, QFont::Bold, true);
  titleLabel->setFont(labelFont);

  const auto makeButton = [this, titleLayout](const QString& toolTip) {
    auto *button = new QToolButton(this);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(toolTip);
    button->setCursor(Qt::ArrowCursor);
    titleLayout->addWidget(button);
    return button;
  };

  if (buttons.testFlag(NewButton)) {
    auto *newButton = makeButton("New collection");
    newButton->setText("New");
    connect(newButton, &QToolButton::clicked, this, &TitleBar::addRequested);
  }

  if (buttons.testFlag(HideButton)) {
    auto *hideButton = makeButton("Hide dock");
    hideButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    connect(hideButton, &QToolButton::clicked, this, &TitleBar::hideRequested);
  }
}
