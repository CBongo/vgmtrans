/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "WindowBar.h"

#include <cmath>
#include <limits>
#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyle>
#include <QToolButton>
#include "Metrics.h"
#include "UIHelpers.h"

namespace {
constexpr int kTitleBarHeight = 40;
constexpr int kMacSystemButtonAreaWidth = 58;
constexpr int kMacWindowBarLeftMargin = 14;
constexpr int kMacWindowBarRightMargin = 10;
constexpr int kTitleBarToggleButtonWidth = 30;
constexpr int kTitleBarToggleButtonHeight = 25;
constexpr int kTitleBarToggleIconSize = 19;
constexpr int kWindowsWindowIconButtonWidth = 36;
constexpr int kWindowsWindowButtonWidth = 46;
constexpr int kWindowsWindowIconSize = 18;
constexpr int kWindowsWindowGlyphSize = 12;
constexpr qreal kIconBarFreeWidthFraction = 0.6;
constexpr qreal kFreeWidthThreshold = 0.25;
constexpr int kFreeWidthToggleMargin = 24;

QIcon multiStateStencilIcon(const QString &iconPath, const QColor &normalColor,
                            const QColor &activeColor, const QColor &disabledColor,
                            const QSize &size) {
  QIcon icon;

  const auto addMode = [&](QIcon::Mode mode, const QColor &color) {
    const QIcon tintedIcon = stencilSvgIcon(iconPath, color);
    icon.addPixmap(tintedIcon.pixmap(size), mode, QIcon::Off);
  };

  addMode(QIcon::Normal, normalColor);
  addMode(QIcon::Active, activeColor);
  addMode(QIcon::Disabled, disabledColor);
  return icon;
}
}

WindowBar::WindowBar(QWidget *parent) : QWidget(parent) {
  setFixedHeight(kTitleBarHeight);
  setAutoFillBackground(true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  m_layout = new QHBoxLayout(this);
  m_layout->setContentsMargins(
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
      kMacWindowBarLeftMargin, 0, kMacWindowBarRightMargin, 0
#else
      0, 0, 0, 0
#endif
  );
  m_layout->setSpacing(
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
      8
#else
      6
#endif
  );

  m_menuBarPlaceholder = new QWidget(this);
  m_menuBarPlaceholder->setFixedSize(0, 0);
  m_menuBarPlaceholder->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_menuBarWidget = m_menuBarPlaceholder;

  m_centerPlaceholder = new QWidget(this);
  m_centerPlaceholder->setFixedSize(0, 0);
  m_centerPlaceholder->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_centerWidget = m_centerPlaceholder;
  m_leftCenterSpacer = new QWidget(this);
  m_leftCenterSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_rightCenterSpacer = new QWidget(this);
  m_rightCenterSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  m_leadingControls = new QWidget(this);
  auto *leadingLayout = new QHBoxLayout(m_leadingControls);
  leadingLayout->setContentsMargins(0, 8, 0, 7);
  leadingLayout->setSpacing(4);
  m_leadingControls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  m_systemButtonArea = new QWidget(this);
  m_systemButtonArea->setFixedSize(kMacSystemButtonAreaWidth, kTitleBarHeight - 4);
  m_layout->addWidget(m_systemButtonArea, 0, Qt::AlignBottom);
  m_layout->addWidget(m_leftCenterSpacer);
  m_layout->addWidget(m_centerWidget, 0, Qt::AlignVCenter);
  m_layout->addWidget(m_rightCenterSpacer);
  m_layout->addWidget(m_leadingControls, 0, Qt::AlignVCenter);
#else
#if defined(Q_OS_WIN)
  m_windowIconButton = createWindowButton(QString());
  m_windowIconButton->setObjectName(QStringLiteral("windowIconButton"));
  applyWindowButtonStyle(m_windowIconButton, false, true);
  m_layout->addWidget(m_windowIconButton, 0, Qt::AlignVCenter);
#endif
  m_layout->addWidget(m_menuBarWidget, 0, Qt::AlignVCenter);
  m_layout->addWidget(m_leftCenterSpacer);
  m_layout->addWidget(m_centerWidget, 0, Qt::AlignVCenter);
  m_layout->addWidget(m_rightCenterSpacer);
  m_layout->addWidget(m_leadingControls, 0, Qt::AlignVCenter);
  m_layout->addSpacing(8);

  m_rightControls = new QWidget(this);
  auto *buttonLayout = new QHBoxLayout(m_rightControls);
  buttonLayout->setContentsMargins(0, 0, 0, 0);
#if defined(Q_OS_WIN)
  buttonLayout->setSpacing(0);
#else
  buttonLayout->setSpacing(4);
#endif
  m_rightControls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

  m_minimizeButton = createWindowButton("Minimize window");
  m_maximizeButton = createWindowButton("Maximize window");
  m_closeButton = createWindowButton("Close window");

#if defined(Q_OS_WIN)
  m_minimizeButton->setObjectName(QStringLiteral("minimizeButton"));
  m_maximizeButton->setObjectName(QStringLiteral("maximizeButton"));
  m_closeButton->setObjectName(QStringLiteral("closeButton"));
  applyWindowButtonStyle(m_minimizeButton);
  applyWindowButtonStyle(m_maximizeButton);
  applyWindowButtonStyle(m_closeButton, true);
#endif

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

  m_layout->addWidget(m_rightControls, 0, Qt::AlignRight | Qt::AlignVCenter);
#endif

  syncWindowButtons();
  updateResponsiveLayout();
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
    widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    widget->show();
  }
  updateResponsiveLayout();
}

