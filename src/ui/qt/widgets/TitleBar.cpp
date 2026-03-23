/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "TitleBar.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QToolButton>
#include "Metrics.h"
#include "UIHelpers.h"

namespace {
constexpr int kTitleBarButtonWidth = 22;
constexpr int kTitleBarButtonHeight = 20;
constexpr int kTitleBarIconSize = 16;
}

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
    m_hideButton = makeButton("Hide");
    m_hideButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_hideButton->setFixedSize(kTitleBarButtonWidth, kTitleBarButtonHeight);
    m_hideButton->setIconSize(QSize(kTitleBarIconSize, kTitleBarIconSize));
    updateHideButtonStyle();
    connect(m_hideButton, &QToolButton::clicked, this, &TitleBar::hideRequested);
  }
}

void TitleBar::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);

  if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
    updateHideButtonStyle();
  }
}

void TitleBar::updateHideButtonStyle() {
  if (!m_hideButton) {
    return;
  }

  m_hideButton->setStyleSheet(toolBarButtonStyle(palette()));
  m_hideButton->setIcon(stencilSvgIcon(QStringLiteral(":/icons/minus.svg"), toolBarButtonIconColor(palette())));
}
