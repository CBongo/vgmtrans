/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QFileDialog>
#include <QStandardPaths>
#include <QVBoxLayout>
#if defined(Q_OS_LINUX)
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QVariantMap>
#endif
#include <QShortcut>
#include <QMessageBox>
#include <QStatusBar>
#include <QResizeEvent>
#include <QAbstractButton>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <algorithm>
#include <filesystem>
#include <version.h>
#include <QWKWidgets/widgetwindowagent.h>
#include "MainWindow.h"
#include "QtVGMRoot.h"
#include "MenuBar.h"
#include "PlaybackControls.h"
#include "About.h"
#include "widgets/ItemViewDensity.h"
#include "Logger.h"
#include "ManualCollectionDialog.h"
#include "SequencePlayer.h"
#include "services/NotificationCenter.h"
#include "services/Settings.h"
#include "workarea/RawFileListView.h"
#include "workarea/VGMFileListView.h"
#include "workarea/VGMCollListView.h"
#include "workarea/VGMCollView.h"
#include "workarea/hexview/HexViewInput.h"
#include "workarea/MdiArea.h"
#include "TitleBar.h"
#include "StatusBarContent.h"
#include "LogManager.h"
#include "widgets/WindowBar.h"
#include "widgets/ToastHost.h"

namespace {
constexpr auto MIME_PORTAL_FILETRANSFER = "application/vnd.portal.filetransfer";
constexpr int kDockLayoutStateVersion = 2;

bool isDockSeparatorCursor(Qt::CursorShape shape) {
  return shape == Qt::SplitHCursor || shape == Qt::SplitVCursor;
}

bool isVisibleDockInArea(const QMainWindow *window, QDockWidget *dock, Qt::DockWidgetArea area) {
  return dock && dock->isVisible() && !dock->isFloating() && window->dockWidgetArea(dock) == area;
}

QDockWidget *firstVisibleDockInArea(const QMainWindow *window, std::initializer_list<QDockWidget *> docks,
                                    Qt::DockWidgetArea area) {
  for (QDockWidget *dock : docks) {
    if (isVisibleDockInArea(window, dock, area)) {
      return dock;
    }
  }
  return nullptr;
}

int dockSizeForOrientation(QDockWidget *dock, Qt::Orientation orientation) {
  return orientation == Qt::Horizontal ? dock->width() : dock->height();
}

int firstVisibleDockSizeInArea(const QMainWindow *window, std::initializer_list<QDockWidget *> docks,
                               Qt::DockWidgetArea area, Qt::Orientation orientation) {
  if (QDockWidget *dock = firstVisibleDockInArea(window, docks, area)) {
    return dockSizeForOrientation(dock, orientation);
  }
  return 0;
}

void setDockSizeConstraint(QDockWidget *dock, Qt::Orientation orientation, bool lock, int preferredSize) {
  if (!dock) {
    return;
  }

  const int minSize = lock ? preferredSize : 0;
  const int maxSize = lock ? preferredSize : QWIDGETSIZE_MAX;
  if (orientation == Qt::Horizontal) {
    dock->setMinimumWidth(minSize);
    dock->setMaximumWidth(maxSize);
    return;
  }

  dock->setMinimumHeight(minSize);
  dock->setMaximumHeight(maxSize);
}

QStringList retrievePortalDroppedFiles([[maybe_unused]] const QMimeData* mimeData) {
#if defined(VGMTRANS_HAVE_DBUS) && defined(Q_OS_LINUX)
  if (!mimeData) {
    return {};
  }

  QString portalMimeFormat;
  for (const QString& format : mimeData->formats()) {
    if (format == MIME_PORTAL_FILETRANSFER || format.startsWith(MIME_PORTAL_FILETRANSFER)) {
      portalMimeFormat = format;
      break;
    }
  }
  if (portalMimeFormat.isEmpty()) {
    return {};
  }

  QByteArray keyBytes = mimeData->data(portalMimeFormat);
  if (const qsizetype nulPos = keyBytes.indexOf('\0'); nulPos >= 0) {
    keyBytes.truncate(nulPos);
  }

  const QString key = QString::fromUtf8(keyBytes.trimmed());
  if (key.isEmpty()) {
    return {};
  }

  QDBusMessage msg = QDBusMessage::createMethodCall(
      QStringLiteral("org.freedesktop.portal.Documents"),
      QStringLiteral("/org/freedesktop/portal/documents"),
      QStringLiteral("org.freedesktop.portal.FileTransfer"),
      QStringLiteral("RetrieveFiles"));
  QVariantMap options;
  msg << key << options;

  QDBusReply<QStringList> reply = QDBusConnection::sessionBus().call(msg);
  if (!reply.isValid()) {
    return {};
  }

  return reply.value();
#else
  return {};
#endif
}
}  // namespace

