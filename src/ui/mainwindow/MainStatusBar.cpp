// MainStatusBar.cpp — modular status bar implementation.
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/shared/CoordFormat.h"
#include "ui/shell/Theme.h"
#include "render/sonar/SSSPalette.h"
#include "render/sonar/SonarDisplayParams.h"   // PaletteIndex::Count

#include <QSpinBox>
#include <QSignalBlocker>
#include <QDoubleSpinBox>
#include <QValidator>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QStyleFactory>
#include <QTimer>
#include <QWidget>
#include <cmath>

namespace dolphin::ui {

// Physical metres-per-pixel at 96 dpi (maps display ratio 1:n to real-world scale).
static constexpr double kPhysicalMpp = 0.0254 / 96.0;

// Stores mpp internally; displays as "1:n" map scale via textFromValue.
// Arrow steps halve/double mpp logarithmically — precision-lossless round trip.
class ScaleSpinBox : public QDoubleSpinBox {
public:
    explicit ScaleSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {}

    QString textFromValue(double mpp) const override {
        if (mpp <= 0.0 || std::isnan(mpp)) return QStringLiteral("1:--");
        const qint64 n = qRound64(mpp / kPhysicalMpp);
        return n >= 1 ? QStringLiteral("1:") + QString::number(n)
                      : QStringLiteral("1:1");
    }

    double valueFromText(const QString& text) const override {
        QString s = text.trimmed();
        if (s.startsWith(QStringLiteral("1:"))) s = s.mid(2).trimmed();
        bool ok = false;
        const double ratio = s.toDouble(&ok);
        return (ok && ratio > 0.0) ? ratio * kPhysicalMpp : value();
    }

    QValidator::State validate(QString& text, int& /*pos*/) const override {
        const QString inner = text.startsWith(QStringLiteral("1:"))
                              ? text.mid(2).trimmed() : text.trimmed();
        if (inner.isEmpty()) return QValidator::Intermediate;
        bool ok = false;
        const double v = inner.toDouble(&ok);
        if (!ok)          return QValidator::Intermediate;
        return v > 0.0    ? QValidator::Acceptable : QValidator::Intermediate;
    }

