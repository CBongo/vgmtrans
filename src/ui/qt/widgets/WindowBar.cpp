/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "WindowBar.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QShowEvent>
#include <QStyle>
#include <QToolButton>
#include "Metrics.h"
#include "TintableSvgIconEngine.h"
#include "UIHelpers.h"

namespace {
constexpr int kTitleBarHeight = 40;
constexpr int kMacSystemButtonAreaWidth = 72;
constexpr int kTitleBarToggleButtonWidth = 36;
constexpr int kTitleBarToggleButtonHeight = 31;
constexpr int kTitleBarToggleIconSize = 23;

QIcon stencilIcon(const QString &iconPath, const QColor &color) {
  return QIcon(new TintableSvgIconEngine(iconPath, color));
}
}

WindowBar::WindowBar(QWidget *parent) : QWidget(parent) {
  setFixedHeight(kTitleBarHeight);
  setAutoFillBackground(true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  m_layout = new QHBoxLayout(this);
  m_layout->setContentsMargins(Margin::HCommon, 0, Margin::HCommon, 0);
  m_layout->setSpacing(8);

  m_centerPlaceholder = new QWidget(this);
  m_centerPlaceholder->setFixedSize(0, 0);
  m_centerPlaceholder->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_centerWidget = m_centerPlaceholder;

  m_leftBalanceSpacer = new QWidget(this);
  m_leftBalanceSpacer->setFixedWidth(0);
  m_leftBalanceSpacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

  m_leadingControls = new QWidget(this);
  auto *leadingLayout = new QHBoxLayout(m_leadingControls);
  leadingLayout->setContentsMargins(0, 5, 0, 4);
  leadingLayout->setSpacing(4);
  m_leadingControls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

  m_rightBalanceSpacer = new QWidget(this);
  m_rightBalanceSpacer->setFixedWidth(0);
  m_rightBalanceSpacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

  m_layout->addWidget(m_leftBalanceSpacer);

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  m_systemButtonArea = new QWidget(this);
  m_systemButtonArea->setFixedWidth(kMacSystemButtonAreaWidth);
  m_systemButtonArea->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

  m_layout->addWidget(m_systemButtonArea);
  m_layout->addWidget(m_leadingControls);
  m_layout->addStretch(1);
  m_layout->addWidget(m_centerWidget);
  m_layout->addStretch(1);
#else
  m_layout->addWidget(m_leadingControls);
  m_layout->addStretch(1);
  m_layout->addWidget(m_centerWidget);
  m_layout->addStretch(1);

  m_rightControls = new QWidget(this);
  auto *buttonLayout = new QHBoxLayout(m_rightControls);
  buttonLayout->setContentsMargins(0, 0, 0, 0);
  buttonLayout->setSpacing(4);
  m_rightControls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

  m_minimizeButton = createWindowButton("Minimize window");
  m_maximizeButton = createWindowButton("Maximize window");
  m_closeButton = createWindowButton("Close window");

  connect(m_minimizeButton, &QToolButton::clicked, this, [this]() {
    if (QWidget *topLevelWindow = window()) {
      topLevelWindow->showMinimized();
    }
  });
  connect(m_maximizeButton, &QToolButton::clicked, this, [this]() {
    if (QWidget *topLevelWindow = window()) {
      topLevelWindow->isMaximized() ? topLevelWindow->showNormal() : topLevelWindow->showMaximized();
    }
  });
  connect(m_closeButton, &QToolButton::clicked, this, [this]() {
    if (QWidget *topLevelWindow = window()) {
      topLevelWindow->close();
    }
  });

  buttonLayout->addWidget(m_minimizeButton);
  buttonLayout->addWidget(m_maximizeButton);
  buttonLayout->addWidget(m_closeButton);

  m_layout->addWidget(m_rightControls);
#endif

  m_layout->addWidget(m_rightBalanceSpacer);

  updateBalanceSpacers();
  syncWindowButtons();
}

QWidget *WindowBar::centerWidget() const {
  return m_centerWidget == m_centerPlaceholder ? nullptr : m_centerWidget;
}

void WindowBar::setCenterWidget(QWidget *widget) {
  QWidget *replacement = widget ? widget : m_centerPlaceholder;
  if (replacement == m_centerWidget) {
    return;
  }

  m_layout->replaceWidget(m_centerWidget, replacement);
  if (m_centerWidget != m_centerPlaceholder) {
    m_centerWidget->setParent(nullptr);
  }

  m_centerWidget = replacement;
  if (widget) {
    widget->setParent(this);
    widget->show();
  }
}

void WindowBar::setLeadingToggleButtons(const QList<ToggleButtonSpec> &buttons) {
  if (!m_leadingControls) {
    return;
  }

  auto *leadingLayout = qobject_cast<QHBoxLayout *>(m_leadingControls->layout());
  if (!leadingLayout) {
    return;
  }

  while (QLayoutItem *item = leadingLayout->takeAt(0)) {
    if (QWidget *widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }
  m_leadingToggleButtons.clear();

  for (const auto &spec : buttons) {
    if (!spec.action) {
      continue;
    }

    auto *button = new QToolButton(m_leadingControls);
    button->setDefaultAction(spec.action);
    button->setAutoRaise(true);
    button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::ArrowCursor);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setFixedSize(kTitleBarToggleButtonWidth, kTitleBarToggleButtonHeight);
    button->setIconSize(QSize(kTitleBarToggleIconSize, kTitleBarToggleIconSize));
    applyLeadingButtonStyle(button);
    leadingLayout->addWidget(button);
    connect(spec.action, &QAction::changed, this, [this]() { refreshLeadingToggleButtonIcons(); });

    m_leadingToggleButtons.append({button, spec.action, spec.iconPath});
  }

  refreshLeadingToggleButtonIcons();
  updateBalanceSpacers();
}

QWidget *WindowBar::leadingControls() const {
  return m_leadingControls;
}

QWidget *WindowBar::systemButtonArea() const {
  return m_systemButtonArea;
}

QAbstractButton *WindowBar::minimizeButton() const {
  return m_minimizeButton;
}

QAbstractButton *WindowBar::maximizeButton() const {
  return m_maximizeButton;
}

QAbstractButton *WindowBar::closeButton() const {
  return m_closeButton;
}

void WindowBar::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);

  if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
    refreshLeadingToggleButtonIcons();
    for (const auto &entry : m_leadingToggleButtons) {
      if (entry.button) {
        applyLeadingButtonStyle(entry.button);
      }
    }
  }
}

