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
#include <QDockWidget>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <filesystem>
#include <version.h>
#include <QWKWidgets/widgetwindowagent.h>
#include "MainWindow.h"
#include "QtVGMRoot.h"
#include "MenuBar.h"
#include "PlaybackControls.h"
#include "About.h"
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

bool isDockSeparatorCursor(Qt::CursorShape shape) {
  return shape == Qt::SplitHCursor || shape == Qt::SplitVCursor;
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

void MainWindow::scheduleDockStateUpdate(bool captureFixedDockHeights) {
  // Defer until the current dock/layout change finishes so we capture the settled user layout.
  QTimer::singleShot(0, this, [this, captureFixedDockHeights]() {
    const auto updatePreferredHeight = [this](QDockWidget *dock, int &preferredHeight) {
      if (dock && dock->isVisible() && !dock->isFloating() && dockWidgetArea(dock) == Qt::LeftDockWidgetArea &&
          dock->height() > 0) {
        preferredHeight = dock->height();
      }
    };

    activateMainLayout();
    if (captureFixedDockHeights) {
      updatePreferredHeight(m_rawfile_dock, m_rawFilePreferredHeight);
      updatePreferredHeight(m_coll_view_dock, m_collViewPreferredHeight);
    } else {
      if (m_rawFilePreferredHeight <= 0) {
        updatePreferredHeight(m_rawfile_dock, m_rawFilePreferredHeight);
      }
      if (m_collViewPreferredHeight <= 0) {
        updatePreferredHeight(m_coll_view_dock, m_collViewPreferredHeight);
      }
    }
    applyLeftDockHeightConstraints();
    activateMainLayout();
    // Preserve only user-driven layout changes; window-driven dock shrink should not become the new baseline.
    m_preferredDockState = saveState();
  });
}

void MainWindow::applyLeftDockHeightConstraints() {
  const auto applyDockConstraint = [this](QDockWidget *dock, int preferredHeight) {
    if (!dock) {
      return;
    }

    const bool shouldLock = !m_dockSeparatorDragActive && preferredHeight > 0 && dock->isVisible() &&
                            !dock->isFloating() && dockWidgetArea(dock) == Qt::LeftDockWidgetArea;
    if (shouldLock) {
      dock->setMinimumHeight(preferredHeight);
      dock->setMaximumHeight(preferredHeight);
      return;
    }

    dock->setMinimumHeight(0);
    dock->setMaximumHeight(QWIDGETSIZE_MAX);
  };

  applyDockConstraint(m_rawfile_dock, m_rawFilePreferredHeight);
  applyDockConstraint(m_coll_view_dock, m_collViewPreferredHeight);
}

void MainWindow::createElements() {
  setDocumentMode(true);
  setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);
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
  m_rawfile_dock->setAllowedAreas(Qt::LeftDockWidgetArea);
  m_rawfile_dock->setWidget(new RawFileListView());
  m_rawfile_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_rawfile_dock, "Scanned Files", TitleBar::HideButton);

  m_vgmfile_dock = new QDockWidget("Detected Files");
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
  m_coll_dock->setAllowedAreas(Qt::BottomDockWidgetArea);
  m_coll_dock->setWidget(m_coll_listview);
  m_coll_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_coll_dock, "Collections", TitleBar::HideButton | TitleBar::NewButton);

  m_coll_view_dock = new QDockWidget("Collection Contents");
  m_coll_view_dock->setAllowedAreas(Qt::LeftDockWidgetArea);
  m_coll_view_dock->setWidget(m_coll_view);
  m_coll_view_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_coll_view_dock, "Collection Contents", TitleBar::HideButton);

  addDockWidget(Qt::LeftDockWidgetArea, m_rawfile_dock);
  splitDockWidget(m_rawfile_dock, m_vgmfile_dock, Qt::Orientation::Vertical);
  splitDockWidget(m_vgmfile_dock, m_coll_view_dock, Qt::Orientation::Vertical);
  m_vgmfile_dock->setFocus();

  m_logger = new Logger();
  m_logger->setWindowTitle("Logs");
  m_logger->setAllowedAreas(Qt::BottomDockWidgetArea);
  m_logger->setContentsMargins(0, 0, 0, 0);
  if (TitleBar *loggerTitleBar = installTitleBar(m_logger, "Logs", TitleBar::HideButton)) {
    m_logger->installTitleBarControls(loggerTitleBar);
  }
  addDockWidget(Qt::BottomDockWidgetArea, m_coll_dock);
  // Keep the bottom docks in a side-by-side layout so the logger can have its own saved width.
  splitDockWidget(m_coll_dock, m_logger, Qt::Horizontal);
  m_rawfile_dock->hide();

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
    connect(dock, &QDockWidget::topLevelChanged, this, [this](bool) { scheduleDockStateUpdate(false); });
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

  if (m_preferredDockState.isEmpty()) {
    // Seed the first-run dock layout once, then rely on the saved preferred state afterward.
    const int totalHeight = height();
    const int collectionDockHeight = totalHeight / 5;
    resizeDocks({m_vgmfile_dock, m_coll_view_dock},
                {totalHeight - collectionDockHeight, collectionDockHeight},
                Qt::Vertical);
    activateMainLayout();
    const int realizedCollectionDockHeight =
        (m_coll_view_dock && m_coll_view_dock->height() > 0) ? m_coll_view_dock->height()
                                                              : collectionDockHeight;
    resizeDocks({m_coll_dock, m_logger},
                {realizedCollectionDockHeight, realizedCollectionDockHeight},
                Qt::Vertical);
    const int bottomDockAreaWidth = m_coll_dock->width() + m_logger->width();
    resizeDocks({m_coll_dock, m_logger},
                {bottomDockAreaWidth / 2, bottomDockAreaWidth / 2},
                Qt::Horizontal);
    activateMainLayout();
    // Save the wider logger width as part of the default hidden layout.
    m_logger->hide();
    m_preferredDockState = saveState();
    if (m_rawfile_dock && m_rawfile_dock->isVisible() && m_rawfile_dock->height() > 0) {
      m_rawFilePreferredHeight = m_rawfile_dock->height();
    }
    m_collViewPreferredHeight = realizedCollectionDockHeight;
    applyLeftDockHeightConstraints();
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
      applyLeftDockHeightConstraints();
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
    applyLeftDockHeightConstraints();
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
      restoreState(m_preferredDockState);
    }
    applyLeftDockHeightConstraints();
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