    void stepBy(int steps) override {
        // ▲ zooms in (smaller mpp = more detail); negate so upward → factor < 1.
        const double factor = std::pow(2.0, -steps);
        setValue(std::clamp(value() * factor, minimum(), maximum()));
    }
};

// Map-palette picker styled like the Scale/Rotation spin boxes: up/down arrows step
// through palettes (wrapping), and the field shows the palette name instead of a number.
class PaletteSpinBox : public QSpinBox {
public:
    explicit PaletteSpinBox(QWidget* parent = nullptr) : QSpinBox(parent) {
        setRange(0, PaletteIndex::Count - 1);
        setWrapping(true);                       // ▲/▼ cycle through all palettes
        if (lineEdit()) lineEdit()->setReadOnly(true);  // arrows/scroll only — no typing
    }
    QString textFromValue(int v) const override {
        return (v >= 0 && v < PaletteIndex::Count)
            ? QString::fromLatin1(SSSPalette::name(v)) : QString();
    }
    int valueFromText(const QString&) const override { return value(); }
};

static constexpr int kAiDotSz = 7;  // AI status indicator dot size

MainStatusBar::MainStatusBar(QWidget* parent)
    : QStatusBar(parent)
{
    // -- Progress bar ----------------------------------------------------------
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setFixedWidth(Theme::kMainProgressBarW);
    m_progress->setFixedHeight(Theme::kProgressBarH);
    m_progress->setTextVisible(false);
    m_progress->setVisible(false);
    // Fusion style required for indeterminate animation on Windows (native style ignores QSS).
    m_progress->setStyle(QStyleFactory::create("Fusion"));

    // -- Busy label (persistent while a background load runs) ------------------
    m_busy = new QLabel(this);
    m_busy->setObjectName("statusChrome");
    m_busy->setVisible(false);

    // -- Context label (project name) ------------------------------------------
    m_context = new QLabel(tr("No project"), this);
    m_context->setObjectName("statusChrome");
    // Ignored horizontal policy: the label accepts whatever width the bar gives it
    // (down to 0) and clips, rather than forcing its full width and overlapping the
    // permanent fields on the right when the window is narrow.
    m_context->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    // -- Transient job message -------------------------------------------------
    m_job = new QLabel(this);
    m_job->setObjectName("statusChrome");

    m_job_timer = new QTimer(this);
    m_job_timer->setSingleShot(true);
    m_job_timer->setInterval(8000);
    connect(m_job_timer, &QTimer::timeout, this, [this]() { m_job->clear(); });

    // -- Field label + value box pairs -----------------------------------------
    auto makeFieldLabel = [this](const char* text) {
        auto* lbl = new QLabel(tr(text), this);
        lbl->setObjectName("statusFieldLabel");
        return lbl;
    };

    // Coordinate: plain QLabel — backward-compat with SidescanViewController.
    m_lbl_coord = makeFieldLabel("Coordinate");
    m_pos = new QLabel(this);
    m_pos->setObjectName("statusValueBox");
    m_pos->setText(QStringLiteral("--"));
    m_pos->setMinimumWidth(148);

    // Scale: stores mpp directly; displays as "1:n" via ScaleSpinBox::textFromValue.
    m_lbl_scale  = makeFieldLabel("Scale");
    m_spin_scale = new ScaleSpinBox(this);
    m_spin_scale->setObjectName("statusSpinBox");
    m_spin_scale->setDecimals(15);                           // full mpp precision
    // Lower bound: 1/100th of physical pixel — covers the map's internal 1e8 zoom limit.
    // Upper bound: ~500 million:1 physical ratio, covering global overview scales.
    m_spin_scale->setRange(kPhysicalMpp * 0.01, kPhysicalMpp * 5e8);
    m_spin_scale->setSingleStep(kPhysicalMpp);
    m_spin_scale->setMinimumWidth(108);
    m_spin_scale->setToolTip(tr("Map scale — type 1:n or use arrows to zoom"));
    connect(m_spin_scale, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double mpp) {
        emit scaleChangeRequested(mpp);
    });
    // Start at a mid-range scale so UP/DOWN arrows both work before a project loads.
    m_spin_scale->blockSignals(true);
    m_spin_scale->setValue(kPhysicalMpp * 50000.0);   // "1:50000" — typical chart scale
    m_spin_scale->blockSignals(false);