MainWindow::MainWindow() : QMainWindow(nullptr) {
  setWindowTitle("VGMTrans");
  setWindowIcon(QIcon(":/vgmtrans.png"));
  setAttribute(Qt::WA_DontCreateNativeAncestors);
  setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
  setAcceptDrops(true);
  setContextMenuPolicy(Qt::NoContextMenu);

  m_windowAgent = new QWK::WidgetWindowAgent(this);
  m_windowAgent->setup(this);

  createElements();
  configureWindowAgent();
  routeSignals();
  qApp->installEventFilter(this);

  m_dragOverlay = new QWidget(this);
  m_dragOverlay->setObjectName(QStringLiteral("dragOverlay"));
  m_dragOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_dragOverlay->setAcceptDrops(false);
  m_dragOverlay->hide();
  updateDragOverlayAppearance();
  updateDragOverlayGeometry();

  if (const QByteArray geometry = Settings::the()->mainWindow.windowGeometry(); !geometry.isEmpty()) {
    restoreGeometry(geometry);
  }
  m_preferredDockState = Settings::the()->mainWindow.dockState();

  auto infostring = QString("Running %1 (%4, %5), BASS %2, Qt %3")
                        .arg(VGMTRANS_VERSION,
                             QString::number(BASS_GetVersion(), 16),
                             qVersion(),
                             VGMTRANS_REVISION,
                             VGMTRANS_BRANCH)
                        .toStdString();
  L_INFO(infostring);
}

void MainWindow::activateMainLayout() {
  if (QLayout *mainLayout = layout()) {
    mainLayout->activate();
  }
}

void MainWindow::captureConstrainedDockSizes(bool onlyIfUnset) {
  const auto captureDockSize = [this, onlyIfUnset](QDockWidget *dock, Qt::DockWidgetArea area,
                                                   Qt::Orientation orientation, int &preferredSize) {
    if ((onlyIfUnset && preferredSize > 0) || !isVisibleDockInArea(this, dock, area)) {
      return;
    }
    if (const int dockSize = dockSizeForOrientation(dock, orientation); dockSize > 0) {
      preferredSize = dockSize;
    }
  };

  captureDockSize(m_rawfile_dock, Qt::LeftDockWidgetArea, Qt::Vertical, m_rawFilePreferredHeight);
  captureDockSize(m_coll_view_dock, Qt::LeftDockWidgetArea, Qt::Vertical, m_collViewPreferredHeight);
  captureDockSize(m_coll_view_dock, Qt::BottomDockWidgetArea, Qt::Horizontal, m_collViewPreferredBottomWidth);
}

void MainWindow::captureLeftDockAreaWidth() {
  if (const int dockSize = firstVisibleDockSizeInArea(
          this,
          {m_rawfile_dock, m_vgmfile_dock, m_coll_dock, m_coll_view_dock},
          Qt::LeftDockWidgetArea,
          Qt::Horizontal);
      dockSize > 0) {
    m_leftDockAreaPreferredWidth = dockSize;
  }
}

