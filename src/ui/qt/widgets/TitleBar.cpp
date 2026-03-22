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

  if (buttons.testFlag(AddButton)) {
    auto *addButton = makeButton("Add collection");
    addButton->setText("Add");
    connect(addButton, &QToolButton::clicked, this, &TitleBar::addRequested);
  }

  if (buttons.testFlag(CollapseButton)) {
    m_collapseButton = makeButton(QString());
    m_collapseButton->setCheckable(true);
    updateCollapseButton();
    connect(m_collapseButton, &QToolButton::toggled, this, [this](bool collapsed) {
      m_collapsed = collapsed;
      updateCollapseButton();
      emit collapseToggled(collapsed);
    });
  }

  if (buttons.testFlag(HideButton)) {
    auto *hideButton = makeButton("Hide dock");
    hideButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    connect(hideButton, &QToolButton::clicked, this, &TitleBar::hideRequested);
  }
}

void TitleBar::updateCollapseButton() {
  if (!m_collapseButton) {
    return;
  }

  m_collapseButton->setIcon(style()->standardIcon(
      m_collapsed ? QStyle::SP_TitleBarUnshadeButton : QStyle::SP_TitleBarShadeButton));
  m_collapseButton->setToolTip(m_collapsed ? "Expand dock" : "Collapse dock");
}
