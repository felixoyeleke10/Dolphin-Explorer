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
#include "ui/shared/panels/ContactPickingPanel.h"
#include "ui/shared/widgets/CollapsibleSection.h"
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
#include <QLabel>
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
    setWindowIcon(Theme::icon(":/icons/bottom_track.svg"));
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

    // Right — display panel + annotation tool sections (Contact Picking / Feature
    // Drawing), each a CollapsibleSection like the rest of the panel.
    {
        auto* right_col = new QWidget(this);
        right_col->setFixedWidth(kAvAnalysisW);
        auto* rc = makeCompactLayout<QVBoxLayout>(right_col);

        m_display = new SubBottomDisplayPanel(right_col);
        m_display->setObjectName("av_analysis");
        rc->addWidget(m_display);

        m_contact_panel = new ContactPickingPanel(right_col);
        auto* contact_sec = new CollapsibleSection(tr("Contact Picking"), right_col);
        contact_sec->setIcon(QStringLiteral(":/icons/add_contact.svg"));
        contact_sec->setContent(m_contact_panel);
        rc->addWidget(contact_sec);

        rc->addStretch(1);
        content->addWidget(right_col);
    }

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

    // User display edits leave the window so the shell can persist them via
    // the display-state authority (Views panel + 3D curtains follow).
    connect(m_display, &SubBottomDisplayPanel::userParamsEdited,
            this,      &SubBottomWindow::displayParamsEdited);

    // Display panel → view rendering + sound speed
    connect(m_display, &SubBottomDisplayPanel::paramsChanged,
            this, [this](SubBottomDisplayParams p) {
                m_view->setDisplayParams(p.palette_index, p.gain, p.contrast,
                                         p.polarity_invert, p.show_bottom_track);
                m_sound_half_speed = p.sound_speed_ms / 2.f;
                refreshContactOverlay();   // marker depth_s depends on sound speed
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

    // Annotation tool sections → view tools (mutually exclusive).
    connect(m_contact_panel, &ContactPickingPanel::pickToggled, this, [this](bool on) {
        if (m_view) m_view->setContactTool(on ? 1 : 0);
        if (on) syncFeatureToolButtons(0);
        if (m_status_left)
            m_status_left->setText(on
                ? tr("Contact — click the section to place a point pick") : QString{});
    });

    // Feature drawing toolbar toggles: manual exclusivity among the three;
    // clicking the active tool again turns drawing off. Activating one
    // deactivates contact picking (the view setter enforces it; the UI mirrors).
    {
        auto wireFeature = [this](QToolButton* btn, int kind) {
            connect(btn, &QToolButton::toggled, this, [this, kind](bool on) {
                if (on) {
                    syncFeatureToolButtons(kind);          // uncheck the other two
                    if (m_view) m_view->setFeatureTool(kind);
                    if (m_contact_panel) m_contact_panel->setPickActive(false);
                    if (m_status_left)
                        m_status_left->setText(
                            kind == 1 ? tr("Polygon: click points, double-click or Enter to close, Esc to cancel.")
                          : kind == 2 ? tr("Line: click points, double-click or Enter to finish, Esc to cancel.")
                                      : tr("Pen: press and drag to draw freehand; release to finish."));
                } else if (!m_btn_feat_poly->isChecked() && !m_btn_feat_line->isChecked()
                           && !m_btn_feat_pen->isChecked()) {
                    if (m_view) m_view->setFeatureTool(0);
                    if (m_status_left) m_status_left->setText(QString{});
                }
            });
        };
        wireFeature(m_btn_feat_poly, 1);
        wireFeature(m_btn_feat_line, 2);
        wireFeature(m_btn_feat_pen,  3);
    }
    connect(m_contact_panel, &ContactPickingPanel::clearRequested,
            this, &SubBottomWindow::clearAllContactsRequested);
    // "Edit Contacts…" → the shared "Edit contact details" editor for this line.
    connect(m_contact_panel, &ContactPickingPanel::editRequested, this, [this]() {
        const QString line = m_layer ? QString::fromStdString(m_layer->id) : QString{};
        emit contactEditRequested(0, line);
    });
    // Marker double-click → the shared editor focused on that contact.
    connect(m_view, &SubBottomView::contactEditRequested, this, [this](uint64_t id) {
        const QString line = m_layer ? QString::fromStdString(m_layer->id) : QString{};
        emit contactEditRequested(id, line);
    });
    // Annotation tools — forward picks/draws up to MainWindow (project owner).
    connect(m_view, &SubBottomView::contactPicked,
            this, [this](int trace_idx, float depth_s, double lat, double lon,
                         bool is_projected) {
                QString cls = m_contact_panel ? m_contact_panel->classification() : tr("Unknown");
                const QString line_id = m_layer ? QString::fromStdString(m_layer->id) : QString{};
                // Two-way travel time × half sound-speed → depth in metres.
                const float depth_m = (depth_s > 0.f) ? depth_s * m_sound_half_speed : 0.f;
                if (m_status_left)
                    m_status_left->setText(tr("Contact placed — trace %1  ·  %2 m")
                                               .arg(trace_idx + 1).arg(depth_m, 0, 'f', 1));
                emit contactCreated(lat, lon, is_projected, depth_m, cls, line_id,
                                    static_cast<uint64_t>(trace_idx));
            });
    connect(m_view, &SubBottomView::featureDrawn,
            this, [this](const std::vector<QPointF>& verts, bool polygon, bool is_projected) {
                const QString line_id = m_layer ? QString::fromStdString(m_layer->id) : QString{};
                if (m_status_left)
                    m_status_left->setText(
                        tr("Feature drawn — %1 vertices").arg(static_cast<int>(verts.size())));
                emit featureCreated(verts, polygon, is_projected, QString{}, line_id);
            });

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

    // Opening the window restores the view; it must not rewrite preferences.
    m_display->refreshParams();

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

void SubBottomWindow::setProjectContacts(std::vector<core::Contact> contacts)
{
    m_project_contacts = std::move(contacts);
    refreshContactOverlay();
}

void SubBottomWindow::refreshContactOverlay()
{
    if (!m_view || !m_layer) return;
    std::vector<SubBottomView::ContactMark> marks;
    for (const auto& c : m_project_contacts) {
        if (c.line_id != m_layer->id) continue;   // only this line's contacts
        if (!c.visible) continue;                  // hidden via the explorer checkbox
        SubBottomView::ContactMark m;
        m.id        = c.id;
        m.trace_idx = static_cast<int>(c.artifact_id);
        // Invert the pick conversion: depth_m = travel time × half sound-speed.
        m.depth_s   = (m_sound_half_speed > 0.f) ? c.depth_m / m_sound_half_speed : 0.f;
        marks.push_back(m);
    }
    m_view->setExternalContacts(std::move(marks));
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
    m_display->refreshParams();
}

void SubBottomWindow::setPalette(int idx)
{
    if (!m_display) return;
    SubBottomDisplayParams p = m_display->currentParams();
    if (p.palette_index == idx) return;
    p.palette_index = idx;
    m_display->setParams(p);
    m_display->refreshParams();
}

void SubBottomWindow::setLineNavEnabled(bool has_prev, bool has_next)
{
    m_has_prev_line = has_prev;
    m_has_next_line = has_next;
    if (m_inspector) m_inspector->setNavEnabled(has_prev, has_next);
}

void SubBottomWindow::applyDisplayParams(const SubBottomDisplayParams& params)
{
    if (!m_display) return;
    // Merge with existing acquisition params (sound speed lives in the SBP window only).
    SubBottomDisplayParams merged = params;
    merged.sound_speed_ms = m_display->currentParams().sound_speed_ms;
    m_display->setParams(merged);
    // Synchronisation from the authority, NOT a user action: refreshParams
    // updates the view without persisting and without re-emitting
    // userParamsEdited (which would echo the push back into the authority).
    m_display->refreshParams();
}

void SubBottomWindow::restoreDisplayParams(const SubBottomDisplayParams& params)
{
    if (!m_display) return;
    SubBottomDisplayParams merged = params;
    merged.sound_speed_ms = m_display->currentParams().sound_speed_ms;
    m_display->setParams(merged);
    m_display->refreshParams();
}

void SubBottomWindow::applySettings(const SubBottomDisplayParams& params,
                                     int px_per_trace, float px_per_sample,
                                     const SubBottomViewStyle& style)
{
    if (m_display) {
        m_display->setParams(params);
        // SubBottomSettingsDialog already persisted the explicit Apply action.
        m_display->refreshParams();
    }
    if (m_view) {
        m_view->setPxPerTrace(px_per_trace);
        m_view->setPxPerSample(px_per_sample);
        m_view->setViewStyle(style);
    }
    if (m_inspector) m_inspector->setViewScale(px_per_trace, px_per_sample);
}

void SubBottomWindow::onContextMenu(const QPoint& global_pos, uint64_t contact_id)
{
    QMenu menu(this);
    if (contact_id != 0) {
        menu.addAction(tr("Edit Contact Details…"), this, [this, contact_id] {
            const QString line = m_layer ? QString::fromStdString(m_layer->id) : QString{};
            emit contactEditRequested(contact_id, line);
        });
        menu.addSeparator();
    }
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
        QSignalBlocker sb(m_hscroll);
        m_hscroll->setValue(m_view->firstVisibleTrace());  // keep scrollbar in sync
    });
    menu.addSeparator();
    menu.addAction(tr("Previous Line"), this, [this] {
        emit prevLineRequested(m_layer ? m_layer->id : std::string{});
    })->setEnabled(m_has_prev_line);
    menu.addAction(tr("Next Line"), this, [this] {
        emit nextLineRequested(m_layer ? m_layer->id : std::string{});
    })->setEnabled(m_has_next_line);
    menu.addSeparator();
    menu.addAction(tr("Metadata…"), this, [this] {
        emit metadataRequested();
    });
    menu.addAction(tr("Settings…"), this, [this] {
        emit settingsRequested();
    });
    menu.exec(global_pos);
}


void SubBottomWindow::syncFeatureToolButtons(int tool)
{
    QToolButton* btns[] = { m_btn_feat_poly, m_btn_feat_line, m_btn_feat_pen };
    for (int i = 0; i < 3; ++i) {
        if (!btns[i]) continue;
        QSignalBlocker sb(btns[i]);
        btns[i]->setChecked(tool == i + 1);
    }
}

} // namespace dolphin::ui
