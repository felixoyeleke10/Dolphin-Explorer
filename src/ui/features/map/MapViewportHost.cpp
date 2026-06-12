// MapViewportHost.cpp — 2D / 3D viewport switcher.
#include "ui/features/map/MapViewportHost.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapView3D.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QColor>
#include <QFileDialog>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>  // std::hash
#include <limits>
#include <vector>

namespace dolphin::ui {

using namespace Theme;

static QColor colorForLayer(const std::string& id)
{
    const size_t h = std::hash<std::string>{}(id);
    const float hue = static_cast<float>(h & 0xFFFFu) / 65536.f;
    return QColor::fromHsvF(hue, 0.80f, 1.0f);
}

MapViewportHost::MapViewportHost(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = makeCompactLayout<QVBoxLayout>(this);

    m_stack = new QStackedWidget(this);

    m_view2d = new MapView(m_stack);

    m_stack->addWidget(m_view2d);   // index 0
    m_stack->setCurrentWidget(m_view2d);

    layout->addWidget(m_stack);

    // -- 2D/3D toggle button (overlaid bottom-right) ----------------------
    m_btn = new QToolButton(this);
    m_btn->setText(tr("3D"));
    m_btn->setObjectName("map3DBtn");
    m_btn->setFixedSize(kMap3DBtnW, kMap3DBtnH);
    m_btn->setToolTip(tr("Switch to 3D OpenGL view"));
    connect(m_btn, &QToolButton::clicked, this, [this]() { setMode3D(!m_is_3d); });

    // -- Load Terrain button (visible in 3D mode only) --------------------
    m_terrain_btn = new QToolButton(this);
    m_terrain_btn->setText(tr("⊞ Terrain"));
    m_terrain_btn->setObjectName("map3DTerrainBtn");
    m_terrain_btn->setFixedHeight(Theme::kFormBtnH);
    m_terrain_btn->setToolTip(tr("Load XYZ/CSV bathymetry file into 3D view"));
    m_terrain_btn->hide();
    connect(m_terrain_btn, &QToolButton::clicked, this, &MapViewportHost::promptLoadTerrain);

    // Status label shown during terrain load
    m_terrain_label = new QLabel(this);
    m_terrain_label->setObjectName("map3DTerrainLabel");
    m_terrain_label->hide();

    // Empty-state overlay — transparent QWidget covering the full viewport.
    // A QVBoxLayout centers the import button automatically; no manual move() needed.
    m_empty_state      = new QWidget(this);
    auto* empty_layout = makeCompactLayout<QVBoxLayout>(m_empty_state);
    empty_layout->addStretch(58);   // push button to ~58 % from top, below the painted hint text

    m_import_hint_btn = new QPushButton(tr("Import Files…"), m_empty_state);
    m_import_hint_btn->setObjectName("mapImportHintBtn");
    connect(m_import_hint_btn, &QPushButton::clicked,
            this, &MapViewportHost::importFilesRequested);

    empty_layout->addWidget(m_import_hint_btn, 0, Qt::AlignHCenter);
    empty_layout->addStretch(42);

    // Hide overlay once any 2D layer loads; restored by setShowImportHint on project change.
    connect(m_view2d, &MapView::layerDataUpdated, this, [this](const std::string&) {
        if (m_empty_state) m_empty_state->hide();
    });

    // Aggregate cursor-moved from whichever view is active.
    connect(m_view2d, &MapView::cursorMoved,
            this, &MapViewportHost::cursorMoved);

    // Forward layer data to the 3D view only if it already exists.
    // The 3D view is created lazily on first setMode3D(true); at that point
    // setMode3D replays all current 2D layer data so nothing is lost.
    connect(m_view2d, &MapView::layerDataUpdated, this, [this](const std::string& lid) {
        if (!m_view3d) return;
        const LayerMapData* ld = m_view2d->layerData(lid);
        if (ld) onLayerDataLoaded(lid, *ld, colorForLayer(lid));
    });

    // 3D context menu → load terrain

    // Relay 3D selection signals so MainWindow can wire them the same as 2D.
}

MapView3D* MapViewportHost::ensureView3D()
{
    if (m_view3d) return m_view3d;

    m_view3d = new MapView3D(m_stack);
    m_stack->addWidget(m_view3d);

    // Apply the display settings that were pushed to the host before the 3D view existed.
    m_view3d->setShowGrid(m_grid_visible);
    m_view3d->setMapBgColor(m_map_bg_color);
    m_view3d->setGridColors(m_grid_minor_3d, m_grid_major_3d);
    m_view3d->setGratLabelSize(m_grat_label_size);
    m_view3d->setGratLabelRotated(m_grat_label_rotated);
    m_view3d->setGratCoordFormat(m_grat_coord_fmt);
    m_view3d->setToolMode(static_cast<int>(m_tool_mode));

    connect(m_view3d, &MapView3D::terrainLoadFinished, this,
            [this](const std::string& /*id*/, bool ok, const QString& err) {
                m_terrain_label->hide();
                m_terrain_btn->setEnabled(true);
                if (!ok) {
                    QMessageBox::warning(this, tr("Terrain Load Error"), err);
                    return;
                }
                // Terrain may have auto-adopted a scene origin; re-push any SSS
                // layers that were skipped earlier because there was no origin yet.
                if (m_view3d && m_view3d->hasOrigin()) {
                    for (const auto& lid : m_view2d->layerDataIds()) {
                        if (const LayerMapData* data = m_view2d->layerData(lid))
                            onLayerDataLoaded(lid, *data, colorForLayer(lid));
                    }
                }
            });
    connect(m_view3d, &MapView3D::cursorMoved,
            this, &MapViewportHost::cursorMoved);
    connect(m_view3d, &MapView3D::loadTerrainRequested,
            this, &MapViewportHost::promptLoadTerrain);
    connect(m_view3d, &MapView3D::layerClicked,
            this, &MapViewportHost::layerClicked);
    connect(m_view3d, &MapView3D::layersSelected,
            this, &MapViewportHost::layersSelected);
    connect(m_view3d, &MapView3D::contextMenuRequested,
            this, &MapViewportHost::contextMenuRequested);
    connect(m_view3d, &MapView3D::gpuInfo,
            this, &MapViewportHost::gpuInfo);
    connect(m_view3d, &MapView3D::glInitError,
            this, &MapViewportHost::glInitError);
    connect(m_view3d, &MapView3D::contactPickedAt,
            this, &MapViewportHost::contactPickedAt);

    return m_view3d;
}

QImage MapViewportHost::grabViewportImage()
{
    if (m_is_3d && m_view3d)
        return m_view3d->grabFramebuffer();

    return m_view2d ? m_view2d->grab().toImage() : QImage();
}

void MapViewportHost::setShowGrid(bool show)
{
    m_grid_visible = show;
    m_view2d->setShowGrid(show);
    if (m_view3d) m_view3d->setShowGrid(show);
}

void MapViewportHost::setMapBgColor(QColor c)
{
    m_map_bg_color = c;
    m_view2d->setMapBgColor(c);
    if (m_view3d) m_view3d->setMapBgColor(c);
}

void MapViewportHost::setGridColors(QColor line2d, QColor minor3d, QColor major3d)
{
    m_grid_minor_3d = minor3d;
    m_grid_major_3d = major3d;
    m_view2d->setGridColor(line2d);
    if (m_view3d) m_view3d->setGridColors(minor3d, major3d);
}

void MapViewportHost::setGratLabelSize(int size)
{
    m_grat_label_size = size;
    m_view2d->setGratLabelSize(size);
    if (m_view3d) m_view3d->setGratLabelSize(size);
}

void MapViewportHost::setGratLabelRotated(bool rotated)
{
    m_grat_label_rotated = rotated;
    m_view2d->setGratLabelRotated(rotated);
    if (m_view3d) m_view3d->setGratLabelRotated(rotated);
}

void MapViewportHost::setGratCoordFormat(int fmt)
{
    m_grat_coord_fmt = fmt;
    m_view2d->setGratCoordFormat(fmt);
    if (m_view3d) m_view3d->setGratCoordFormat(fmt);
}

void MapViewportHost::setLayerVisible(const std::string& layer_id, bool visible)
{
    m_layer_visibility[layer_id] = visible;
    m_view2d->setLayerVisible(layer_id, visible);
    if (m_view3d) m_view3d->setLayerVisible(layer_id, visible);
}

void MapViewportHost::setNavTrackVisible(const std::string& layer_id, bool visible)
{
    m_layer_visibility[layer_id] = visible;
    m_view2d->setNavTrackVisible(layer_id, visible);
    if (m_view3d) m_view3d->setLayerVisible(layer_id, visible);
}

void MapViewportHost::setActiveLayer(const std::string& layer_id)
{
    m_view2d->setActiveLayer(layer_id);
    if (m_view3d) m_view3d->setActiveLayer(layer_id);
}

void MapViewportHost::setSelectedLayers(const std::vector<std::string>& ids)
{
    m_view2d->setSelectedLayers(ids);
    if (m_view3d) m_view3d->setSelectedLayers(ids);
}

void MapViewportHost::setMode3D(bool on)
{
    if (m_is_3d == on) return;
    m_is_3d = on;
    if (on) {
        const bool first_open = (m_view3d == nullptr);
        auto* view3d = ensureView3D();
        if (first_open) {
            // Replay all layer data that arrived while we were in 2D mode.
            for (const auto& lid : m_view2d->layerDataIds()) {
                if (const LayerMapData* ld = m_view2d->layerData(lid))
                    onLayerDataLoaded(lid, *ld, colorForLayer(lid));
            }
        }
        m_stack->setCurrentWidget(view3d);
    } else {
        m_stack->setCurrentWidget(m_view2d);
    }
    m_btn->setText(on ? tr("2D") : tr("3D"));
    m_btn->setToolTip(on ? tr("Switch to 2D plan view")
                         : tr("Switch to 3D OpenGL view"));
    if (m_terrain_btn)
        m_terrain_btn->setVisible(on);
    positionOverlay();
    emit modeChanged(on);

    // QOpenGLWidget creates a native HWND on first show; Windows briefly
    // activates it, pushing the main window behind other apps. Reclaim focus.
    if (auto* w = window()) {
        w->activateWindow();
        w->raise();
    }
}

void MapViewportHost::promptLoadTerrain()
{
    if (!m_is_3d) return;
    auto* view3d = ensureView3D();

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Bathymetry File"), {},
        tr("XYZ / CSV files (*.xyz *.csv *.txt);;All files (*)"));
    if (path.isEmpty()) return;

