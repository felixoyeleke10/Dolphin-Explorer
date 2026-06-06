// WaterfallAnalysisPanelSeabed.cpp — SEABED PICKING section

#include "ui/features/waterfall/panels/WaterfallAnalysisPanel.h"
#include "ui/features/waterfall/components/WfValueRow.h"
#include "ui/shell/Theme.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace dolphin::ui {

// Per-glyph font sizes that normalise the visual weight of each Unicode icon
// across the seabed tool buttons. These are rendering constants, not theme tokens.
static constexpr int kGlyphPenPx   = 18;  // ✏ pen glyph (U+270F)
static constexpr int kGlyphBoxPx   = 22;  // ▭ box/rect glyph (U+25AD)
static constexpr int kGlyphErasePx = 11;  // ⌦ erase glyph (U+2326)

static constexpr int kComboW = 90;
static constexpr int kLabelL = 14;
static constexpr int kRowR   = 10;
static constexpr int kRowH   = 28;

static QComboBox* addComboRow(QVBoxLayout* bl, const QString& lbl, QWidget* parent)
{
    auto* row = new QWidget(parent);
    row->setFixedHeight(kRowH);
    auto* rl  = new QHBoxLayout(row);
    rl->setContentsMargins(kLabelL, 3, kRowR, 3);
    rl->setSpacing(Theme::kSpacing2);
    auto* k = new QLabel(lbl, row);
    k->setObjectName("wfParamLabel");
    k->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* combo = new QComboBox(row);
    combo->setObjectName("wfCombo");
    combo->setFixedWidth(kComboW);
    rl->addWidget(k);
    rl->addWidget(combo);
    bl->addWidget(row);
    return combo;
}