bool WindowBar::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_trackedWindow &&
      (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show)) {
    syncWindowButtons();
  }
  return QWidget::eventFilter(watched, event);
}

void WindowBar::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  attachToTopLevelWindow();
  syncWindowButtons();
}

void WindowBar::attachToTopLevelWindow() {
  QWidget *topLevelWindow = window();
  if (m_trackedWindow == topLevelWindow) {
    return;
  }

  if (m_trackedWindow) {
    m_trackedWindow->removeEventFilter(this);
  }

  m_trackedWindow = topLevelWindow;
  if (m_trackedWindow) {
    m_trackedWindow->installEventFilter(this);
  }
}

void WindowBar::applyLeadingButtonStyle(QToolButton *button) const {
  if (!button) {
    return;
  }

  const bool darkPalette = isDarkPalette(palette());
  QColor hoverFill = palette().color(QPalette::Text);
  hoverFill.setAlpha(darkPalette ? 18 : 12);
  QColor pressedFill = palette().color(QPalette::Text);
  pressedFill.setAlpha(darkPalette ? 28 : 20);
  QColor checkedFill = palette().color(QPalette::Text);
  checkedFill.setAlpha(darkPalette ? 24 : 16);

  button->setStyleSheet(QStringLiteral(
      "QToolButton {"
      " border: none;"
      " background: transparent;"
      " border-radius: 6px;"
      " padding: 0px;"
      " margin: 0px;"
      "}"
      "QToolButton:hover { background: %1; }"
      "QToolButton:pressed { background: %2; }"
      "QToolButton:checked { background: %3; }")
                            .arg(cssColor(hoverFill))
                            .arg(cssColor(pressedFill))
                            .arg(cssColor(checkedFill)));
}

QToolButton *WindowBar::createWindowButton(const QString& toolTip) {
  auto *button = new QToolButton(this);
  button->setAutoRaise(true);
  button->setFocusPolicy(Qt::NoFocus);
  button->setToolTip(toolTip);
  button->setCursor(Qt::ArrowCursor);

  const int buttonSize = style()->pixelMetric(QStyle::PM_TitleBarButtonSize, nullptr, this);
  const int iconSize = style()->pixelMetric(QStyle::PM_TitleBarButtonIconSize, nullptr, this);
  if (buttonSize > 0) {
    button->setFixedSize(buttonSize, buttonSize);
  }
  if (iconSize > 0) {
    button->setIconSize(QSize(iconSize, iconSize));
  }

  return button;
}

void WindowBar::refreshLeadingToggleButtonIcons() {
  const QColor windowColor = palette().color(QPalette::Window);
  const bool darkPalette = isDarkPalette(palette());
  const QColor enabledColor =
      blendColors(palette().color(QPalette::Text), windowColor, darkPalette ? 0.72 : 0.56);
  const QColor disabledColor =
      blendColors(palette().color(QPalette::Disabled, QPalette::Text), windowColor, darkPalette ? 0.6 : 0.46);

  for (const auto &entry : m_leadingToggleButtons) {
    if (!entry.button) {
      continue;
    }
    entry.button->setIcon(stencilIcon(entry.iconPath, entry.button->isEnabled() ? enabledColor : disabledColor));
  }
}

void WindowBar::updateBalanceSpacers() {
  if (!m_leftBalanceSpacer || !m_rightBalanceSpacer) {
    return;
  }

  int leftWidth = 0;
  if (m_leadingControls) {
    leftWidth += m_leadingControls->sizeHint().width();
  }

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  if (m_systemButtonArea) {
    leftWidth += m_systemButtonArea->sizeHint().width();
  }
#endif

  int rightWidth = 0;
#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
  if (m_rightControls) {
    rightWidth += m_rightControls->sizeHint().width();
  }
#endif

  if (leftWidth > rightWidth) {
    m_leftBalanceSpacer->setFixedWidth(0);
    m_rightBalanceSpacer->setFixedWidth(leftWidth - rightWidth);
  } else {
    m_leftBalanceSpacer->setFixedWidth(rightWidth - leftWidth);
    m_rightBalanceSpacer->setFixedWidth(0);
  }
}

void WindowBar::syncWindowButtons() {
#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
  const bool maximized = window() && window()->isMaximized();
  if (m_minimizeButton) {
    m_minimizeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMinButton, nullptr, this));
  }
  if (m_maximizeButton) {
    m_maximizeButton->setIcon(style()->standardIcon(
        maximized ? QStyle::SP_TitleBarNormalButton : QStyle::SP_TitleBarMaxButton, nullptr, this));
    m_maximizeButton->setToolTip(maximized ? "Restore window" : "Maximize window");
  }
  if (m_closeButton) {
    m_closeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton, nullptr, this));
  }
#endif
}