void MainWindow::captureBottomDockAreaHeight() {
  if (const int dockSize = firstVisibleDockSizeInArea(
          this,
          {m_coll_dock, m_coll_view_dock, m_logger},
          Qt::BottomDockWidgetArea,
          Qt::Vertical);
      dockSize > 0) {
    m_bottomDockAreaPreferredHeight = dockSize;
  }
}

void MainWindow::applyDockAreaTargets(bool applyLeftWidth, bool applyBottomHeight) {
  bool resized = false;
  const auto resizeAreaToPreferredSize =
      [this, &resized](std::initializer_list<QDockWidget *> docks, Qt::DockWidgetArea area,
                       Qt::Orientation orientation, int preferredSize) {
        if (preferredSize <= 0) {
          return;
        }

        if (QDockWidget *dock = firstVisibleDockInArea(this, docks, area)) {
          resizeDocks({dock}, {preferredSize}, orientation);
          resized = true;
        }
      };

  if (applyLeftWidth) {
    resizeAreaToPreferredSize({m_rawfile_dock, m_vgmfile_dock, m_coll_dock, m_coll_view_dock},
                              Qt::LeftDockWidgetArea,
                              Qt::Horizontal,
                              m_leftDockAreaPreferredWidth);
  }

  if (applyBottomHeight) {
    resizeAreaToPreferredSize({m_coll_dock, m_coll_view_dock, m_logger},
                              Qt::BottomDockWidgetArea,
                              Qt::Vertical,
                              m_bottomDockAreaPreferredHeight);
  }

  if (resized) {
    activateMainLayout();
  }
}

void MainWindow::scheduleDockStateUpdate(bool capturePreferredDockSizes) {
  // Defer until the current dock/layout change finishes so we capture the settled user layout.
  QTimer::singleShot(0, this, [this, capturePreferredDockSizes]() {
    activateMainLayout();
    if (capturePreferredDockSizes) {
      captureConstrainedDockSizes(false);
      captureLeftDockAreaWidth();
      captureBottomDockAreaHeight();
    } else {
      captureConstrainedDockSizes(true);
    }
    applyDockSizeConstraints();
    activateMainLayout();
    // Preserve only user-driven layout changes; window-driven dock shrink should not become the new baseline.
    m_preferredDockState = saveState(kDockLayoutStateVersion);
  });
}

void MainWindow::applyDockSizeConstraints() {
  const auto applyDockConstraint = [this](QDockWidget *dock, Qt::DockWidgetArea area, Qt::Orientation orientation,
                                          int preferredSize) {
    const bool shouldLock = !m_dockSeparatorDragActive && preferredSize > 0 && isVisibleDockInArea(this, dock, area);
    setDockSizeConstraint(dock, orientation, shouldLock, preferredSize);
  };

  applyDockConstraint(m_rawfile_dock, Qt::LeftDockWidgetArea, Qt::Vertical, m_rawFilePreferredHeight);
  applyDockConstraint(m_coll_view_dock, Qt::LeftDockWidgetArea, Qt::Vertical, m_collViewPreferredHeight);
  applyDockConstraint(m_coll_view_dock, Qt::BottomDockWidgetArea, Qt::Horizontal, m_collViewPreferredBottomWidth);
}

