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
#include <QApplication>
#include <QKeyEvent>
#include <filesystem>
#include <version.h>
#include <QWKWidgets/widgetwindowagent.h>
#include "MainWindow.h"
#include "QtVGMRoot.h"
#include "MenuBar.h"
#include "IconBar.h"
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

void MainWindow::createElements() {
  setDocumentMode(true);
  setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);
  setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

  const auto dockResizeGroup = [this](QDockWidget *dock) {
    if (dock == m_rawfile_dock || dock == m_vgmfile_dock || dock == m_coll_view_dock) {
      return QList<QDockWidget *>{m_rawfile_dock, m_vgmfile_dock, m_coll_view_dock};
    }
    if (dock == m_logger || dock == m_coll_dock) {
      return QList<QDockWidget *>{m_logger, m_coll_dock};
    }
    return QList<QDockWidget *>{dock};
  };

  const auto resizeBuddyDock = [dockResizeGroup](QDockWidget *dock) -> QDockWidget * {
    const QList<QDockWidget *> group = dockResizeGroup(dock);
    const int dockIndex = group.indexOf(dock);
    if (dockIndex < 0) {
      return nullptr;
    }

    const auto canResize = [](QDockWidget *candidate) {
      return candidate && candidate->isVisible() && !candidate->property("dockCollapsed").toBool();
    };

    for (int index = dockIndex - 1; index >= 0; --index) {
      if (canResize(group[index])) {
        return group[index];
      }
    }
    for (int index = dockIndex + 1; index < group.size(); ++index) {
      if (canResize(group[index])) {
        return group[index];
      }
    }
    return nullptr;
  };

  const auto installTitleBar = [this, resizeBuddyDock](QDockWidget *dock, const QString& title,
                                                       TitleBar::Buttons buttons) {
    auto *titleBar = new TitleBar(title, buttons, dock);
    connect(titleBar, &TitleBar::hideRequested, dock, &QDockWidget::hide);
    connect(titleBar, &TitleBar::addRequested, this, [this]() {
      ManualCollectionDialog dialog(this);
      dialog.exec();
    });
    connect(titleBar, &TitleBar::collapseToggled, this,
            [this, dock, titleBar, resizeBuddyDock](bool collapsed) {
      QWidget *dockContents = dock->widget();
      if (!dockContents) {
        return;
      }

      const int titleBarHeight = titleBar->height() > 0 ? titleBar->height() : titleBar->sizeHint().height();
      const int currentHeight = dock->height() > 0 ? dock->height() : dock->sizeHint().height();
      if (collapsed) {
        dock->setProperty("expandedHeight", currentHeight > titleBarHeight ? currentHeight
                                                                           : dock->sizeHint().height());
        dockContents->setProperty("minimumHeightBeforeCollapse", dockContents->minimumHeight());
        dockContents->setProperty("maximumHeightBeforeCollapse", dockContents->maximumHeight());
        dockContents->setProperty(
            "verticalSizePolicyBeforeCollapse",
            static_cast<int>(dockContents->sizePolicy().verticalPolicy()));
        QSizePolicy collapsedPolicy = dockContents->sizePolicy();
        collapsedPolicy.setVerticalPolicy(QSizePolicy::Fixed);
        dockContents->setSizePolicy(collapsedPolicy);
        dockContents->setMinimumHeight(0);
        dockContents->setMaximumHeight(0);
      } else {
        QSizePolicy expandedPolicy = dockContents->sizePolicy();
        expandedPolicy.setVerticalPolicy(
            static_cast<QSizePolicy::Policy>(
                dockContents->property("verticalSizePolicyBeforeCollapse").toInt()));
        dockContents->setSizePolicy(expandedPolicy);
        dockContents->setMinimumHeight(dockContents->property("minimumHeightBeforeCollapse").toInt());
        const int maximumHeight = dockContents->property("maximumHeightBeforeCollapse").toInt();
        dockContents->setMaximumHeight(maximumHeight > 0 ? maximumHeight : QWIDGETSIZE_MAX);
      }
      dock->setProperty("dockCollapsed", collapsed);
      dockContents->updateGeometry();
      dock->updateGeometry();

      const int savedExpandedHeight = dock->property("expandedHeight").toInt();
      const int minimumExpandedHeight = titleBarHeight + dockContents->minimumHeight();
      const int targetHeight = collapsed
                                   ? titleBarHeight
                                   : (savedExpandedHeight > 0 ? savedExpandedHeight
                                                              : minimumExpandedHeight);
      if (dock->isFloating()) {
        dock->resize(dock->width(), targetHeight);
      } else if (QDockWidget *buddy = resizeBuddyDock(dock)) {
        const int heightDelta = targetHeight - currentHeight;
        const int buddyTargetHeight = std::max(0, buddy->height() - heightDelta);
        resizeDocks({buddy, dock}, {buddyTargetHeight, targetHeight}, Qt::Vertical);
      } else {
        resizeDocks({dock}, {targetHeight}, Qt::Vertical);
      }
    });
    dock->setTitleBarWidget(titleBar);
  };

  m_rawfile_dock = new QDockWidget("Raw files");
  m_rawfile_dock->setAllowedAreas(Qt::LeftDockWidgetArea);
  m_rawfile_dock->setWidget(new RawFileListView());
  m_rawfile_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_rawfile_dock, "Scanned Files",
                  TitleBar::HideButton | TitleBar::CollapseButton);

  m_vgmfile_dock = new QDockWidget("Detected Music Files");
  m_vgmfile_dock->setAllowedAreas(Qt::LeftDockWidgetArea);
  m_vgmfile_dock->setWidget(new VGMFileListView());
  m_vgmfile_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_vgmfile_dock, "Detected Music Files",
                  TitleBar::HideButton | TitleBar::CollapseButton);

  m_coll_listview = new VGMCollListView();
  m_coll_view = new VGMCollView();
  m_icon_bar = new IconBar();

  auto *central_wrapper = new QWidget(this);
  auto *central_layout = new QVBoxLayout();
  central_layout->setContentsMargins(0, 0, 0, 0);
  central_layout->setSpacing(0);
  central_layout->addWidget(MdiArea::the(), 1);
  central_layout->addWidget(m_icon_bar);
  central_wrapper->setLayout(central_layout);
  setCentralWidget(central_wrapper);

  m_coll_dock = new QDockWidget("Collections");
  m_coll_dock->setAllowedAreas(Qt::BottomDockWidgetArea);
  m_coll_dock->setWidget(m_coll_listview);
  m_coll_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_coll_dock, "Collections",
                  TitleBar::HideButton | TitleBar::NewButton | TitleBar::CollapseButton);

  m_coll_view_dock = new QDockWidget("Collection contents");
  m_coll_view_dock->setAllowedAreas(Qt::LeftDockWidgetArea);
  m_coll_view_dock->setWidget(m_coll_view);
  m_coll_view_dock->setContentsMargins(0, 0, 0, 0);
  installTitleBar(m_coll_view_dock, "Collection Contents",
                  TitleBar::HideButton | TitleBar::CollapseButton);

  addDockWidget(Qt::LeftDockWidgetArea, m_rawfile_dock);
  splitDockWidget(m_rawfile_dock, m_vgmfile_dock, Qt::Orientation::Vertical);
  splitDockWidget(m_vgmfile_dock, m_coll_view_dock, Qt::Orientation::Vertical);
  m_vgmfile_dock->setFocus();

  m_logger = new Logger();
  m_logger->setAllowedAreas(Qt::BottomDockWidgetArea);
  addDockWidget(Qt::BottomDockWidgetArea, m_logger);
  addDockWidget(Qt::BottomDockWidgetArea, m_coll_dock);
  m_logger->hide();
  m_rawfile_dock->hide();
  m_coll_dock->hide();

  QList<QDockWidget *> docks = findChildren<QDockWidget *>(QString(), Qt::FindDirectChildrenOnly);

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  m_windowBar = new WindowBar(this);
  m_menu_bar = new MenuBar(nullptr, docks);
  m_menu_bar->setNativeMenuBar(true);