    // Use the file path as the layer ID (unique per file)
    const std::string lid = path.toStdString();

    m_terrain_btn->setEnabled(false);
    m_terrain_label->setText(tr("Loading terrain…"));
    m_terrain_label->adjustSize();
    m_terrain_label->show();
    positionOverlay();

    // z_is_depth = true: common convention for bathy XYZ files (Z = positive depth)
    view3d->loadTerrainFile(lid, path, /*z_is_depth=*/true);
}

void MapViewportHost::onLayerDataLoaded(const std::string& layer_id,
                                     const LayerMapData& data,
                                     QColor color)
{
    if (!m_view3d || data.nav_track.empty()) return;

    // Set scene origin from the first valid nav point if not yet set.
    if (!m_view3d->hasOrigin()) {
        for (const auto& pt : data.nav_track) {
            if (!std::isnan(pt.x()) && !std::isnan(pt.y())) {
                m_view3d->setSceneOrigin(pt.x(), pt.y(), data.is_projected);
                break;
            }
        }
    }

    if (m_view3d->hasOrigin()) {
        m_view3d->updateNavTrack(layer_id, data, color);
        // Replay any explicit hide set before the 3D layer was created.
        {
            auto it = m_layer_visibility.find(layer_id);
            if (it != m_layer_visibility.end() && !it->second)
                m_view3d->setLayerVisible(layer_id, false);
        }

        if (data.kind == LayerMapKind::Profile) {
            m_view3d->setProfileCurtain(layer_id, data);
        }

        if (!data.preview_image.isNull() &&
            data.lon_min < data.lon_max && data.lat_min < data.lat_max) {

            // Build per-ribbon-pair outline matching drawMergedSwath in MapViewPaint.Sonar.cpp.
            // Ribbon layout: [0..n/2-1] = inner edge (chronological),
            //                [n/2..n-1] = outer edge REVERSED (newest stored first).
            // Per ribbon pair we build the same outer polygon the 2D code draws:
            //   port outer: j from pn-1 DOWN to ph  → oldest→newest (time forward)
            //   stbd outer: j from sh   UP  to sn-1 → newest→oldest (time backward)
            // Segments are separated by NaN sentinels; buildDrapeHullVbo closes each one.
            const SwathCoverage* port_cov = nullptr;
            const SwathCoverage* stbd_cov = nullptr;
            for (const auto& cov : data.coverage) {
                if      (cov.channel == core::SidescanChannel::Port)      port_cov = &cov;
                else if (cov.channel == core::SidescanChannel::Starboard) stbd_cov = &cov;
            }

            static const QPointF kNaN { std::numeric_limits<double>::quiet_NaN(),
                                        std::numeric_limits<double>::quiet_NaN() };
            std::vector<QPointF> hull_geo;
            if (port_cov && stbd_cov) {
                const int count = static_cast<int>(
                    std::min(port_cov->ribbons.size(), stbd_cov->ribbons.size()));
                for (int i = 0; i < count; ++i) {
                    const auto& pr = port_cov->ribbons[static_cast<size_t>(i)];
                    const auto& sr = stbd_cov->ribbons[static_cast<size_t>(i)];
                    const int pn = static_cast<int>(pr.size());
                    const int sn = static_cast<int>(sr.size());
                    if (pn < 4 || sn < 4) continue;
                    const int ph = pn / 2, sh = sn / 2;
                    for (int j = pn - 1; j >= ph; --j)
                        hull_geo.push_back(pr[static_cast<size_t>(j)]);
                    for (int j = sh; j < sn; ++j)
                        hull_geo.push_back(sr[static_cast<size_t>(j)]);
                    hull_geo.push_back(kNaN);   // segment separator
                }
            } else if (port_cov || stbd_cov) {
                const SwathCoverage* cov = port_cov ? port_cov : stbd_cov;
                for (const auto& ribbon : cov->ribbons) {
                    const int n  = static_cast<int>(ribbon.size());
                    if (n < 4) continue;
                    const int h = n / 2;
                    for (int j = n - 1; j >= h; --j)
                        hull_geo.push_back(ribbon[static_cast<size_t>(j)]);
                    for (int j = 0; j < h; ++j)
                        hull_geo.push_back(ribbon[static_cast<size_t>(j)]);
                    hull_geo.push_back(kNaN);
                }
            }

            m_view3d->setSonarDrape(layer_id, data.preview_image,
                                    data.lon_min, data.lat_min,
                                    data.lon_max, data.lat_max,
                                    std::move(hull_geo));
        }
    }
}

