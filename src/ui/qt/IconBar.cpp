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
#include "UIHelpers.h"

namespace {
constexpr int kTransportControlHeight = 32;
constexpr int kTransportButtonSize = 32;
constexpr int kTransportIconSize = 28;
constexpr int kInactiveTransportIconAlpha = 120;
const QColor kDarkPlayColor(0x2f, 0xbf, 0x71);
const QColor kLightPlayColor(0x24, 0x96, 0x59);
const QColor kDarkStopColor(0xd8, 0x6b, 0x6b);
const QColor kLightStopColor(0xb8, 0x4f, 0x4f);

QIcon gradientTransportIcon(const QString &iconPath, QColor baseColor) {
  const int alpha = baseColor.alpha();
  QColor startColor = blendColors(baseColor, QColor(Qt::white), 0.8);
  QColor endColor = blendColors(baseColor, QColor(Qt::black), 0.7);
  startColor.setAlpha(alpha);
  endColor.setAlpha(alpha);
  return gradientStencilSvgIcon(iconPath, startColor, endColor);
}
}

IconBar::IconBar(QWidget *parent) : QWidget(parent) {
  setLayout(new QHBoxLayout());
  layout()->setContentsMargins(0, 0, 0, 0);
  setupControls();
}

void IconBar::setupControls() {
  const bool darkPalette = isDarkPalette(palette());
  const QColor playColor = darkPalette ? kDarkPlayColor : kLightPlayColor;
  const QColor stopColor = darkPalette ? kDarkStopColor : kLightStopColor;
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

  const auto makeButton = [this, &buttonStyle](const QString &iconPath, const QString &toolTip,
                                               const QColor &color) {
    auto *button = new QToolButton(this);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::ArrowCursor);
    button->setFixedSize(kTransportButtonSize, kTransportButtonSize);
    button->setIconSize(QSize(kTransportIconSize, kTransportIconSize));
    button->setStyleSheet(buttonStyle);
    button->setIcon(gradientTransportIcon(iconPath, color));
    button->setToolTip(toolTip);
    return button;
  };

  m_play = makeButton(QStringLiteral(":/icons/play.svg"), QStringLiteral("Play selected collection (Space)"), playColor);
  m_play->setEnabled(false);
  m_play->setWhatsThis("Select a collection in the panel above and click this \u25b6 button or press 'Space' to play it.\n"
                       "Clicking the button again will pause playback or play a different collection "
                       "if you have changed the selection.");
  connect(m_play, &QToolButton::pressed, this, &IconBar::playToggle);
  layout()->addWidget(m_play);

  m_stop = makeButton(QStringLiteral(":/icons/stop.svg"), QStringLiteral("Stop playback (Esc)"), stopColor);
  m_stop->setEnabled(false);
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
  const bool darkPalette = isDarkPalette(palette());

  QColor playColor = darkPalette ? kDarkPlayColor : kLightPlayColor;
  if (!(playing || canPlay)) {
    playColor.setAlpha(kInactiveTransportIconAlpha);
  }
  m_play->setIcon(gradientTransportIcon(playing ? QStringLiteral(":/icons/pause.svg")
                                                : QStringLiteral(":/icons/play.svg"),
                                        playColor));

  QColor stopColor = darkPalette ? kDarkStopColor : kLightStopColor;
  m_stop->setEnabled(hasActive);
  if (!m_stop->isEnabled()) {
    stopColor.setAlpha(kInactiveTransportIconAlpha);
  }
  m_stop->setIcon(gradientTransportIcon(QStringLiteral(":/icons/stop.svg"), stopColor));
  m_slider->setEnabled(hasActive);
}
