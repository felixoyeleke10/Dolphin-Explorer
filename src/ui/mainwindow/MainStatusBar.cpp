// MainStatusBar.cpp — modular status bar implementation.
#include "ui/mainwindow/MainStatusBar.h"
#include "ui/shared/CoordFormat.h"
#include "ui/shell/Theme.h"

#include <QDoubleSpinBox>
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

// Local spin box subclass: logarithmic step so ▲ zooms IN (smaller ratio = larger scale).
class ScaleSpinBox : public QDoubleSpinBox {
public:
    explicit ScaleSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {}
    void stepBy(int steps) override {
        // Each arrow click changes scale by a factor of 2 (halves/doubles ratio).
        // Negate steps so ▲ decreases the ratio (zoom in = more detail).
        const double factor = std::pow(2.0, -steps);
        setValue(std::clamp(value() * factor, minimum(), maximum()));
    }
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

    // -- Context label (project · active layer) --------------------------------
    m_context = new QLabel(tr("No project"), this);
    m_context->setObjectName("statusChrome");

    // -- Transient job message -------------------------------------------------
    m_job = new QLabel(this);
    m_job->setObjectName("statusChrome");

    m_job_timer = new QTimer(this);
    m_job_timer->setSingleShot(true);
    m_job_timer->setInterval(8000);
    connect(m_job_timer, &QTimer::timeout, this, [this]() { m_job->clear(); });

    // -- Sidescan / sonar cursor data (shown only when instrument is active) ----
    m_range = new QLabel(this);
    m_range->setObjectName("statusChrome");

    m_depth = new QLabel(this);
    m_depth->setObjectName("statusChrome");

    // -- QGIS-style field label + value box pairs ------------------------------
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

    // Scale: QDoubleSpinBox — "1:50000" format, controls map zoom.
    static constexpr double kPhysicalMpp = 0.0254 / 96.0;

    m_lbl_scale  = makeFieldLabel("Scale");
    m_spin_scale = new ScaleSpinBox(this);
    m_spin_scale->setObjectName("statusSpinBox");
    m_spin_scale->setPrefix(QStringLiteral("1:"));
    m_spin_scale->setDecimals(0);
    m_spin_scale->setRange(1.0, 1e8);
    m_spin_scale->setSingleStep(5000.0);
    m_spin_scale->setMinimumWidth(108);
    m_spin_scale->setToolTip(tr("Map scale — use arrows to zoom"));
    // Disable text input without disabling the arrows (setReadOnly would kill stepBy too).
    if (auto* le = m_spin_scale->findChild<QLineEdit*>())
        le->setReadOnly(true);
    connect(m_spin_scale, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double ratio) {
        static constexpr double kPhy = 0.0254 / 96.0;
        emit scaleChangeRequested(ratio * kPhy);
    });
    Q_UNUSED(kPhysicalMpp);

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
    m_spin_rot->setToolTip(tr("Map rotation — use arrows or type to rotate"));
    if (auto* le = m_spin_rot->findChild<QLineEdit*>())
        le->setReadOnly(true);  // arrows active; block direct text entry
    connect(m_spin_rot, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double deg) {
        emit rotationChangeRequested(deg);
    });

    // Force visible text + arrow colors in dark theme via palette override.
    // QSS `color:` alone doesn't reliably propagate to the internal QLineEdit on Windows.
    auto applySpinPalette = [](QDoubleSpinBox* spin) {
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
    addWidget(m_context, 1);
    addWidget(m_job);

    // QGIS-style indicator pairs
    addPermanentWidget(m_lbl_coord);
    addPermanentWidget(m_pos);
    addPermanentWidget(m_lbl_scale);
    addPermanentWidget(m_spin_scale);
    addPermanentWidget(m_lbl_rot);
    addPermanentWidget(m_spin_rot);
    addPermanentWidget(m_lbl_crs);
    addPermanentWidget(m_vp_crs);

    addPermanentWidget(m_ai_widget);
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

void MainStatusBar::setCursorRange(const QString& side_label, float metres)
{
    m_range->setText(QString("%1  %2 m").arg(side_label).arg(metres, 0, 'f', 1));
}

void MainStatusBar::clearCursorRange()
{
    m_range->clear();
}

void MainStatusBar::setCursorDepth(float metres)
{
    m_depth->setText(QString("~%1 m depth").arg(metres, 0, 'f', 0));
}

void MainStatusBar::clearCursorDepth()
{
    m_depth->clear();
}

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
    m_range->clear();
    m_depth->clear();
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

void MainStatusBar::hideProgress()
{
    m_progress->setRange(0, 100);
    m_progress->setVisible(false);
}

// -- Viewport info -------------------------------------------------------------

void MainStatusBar::setViewportInfo(double mpp, double rot_deg)
{
    static constexpr double kPhysicalMpp = 0.0254 / 96.0;

    // Block signals so updating the display doesn't trigger a scaleChangeRequested loop.
    m_spin_scale->blockSignals(true);
    if (mpp > 0.0 && !std::isnan(mpp))
        m_spin_scale->setValue(std::round(mpp / kPhysicalMpp));
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