#else
  m_topChrome = new QWidget(this);
  auto *topChromeLayout = new QVBoxLayout(m_topChrome);
  topChromeLayout->setContentsMargins(0, 0, 0, 0);
  topChromeLayout->setSpacing(0);

  m_windowBar = new WindowBar(m_topChrome);
  topChromeLayout->addWidget(m_windowBar);

  m_menu_bar = new MenuBar(m_topChrome, docks);
  m_menu_bar->setNativeMenuBar(false);
  topChromeLayout->addWidget(m_menu_bar);
#endif
  createStatusBar();
  m_toastHost = new ToastHost(this);
}

void MainWindow::configureWindowAgent() {
  if (!m_windowAgent || !m_windowBar) {
    return;
  }

  m_windowAgent->setTitleBar(m_windowBar);
  if (QWidget *dummyButton = m_windowBar->dummyButton()) {
    m_windowAgent->setHitTestVisible(dummyButton, true);
  }

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  m_windowAgent->setWindowAttribute(QStringLiteral("no-system-buttons"), false);
  if (QWidget *systemButtonArea = m_windowBar->systemButtonArea()) {
    m_windowAgent->setSystemButtonArea(systemButtonArea);
  }
  setMenuWidget(m_windowBar);
#else
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
  setMenuWidget(m_topChrome);
#endif
}