void MainWindow::applyDefaultDockLayout() {
  m_rawfile_dock->show();
  m_vgmfile_dock->show();
  m_coll_dock->show();
  m_coll_view_dock->show();
  m_logger->show();
  activateMainLayout();

  const auto *collListView = qobject_cast<VGMCollListView *>(m_coll_dock ? m_coll_dock->widget() : nullptr);
  if (!collListView) {
    return;
  }

  const int scrollBarHeight = horizontalScrollBarReservedHeight(collListView);
  const int fixedLeftDockHeight = Size::VTab + scrollBarHeight +
      static_cast<int>(4.5 * ItemViewDensity::listItemStride(collListView));
  const int availableLeftDockHeight = m_rawfile_dock->height() + m_vgmfile_dock->height();
  const int remainingLeftDockHeight = std::max(1, availableLeftDockHeight - fixedLeftDockHeight);

  resizeDocks({m_rawfile_dock, m_vgmfile_dock},
              {fixedLeftDockHeight, remainingLeftDockHeight},
              Qt::Vertical);
  activateMainLayout();

  resizeDocks({m_coll_dock, m_coll_view_dock, m_logger},
              {fixedLeftDockHeight, fixedLeftDockHeight, fixedLeftDockHeight},
              Qt::Vertical);

  const int bottomDockAreaWidth = std::max(1, m_coll_dock->width() + m_coll_view_dock->width());
  const int collectionContentsWidth = std::max(1, bottomDockAreaWidth / 4);
  resizeDocks({m_coll_dock, m_coll_view_dock},
              {std::max(1, bottomDockAreaWidth - collectionContentsWidth), collectionContentsWidth},
              Qt::Horizontal);
  activateMainLayout();
  m_logger->hide();
}

void MainWindow::showRestoredFloatingDocks() {
  QTimer::singleShot(0, this, [this]() {
    for (QDockWidget *dock : std::initializer_list<QDockWidget *>{m_rawfile_dock, m_vgmfile_dock, m_coll_dock,
                                                                   m_coll_view_dock, m_logger}) {
      if (!dock || !dock->isFloating() || !dock->toggleViewAction()->isChecked()) {
        continue;
      }
      dock->show();
      const QByteArray geometry = Settings::the()->mainWindow.floatingDockGeometry(dock->objectName());
      if (!geometry.isEmpty()) {
        dock->restoreGeometry(geometry);
      }
      dock->raise();
    }
  });
}

void MainWindow::resetDockLayout() {
  if (m_defaultDockState.isEmpty()) {
    return;
  }

  ++m_dockResizeSyncGeneration;
  m_dockSeparatorDragActive = false;
  m_rawFilePreferredHeight = 0;
  m_collViewPreferredHeight = 0;
  m_collViewPreferredBottomWidth = 0;
  m_leftDockAreaPreferredWidth = 0;
  m_bottomDockAreaPreferredHeight = 0;
  applyDockSizeConstraints();

  if (!restoreState(m_defaultDockState, kDockLayoutStateVersion)) {
    return;
  }

  applyDefaultDockLayout();
  activateMainLayout();
  captureConstrainedDockSizes(false);
  captureLeftDockAreaWidth();
  captureBottomDockAreaHeight();
  applyDockSizeConstraints();
  m_preferredDockState = saveState(kDockLayoutStateVersion);
  saveLayoutSettings();
}

void MainWindow::saveLayoutSettings() const {
  Settings::the()->mainWindow.setWindowGeometry(saveGeometry());
  for (QDockWidget *dock : std::initializer_list<QDockWidget *>{m_rawfile_dock, m_vgmfile_dock, m_coll_dock,
                                                                 m_coll_view_dock, m_logger}) {
    if (!dock) {
      continue;
    }
    if (dock->isFloating()) {
      Settings::the()->mainWindow.setFloatingDockGeometry(dock->objectName(), dock->saveGeometry());
    }
  }
  if (!m_preferredDockState.isEmpty()) {
    Settings::the()->mainWindow.setDockState(m_preferredDockState);
  } else {
    Settings::the()->mainWindow.clearDockState();
  }
}