QWidget *WindowBar::menuBarWidget() const {
  return m_menuBarWidget == m_menuBarPlaceholder ? nullptr : m_menuBarWidget;
}

void WindowBar::setMenuBarWidget(QWidget *widget) {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  Q_UNUSED(widget);
  return;
#else
  QWidget *replacement = widget ? widget : m_menuBarPlaceholder;
  if (replacement == m_menuBarWidget) {
    return;
  }

  m_layout->replaceWidget(m_menuBarWidget, replacement);
  if (m_menuBarWidget != m_menuBarPlaceholder) {
    m_menuBarWidget->setParent(nullptr);
  }

  m_menuBarWidget = replacement;
  if (widget) {
    widget->setParent(this);
    widget->setContentsMargins(0, 0, 0, 0);
    widget->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
#if defined(Q_OS_WIN)
    QFont menuFont = widget->font();
    if (menuFont.pointSizeF() > 0) {
      menuFont.setPointSizeF(menuFont.pointSizeF() + 1.0);
    } else if (menuFont.pixelSize() > 0) {
      menuFont.setPixelSize(menuFont.pixelSize() + 2);
    }
    widget->setFont(menuFont);
    if (auto *menuBar = qobject_cast<QMenuBar *>(widget)) {
      for (QAction *action : menuBar->actions()) {
        if (QMenu *menu = action ? action->menu() : nullptr) {
          menu->setFont(menuFont);
        }
      }
    }
    widget->setStyleSheet(QStringLiteral(
        "QMenuBar { background: transparent; border: none; }"
        "QMenuBar::item { padding: 3px 8px; margin: 0px; background: transparent; }"
        "QMenu { padding: 4px 0px; }"
        "QMenu::item { padding: 6px 12px; margin: 1px 4px; }"));
#else
    widget->setStyleSheet(QStringLiteral("QMenuBar { background: transparent; border: none; }"));
#endif
    widget->show();
  }
  updateResponsiveLayout();
#endif
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

    m_leadingToggleButtons.append({button, spec.iconPath});
  }

  refreshLeadingToggleButtonIcons();
  updateResponsiveLayout();
}

QWidget *WindowBar::leadingControls() const {
  return m_leadingControls;
}

