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

namespace {
constexpr int kWindowBarHeight = 42;
constexpr int kMacSystemButtonAreaWidth = 72;
constexpr int kMacLeadingButtonSize = 24;
constexpr int kMacLeadingIconSize = 17;

QString cssColor(const QColor &color) {
  return QStringLiteral("rgba(%1, %2, %3, %4)")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(color.alpha());
}

QIcon stencilIcon(const QString &iconPath, const QColor &color) {
  return QIcon(new TintableSvgIconEngine(iconPath, color));
}
}

WindowBar::WindowBar(QWidget *parent) : QWidget(parent) {
  setFixedHeight(kWindowBarHeight);
  setAutoFillBackground(true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  m_layout = new QHBoxLayout(this);
  m_layout->setContentsMargins(Margin::HCommon, 0, Margin::HCommon, 0);
  m_layout->setSpacing(8);

  m_centerPlaceholder = new QWidget(this);
  m_centerPlaceholder->setFixedSize(0, 0);
  m_centerPlaceholder->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_centerWidget = m_centerPlaceholder;

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  m_systemButtonArea = new QWidget(this);
  m_systemButtonArea->setFixedWidth(kMacSystemButtonAreaWidth);
  m_systemButtonArea->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

  m_leadingControls = new QWidget(this);
  auto *leadingLayout = new QHBoxLayout(m_leadingControls);
  leadingLayout->setContentsMargins(0, 0, 0, 0);
  leadingLayout->setSpacing(4);
  m_leadingControls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

  m_macTrailingSpacer = new QWidget(this);
  m_macTrailingSpacer->setFixedWidth(kMacSystemButtonAreaWidth);
  m_macTrailingSpacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

  m_layout->addWidget(m_systemButtonArea);
  m_layout->addWidget(m_leadingControls);
  m_layout->addStretch(1);
  m_layout->addWidget(m_centerWidget);
  m_layout->addStretch(1);
  m_layout->addWidget(m_macTrailingSpacer);
#else
  auto *buttonLayout = new QHBoxLayout();
  buttonLayout->setContentsMargins(0, 0, 0, 0);
  buttonLayout->setSpacing(4);

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

  m_layout->addStretch(1);
  m_layout->addWidget(m_centerWidget);
  m_layout->addStretch(1);
  m_layout->addLayout(buttonLayout);
#endif

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
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
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
    button->setFixedSize(kMacLeadingButtonSize, kMacLeadingButtonSize);
    button->setIconSize(QSize(kMacLeadingIconSize, kMacLeadingIconSize));
    applyLeadingButtonStyle(button);
    leadingLayout->addWidget(button);
    connect(spec.action, &QAction::changed, this, [this]() { refreshLeadingToggleButtonIcons(); });

    m_leadingToggleButtons.append({button, spec.action, spec.iconPath});
  }

  refreshLeadingToggleButtonIcons();
  updateMacTrailingSpacerWidth();
#else
  Q_UNUSED(buttons);
#endif
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

  QColor hoverFill = palette().color(QPalette::Text);
  hoverFill.setAlpha(24);
  QColor pressedFill = palette().color(QPalette::Text);
  pressedFill.setAlpha(40);
  QColor checkedFill = palette().color(QPalette::Text);
  checkedFill.setAlpha(32);

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
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  const QColor enabledColor = palette().color(QPalette::Text);
  const QColor disabledColor = palette().color(QPalette::Disabled, QPalette::Text);

  for (const auto &entry : m_leadingToggleButtons) {
    if (!entry.button) {
      continue;
    }
    entry.button->setIcon(stencilIcon(entry.iconPath, entry.button->isEnabled() ? enabledColor : disabledColor));
  }
#endif
}

void WindowBar::updateMacTrailingSpacerWidth() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  if (!m_macTrailingSpacer) {
    return;
  }

  int width = kMacSystemButtonAreaWidth;
  if (m_leadingControls) {
    width += m_leadingControls->sizeHint().width();
  }
  m_macTrailingSpacer->setFixedWidth(width);
#endif
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