void MainWindow::createElements() {
  setDocumentMode(true);
  setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);
  setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

  const auto installTitleBar = [this](QDockWidget *dock, const QString& title,
                                      TitleBar::Buttons buttons) {
    auto *titleBar = new TitleBar(title, buttons, dock);
    connect(titleBar, &TitleBar::hideRequested, dock, &QDockWidget::hide);
    connect(titleBar, &TitleBar::addRequested, this, [this]() {
      ManualCollectionDialog dialog(this);
      dialog.exec();
    });
    dock->setTitleBarWidget(titleBar);
    return titleBar;
  };

  m_rawfile_dock = new QDockWidget("Scanned Files");
  m_rawfile_dock->setObjectName(QStringLiteral("rawFileListDock"));
  m_rawfile_dock->setAllowedAreas(Qt::LeftDockWidgetArea);
  m_rawfile_dock->setWidget(new RawFileListView());
  m_rawfile_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_rawfile_dock, "Scanned Files", TitleBar::HideButton);

  m_vgmfile_dock = new QDockWidget("Detected Files");
  m_vgmfile_dock->setObjectName(QStringLiteral("vgmFileListDock"));
  m_vgmfile_dock->setAllowedAreas(Qt::LeftDockWidgetArea);
  m_vgmfile_dock->setWidget(new VGMFileListView());
  m_vgmfile_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_vgmfile_dock, "Detected Files", TitleBar::HideButton);

  m_coll_listview = new VGMCollListView();
  m_coll_view = new VGMCollView();
  m_playback_controls = new PlaybackControls();

  auto *central_wrapper = new QWidget(this);
  auto *central_layout = new QVBoxLayout();
  central_layout->setContentsMargins(0, 0, 0, 0);
  central_layout->setSpacing(0);
  central_layout->addWidget(MdiArea::the(), 1);
  central_wrapper->setLayout(central_layout);
  setCentralWidget(central_wrapper);

  m_coll_dock = new QDockWidget("Collections");
  m_coll_dock->setObjectName(QStringLiteral("collectionListDock"));
  m_coll_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::BottomDockWidgetArea);
  m_coll_dock->setWidget(m_coll_listview);
  m_coll_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_coll_dock, "Collections", TitleBar::HideButton | TitleBar::NewButton);

  m_coll_view_dock = new QDockWidget("Collection Contents");
  m_coll_view_dock->setObjectName(QStringLiteral("collectionContentDock"));
  m_coll_view_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::BottomDockWidgetArea);
  m_coll_view_dock->setWidget(m_coll_view);
  m_coll_view_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_coll_view_dock, "Collection Contents", TitleBar::HideButton);

  addDockWidget(Qt::LeftDockWidgetArea, m_rawfile_dock);
  splitDockWidget(m_rawfile_dock, m_vgmfile_dock, Qt::Orientation::Vertical);
  m_vgmfile_dock->setFocus();

  m_logger = new Logger();
  m_logger->setObjectName(QStringLiteral("loggerDock"));
  m_logger->setWindowTitle("Logs");
  m_logger->setAllowedAreas(Qt::BottomDockWidgetArea);
  m_logger->setContentsMargins(0, 0, 0, 0);
  if (TitleBar *loggerTitleBar = installTitleBar(m_logger, "Logs", TitleBar::HideButton)) {
    m_logger->installTitleBarControls(loggerTitleBar);
  }
  addDockWidget(Qt::BottomDockWidgetArea, m_coll_dock);
  // Keep the bottom docks in a side-by-side layout so each dock preserves its own width.
  splitDockWidget(m_coll_dock, m_coll_view_dock, Qt::Horizontal);
  splitDockWidget(m_coll_view_dock, m_logger, Qt::Horizontal);

  const QList<QDockWidget *> viewMenuDocks{
      m_vgmfile_dock, m_coll_dock, m_coll_view_dock, m_rawfile_dock, m_logger,
  };

  for (QDockWidget *dock : viewMenuDocks) {
    if (!dock) {
      continue;
    }
    connect(dock, &QDockWidget::visibilityChanged, this, [this](bool) { scheduleDockStateUpdate(false); });
    connect(dock, &QDockWidget::dockLocationChanged, this,
            [this](Qt::DockWidgetArea) { scheduleDockStateUpdate(false); });
    connect(dock, &QDockWidget::topLevelChanged, this, [this, dock](bool floating) {
      if (!floating) {
        m_dockSeparatorDragActive = true;
        applyDockSizeConstraints();
        QTimer::singleShot(0, this, [this]() {
          m_dockSeparatorDragActive = false;
          applyDockAreaTargets(true, true);
          applyDockSizeConstraints();
        });
      }
      scheduleDockStateUpdate(false);
    });
  }
  m_windowBar = new WindowBar(this);

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  m_menu_bar = new MenuBar(nullptr, viewMenuDocks);
  m_menu_bar->setNativeMenuBar(true);