void MainWindow::createStatusBar() {
  statusBarContent = new StatusBarContent;
  statusBar()->setMaximumHeight(statusBarContent->maximumHeight());
  statusBar()->addPermanentWidget(statusBarContent, 1);
}

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);

  const int totalHeight = height();
  const int collectionDockHeight = totalHeight / 5;
  resizeDocks({m_vgmfile_dock, m_coll_view_dock},
              {totalHeight - collectionDockHeight, collectionDockHeight},
              Qt::Vertical);
  if (QLayout *mainLayout = layout()) {
    mainLayout->activate();
  }
  const int realizedCollectionDockHeight =
      (m_coll_view_dock && m_coll_view_dock->height() > 0) ? m_coll_view_dock->height()
                                                            : collectionDockHeight;
  resizeDocks({m_coll_dock, m_logger},
              {realizedCollectionDockHeight, realizedCollectionDockHeight},
              Qt::Vertical);

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

  connect(m_icon_bar, &IconBar::playToggle, m_coll_listview,
          &VGMCollListView::handlePlaybackRequest);
  connect(m_coll_listview, &VGMCollListView::nothingToPlay, m_icon_bar, &IconBar::showPlayInfo);
  connect(m_icon_bar, &IconBar::stopPressed, m_coll_listview, &VGMCollListView::handleStopRequest);
  connect(m_icon_bar, &IconBar::seekingTo, &SequencePlayer::the(), &SequencePlayer::seek);
  connect(&qtVGMRoot, &QtVGMRoot::UI_toastRequested, this, &MainWindow::showToast);

  auto *playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
  playShortcut->setContext(Qt::WindowShortcut);
  connect(playShortcut, &QShortcut::activated, m_coll_listview, &VGMCollListView::handlePlaybackRequest);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
  if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (!keyEvent->isAutoRepeat() && keyEvent->key() == HexViewInput::kModifierKey) {
      const bool active = event->type() == QEvent::KeyPress;
      NotificationCenter::the()->setSeekModifierActive(active);
    }
  } else if (event->type() == QEvent::ApplicationDeactivate) {
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
  QMainWindow::resizeEvent(event);
  updateDragOverlayGeometry();
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