void MapViewportHost::onLayerRemoved(const std::string& layer_id)
{
    m_layer_visibility.erase(layer_id);
    m_view2d->removeLayerData(layer_id);
    if (!m_view3d) return;
    m_view3d->removeLayer(layer_id);
    m_view3d->removeProfileCurtain(layer_id);
    m_view3d->removeSonarDrape(layer_id);
}

void MapViewportHost::clearScene()
{
    m_layer_visibility.clear();
    m_view2d->clearAllLayerData();
    if (m_view3d) m_view3d->clearScene();
}

void MapViewportHost::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (m_empty_state) m_empty_state->setGeometry(rect());
    positionOverlay();
}

void MapViewportHost::setShowImportHint(bool show)
{
    if (!m_empty_state) return;
    m_empty_state->setGeometry(rect());
    m_empty_state->setVisible(show);
    if (show) m_empty_state->raise();
}

void MapViewportHost::setToolMode(ToolMode mode)
{
    m_tool_mode = mode;
    if (m_view3d)
        m_view3d->setToolMode(static_cast<int>(mode));
}

void MapViewportHost::positionOverlay()
{
    // 2D/3D toggle — bottom-right corner
    if (m_btn) {
        m_btn->move(width()  - m_btn->width()  - kMap3DMargin,
                    height() - m_btn->height() - kMap3DMargin);
        m_btn->raise();
    }

    // Load Terrain button — immediately to the left of the toggle
    if (m_terrain_btn && m_terrain_btn->isVisible()) {
        m_terrain_btn->adjustSize();
        m_terrain_btn->move(
            m_btn ? m_btn->x() - m_terrain_btn->width() - kSpacing2
                  : width() - m_terrain_btn->width() - kMap3DMargin,
            height() - m_terrain_btn->height() - kMap3DMargin);
        m_terrain_btn->raise();
    }

    // "Loading terrain…" label — above the terrain button
    if (m_terrain_label && m_terrain_label->isVisible() && m_terrain_btn) {
        m_terrain_label->move(m_terrain_btn->x(),
                              m_terrain_btn->y() - m_terrain_label->height() - 4);
        m_terrain_label->raise();
    }

}

} // namespace dolphin::ui