QAbstractButton *WindowBar::windowIconButton() const {
  return m_windowIconButton;
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
    applyWindowButtonStyle(m_windowIconButton, false, true);
    applyWindowButtonStyle(m_minimizeButton);
    applyWindowButtonStyle(m_maximizeButton);
    applyWindowButtonStyle(m_closeButton, true);
    syncWindowButtons();
  }
}

bool WindowBar::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_trackedWindow &&
      (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show ||
       event->type() == QEvent::WindowIconChange)) {
    syncWindowButtons();
  }
  return QWidget::eventFilter(watched, event);
}

void WindowBar::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  attachToTopLevelWindow();
  syncWindowButtons();
  updateResponsiveLayout();
}

void WindowBar::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  updateResponsiveLayout();
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

void WindowBar::updateResponsiveLayout() {
  if (!m_layout || !m_centerWidget || m_centerWidget == m_centerPlaceholder) {
    return;
  }

  const auto visibleWidth = [](QWidget *widget) {
    return widget && !widget->isHidden() ? widget->sizeHint().width() : 0;
  };

  const QMargins margins = m_layout->contentsMargins();
  const int centerMinimumWidth = std::max(0, m_centerWidget->minimumSizeHint().width());
  const int leadingWidth = m_leadingToggleButtons.isEmpty() ? 0 : m_leadingControls->sizeHint().width();
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  const int fixedWidth = margins.left() + margins.right() + visibleWidth(m_systemButtonArea);
#else
  const int fixedWidth =
      margins.left() + margins.right() + visibleWidth(m_windowIconButton) + visibleWidth(m_menuBarWidget) +
      visibleWidth(m_rightControls) + 8;
#endif
  const qreal remainingFreeWidthFraction = 1.0 - kIconBarFreeWidthFraction;
  const qreal denominator = remainingFreeWidthFraction - kFreeWidthThreshold;
  const int cutoffWidth =
      denominator > 0.0
          ? static_cast<int>(std::ceil((remainingFreeWidthFraction * (fixedWidth + leadingWidth)) / denominator))
          : std::numeric_limits<int>::max();
  const bool leadingControlsVisible = !m_leadingControls->isHidden();
  const bool showLeadingControls =
      leadingWidth > 0 &&
      width() >= cutoffWidth + (leadingControlsVisible ? -kFreeWidthToggleMargin : kFreeWidthToggleMargin);

  m_leadingControls->setVisible(showLeadingControls);
  m_rightCenterSpacer->setVisible(showLeadingControls);

  const int freeWidth = std::max(0, width() - fixedWidth - (showLeadingControls ? leadingWidth : 0));
  const int desiredCenterWidth = static_cast<int>(std::lround(freeWidth * kIconBarFreeWidthFraction));
  m_centerWidget->setFixedWidth(std::min(freeWidth, std::max(centerMinimumWidth, desiredCenterWidth)));
  m_layout->invalidate();
}

void WindowBar::applyLeadingButtonStyle(QToolButton *button) const {
  if (!button) {
    return;
  }
  button->setStyleSheet(toolBarButtonStyle(palette(), true));
}

void WindowBar::applyWindowButtonStyle(QToolButton *button, bool closeButton, bool iconButton) const {
  if (!button) {
    return;
  }

#if defined(Q_OS_WIN)
  if (iconButton) {
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setFixedSize(kWindowsWindowIconButtonWidth, kTitleBarHeight);
    button->setIconSize(QSize(kWindowsWindowIconSize, kWindowsWindowIconSize));
    button->setStyleSheet(QStringLiteral(
        "QToolButton {"
        " border: none;"
        " background: transparent;"
        " padding: 0px;"
        " margin: 0px;"
        "}"));
    return;
  }

  const bool darkPalette = isDarkPalette(palette());
  QColor hoverFill = darkPalette ? QColor(255, 255, 255, 38) : QColor(0, 0, 0, 24);
  QColor pressedFill = darkPalette ? QColor(255, 255, 255, 52) : QColor(0, 0, 0, 34);
  QColor closeHoverFill(QStringLiteral("#e81123"));
  QColor closePressedFill(QStringLiteral("#c50f1f"));

  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setFixedSize(kWindowsWindowButtonWidth, kTitleBarHeight);
  button->setIconSize(QSize(kWindowsWindowGlyphSize, kWindowsWindowGlyphSize));
  button->setStyleSheet(QStringLiteral(
      "QToolButton {"
      " border: none;"
      " background: transparent;"
      " padding: 0px;"
      " margin: 0px;"
      "}"
      "QToolButton:hover { background: %1; }"
      "QToolButton:pressed { background: %2; }")
                            .arg(cssColor(closeButton ? closeHoverFill : hoverFill))
                            .arg(cssColor(closeButton ? closePressedFill : pressedFill)));
#else
  Q_UNUSED(closeButton);
  Q_UNUSED(iconButton);
#endif
}

