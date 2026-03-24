/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Logger.h"

#include <QActionGroup>
#include <QColor>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGridLayout>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QPalette>
#include <LogItem.h>
#include "QtVGMRoot.h"
#include "TitleBar.h"
#include "util/UIHelpers.h"

static Logger *s_instance = nullptr;

namespace {

constexpr int FLUSH_INTERVAL_MS = 500;
constexpr int FLUSH_MESSAGE_THRESHOLD = 5000;

QString filterButtonText(int level) {
  switch (level) {
  case LOG_LEVEL_ERR:
    return QStringLiteral(u"Errors ▾");
  case LOG_LEVEL_WARN:
    return QStringLiteral(u"Warnings+ ▾");
  case LOG_LEVEL_INFO:
    return QStringLiteral(u"Info+ ▾");
  case LOG_LEVEL_DEBUG:
  default:
    return QStringLiteral(u"Debug ▾");
  }
}

QString filterMenuText(int level) {
  switch (level) {
  case LOG_LEVEL_ERR:
    return QStringLiteral("Errors");
  case LOG_LEVEL_WARN:
    return QStringLiteral("Errors, warnings");
  case LOG_LEVEL_INFO:
    return QStringLiteral("Errors, warnings, information");
  case LOG_LEVEL_DEBUG:
  default:
    return QStringLiteral("Complete debug information");
  }
}

QString levelPrefix(LogLevel level) {
  switch (level) {
  case LOG_LEVEL_ERR:
    return QStringLiteral("[ERR]");
  case LOG_LEVEL_WARN:
    return QStringLiteral("[WRN]");
  case LOG_LEVEL_INFO:
    return QStringLiteral("[INF]");
  case LOG_LEVEL_DEBUG:
  default:
    return QStringLiteral("[DBG]");
  }
}

QColor levelColor(LogLevel level) {
  switch (level) {
  case LOG_LEVEL_ERR:
    return QColor(QStringLiteral("red"));
  case LOG_LEVEL_WARN:
    return QColor(QStringLiteral("orange"));
  case LOG_LEVEL_INFO:
    return QColor(QStringLiteral("cyan"));
  case LOG_LEVEL_DEBUG:
  default:
    return QColor(QStringLiteral("mediumpurple"));
  }
}

} // namespace

Logger::Logger(QWidget *parent)
    : QDockWidget("Log", parent), m_level(LOG_LEVEL_INFO), m_flushTimer(new QTimer(this)) {
  s_instance = this;
  setAllowedAreas(Qt::AllDockWidgetAreas);

  createElements();
  connectElements();
}

void Logger::createElements() {
  logger_wrapper = new QWidget;

  logger_textarea = new QPlainTextEdit(logger_wrapper);
  logger_textarea->setReadOnly(true);
  logger_textarea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  logger_textarea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  logger_textarea->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  m_flushTimer->setSingleShot(true);
  connect(m_flushTimer, &QTimer::timeout, this, &Logger::flushPending);

  QGridLayout *logger_layout = new QGridLayout;
  logger_layout->addWidget(logger_textarea, 0, 0);

  logger_wrapper->setLayout(logger_layout);

  setWidget(logger_wrapper);
};

void Logger::connectElements() {
  connect(&qtVGMRoot, &QtVGMRoot::UI_log, this, &Logger::push);
}

QString Logger::getLogText() {
  if (s_instance) {
    s_instance->flushPending();
    return s_instance->logger_textarea->toPlainText();
  }
  return {};
}

