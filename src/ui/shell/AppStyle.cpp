// AppStyle.cpp — global application stylesheet
//
// ALL visual styling lives here. Widget files set objectNames; this file owns
// the CSS rules. No per-widget setStyleSheet() calls are needed for anything
// covered below. Exceptions: field color button in metadata windows
// (user-chosen chart color). AI status is driven by dynamic QSS properties.
//
// Color and font tokens are defined in Theme.h and injected at runtime via
// applyTokens(). Use @tokenName inside the CSS template — see the token table
// in applyTokens() below.  Never hard-code hex values or font names directly
// in the CSS strings; change Theme.h instead.
//
// CSS is split across AppStyleBase/Shell/Panels/Dialogs/Waterfall/NodeGraph.cpp.
// Each defines one detail::qssXxx() function; sheet() concatenates then tokenises.

#include "ui/shell/AppStyle.h"
#include "ui/shell/AppStylePrivate.h"
#include "ui/shell/Theme.h"
#include "ui/shell/WindowChrome.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QString>
#include <QWidget>

namespace dolphin::ui {

namespace {

// Replace @token placeholders in the CSS template with values from Theme.h.
// Order matters: longer tokens must come before any prefix they share
// (e.g. @borderMenu before @border, @accentHover before @accent).
QString applyTokens(QString css)
{
    using namespace Theme;
    const bool light = (Theme::mode() == Theme::Mode::Light);

    // Mode-dependent colour: dark value comes from Theme.h (source of truth),
    // light value is its counterpart. Non-colour tokens ignore the mode.
    auto C = [light](const char* dark, const char* light_v) {
        return QLatin1String(light ? light_v : dark);
    };

    // Font family and size compatibility.
    //
    // Explicit @fontXxx sizes were historically ignored because @font was
    // substituted first. The established UI was tuned around inherited font
    // sizes, so remove those declarations intentionally instead of emitting
    // malformed QSS. Enabling them is an app-wide visual-design change.
    constexpr bool kUseExplicitFontSizes = false;
    if constexpr (kUseExplicitFontSizes) {
        // Longer tokens must precede the @font family prefix.
        css.replace(QLatin1String("@fontBase"), QLatin1String(kFontBase));
        css.replace(QLatin1String("@fontXxs"),  QLatin1String(kFontXxs));
        css.replace(QLatin1String("@fontXs"),   QLatin1String(kFontXs));
        css.replace(QLatin1String("@fontSm"),   QLatin1String(kFontSm));
        css.replace(QLatin1String("@fontMd"),   QLatin1String(kFontMd));
        css.replace(QLatin1String("@fontLg"),   QLatin1String(kFontLg));
    } else {
        css.replace(QLatin1String("font-size: @fontBase;"), QString{});
        css.replace(QLatin1String("font-size: @fontXxs;"),  QString{});
        css.replace(QLatin1String("font-size: @fontXs;"),   QString{});
        css.replace(QLatin1String("font-size: @fontSm;"),   QString{});
        css.replace(QLatin1String("font-size: @fontMd;"),   QString{});
        css.replace(QLatin1String("font-size: @fontLg;"),   QString{});
    }
    css.replace(QLatin1String("@font"), QLatin1String(kFontFamily));
    // Backgrounds — longer tokens before shorter prefixes (@bgHover before @bg)
    css.replace(QLatin1String("@bgPanel"),        C(kBgPanel,     "#f2f2f4"));
    css.replace(QLatin1String("@bgCard"),         C(kBgCard,      "#ffffff"));
    css.replace(QLatin1String("@bgHover"),        C(kBgHover,     "#e2e2e6"));
    css.replace(QLatin1String("@bgEl"),           C(kBgElevated,  "#f7f7f9"));
    css.replace(QLatin1String("@bg"),             C(kBg,          "#ececee"));
    // Borders
    css.replace(QLatin1String("@borderMenu"),     C(kBorderMenu,  "#c8c8cd"));
    css.replace(QLatin1String("@border"),         C(kBorder,      "#d6d6da"));
    // Accent — RGB channels before named tokens to prevent partial matches
    css.replace(QLatin1String("@accentRgb"),      C(kAccentRgb,   "0,122,255"));
    css.replace(QLatin1String("@accentSoft"),     C(kAccentSoft,  "#2f7ddb"));
    css.replace(QLatin1String("@accentHover"),    C(kAccentHover, "#268fff"));
    css.replace(QLatin1String("@accentPress"),    C(kAccentPressed, "#0063cf"));
    css.replace(QLatin1String("@accent"),         C(kAccent,      "#007aff"));
    // Text
    css.replace(QLatin1String("@textPrimary"),    C(kTextPrimary,  "#1c1c1e"));
    css.replace(QLatin1String("@textSecond"),     C(kTextSecond,   "#2c2c2e"));
    css.replace(QLatin1String("@textSubtle"),     C(kTextSubtle,   "#54545a"));
    css.replace(QLatin1String("@textMuted"),      C(kTextMuted,    "#66666c"));
    css.replace(QLatin1String("@textSoft"),       C(kTextSoft,     "#46464c"));
    css.replace(QLatin1String("@textDim"),        C(kTextDim,      "#84909c"));
    css.replace(QLatin1String("@textDisabled"),   C(kTextDisabled, "#b8b8bd"));
    // Icons
    css.replace(QLatin1String("@iconStroke"),     C(kIconStroke,   "#3c3c42"));
    // Semantic — RGB channels before named tokens
    css.replace(QLatin1String("@successRgb"),     QLatin1String(kSuccessRgb));
    css.replace(QLatin1String("@dangerRgb"),      QLatin1String(kDangerRgb));
    css.replace(QLatin1String("@cautionRgb"),     QLatin1String(kCautionRgb));
    css.replace(QLatin1String("@success"),        C(kSuccess, "#2e7d32"));
    // dangerBright before danger to avoid prefix collision
    css.replace(QLatin1String("@dangerBrightHov"), C(kDangerBrightHover, "#ff3b30"));
    css.replace(QLatin1String("@dangerBright"),    C(kDangerBright,      "#d70015"));
    css.replace(QLatin1String("@danger"),          QLatin1String(kDanger));
    css.replace(QLatin1String("@warning"),         C(kWarning, "#9a6a00"));
    css.replace(QLatin1String("@caution"),         C(kCaution, "#a05f00"));
    css.replace(QLatin1String("@white"),           QLatin1String(kWhite));
    // AI integration accent colors — provider-to-role mapping is in MainStatusBar.cpp
    css.replace(QLatin1String("@aiProviderPrimary"),   QLatin1String(kAiProviderPrimary));
    css.replace(QLatin1String("@aiIntegration"),       QLatin1String(kAiIntegrationAccent));
    // Interaction overrides
    css.replace(QLatin1String("@closeBtnHover"),      QLatin1String(kCloseBtnHover));
    // Border radius — longer names first to avoid prefix collisions
    css.replace(QLatin1String("@radius3"), QLatin1String(kRadius3Px));
    css.replace(QLatin1String("@radius2"), QLatin1String(kRadius2Px));
    css.replace(QLatin1String("@radius1"), QLatin1String(kRadius1Px));

    // Component dimensions — single source of truth for sizes shared by C++ layout
    // code and QSS (e.g. the format badge: setFixedSize(kFormatBadgeSize) + the
    // QLabel#formatBadge min/max below both resolve from this one token).
    css.replace(QLatin1String("@badgeSize"),
                QString::number(kFormatBadgeSize) + QLatin1String("px"));
    // White overlay surfaces (rgba, dark-UI convention) — semantic tier:
    //   @overlayEl   = element resting surface (inputs, combos, buttons)
    //   @overlayHov  = hover feedback (dominant: 28 occurrences)
    //   @overlayMut  = muted feedback (pressed states, softer hover)
    css.replace(QLatin1String("@overlayEl"),  C("rgba(255,255,255,0.06)", "rgba(0,0,0,0.05)"));
    css.replace(QLatin1String("@overlayHov"), C("rgba(255,255,255,0.08)", "rgba(0,0,0,0.07)"));
    css.replace(QLatin1String("@overlayMut"), C("rgba(255,255,255,0.05)", "rgba(0,0,0,0.04)"));
    // Padding scale — maps to kSpacing1/kSpacing2 where they align
    css.replace(QLatin1String("@padLg"),  QLatin1String("8px"));
    css.replace(QLatin1String("@padMd"),  QLatin1String("6px"));
    css.replace(QLatin1String("@padSm"),  QLatin1String("4px"));
    css.replace(QLatin1String("@padXs"),  QLatin1String("2px"));

    if (light) {
        // The QSS segments still carry hardcoded white-alpha literals (hover
        // borders, subtle fills) from the dark-first design. On light surfaces
        // the same overlay must be black-based — flip them wholesale. Black
        // literals (shadows) stay black in both modes.
        css.replace(QLatin1String("rgba(255,255,255,"), QLatin1String("rgba(0,0,0,"));
        // QSS-referenced glyph images are authored near-white for dark mode —
        // swap in the dark-stroke variants so spin/combo arrows stay visible.
        css.replace(QLatin1String(":/icons/spin_up.svg"),
                    QLatin1String(":/icons/spin_up_light.svg"));
        css.replace(QLatin1String(":/icons/spin_down.svg"),
                    QLatin1String(":/icons/spin_down_light.svg"));
    }
    return css;
}

} // anonymous namespace

QString AppStyle::sheet()
{
    using namespace detail;
    return applyTokens(
        qssBase() + qssShell() + qssPanels() +
        qssDialogs() + qssAcousticViews() + qssNodeGraph()
    );
}

void AppStyle::apply(Theme::Mode mode)
{
    Theme::setMode(mode);

    QPalette pal;
    if (mode == Theme::Mode::Dark) {
        // macOS system dark (moved here from main.cpp — one styling authority).
        pal.setColor(QPalette::Window,          QColor(0x1c, 0x1c, 0x1e));
        pal.setColor(QPalette::WindowText,      QColor(0xf2, 0xf2, 0xf7));
        pal.setColor(QPalette::Base,            QColor(0x1c, 0x1c, 0x1e));
        pal.setColor(QPalette::AlternateBase,   QColor(0x28, 0x28, 0x2a));
        pal.setColor(QPalette::ToolTipBase,     QColor(0x2c, 0x2c, 0x2e));
        pal.setColor(QPalette::ToolTipText,     QColor(0xf2, 0xf2, 0xf7));
        pal.setColor(QPalette::Text,            QColor(0xf2, 0xf2, 0xf7));
        pal.setColor(QPalette::Button,          QColor(0x2c, 0x2c, 0x2e));
        pal.setColor(QPalette::ButtonText,      QColor(0xf2, 0xf2, 0xf7));
        pal.setColor(QPalette::BrightText,      Qt::white);
        pal.setColor(QPalette::Link,            QColor(0x0a, 0x84, 0xff));
        pal.setColor(QPalette::Highlight,       QColor(0x0a, 0x84, 0xff));
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::Mid,             QColor(0x38, 0x38, 0x3a));
        pal.setColor(QPalette::Dark,            QColor(0x11, 0x11, 0x13));
        pal.setColor(QPalette::Shadow,          QColor(0x00, 0x00, 0x00));
    } else {
        // macOS system light — counterpart values to the token table above.
        pal.setColor(QPalette::Window,          QColor(0xec, 0xec, 0xee));
        pal.setColor(QPalette::WindowText,      QColor(0x1c, 0x1c, 0x1e));
        pal.setColor(QPalette::Base,            QColor(0xff, 0xff, 0xff));
        pal.setColor(QPalette::AlternateBase,   QColor(0xf2, 0xf2, 0xf4));
        pal.setColor(QPalette::ToolTipBase,     QColor(0xff, 0xff, 0xff));
        pal.setColor(QPalette::ToolTipText,     QColor(0x1c, 0x1c, 0x1e));
        pal.setColor(QPalette::Text,            QColor(0x1c, 0x1c, 0x1e));
        pal.setColor(QPalette::Button,          QColor(0xf7, 0xf7, 0xf9));
        pal.setColor(QPalette::ButtonText,      QColor(0x1c, 0x1c, 0x1e));
        pal.setColor(QPalette::BrightText,      Qt::black);
        pal.setColor(QPalette::Link,            QColor(0x00, 0x7a, 0xff));
        pal.setColor(QPalette::Highlight,       QColor(0x00, 0x7a, 0xff));
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::Mid,             QColor(0xc8, 0xc8, 0xcd));
        pal.setColor(QPalette::Dark,            QColor(0xd6, 0xd6, 0xda));
        pal.setColor(QPalette::Shadow,          QColor(0xa0, 0xa0, 0xa8));
    }
    qApp->setPalette(pal);
    qApp->setStyleSheet(sheet());

    // Native window frames (viewers, dialogs) follow the theme too.
    for (QWidget* w : QApplication::topLevelWidgets())
        applyDarkTitleBar(w);
}

} // namespace dolphin::ui