#else
  m_menu_bar = new MenuBar(nullptr, viewMenuDocks);
  m_menu_bar->setNativeMenuBar(false);
  m_windowBar->setMenuBarWidget(m_menu_bar);
#endif
  m_menu_bar->setShortcutHost(this);
  m_windowBar->setCenterWidget(m_playback_controls);
  m_windowBar->setDockToggleButtons({
      {m_vgmfile_dock->toggleViewAction(), QStringLiteral(":/icons/music-box-outline.svg")},
      {m_coll_dock->toggleViewAction(), QStringLiteral(":/icons/music-box-multiple-outline.svg")},
      {m_coll_view_dock->toggleViewAction(), QStringLiteral(":/icons/package-variant.svg")},
      {m_rawfile_dock->toggleViewAction(), QStringLiteral(":/icons/file-search-outline.svg")},
      {m_logger->toggleViewAction(), QStringLiteral(":/icons/book-open-variant-outline.svg")},
  });
  createStatusBar();
  m_toastHost = new ToastHost(this);
}

void MainWindow::configureWindowAgent() {
  if (!m_windowAgent || !m_windowBar) {
    return;
  }

  m_windowAgent->setTitleBar(m_windowBar);
  if (QWidget *dockControls = m_windowBar->dockControls()) {
    m_windowAgent->setHitTestVisible(dockControls, true);
  }
  if (QWidget *menuBarWidget = m_windowBar->menuBarWidget()) {
    m_windowAgent->setHitTestVisible(menuBarWidget, true);
  }
  if (QWidget *centerWidget = m_windowBar->centerWidget()) {
    m_windowAgent->setHitTestVisible(centerWidget, true);
  }

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  m_windowAgent->setWindowAttribute(QStringLiteral("no-system-buttons"), false);
  if (QWidget *systemButtonArea = m_windowBar->systemButtonArea()) {
    m_windowAgent->setSystemButtonArea(systemButtonArea);
  }
  setMenuWidget(m_windowBar);
#else
  if (m_windowBar->windowIconButton()) {
    QAbstractButton *windowIconButton = m_windowBar->windowIconButton();
    m_windowAgent->setSystemButton(QWK::WindowAgentBase::WindowIcon, windowIconButton);
  }
  if (m_windowBar->minimizeButton()) {
    QAbstractButton *minimizeButton = m_windowBar->minimizeButton();
    m_windowAgent->setSystemButton(QWK::WindowAgentBase::Minimize, minimizeButton);
  }
  if (m_windowBar->maximizeButton()) {
    QAbstractButton *maximizeButton = m_windowBar->maximizeButton();
    m_windowAgent->setSystemButton(QWK::WindowAgentBase::Maximize, maximizeButton);
  }
  if (m_windowBar->closeButton()) {
    QAbstractButton *closeButton = m_windowBar->closeButton();
    m_windowAgent->setSystemButton(QWK::WindowAgentBase::Close, closeButton);
  }
  setMenuWidget(m_windowBar);
#endif
}

void MainWindow::createStatusBar() {
  statusBarContent = new StatusBarContent;
  statusBar()->setMaximumHeight(statusBarContent->maximumHeight());
  statusBar()->addPermanentWidget(statusBarContent, 1);
}

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);

  if (m_defaultDockState.isEmpty()) {
    applyDefaultDockLayout();
    m_defaultDockState = saveState(kDockLayoutStateVersion);

    if (!m_preferredDockState.isEmpty() &&
        !restoreState(m_preferredDockState, kDockLayoutStateVersion)) {
      m_preferredDockState.clear();
    }
    showRestoredFloatingDocks();

    if (m_preferredDockState.isEmpty()) {
      m_preferredDockState = m_defaultDockState;
    }

    activateMainLayout();
    captureConstrainedDockSizes(false);
    captureLeftDockAreaWidth();
    captureBottomDockAreaHeight();
    applyDockSizeConstraints();
  }

  updateDragOverlayGeometry();
}

