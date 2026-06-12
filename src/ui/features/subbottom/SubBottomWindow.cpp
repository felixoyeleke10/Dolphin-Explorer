// SubBottomWindow.cpp — window layout, content assembly, and signal wiring.
//
// Toolbar build  → SubBottomWindow.Toolbar.cpp
// Status bar     → SubBottomWindow.Status.cpp
// Data loading   → SubBottomWindow.Load.cpp

#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/shared/UiUtils.h"
#include "ui/systems/AppState.h"
#include "ui/features/subbottom/SubBottomView.h"
#include "ui/features/subbottom/panels/SubBottomInspectorPanel.h"
#include "ui/features/subbottom/panels/SubBottomDisplayPanel.h"
#include "ui/shared/widgets/CommandBar.h"
#include "ui/shell/Theme.h"
#include "app/layers/DataLayer.h"

#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace dolphin::ui {

using namespace Theme;

static constexpr int kMinW     = 960;
static constexpr int kMinH     = 600;
static constexpr int kInitW    = 1340;
static constexpr int kInitH    = 760;

SubBottomWindow::SubBottomWindow(AppState* app_state, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_app_state(app_state)
{
    setWindowTitle(tr("Sub-Bottom Viewer — Dolphin Explorer"));
    setWindowIcon(QIcon(":/icons/bottom_track.svg"));
    setMinimumSize(kMinW, kMinH);
    setAttribute(Qt::WA_DeleteOnClose, false);

    {
        QSettings s;
        const QByteArray geo = s.value("sbpWindow/geometry").toByteArray();
        if (!geo.isEmpty()) restoreGeometry(geo);
        else                resize(kInitW, kInitH);
    }

    auto* root = makeCompactLayout<QVBoxLayout>(this);

    buildToolbar();

    // -- Content: inspector | view+scroll | display panel ------------------
    auto* content = makeCompactLayout<QHBoxLayout>();

    // Left — inspector panel
    m_inspector = new SubBottomInspectorPanel(this);
    m_inspector->setObjectName("av_inspector");
    m_inspector->setFixedWidth(kAvInspectorW);
    content->addWidget(m_inspector);

    auto* div_l = new QFrame(this);
    div_l->setFixedWidth(Theme::kSepSz);
    div_l->setObjectName("av_divider");
    content->addWidget(div_l);

    // Centre — view + horizontal scrollbar stacked vertically
    auto* centre = makeCompactLayout<QVBoxLayout>();

    m_view = new SubBottomView(this);
    centre->addWidget(m_view, 1);

    auto* view_div = new QFrame(this);
    view_div->setFixedHeight(Theme::kSepSz);
    view_div->setObjectName("av_divider");
    centre->addWidget(view_div);

    m_hscroll = new QScrollBar(Qt::Horizontal, this);
    m_hscroll->setObjectName("sbp_hscroll");
    m_hscroll->setFixedHeight(kAvScrollBarW);
    m_hscroll->setSingleStep(10);
    m_hscroll->setPageStep(100);
    m_hscroll->setRange(0, 0);
    centre->addWidget(m_hscroll);

    content->addLayout(centre, 1);

    auto* div_r = new QFrame(this);
    div_r->setFixedWidth(Theme::kSepSz);
    div_r->setObjectName("av_divider");
    content->addWidget(div_r);

    // Right — display panel
    m_display = new SubBottomDisplayPanel(this);
    m_display->setObjectName("av_analysis");
    m_display->setFixedWidth(kAvAnalysisW);
    content->addWidget(m_display);

    root->addLayout(content, 1);

    buildStatusBar();  // inserts status bar into root

    // -- Signal wiring ------------------------------------------------------

    // Inspector → view scale
    connect(m_inspector, &SubBottomInspectorPanel::traceWidthChanged,
            m_view, &SubBottomView::setPxPerTrace);
    connect(m_inspector, &SubBottomInspectorPanel::depthScaleChanged,
            m_view, &SubBottomView::setPxPerSample);

    // Inspector → line navigation (with layer id)
    connect(m_inspector, &SubBottomInspectorPanel::prevLineRequested,
            this, [this] { emit prevLineRequested(m_layer ? m_layer->id : std::string{}); });
    connect(m_inspector, &SubBottomInspectorPanel::nextLineRequested,
            this, [this] { emit nextLineRequested(m_layer ? m_layer->id : std::string{}); });
    connect(m_inspector, &SubBottomInspectorPanel::layerChangeRequested,
            this, &SubBottomWindow::layerChangeRequested);

    // Display panel → view rendering + sound speed
    connect(m_display, &SubBottomDisplayPanel::paramsChanged,
            this, [this](SubBottomDisplayParams p) {
                m_view->setDisplayParams(p.palette_index, p.gain, p.contrast,
                                         p.polarity_invert, p.show_bottom_track);
                m_sound_half_speed = p.sound_speed_ms / 2.f;
                if (m_btn_bottom_track_tb)
                    m_btn_bottom_track_tb->setChecked(p.show_bottom_track);
                // Refresh the sound-speed label in the inspector
                if (m_inspector && m_layer)
                    m_inspector->refresh(m_layer, m_source_path, m_source_size_bytes,
                                         m_total_traces, 0, 0.f, 0.f,
                                         m_layer->frequency_hz, p.sound_speed_ms);
            });

    // Bottom track toolbar button → display panel (keep in sync)
    connect(m_btn_bottom_track_tb, &QToolButton::clicked,
            this, [this](bool checked) {
                auto p = m_display->currentParams();
                p.show_bottom_track = checked;
                m_display->setParams(p);
                m_display->notifyParamsChanged();
            });
    // Sync initial check state with the display panel's restored settings
    m_btn_bottom_track_tb->setChecked(m_display->currentParams().show_bottom_track);

    // Scrollbar ↔ view
    connect(m_hscroll, &QScrollBar::valueChanged,
            this, &SubBottomWindow::onScrollbarMoved);
    connect(m_view, &SubBottomView::scrollChanged,
            this, &SubBottomWindow::onViewScrollChanged);

    // Scale sync: wheel zoom in view → update inspector spinboxes
    connect(m_view, &SubBottomView::scaleChanged,
            m_inspector, &SubBottomInspectorPanel::setViewScale);

    // Cursor
    connect(m_view, &SubBottomView::cursorMoved,
            this, &SubBottomWindow::onCursorMoved);
    connect(m_view, &SubBottomView::cursorLeft,
            this, &SubBottomWindow::onCursorLeft);

    // Context menu
    connect(m_view, &SubBottomView::contextMenuRequested,
            this, &SubBottomWindow::onContextMenu);

    // Restore view scale and overlay style from persisted QSettings.
    {
        QSettings qs;
        const int   px_trace  = qs.value(QStringLiteral("sbpView/pxPerTrace"),  2).toInt();
        const float px_sample = static_cast<float>(
            qs.value(QStringLiteral("sbpView/pxPerSample"), 0.5).toDouble());
        m_view->setPxPerTrace(px_trace);
        m_view->setPxPerSample(px_sample);

        SubBottomViewStyle st;
        st.xhair_show       = qs.value(QStringLiteral("sbpStyle/xhairShow"),    st.xhair_show).toBool();
        st.xhair_color      = QColor(qs.value(QStringLiteral("sbpStyle/xhairColor"),
                                               st.xhair_color.name()).toString());
        st.xhair_style      = static_cast<Qt::PenStyle>(
            qs.value(QStringLiteral("sbpStyle/xhairStyle"),
                     static_cast<int>(st.xhair_style)).toInt());
        st.xhair_width      = qs.value(QStringLiteral("sbpStyle/xhairWidth"),   st.xhair_width).toInt();
        st.xhair_opacity    = qs.value(QStringLiteral("sbpStyle/xhairOpacity"), st.xhair_opacity).toInt();
        st.grid_show        = qs.value(QStringLiteral("sbpStyle/gridShow"),     st.grid_show).toBool();
        st.grid_color       = QColor(qs.value(QStringLiteral("sbpStyle/gridColor"),
                                               st.grid_color.name()).toString());
        st.grid_opacity     = qs.value(QStringLiteral("sbpStyle/gridOpacity"),  st.grid_opacity).toInt();
        st.grid_interval_ms = static_cast<float>(
            qs.value(QStringLiteral("sbpStyle/gridInterval"), 0.0).toDouble());
        st.bt_color         = QColor(qs.value(QStringLiteral("sbpStyle/btColor"),
                                               st.bt_color.name()).toString());
        st.bt_thickness     = qs.value(QStringLiteral("sbpStyle/btThickness"),  st.bt_thickness).toInt();
        m_view->setViewStyle(st);
    }

    // Push restored display preferences to the view now that all connections are live.
    m_display->notifyParamsChanged();

    m_proc_debounce = new QTimer(this);
    m_proc_debounce->setSingleShot(true);
    m_proc_debounce->setInterval(80);
    connect(m_proc_debounce, &QTimer::timeout,
            this,             &SubBottomWindow::onProcDebounce);
}

void SubBottomWindow::closeEvent(QCloseEvent* e)
{
    QSettings s;
    s.setValue("sbpWindow/geometry", saveGeometry());
    QWidget::closeEvent(e);
}

void SubBottomWindow::setProjectLayers(
    const std::vector<std::pair<std::string, std::string>>& layers)
{
    if (m_inspector) m_inspector->setProjectLayers(layers);
}

SubBottomDisplayParams SubBottomWindow::displayParams() const
{
    return m_display ? m_display->currentParams() : SubBottomDisplayParams{};
}

int SubBottomWindow::pxPerTrace() const
{
    return m_view ? m_view->pxPerTrace() : 2;
}

float SubBottomWindow::pxPerSample() const
{
    return m_view ? m_view->pxPerSample() : 0.5f;
}

const SubBottomViewStyle& SubBottomWindow::viewStyle() const
{
    static const SubBottomViewStyle kDefault;
    return m_view ? m_view->viewStyle() : kDefault;
}

void SubBottomWindow::setSoundVelocity(double sv)
{
    if (!m_display) return;
    SubBottomDisplayParams p = m_display->currentParams();
    if (p.sound_speed_ms == static_cast<float>(sv)) return;
    p.sound_speed_ms = static_cast<float>(sv);
    m_display->setParams(p);
    m_display->notifyParamsChanged();
}

void SubBottomWindow::setPalette(int idx)
{
    if (!m_display) return;
    SubBottomDisplayParams p = m_display->currentParams();
    if (p.palette_index == idx) return;
    p.palette_index = idx;
    m_display->setParams(p);
    m_display->notifyParamsChanged();
}

void SubBottomWindow::applyDisplayParams(const SubBottomDisplayParams& params)
{
    if (!m_display) return;
    // Merge with existing acquisition params (sound speed lives in the SBP window only).
    SubBottomDisplayParams merged = params;
    merged.sound_speed_ms = m_display->currentParams().sound_speed_ms;
    m_display->setParams(merged);
    m_display->notifyParamsChanged();  // fires paramsChanged → view update + QSettings save
}

void SubBottomWindow::applySettings(const SubBottomDisplayParams& params,
                                     int px_per_trace, float px_per_sample,
                                     const SubBottomViewStyle& style)
{
    if (m_display) {
        m_display->setParams(params);
        m_display->notifyParamsChanged();
    }
    if (m_view) {
        m_view->setPxPerTrace(px_per_trace);
        m_view->setPxPerSample(px_per_sample);
        m_view->setViewStyle(style);
    }
    if (m_inspector) m_inspector->setViewScale(px_per_trace, px_per_sample);
}

void SubBottomWindow::onContextMenu(const QPoint& global_pos)
{
    QMenu menu(this);
    menu.addAction(tr("Zoom In"),  this, [this] {
        if (m_view) m_view->setPxPerTrace(m_view->pxPerTrace() + 1);
    });
    menu.addAction(tr("Zoom Out"), this, [this] {
        if (m_view) m_view->setPxPerTrace(std::max(1, m_view->pxPerTrace() - 1));
    });
    menu.addAction(tr("Reset Zoom"), this, [this] {
        if (m_view) {
            m_view->setPxPerTrace(2);
            m_view->setPxPerSample(0.5f);
        }
    });
    menu.addSeparator();
    menu.addAction(tr("Scroll to Start"), this, [this] {
        m_view->scrollToTrace(0);
        QSignalBlocker sb(m_hscroll);
        m_hscroll->setValue(0);
    });
    menu.addAction(tr("Scroll to End"), this, [this] {
        m_view->scrollToTrace(m_total_traces);
    });
    menu.addSeparator();
    menu.addAction(tr("Previous Line"), this, [this] {
        emit prevLineRequested(m_layer ? m_layer->id : std::string{});
    });
    menu.addAction(tr("Next Line"), this, [this] {
        emit nextLineRequested(m_layer ? m_layer->id : std::string{});
    });
    menu.addSeparator();
    menu.addAction(tr("Metadata…"), this, [this] {
        emit metadataRequested();
    });
    menu.addAction(tr("Settings…"), this, [this] {
        emit settingsRequested();
    });
    menu.exec(global_pos);
}

} // namespace dolphin::ui
