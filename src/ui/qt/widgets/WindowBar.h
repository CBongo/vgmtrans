/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include <QList>
#include <QPointer>
#include <QString>
#include <QWidget>

class QAbstractButton;
class QAction;
class QToolButton;

class WindowBar final : public QWidget {
  Q_OBJECT

public:
  struct ToggleButtonSpec {
    QAction *action{};
    QString iconPath;
  };

  explicit WindowBar(QWidget *parent = nullptr);

  QWidget *centerWidget() const;
  void setCenterWidget(QWidget *widget);
  void setLeadingToggleButtons(const QList<ToggleButtonSpec> &buttons);
  QWidget *leadingControls() const;
  QWidget *systemButtonArea() const;
  QAbstractButton *minimizeButton() const;
  QAbstractButton *maximizeButton() const;
  QAbstractButton *closeButton() const;

protected:
  void changeEvent(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
  void showEvent(QShowEvent *event) override;

private:
  void applyLeadingButtonStyle(QToolButton *button) const;
  void attachToTopLevelWindow();
  QToolButton *createWindowButton(const QString& toolTip);
  void refreshLeadingToggleButtonIcons();
  void updateMacTrailingSpacerWidth();
  void syncWindowButtons();

  struct LeadingToggleButton {
    QToolButton *button{};
    QAction *action{};
    QString iconPath;
  };

  class QHBoxLayout *m_layout{};
  QWidget *m_centerPlaceholder{};
  QWidget *m_centerWidget{};
  QWidget *m_leadingControls{};
  QWidget *m_macTrailingSpacer{};
  QWidget *m_systemButtonArea{};
  QToolButton *m_minimizeButton{};
  QToolButton *m_maximizeButton{};
  QToolButton *m_closeButton{};
  QPointer<QWidget> m_trackedWindow;
  QList<LeadingToggleButton> m_leadingToggleButtons;
};