void MainWindow::routeSignals() {
  connect(m_menu_bar, &MenuBar::openFile, this, &MainWindow::openFile);
  connect(m_menu_bar, &MenuBar::openRecentFile, this, &MainWindow::openFileInternal);
  connect(m_menu_bar, &MenuBar::exit, this, &MainWindow::close);
  connect(m_menu_bar, &MenuBar::showAbout, [this]() {
    About about(this);
    about.exec();
  });
  connect(m_menu_bar, &MenuBar::resetDockLayout, this, &MainWindow::resetDockLayout);

  connect(m_playback_controls, &PlaybackControls::playToggle, m_coll_listview,
          &VGMCollListView::handlePlaybackRequest);
  connect(m_coll_listview, &VGMCollListView::nothingToPlay, m_playback_controls,
          &PlaybackControls::showPlayInfo);
  connect(m_playback_controls, &PlaybackControls::stopPressed, m_coll_listview,
          &VGMCollListView::handleStopRequest);
  connect(m_playback_controls, &PlaybackControls::seekingTo, &SequencePlayer::the(), &SequencePlayer::seek);
  connect(&qtVGMRoot, &QtVGMRoot::UI_toastRequested, this, &MainWindow::showToast);

  auto *playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
  playShortcut->setContext(Qt::WindowShortcut);
  connect(playShortcut, &QShortcut::activated, m_coll_listview, &VGMCollListView::handlePlaybackRequest);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
  if (event->type() == QEvent::MouseButtonPress) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    auto *widget = qobject_cast<QWidget *>(obj);
    if (mouseEvent->button() == Qt::LeftButton && widget && (widget == this || isAncestorOf(widget)) &&
        isDockSeparatorCursor(cursor().shape())) {
      m_dockSeparatorDragActive = true;
      applyDockSizeConstraints();
    }
  } else if (event->type() == QEvent::MouseButtonRelease) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() == Qt::LeftButton && m_dockSeparatorDragActive) {
      m_dockSeparatorDragActive = false;
      scheduleDockStateUpdate(true);
    }
  }

  if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (!keyEvent->isAutoRepeat() && keyEvent->key() == HexViewInput::kModifierKey) {
      const bool active = event->type() == QEvent::KeyPress;
      NotificationCenter::the()->setSeekModifierActive(active);
    }
  } else if (event->type() == QEvent::ApplicationDeactivate) {
    m_dockSeparatorDragActive = false;
    applyDockSizeConstraints();
    NotificationCenter::the()->setSeekModifierActive(false);
  }
  return QMainWindow::eventFilter(obj, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
  event->acceptProposedAction();
  showDragOverlay();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event) {
  event->acceptProposedAction();
  showDragOverlay();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event) {
  event->accept();
  hideDragOverlay();
}

void MainWindow::dropEvent(QDropEvent *event) {
  hideDragOverlay();

  const QMimeData* mimeData = event->mimeData();
  const QStringList portalFiles = retrievePortalDroppedFiles(mimeData);
  if (!portalFiles.isEmpty()) {
    for (const QString& filePath : portalFiles) {
      if (!filePath.isEmpty()) {
        openFileInternal(filePath);
      }
    }
    event->acceptProposedAction();
    return;
  }

  handleDroppedUrls(mimeData->urls());
  event->acceptProposedAction();
}

void MainWindow::closeEvent(QCloseEvent *event) {
  saveLayoutSettings();
  QMainWindow::closeEvent(event);
}