void WaterfallAnalysisPanel::buildSeabedSection(QVBoxLayout* vl, QWidget* container)
{
    auto* bl = makeSection(tr("Seabed Picking"), false, container, vl, &m_seabed_hdr);

    // -- AUTO sub-section --------------------------------------------------
    {
        auto* sub = new QLabel(tr("AUTO"), container);
        sub->setObjectName("wfSubSectionLabel");
        sub->setContentsMargins(kLabelL, 8, kRowR, 2);
        bl->addWidget(sub);
    }

    m_seabed_method_combo = addComboRow(bl, tr("Method"), container);
    m_seabed_method_combo->addItem(tr("Threshold"));
    m_seabed_method_combo->addItem(tr("First Return"));
    m_seabed_method_combo->setCurrentIndex(0);
    m_seabed_method_combo->setToolTip(
        tr("Choose the automatic seabed detection method.\n"
           "Threshold: first sustained return above a percentage of the ping peak; good general start.\n"
           "First Return: first sample above the noise/SNR gate; useful for clean, sharp bottom returns."));

    addValueRow(bl, tr("Blanking"),       m_seabed_blank,    0,  30, 10, 1,   0, tr("%"));
    m_seabed_blank->setToolTip(
        tr("Ignore the near-nadir water-column area before searching for seabed.\n"
           "Start at 10%. Increase if the detector grabs nadir/water-column noise.\n"
           "Use lower values only when the seabed begins very close to nadir."));
    addValueRow(bl, tr("Threshold"),      m_seabed_thresh,   1,  50, 17, 1,   0, tr("%"));
    m_seabed_thresh->setToolTip(
        tr("Threshold method sensitivity as a percentage of each ping's peak amplitude.\n"
           "Start at 17%. Lower finds weaker bottom but may pick noise; higher is stricter."));
    addValueRow(bl, tr("Min SNR"),        m_seabed_snr,      1,  10,  3, 0.1, 1, tr("\u00d7"));
    m_seabed_snr->setToolTip(
        tr("First Return sensitivity: required signal-to-noise ratio above the water-column noise floor.\n"
           "Start at 3x. Lower is more sensitive; higher avoids weak clutter."));
    addValueRow(bl, tr("Outlier Reject"), m_seabed_outlier,  0,  20,  5, 0.5, 1, tr(" m"));
    m_seabed_outlier->setSpecialValueText(0.0, tr("Off"));
    m_seabed_outlier->setToolTip(
        tr("Maximum allowed seabed jump from neighboring pings before a pick is rejected.\n"
           "Start at 5 m. Use 0 to turn off. Increase for steep slopes; decrease for flat seabed."));
    addValueRow(bl, tr("Ch. Agree"), m_seabed_ch_agree, 0.5, 20.0, 3.0, 0.5, 1, tr(" m"));
    m_seabed_ch_agree->setToolTip(
        tr("Maximum port/starboard range disagreement before falling back to the stronger channel.\n"
           "Start at 3 m. Lower for shallow surveys; raise for deep water with steep slopes."));
    addValueRow(bl, tr("Smoothing"),      m_seabed_smooth,   0,  20,  0, 1,   0);
    m_seabed_smooth->setToolTip(
        tr("Moving-average smoothing radius for the automatic seabed line.\n"
           "Start at 0. Use 2-5 to reduce jitter. Avoid high values where the real seabed changes quickly."));

    // -- MANUAL sub-section ------------------------------------------------
    {
        auto* sep = new QFrame(container);
        sep->setFrameShape(QFrame::HLine);
        sep->setObjectName("avHRule");
        bl->addWidget(sep);
    }
    {
        auto* sub = new QLabel(tr("Manual"), container);
        sub->setObjectName("wfSubSectionLabel");
        sub->setContentsMargins(kLabelL, 6, kRowR, 2);
        bl->addWidget(sub);
    }

    // Channel — which side the manual tools edit
    m_seabed_channel_combo = addComboRow(bl, tr("Channel"), container);
    m_seabed_channel_combo->addItem(tr("Both"));
    m_seabed_channel_combo->addItem(tr("Port"));
    m_seabed_channel_combo->addItem(tr("Starboard"));
    m_seabed_channel_combo->setToolTip(
        tr("Choose which side manual seabed tools edit.\n"
           "Both follows the side under the cursor. Port or Starboard forces picks to that channel."));

    {
        auto* row = new QWidget(container);
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(kLabelL, 4, kRowR, 8);
        rl->setSpacing(Theme::kSpacing1);

        auto makeToolBtn = [&](const QString& icon, const QString& tip, int font_px) {
            auto* b = new QToolButton(row);
            b->setText(icon);
            b->setToolTip(tip);
            b->setObjectName("wfSeabedTool");
            b->setCheckable(true);
            b->setFixedSize(Theme::kSeabedBtnSz, Theme::kSeabedBtnSz);
            // Override the stylesheet font-size per glyph so all icons look the
            // same visual weight despite different Unicode character metrics.
            b->setStyleSheet(QString("font-size: %1px;").arg(font_px));
            rl->addWidget(b);
            return b;
        };

        // ✏  Pen — drag top-to-bottom along the swath to reshape the seabed line.
        m_tool_pen = makeToolBtn(
            QStringLiteral("\u270F"),
            tr("Pen \u2014 drag top-to-bottom to freely reshape the seabed line.\n"
               "Hold and drag along the water column direction.\n"
               "Use this for hand-correcting short sections.\n"
               "Gaps from fast movement are filled automatically."),
            kGlyphPenPx);

        // ▭  Box — drag top-to-bottom to draw a straight seabed segment.
        m_tool_box = makeToolBtn(
            QStringLiteral("\u25AD"),
            tr("Box \u2014 drag top-to-bottom to define a straight seabed segment.\n"
               "Click where the seabed starts, drag to where it ends.\n"
               "Use this when the seabed is obvious but the auto detector missed a block.\n"
               "Range is linearly interpolated across the selected pings."),
            kGlyphBoxPx);

        // ⌦  Eraser — drag to clear; gaps auto-fill with a straight line on release.
        m_tool_erase = makeToolBtn(
            QStringLiteral("\u2326"),
            tr("Eraser \u2014 drag top-to-bottom to remove seabed picks.\n"
               "Gaps are automatically filled with a straight interpolated line\n"
               "between the nearest valid picks when you release.\n"
               "Use when the current line is visibly wrong."),
            kGlyphErasePx);

        rl->addStretch();
        bl->addWidget(row);
    }

    // -- Dirty indicator connections ---------------------------------------
    {
        auto refresh = [this](auto) { refreshSeabedDirty(); };
        connect(m_seabed_blank,    &WfValueRow::valueChanged, this, refresh);
        connect(m_seabed_thresh,   &WfValueRow::valueChanged, this, refresh);
        connect(m_seabed_snr,      &WfValueRow::valueChanged, this, refresh);
        connect(m_seabed_outlier,  &WfValueRow::valueChanged, this, refresh);
        connect(m_seabed_ch_agree, &WfValueRow::valueChanged, this, refresh);
        connect(m_seabed_smooth,   &WfValueRow::valueChanged, this, refresh);
        connect(m_seabed_method_combo,  QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { refreshSeabedDirty(); });
        connect(m_seabed_channel_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int ch) { emit seabedChannelChanged(ch); });
    }

    // -- Manual tool mutual exclusion --------------------------------------
    auto onTool = [this](QToolButton* pressed, int tool) {
        for (auto* b : { m_tool_pen, m_tool_box, m_tool_erase })
            if (b != pressed) { QSignalBlocker sb(b); b->setChecked(false); }
        emit seabedToolChanged(pressed->isChecked() ? tool : ToolNone);
    };
    connect(m_tool_pen,  &QToolButton::clicked, this,
            [this, onTool]() { onTool(m_tool_pen,  ToolPen);    });
    connect(m_tool_box,  &QToolButton::clicked, this,
            [this, onTool]() { onTool(m_tool_box,  ToolBox);    });
    connect(m_tool_erase, &QToolButton::clicked, this,
            [this, onTool]() { onTool(m_tool_erase, ToolEraser); });
}

void WaterfallAnalysisPanel::setSeabedToolActive(int tool)
{
    const struct { QToolButton* btn; int id; } kTools[] = {
        { m_tool_pen,   ToolPen    },
        { m_tool_box,   ToolBox    },
        { m_tool_erase, ToolEraser },
    };
    for (const auto& t : kTools) {
        if (t.btn) { QSignalBlocker sb(t.btn); t.btn->setChecked(tool == t.id); }
    }
}

} // namespace dolphin::ui