void Logger::installTitleBarControls(TitleBar *titleBar) {
  if (!titleBar) {
    return;
  }

  const QColor buttonColor = toolBarButtonIconColor(titleBar->palette());
  const QString buttonStyle = toolBarButtonStyle(titleBar->palette());
  const auto addIconButton = [titleBar, &buttonColor, &buttonStyle](const QString &iconPath, const QString &toolTip) {
    auto *button = new QToolButton(titleBar);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::ArrowCursor);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setToolTip(toolTip);
    button->setFixedSize(22, 20);
    button->setIconSize(QSize(16, 16));
    button->setStyleSheet(buttonStyle);
    button->setIcon(stencilSvgIcon(iconPath, buttonColor));
    titleBar->addLeadingWidget(button);
    return button;
  };

  m_filterButton = new QToolButton(titleBar);
  m_filterButton->setAutoRaise(true);
  m_filterButton->setFocusPolicy(Qt::NoFocus);
  m_filterButton->setCursor(Qt::ArrowCursor);
  m_filterButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  m_filterButton->setPopupMode(QToolButton::InstantPopup);
  m_filterButton->setToolTip(QStringLiteral("Log level"));
  m_filterButton->setText(filterButtonText(m_level));
  m_filterButton->setStyleSheet(
      QStringLiteral(
          "QToolButton { border: none; background: transparent; padding: 0px; margin: 0px; color: %1; }"
          "QToolButton::menu-indicator { image: none; width: 0px; }")
          .arg(cssColor(buttonColor)));
  QFont font = m_filterButton->font();
  font.setPointSizeF(font.pointSizeF() + 1.0);
  m_filterButton->setFont(font);
  m_filterButton->setMinimumWidth(m_filterButton->fontMetrics().horizontalAdvance(filterButtonText(LOG_LEVEL_WARN)));

  auto *filterMenu = new QMenu(m_filterButton);
  auto *filterActions = new QActionGroup(filterMenu);
  filterActions->setExclusive(true);
  for (int level = LOG_LEVEL_ERR; level <= LOG_LEVEL_DEBUG; ++level) {
    QAction *action = filterMenu->addAction(filterMenuText(level));
    action->setData(level);
    action->setCheckable(true);
    action->setChecked(level == m_level);
    filterActions->addAction(action);
  }
  connect(filterActions, &QActionGroup::triggered, this,
          [this](QAction *action) { setLevel(action ? action->data().toInt() : LOG_LEVEL_INFO); });
  m_filterButton->setMenu(filterMenu);
  titleBar->addLeadingWidget(m_filterButton);

  if (QToolButton *clearButton = addIconButton(QStringLiteral(":/icons/eraser.svg"), QStringLiteral("Clear log"))) {
    connect(clearButton, &QToolButton::clicked, this, &Logger::clearLog);
  }
  if (QToolButton *exportButton = addIconButton(QStringLiteral(":/icons/export.svg"), QStringLiteral("Export log"))) {
    connect(exportButton, &QToolButton::clicked, this, &Logger::exportLog);
  }
}

void Logger::exportLog() {
  flushPending();
  if (logger_textarea->toPlainText().isEmpty()) {
    return;
  }

  auto path = QFileDialog::getSaveFileName(this, "Export log", "", "Log files (*.log)");
  if (path.isEmpty()) {
    return;
  }

  QSaveFile log(path);
  log.open(QIODevice::WriteOnly);

  QByteArray out_buf;
  out_buf.append(logger_textarea->toPlainText().toUtf8());
  log.write(out_buf);
  log.commit();
}

void Logger::clearLog() {
  m_flushTimer->stop();
  m_pendingMessages.clear();
  logger_textarea->clear();
}

void Logger::setLevel(int level) {
  m_level = level;
  if (!m_filterButton) {
    return;
  }

  m_filterButton->setText(filterButtonText(level));
  if (QMenu *filterMenu = m_filterButton->menu()) {
    for (QAction *action : filterMenu->actions()) {
      action->setChecked(action->data().toInt() == level);
    }
  }
}

void Logger::push(const LogItem *item) {
  if (item->logLevel() > m_level) {
    return;
  }

  // If the source string is empty, don't print it, otherwise encapsulate it in brackets
  QString message;
  if (!item->source().empty()) {
    message.append('[');
    message.append(QString::fromStdString(item->source()));
    message.append("] ");
  }
  message.append(QString::fromStdString(item->text()));

  m_pendingMessages.append({message, item->logLevel()});

  if (m_pendingMessages.size() >= FLUSH_MESSAGE_THRESHOLD) {
    flushPending();
    return;
  }

  m_flushTimer->start(FLUSH_INTERVAL_MS);
}

void Logger::flushPending() {
  m_flushTimer->stop();

  if (m_pendingMessages.isEmpty()) {
    return;
  }

  QTextCursor cursor = logger_textarea->textCursor();
  cursor.movePosition(QTextCursor::End);
  cursor.beginEditBlock();

  logger_textarea->setUpdatesEnabled(false);

  QTextCharFormat text_format;
  text_format.setForeground(logger_textarea->palette().color(QPalette::Text));

  for (const PendingMessage &entry : m_pendingMessages) {
    QTextCharFormat prefix_format;
    prefix_format.setForeground(levelColor(entry.level));
    cursor.insertText(levelPrefix(entry.level), prefix_format);
    cursor.insertText(QStringLiteral(" "), text_format);
    cursor.insertText(entry.text, text_format);
    cursor.insertBlock();
  }

  cursor.endEditBlock();
  logger_textarea->setTextCursor(cursor);
  logger_textarea->setUpdatesEnabled(true);
  logger_textarea->ensureCursorVisible();

  m_pendingMessages.clear();
}
