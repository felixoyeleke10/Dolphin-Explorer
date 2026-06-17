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

#include <QString>

namespace dolphin::ui {

namespace {

// Replace @token placeholders in the CSS template with values from Theme.h.
// Order matters: longer tokens must come before any prefix they share
// (e.g. @borderMenu before @border, @accentHover before @accent).
QString applyTokens(QString css)
{
    using namespace Theme;
    // Font family
    css.replace(QLatin1String("@font"),           QLatin1String(kFontFamily));
    // Font sizes — longer tokens before shorter prefixes (@fontBase before @fontB, etc.)
    css.replace(QLatin1String("@fontBase"),       QLatin1String(kFontBase));
    css.replace(QLatin1String("@fontXxs"),        QLatin1String(kFontXxs));
    css.replace(QLatin1String("@fontXs"),         QLatin1String(kFontXs));
    css.replace(QLatin1String("@fontSm"),         QLatin1String(kFontSm));
    css.replace(QLatin1String("@fontMd"),         QLatin1String(kFontMd));
    css.replace(QLatin1String("@fontLg"),         QLatin1String(kFontLg));
    // Backgrounds — longer tokens before shorter prefixes (@bgHover before @bg)
    css.replace(QLatin1String("@bgPanel"),        QLatin1String(kBgPanel));
    css.replace(QLatin1String("@bgCard"),         QLatin1String(kBgCard));
    css.replace(QLatin1String("@bgHover"),        QLatin1String(kBgHover));
    css.replace(QLatin1String("@bgEl"),           QLatin1String(kBgElevated));
    css.replace(QLatin1String("@bg"),             QLatin1String(kBg));
    // Borders
    css.replace(QLatin1String("@borderMenu"),     QLatin1String(kBorderMenu));
    css.replace(QLatin1String("@border"),         QLatin1String(kBorder));
    // Accent — RGB channels before named tokens to prevent partial matches
    css.replace(QLatin1String("@accentRgb"),      QLatin1String(kAccentRgb));
    css.replace(QLatin1String("@accentSoft"),     QLatin1String(kAccentSoft));
    css.replace(QLatin1String("@accentHover"),    QLatin1String(kAccentHover));
    css.replace(QLatin1String("@accentPress"),    QLatin1String(kAccentPressed));
    css.replace(QLatin1String("@accent"),         QLatin1String(kAccent));
    // Text
    css.replace(QLatin1String("@textPrimary"),    QLatin1String(kTextPrimary));
    css.replace(QLatin1String("@textSecond"),     QLatin1String(kTextSecond));
    css.replace(QLatin1String("@textSubtle"),     QLatin1String(kTextSubtle));
    css.replace(QLatin1String("@textMuted"),      QLatin1String(kTextMuted));
    css.replace(QLatin1String("@textSoft"),       QLatin1String(kTextSoft));
    css.replace(QLatin1String("@textDim"),        QLatin1String(kTextDim));
    css.replace(QLatin1String("@textDisabled"),   QLatin1String(kTextDisabled));
    // Icons
    css.replace(QLatin1String("@iconStroke"),     QLatin1String(kIconStroke));
    // Semantic — RGB channels before named tokens
    css.replace(QLatin1String("@successRgb"),     QLatin1String(kSuccessRgb));
    css.replace(QLatin1String("@dangerRgb"),      QLatin1String(kDangerRgb));
    css.replace(QLatin1String("@cautionRgb"),     QLatin1String(kCautionRgb));
    css.replace(QLatin1String("@success"),            QLatin1String(kSuccess));
    // dangerBright before danger to avoid prefix collision
    css.replace(QLatin1String("@dangerBrightHov"),    QLatin1String(kDangerBrightHover));
    css.replace(QLatin1String("@dangerBright"),       QLatin1String(kDangerBright));
    css.replace(QLatin1String("@danger"),             QLatin1String(kDanger));
    css.replace(QLatin1String("@warning"),            QLatin1String(kWarning));
    css.replace(QLatin1String("@caution"),            QLatin1String(kCaution));
    css.replace(QLatin1String("@white"),              QLatin1String(kWhite));
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
    css.replace(QLatin1String("@overlayEl"),  QLatin1String("rgba(255,255,255,0.06)"));
    css.replace(QLatin1String("@overlayHov"), QLatin1String("rgba(255,255,255,0.08)"));
    css.replace(QLatin1String("@overlayMut"), QLatin1String("rgba(255,255,255,0.05)"));
    // Padding scale — maps to kSpacing1/kSpacing2 where they align
    css.replace(QLatin1String("@padLg"),  QLatin1String("8px"));
    css.replace(QLatin1String("@padMd"),  QLatin1String("6px"));
    css.replace(QLatin1String("@padSm"),  QLatin1String("4px"));
    css.replace(QLatin1String("@padXs"),  QLatin1String("2px"));
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

} // namespace dolphin::ui