    // Rotation: QDoubleSpinBox — degrees, display-only for 2D (no map rotation yet).
    m_lbl_rot  = makeFieldLabel("Rotation");
    m_spin_rot = new QDoubleSpinBox(this);
    m_spin_rot->setObjectName("statusSpinBox");
    m_spin_rot->setSuffix(QStringLiteral(" \xc2\xb0"));  // " °"
    m_spin_rot->setDecimals(1);
    m_spin_rot->setRange(0.0, 360.0);
    m_spin_rot->setSingleStep(0.5);
    m_spin_rot->setWrapping(true);
    m_spin_rot->setMinimumWidth(72);
    m_spin_rot->setToolTip(tr("Map rotation — type a value or use arrows to rotate"));
    connect(m_spin_rot, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double deg) {
        emit rotationChangeRequested(deg);
    });

    // Force visible text + arrow colors in dark theme via palette override.
    // QSS `color:` alone doesn't reliably propagate to the internal QLineEdit on Windows.
    auto applySpinPalette = [](QAbstractSpinBox* spin) {
        QPalette pal = spin->palette();
        const QColor text(0xf2, 0xf2, 0xf7);   // @textPrimary
        const QColor bg  (0x2c, 0x2c, 0x2e);   // @bgCard
        pal.setColor(QPalette::Base,                      bg);
        pal.setColor(QPalette::Button,                    bg);
        pal.setColor(QPalette::Text,                      text);
        pal.setColor(QPalette::ButtonText,                text);
        pal.setColor(QPalette::Disabled, QPalette::Text,       text);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, text);
        spin->setPalette(pal);
    };
    applySpinPalette(m_spin_scale);
    applySpinPalette(m_spin_rot);

    // CRS: clickable badge — opens the Geodetic Settings dialog on click.
    m_lbl_crs = makeFieldLabel("\xe2\x8a\x99"); // ⊙ globe glyph
    m_vp_crs  = new QPushButton(QStringLiteral("--"), this);
    m_vp_crs->setObjectName("statusCrsBtn");
    m_vp_crs->setFlat(true);
    m_vp_crs->setMinimumWidth(80);
    m_vp_crs->setCursor(Qt::PointingHandCursor);
    m_vp_crs->setToolTip(tr("Click to open Geodetic Settings"));
    connect(m_vp_crs, &QPushButton::clicked, this, &MainStatusBar::crsClicked);

    // Map colour palette — spin-box picker (up/down arrows step palettes), styled to
    // match the Scale/Rotation fields. Lives in the status bar (in-window chrome), so
    // it never overlays the OpenGL viewport.
    m_lbl_palette = makeFieldLabel("Palette");
    m_palette     = new PaletteSpinBox(this);
    m_palette->setObjectName("statusSpinBox");
    m_palette->setMinimumWidth(124);   // fit longest palette names + arrows ("Greyscale")
    m_palette->setToolTip(tr("Map colour palette — use arrows to change"));
    connect(m_palette, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int idx) { emit paletteRequested(idx); });
    applySpinPalette(m_palette);   // match Scale/Rotation text + arrow colours

    // -- AI provider indicator -------------------------------------------------
    m_ai_widget = new QWidget(this);
    m_ai_widget->setObjectName("statusAiSection");
    auto* ai_layout = new QHBoxLayout(m_ai_widget);
    ai_layout->setContentsMargins(Theme::kSpacing1, 0, Theme::kSpacing1, 0);
    ai_layout->setSpacing(Theme::kSpacing1);

    m_ai_icon = new QLabel(m_ai_widget);
    m_ai_icon->setObjectName("aiIcon");

    m_ai_dot = new QLabel(m_ai_widget);
    m_ai_dot->setObjectName("aiDot");
    m_ai_dot->setFixedSize(kAiDotSz, kAiDotSz);

    ai_layout->addWidget(m_ai_icon);
    ai_layout->addWidget(m_ai_dot);
    m_ai_widget->hide();

    // -- Assemble left → right -------------------------------------------------
    addWidget(m_progress);
    addWidget(m_busy);
    addWidget(m_context, 1);
    addWidget(m_job);

    // Indicator pairs
    addPermanentWidget(m_lbl_coord);
    addPermanentWidget(m_pos);
    addPermanentWidget(m_lbl_scale);
    addPermanentWidget(m_spin_scale);
    addPermanentWidget(m_lbl_rot);
    addPermanentWidget(m_spin_rot);
    addPermanentWidget(m_lbl_palette);
    addPermanentWidget(m_palette);
    addPermanentWidget(m_lbl_crs);
    addPermanentWidget(m_vp_crs);

    addPermanentWidget(m_ai_widget);
}

// -- Map palette ---------------------------------------------------------------

void MainStatusBar::setMapPalette(int idx)
{
    if (!m_palette) return;
    const QSignalBlocker b(m_palette);   // sync only — don't re-emit paletteRequested
    if (idx >= m_palette->minimum() && idx <= m_palette->maximum())
        m_palette->setValue(idx);
}

// -- Context -------------------------------------------------------------------

void MainStatusBar::setProjectContext(const QString& project, const QString& layer)
{
    QString text = project;
    if (!layer.isEmpty())
        text += "  ·  " + layer;
    m_context->setText(text);
}

void MainStatusBar::clearContext()
{
    m_context->setText(tr("No project"));
}

// -- Job messages --------------------------------------------------------------

void MainStatusBar::showJobMessage(const QString& msg, int timeout_ms)
{
    m_job->setText(msg);
    m_job_timer->setInterval(timeout_ms);
    m_job_timer->start();
}

// -- Cursor data ---------------------------------------------------------------

void MainStatusBar::setCursorPosition(double lat_or_y, double lon_or_x, bool is_projected)
{
    m_pos->setText(formatPosition(lat_or_y, lon_or_x, is_projected));
}