QToolButton *WindowBar::createWindowButton(const QString& toolTip) {
  auto *button = new QToolButton(this);
  button->setAutoRaise(true);
  button->setFocusPolicy(Qt::NoFocus);
  button->setToolTip(toolTip);
  button->setCursor(Qt::ArrowCursor);

#if defined(Q_OS_WIN)
  button->setFixedHeight(kTitleBarHeight);
#else
  const int buttonSize = style()->pixelMetric(QStyle::PM_TitleBarButtonSize, nullptr, this);
  const int iconSize = style()->pixelMetric(QStyle::PM_TitleBarButtonIconSize, nullptr, this);
  if (buttonSize > 0) {
    button->setFixedSize(buttonSize, buttonSize);
  }
  if (iconSize > 0) {
    button->setIconSize(QSize(iconSize, iconSize));
  }
#endif

  return button;
}

void WindowBar::refreshLeadingToggleButtonIcons() {
  for (const auto &entry : m_leadingToggleButtons) {
    if (!entry.button) {
      continue;
    }
    entry.button->setIcon(stencilSvgIcon(entry.iconPath, toolBarButtonIconColor(palette(), entry.button->isEnabled())));
  }
}

void WindowBar::syncWindowButtons() {
#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
  const bool maximized = window() && window()->isMaximized();

#if defined(Q_OS_WIN)
  const bool darkPalette = isDarkPalette(palette());
  const QColor windowColor = palette().color(QPalette::Window);
  const QColor buttonColor =
      blendColors(palette().color(QPalette::Text), windowColor, darkPalette ? 0.88 : 0.72);
  const QColor disabledColor =
      blendColors(palette().color(QPalette::Disabled, QPalette::Text), windowColor, darkPalette ? 0.6 : 0.48);
  if (m_windowIconButton) {
    const QIcon windowIcon = window() ? window()->windowIcon() : QIcon(QStringLiteral(":/vgmtrans.png"));
    m_windowIconButton->setIcon(windowIcon);
  }
  if (m_minimizeButton) {
    m_minimizeButton->setIcon(multiStateStencilIcon(
        QStringLiteral(":/window-bar/minimize.svg"), buttonColor, buttonColor, disabledColor,
        QSize(kWindowsWindowGlyphSize, kWindowsWindowGlyphSize)));
  }
  if (m_maximizeButton) {
    m_maximizeButton->setIcon(multiStateStencilIcon(
        maximized ? QStringLiteral(":/window-bar/restore.svg") : QStringLiteral(":/window-bar/maximize.svg"),
        buttonColor, buttonColor, disabledColor, QSize(kWindowsWindowGlyphSize, kWindowsWindowGlyphSize)));
    m_maximizeButton->setToolTip(maximized ? "Restore window" : "Maximize window");
  }
  if (m_closeButton) {
    m_closeButton->setIcon(multiStateStencilIcon(
        QStringLiteral(":/window-bar/close.svg"), buttonColor, Qt::white, disabledColor,
        QSize(kWindowsWindowGlyphSize, kWindowsWindowGlyphSize)));
  }
#else
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
#endif
}