void MainWindow::showDragOverlay() {
  if (!m_dragOverlay) {
    return;
  }
  updateDragOverlayGeometry();
  if (!m_dragOverlay->isVisible()) {
    m_dragOverlay->show();
  }
  m_dragOverlay->raise();
}

void MainWindow::hideDragOverlay() {
  if (m_dragOverlay) {
    m_dragOverlay->hide();
  }
}

void MainWindow::handleDroppedUrls(const QList<QUrl>& urls) {
  hideDragOverlay();

  if (urls.isEmpty()) {
    return;
  }

  for (const auto &url : urls) {
    if (!url.isLocalFile()) {
      continue;
    }

    const QString localFile = url.toLocalFile();
    if (!localFile.isEmpty()) {
      openFileInternal(localFile);
    }
  }
}

void MainWindow::openFile() {
  auto filenames = QFileDialog::getOpenFileNames(
      this, "Select a file...", QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
      "All files (*)");

  if (filenames.isEmpty())
    return;

  for (QString &filename : filenames) {
    openFileInternal(filename);
  }
}

void MainWindow::openFileInternal(const QString& filename) {
  static QString UNSUPPORTED_RAW_IMAGE_WARNING{
      "'%1' is a raw image file. Data is unlikely to be read correctly, do you wish "
      "to continue anyway?"};

  static QString UNSUPPORTED_RAW_IMAGE_DESCRIPTION{
      "If this is a dump of a CD or DVD (e.g. PlayStation), please "
      "convert it to '.iso'. The program cannot read raw dumps from optical media."};

  auto file_info = QFileInfo(filename);
  if (file_info.completeSuffix().contains("img")) {
    QMessageBox user_choice(QMessageBox::Icon::Warning, "File format might be unsopported",
                            UNSUPPORTED_RAW_IMAGE_WARNING.arg(file_info.fileName()),
                            QMessageBox::StandardButton::Yes | QMessageBox::StandardButton::No,
                            this);
    user_choice.setInformativeText(UNSUPPORTED_RAW_IMAGE_DESCRIPTION);
    user_choice.setWindowModality(Qt::WindowModal);
    user_choice.exec();

    if (user_choice.result() != QMessageBox::StandardButton::Yes) {
      return;
    }
  }

  if (qtVGMRoot.openRawFile(filename.toStdWString())) {
    Settings::the()->recentFiles.add(filename);
    m_menu_bar->updateRecentFilesMenu();
  }
}

void MainWindow::showToast(const QString& message, ToastType type, int duration_ms) {
  if (m_toastHost)
    m_toastHost->showToast(message, type, duration_ms);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
  const bool widthExpanded = event->oldSize().width() >= 0 && event->size().width() > event->oldSize().width();
  const bool heightExpanded = event->oldSize().height() >= 0 && event->size().height() > event->oldSize().height();
  const int syncGeneration = ++m_dockResizeSyncGeneration;

  QMainWindow::resizeEvent(event);
  updateDragOverlayGeometry();

  if (!widthExpanded && !heightExpanded) {
    return;
  }

  // Defer until QMainWindow finishes its resize layout pass before restoring the preferred dock sizes.
  QTimer::singleShot(0, this, [this, syncGeneration, widthExpanded, heightExpanded]() {
    if (syncGeneration != m_dockResizeSyncGeneration) {
      return;
    }

    if ((widthExpanded || heightExpanded) && !m_preferredDockState.isEmpty()) {
      restoreState(m_preferredDockState, kDockLayoutStateVersion);
    }
    applyDockAreaTargets(widthExpanded, heightExpanded);
    applyDockSizeConstraints();
    activateMainLayout();
  });
}

void MainWindow::updateDragOverlayAppearance() {
  if (!m_dragOverlay)
    return;

  m_dragOverlay->setStyleSheet(QStringLiteral("background-color: rgba(0, 0, 0, 102);"));
}

void MainWindow::updateDragOverlayGeometry() {
  if (!m_dragOverlay)
    return;

  m_dragOverlay->setGeometry(rect());
  m_dragOverlay->raise();
}
