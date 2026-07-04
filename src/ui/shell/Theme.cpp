#include "ui/shell/Theme.h"

#include <QColor>
#include <QFile>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace dolphin::ui::Theme {

namespace {
Mode s_mode = Mode::Dark;

// Dark value from the k* constants; light value mirrors AppStyle's token table.
QColor mc(const char* dark, const char* light)
{
    return QColor(QLatin1String(s_mode == Mode::Light ? light : dark));
}
} // namespace

Mode mode()           { return s_mode; }
void setMode(Mode m)  { s_mode = m; }

QColor textPrimaryColor()  { return mc(kTextPrimary,  "#1c1c1e"); }
QColor textSecondColor()   { return mc(kTextSecond,   "#2c2c2e"); }
QColor textSubtleColor()   { return mc(kTextSubtle,   "#54545a"); }
QColor textMutedColor()    { return mc(kTextMuted,    "#66666c"); }
QColor textSoftColor()     { return mc(kTextSoft,     "#46464c"); }
QColor textDimColor()      { return mc(kTextDim,      "#84909c"); }
QColor textDisabledColor() { return mc(kTextDisabled, "#b8b8bd"); }
QColor iconStrokeColor()   { return mc(kIconStroke,   "#3c3c42"); }
QColor borderColor()       { return mc(kBorder,       "#d6d6da"); }
QColor borderMenuColor()   { return mc(kBorderMenu,   "#c8c8cd"); }
QColor bgColor()           { return mc(kBg,           "#ececee"); }
QColor bgElevatedColor()   { return mc(kBgElevated,   "#f7f7f9"); }
QColor bgPanelColor()      { return mc(kBgPanel,      "#f2f2f4"); }
QColor bgCardColor()       { return mc(kBgCard,       "#ffffff"); }

QIcon icon(const QString& resource)
{
    // One cache per mode: widgets created after a theme switch pick up the
    // right tint, and repeated loads are free.
    static QHash<QString, QIcon> cache[2];
    auto& c = cache[s_mode == Mode::Light ? 1 : 0];
    if (const auto it = c.constFind(resource); it != c.constEnd())
        return *it;

    QIcon result;
    if (s_mode == Mode::Light && resource.endsWith(QLatin1String(".svg"))) {
        // Re-tint the dark-theme stroke for light surfaces. Icons that use
        // their own colours are unaffected (the replace is a no-op).
        QFile f(resource);
        if (f.open(QIODevice::ReadOnly)) {
            QByteArray svg = f.readAll();
            svg.replace("#aeaeb2", "#3c3c42");
            QSvgRenderer renderer(svg);
            if (renderer.isValid()) {
                for (const int side : { 16, 20, 24, 32, 48 }) {
                    QPixmap pm(side * 2, side * 2);
                    pm.fill(Qt::transparent);
                    QPainter p(&pm);
                    renderer.render(&p);
                    p.end();
                    pm.setDevicePixelRatio(2.0);
                    result.addPixmap(pm);
                }
            }
        }
    }
    if (result.isNull())
        result = QIcon(resource);

    c.insert(resource, result);
    return result;
}

} // namespace dolphin::ui::Theme