void MainStatusBar::clearCursorPosition()
{
    m_pos->setText(QStringLiteral("--"));
}

void MainStatusBar::clearCursorData()
{
    m_pos->setText(QStringLiteral("--"));
}

// -- Progress ------------------------------------------------------------------

void MainStatusBar::setProgressIndeterminate()
{
    m_progress->setRange(0, 0);
    m_progress->setVisible(true);
}

void MainStatusBar::setProgress(int percent, bool visible)
{
    m_progress->setRange(0, 100);
    m_progress->setValue(percent);
    m_progress->setVisible(visible);
}

void MainStatusBar::setBusyText(const QString& text)
{
    if (!m_busy) return;
    m_busy->setText(text);
    m_busy->setVisible(!text.isEmpty());
}

void MainStatusBar::clearBusyText()
{
    if (!m_busy) return;
    m_busy->clear();
    m_busy->setVisible(false);
}

void MainStatusBar::hideProgress()
{
    m_progress->setRange(0, 100);
    m_progress->setVisible(false);
}

// -- Viewport info -------------------------------------------------------------

void MainStatusBar::setViewportInfo(double mpp, double rot_deg)
{
    // Block signals so updating the display doesn't trigger a scaleChangeRequested loop.
    m_spin_scale->blockSignals(true);
    if (mpp > 0.0 && !std::isnan(mpp))
        m_spin_scale->setValue(mpp);    // full precision; textFromValue handles display
    m_spin_scale->blockSignals(false);

    m_spin_rot->blockSignals(true);
    m_spin_rot->setValue(rot_deg);
    m_spin_rot->blockSignals(false);
}

void MainStatusBar::setViewCrs(const QString& crs_name)
{
    const QString label = crs_name.isEmpty() ? QStringLiteral("--") : crs_name;
    m_vp_crs->setText(label);
    m_vp_crs->setEnabled(!crs_name.isEmpty());  // grey out until a project sets a CRS
    m_vp_crs->setToolTip(crs_name.isEmpty()
        ? QString()
        : tr("Project working CRS (survey grid). The per-layer source CRS is "
             "shown in the inspector."));
}

// -- AI indicator --------------------------------------------------------------

void MainStatusBar::setAiProvider(AiProvider provider)
{
    if (m_ai_provider == provider) return;
    m_ai_provider = provider;
    rebuildAiSection();
}

void MainStatusBar::setAiStatus(AiStatus status)
{
    if (m_ai_status == status) return;
    m_ai_status = status;
    rebuildAiSection();
}

void MainStatusBar::rebuildAiSection()
{
    if (m_ai_provider == AiProvider::None) {
        m_ai_widget->hide();
        return;
    }

    if (m_ai_provider == AiProvider::Primary) {
        m_ai_icon->setText(QStringLiteral("✦"));   // ✦ BLACK FOUR POINTED STAR
        m_ai_icon->setProperty("aiProvider", QStringLiteral("primary"));
    } else {
        m_ai_icon->setText(QStringLiteral("⊚"));   // ⊚ CIRCLED RING OPERATOR
        m_ai_icon->setProperty("aiProvider", QStringLiteral("integration"));
    }
    m_ai_icon->setToolTip(tr("AI Assistant"));

    const char* status_str;
    const char* dot_tip;
    switch (m_ai_status) {
        case AiStatus::Offline: status_str = "offline"; dot_tip = QT_TR_NOOP("Offline"); break;
        case AiStatus::Ready:   status_str = "ready";   dot_tip = QT_TR_NOOP("Ready");   break;
        case AiStatus::Active:  status_str = "active";  dot_tip = QT_TR_NOOP("Active");  break;
    }
    m_ai_dot->setProperty("aiStatus", QLatin1String(status_str));
    m_ai_dot->setToolTip(tr(dot_tip));

    // Force QSS re-evaluation after dynamic property changes.
    auto repolish = [](QWidget* w) {
        w->style()->unpolish(w);
        w->style()->polish(w);
    };
    repolish(m_ai_icon);
    repolish(m_ai_dot);

    m_ai_widget->show();
}

} // namespace dolphin::ui
