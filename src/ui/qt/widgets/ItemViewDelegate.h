/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QHeaderView>
#include <QListView>
#include <QStyledItemDelegate>
#include <QTableView>

#include "Metrics.h"

class ItemViewDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  static constexpr int itemHeight() noexcept {
    return Size::ItemViewRow;
  }

  static constexpr int itemSpacing() noexcept {
    return Spacing::ItemView;
  }

  static void apply(QListView *view) {
    if (!view) {
      return;
    }

    view->setSpacing(itemSpacing());
  }

  static void apply(QTableView *view) {
    if (!view) {
      return;
    }

    view->verticalHeader()->setDefaultSectionSize(itemHeight());
  }

protected:
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
    QSize hint = QStyledItemDelegate::sizeHint(option, index);
    hint.setHeight(itemHeight());
    return hint;
  }
};
