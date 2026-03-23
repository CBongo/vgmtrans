/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "IconBar.h"

#include <QLayout>
#include <QToolButton>
#include <QWhatsThis>

#include "services/NotificationCenter.h"
#include "SequencePlayer.h"
#include "SeekBar.h"
#include "TintableSvgIconEngine.h"
#include "UIHelpers.h"

namespace {
constexpr int kTransportControlHeight = 32;
constexpr int kTransportButtonSize = 32;
constexpr int kTransportIconSize = 24;

QIcon stencilIcon(const QString &iconPath, const QColor &color) {
  return QIcon(new TintableSvgIconEngine(iconPath, color));
}
}

IconBar::IconBar(QWidget *parent) : QWidget(parent) {
  setLayout(new QHBoxLayout());
  layout()->setContentsMargins(0, 0, 0, 0);
  setupControls();
}

void IconBar::setupControls() {
  const bool darkPalette = isDarkPalette(palette());
  const QColor hoverFill = darkPalette ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 12);
  const QColor pressedFill = darkPalette ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 20);
  const QString buttonStyle = QStringLiteral(
      "QToolButton {"
      " border: none;"
      " background: transparent;"
      " border-radius: 6px;"
      " padding: 0px;"
      " margin: 0px;"
      "}"
      "QToolButton:hover { background: %1; }"
      "QToolButton:pressed { background: %2; }")
                                  .arg(cssColor(hoverFill))
                                  .arg(cssColor(pressedFill));

  m_play = new QToolButton();
  m_play->setAutoRaise(true);
  m_play->setToolButtonStyle(Qt::ToolButtonIconOnly);
  m_play->setFocusPolicy(Qt::NoFocus);
  m_play->setCursor(Qt::ArrowCursor);
  m_play->setFixedSize(kTransportButtonSize, kTransportButtonSize);
  m_play->setIconSize(QSize(kTransportIconSize, kTransportIconSize));
  m_play->setStyleSheet(buttonStyle);
  m_play->setIcon(stencilIcon(QStringLiteral(":/icons/play.svg"), QColor(QStringLiteral("#2fbf71"))));
  m_play->setDisabled(true);
  m_play->setToolTip("Play selected collection (Space)");
  m_play->setWhatsThis("Select a collection in the panel above and click this \u25b6 button or press 'Space' to play it.\n"
                       "Clicking the button again will pause playback or play a different collection "
                       "if you have changed the selection.");
  connect(m_play, &QToolButton::pressed, this, &IconBar::playToggle);
  layout()->addWidget(m_play);

  m_stop = new QToolButton();
  m_stop->setAutoRaise(true);
  m_stop->setToolButtonStyle(Qt::ToolButtonIconOnly);
  m_stop->setFocusPolicy(Qt::NoFocus);
  m_stop->setCursor(Qt::ArrowCursor);
  m_stop->setFixedSize(kTransportButtonSize, kTransportButtonSize);
  m_stop->setIconSize(QSize(kTransportIconSize, kTransportIconSize));
  m_stop->setStyleSheet(buttonStyle);
  m_stop->setIcon(stencilIcon(QStringLiteral(":/icons/stop.svg"), QColor(QStringLiteral("#d86b6b"))));
  m_stop->setDisabled(true);
  m_stop->setToolTip("Stop playback (Esc)");
  connect(m_stop, &QToolButton::pressed, this, &IconBar::stopPressed);
  layout()->addWidget(m_stop);

  m_slider = new SeekBar();
  /* Needed to make sure the slider is properly rendered */
  m_slider->setRange(0, 1);
  m_slider->setValue(0);
  m_slider->setFixedHeight(kTransportControlHeight);
  
  m_slider->setEnabled(false);
  m_slider->setToolTip("Seek");
  connect(m_slider, &SeekBar::sliderMoved, [this](int value) {
    seekingTo(value, PositionChangeOrigin::SeekBar);
  });
  connect(m_slider, &SeekBar::sliderReleased, [this]() {
    seekingTo(m_slider->value(), PositionChangeOrigin::SeekBar);
  });
  layout()->addWidget(m_slider);

  connect(NotificationCenter::the(), &NotificationCenter::vgmCollSelected, this,
          [this](VGMColl *coll, QWidget *) {
            m_play->setEnabled(coll != nullptr);
            playerStatusChanged(SequencePlayer::the().playing());
          });
  connect(&SequencePlayer::the(), &SequencePlayer::statusChange, this, &IconBar::playerStatusChanged);
  connect(&SequencePlayer::the(), &SequencePlayer::playbackPositionChanged, this, &IconBar::playbackRangeUpdate);
  playerStatusChanged(SequencePlayer::the().playing());
}

void IconBar::showPlayInfo() {
  QWhatsThis::showText(m_play->mapToGlobal(m_play->pos()), m_play->whatsThis(), this);
  m_play->clearFocus();
}

void IconBar::playbackRangeUpdate(int cur, int max, PositionChangeOrigin origin) {
  const int previousMaximum = m_slider->maximum();
  const bool rangeChanged = max != previousMaximum;
  const bool forceImmediateUpdate = cur == m_slider->minimum() || rangeChanged;

  if (rangeChanged) {
    m_slider->setRange(0, max);
  }

  if (m_slider->isSliderDown()) {
    m_skipNextPlaybackSliderUpdate = false;
    return;
  }

  if (origin == PositionChangeOrigin::Playback && !forceImmediateUpdate) {
    m_skipNextPlaybackSliderUpdate = !m_skipNextPlaybackSliderUpdate;
    if (m_skipNextPlaybackSliderUpdate) {
      return;
    }
  } else {
    m_skipNextPlaybackSliderUpdate = false;
  }

  m_slider->setValue(cur);
}

void IconBar::playerStatusChanged(bool playing) {
  m_skipNextPlaybackSliderUpdate = false;
  const bool hasActive = SequencePlayer::the().activeCollection() != nullptr;
  const bool canPlay = m_play->isEnabled();

  QColor playColor(QStringLiteral("#2fbf71"));
  // playColor.setAlpha(playing ? 210 : (canPlay ? 210 : 120));
  playColor.setAlpha(playing || canPlay ? 210 : 120);
  m_play->setIcon(stencilIcon(playing ? QStringLiteral(":/icons/pause.svg")
                                      : QStringLiteral(":/icons/play.svg"),
                              playColor));

  QColor stopColor(QStringLiteral("#d86b6b"));
  m_stop->setEnabled(hasActive);
  // stopColor.setAlpha(m_stop->isEnabled() ? (playing ? 210 : 210) : 120);
  stopColor.setAlpha(m_stop->isEnabled() && playing ? 210 : 120);
  m_stop->setIcon(stencilIcon(QStringLiteral(":/icons/stop.svg"), stopColor));
  m_slider->setEnabled(hasActive);
}
