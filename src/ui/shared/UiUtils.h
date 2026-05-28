#pragma once
#include "ui/shell/Theme.h"

#include <QColor>
#include <QLayout>
#include <QLayoutItem>
#include <QString>

namespace dolphin::ui {

// Returns a stylesheet for a QPushButton colour-picker swatch (settable background
// with hover border). Both border colours come from Theme tokens so the swatch
// integrates with the dark-panel style without any per-site customisation.
inline QString colorPickerSheet(const QColor& c)
{
    return QString(
        "QPushButton { background: %1; border: 1px solid %2; border-radius: %3px; }"
        "QPushButton:hover { border: 1px solid %4; }"
    ).arg(c.name(),
         QLatin1String(Theme::kBorder),
         QString::number(Theme::kRadius1),
         QLatin1String(Theme::kTextSubtle));
}

// Returns a stylesheet for a small colour-indicator swatch (QToolButton / QLabel).
inline QString colorSwatchSheet(const QColor& c)
{
    return QString("background:%1; border:1px solid %2;")
        .arg(c.name(), QLatin1String(Theme::kTextMuted));
}

inline void clearLayout(QLayout* layout)
{
    while (layout->count() > 0) {
        QLayoutItem* item = layout->takeAt(0);
        if (item->widget())      item->widget()->deleteLater();
        else if (item->layout()) clearLayout(item->layout());
        delete item;
    }
}

} // namespace dolphin::ui
