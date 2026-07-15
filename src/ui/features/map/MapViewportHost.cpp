// MapViewportHost.cpp — 2D / 3D viewport switcher.
#include "ui/features/map/MapViewportHost.h"
#include "ui/features/map/MapView.h"
#include "ui/features/map/MapView3D.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QColor>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QIcon>
#include <QImage>
#include <QLocale>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
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

    // (No in-viewport 2D/3D switch — the checkable button on the main toolbar
    // is the single mode control, per user direction. Terrain/draping loads
    // live in Views ▸ MAP ▸ Draping surface.)

    // Empty-state launcher — transparent overlay covering the full viewport.
    // Welcome-window composition (a la Xcode): ONE solid card carrying the app
    // identity, primary/secondary actions, and the recent-projects list. The
    // card is a theme surface, so text contrast never depends on the canvas
    // colour (user-configurable, and often dark even in the light theme).
    m_empty_state      = new QWidget(this);
    auto* empty_layout = makeCompactLayout<QVBoxLayout>(m_empty_state);
    empty_layout->addStretch(40);

    auto* card = new QFrame(m_empty_state);
    card->setObjectName("launcherCard");
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setFixedWidth(420);
    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(24, 30, 24, 14);
    cl->setSpacing(0);

    // Hero — logo + name + one-line identity.
    auto* logo = new QLabel(card);
    logo->setPixmap(QIcon(QStringLiteral(":/icons/dolphin_logo.svg")).pixmap(48, 48));
    logo->setAlignment(Qt::AlignCenter);
    cl->addWidget(logo, 0, Qt::AlignHCenter);
    cl->addSpacing(10);

    auto* title = new QLabel(tr("Dolphin Explorer"), card);
    title->setObjectName("launcherTitle");
    cl->addWidget(title, 0, Qt::AlignHCenter);
    cl->addSpacing(2);

    auto* subtitle = new QLabel(tr("Marine survey workstation"), card);
    subtitle->setObjectName("launcherSub");
    cl->addWidget(subtitle, 0, Qt::AlignHCenter);
    cl->addSpacing(22);

    // Actions — filled accent primary + quiet secondary.
    auto* actions = new QWidget(card);
    auto* al = makeCompactLayout<QHBoxLayout>(actions);
    al->setSpacing(Theme::kSpacing3);
    m_import_hint_btn = new QPushButton(tr("Import Files…"), actions);
    m_import_hint_btn->setObjectName("mapImportHintBtn");
    m_import_hint_btn->setCursor(Qt::PointingHandCursor);
    connect(m_import_hint_btn, &QPushButton::clicked,
            this, &MapViewportHost::importFilesRequested);
    auto* new_proj_btn = new QPushButton(tr("New Project"), actions);
    new_proj_btn->setObjectName("launcherNewBtn");
    new_proj_btn->setCursor(Qt::PointingHandCursor);
    connect(new_proj_btn, &QPushButton::clicked,
            this, &MapViewportHost::newProjectRequested);
    al->addWidget(m_import_hint_btn);
    al->addWidget(new_proj_btn);
    cl->addWidget(actions, 0, Qt::AlignHCenter);
    cl->addSpacing(24);

    // Recent projects list — populated by setRecentProjects (hidden when empty).
    m_recent_box = new QFrame(card);
    m_recent_box->setObjectName("mapRecentBox");
    auto* rb = new QVBoxLayout(m_recent_box);
    rb->setContentsMargins(0, 0, 0, 0);
    rb->setSpacing(2);
    auto* recent_hdr = new QLabel(tr("RECENT"), m_recent_box);
    recent_hdr->setObjectName("mapRecentHdr");
    rb->addWidget(recent_hdr);
    m_recent_items_l = new QVBoxLayout();
    m_recent_items_l->setContentsMargins(0, 2, 0, 0);
    m_recent_items_l->setSpacing(1);
    rb->addLayout(m_recent_items_l);
    m_recent_box->hide();
    cl->addWidget(m_recent_box);

    empty_layout->addWidget(card, 0, Qt::AlignHCenter);
    empty_layout->addStretch(52);

    // Hide overlay once any 2D layer loads; restored by setShowImportHint on project change.
    connect(m_view2d, &MapView::layerDataUpdated, this, [this](const std::string&) {
        if (m_empty_state) m_empty_state->hide();
    });

    // Aggregate cursor-moved and viewport-changed from whichever view is active.
    connect(m_view2d, &MapView::cursorMoved,
            this, &MapViewportHost::cursorMoved);
    connect(m_view2d, &MapView::viewportChanged,
            this, [this](double mpp, double rot) {
                m_last_mpp     = mpp;
                m_last_rot_deg = rot;
                if (!m_is_3d) emit viewportChanged(mpp, rot);
            });

    // Forward all 2D layer data to the pre-created 3D view.
    connect(m_view2d, &MapView::layerDataUpdated, this, [this](const std::string& lid) {
        const LayerMapData* ld = m_view2d->layerData(lid);
        if (ld) onLayerDataLoaded(lid, *ld, colorForLayer(lid));
    });

    // Default the map background to the theme background so the transition cover is
    // always opaque — even before AppSettings pushes a colour. Otherwise the first
    // 2D→3D switch shows the cover transparent and the black GL zero-frame flickers
    // through. MapView3D uses the same default, so cover and first frame match.
    m_map_bg_color = Theme::kBg;

    // Transition cover — a solid-colour widget that floats over the stack during the
    // brief window between the first 2D→3D switch and the first completed GL frame.
    m_transition_cover = new QWidget(this);
    m_transition_cover->setObjectName("mapTransitionCover");
    m_transition_cover->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_transition_cover->setAutoFillBackground(true);
    m_transition_cover->hide();

    // Create the 3D QOpenGLWidget up front (kept hidden behind the 2D page) so the
    // top-level window is GL-capable from its first show. If the first OpenGL surface
    // is introduced *after* the window is shown (lazy creation on the first 2D→3D
    // switch), Windows recreates the native top-level window — the whole app visibly
    // disappears and reappears. A hidden stack page does not run paintGL, and layer
    // data only sets dirty flags, so there is no rendering cost in 2D mode. (The GL
    // context is shared app-wide via AA_ShareOpenGLContexts, set in main().)
    ensureView3D();
}

MapView3D* MapViewportHost::ensureView3D()
{
    if (m_view3d) return m_view3d;

    // MapView3D is a native QOpenGLWindow; embed it via createWindowContainer so it
    // lives in the stack as a normal page. The native surface keeps the rest of the
    // app off the GL composition path (no whole-window flicker).
    m_view3d = new MapView3D();
    m_view3d_container = QWidget::createWindowContainer(m_view3d, m_stack);
    m_view3d_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_stack->addWidget(m_view3d_container);

    // Apply the display settings that were pushed to the host before the 3D view existed.
    m_view3d->setShowGrid(m_grid_visible);
    m_view3d->setMapBgColor(m_map_bg_color);
    m_view3d->setGridColors(m_grid_minor_3d, m_grid_major_3d);
    m_view3d->setGratLabelSize(m_grat_label_size);
    m_view3d->setGratLabelRotated(m_grat_label_rotated);
    m_view3d->setGratCoordFormat(m_grat_coord_fmt);
    m_view3d->setToolMode(static_cast<int>(m_tool_mode));

    connect(m_view3d, &MapView3D::terrainLoadFinished, this,
            [this](const std::string& id, bool ok, const QString& err) {
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
                if (const auto it = m_layer_visibility.find(id);
                    it != m_layer_visibility.end()) {
                    m_view3d->setLayerVisible(id, it->second);
                }
            });
    connect(m_view3d, &MapView3D::cursorMoved,
            this, &MapViewportHost::cursorMoved);
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

    connect(m_view3d, &MapView3D::viewportChanged,
            this, [this](double mpp, double rot) {
                m_last_mpp     = mpp;
                m_last_rot_deg = rot;
                if (m_is_3d) emit viewportChanged(mpp, rot);
            });

    // Replay layer data that accumulated in the 2D view while the 3D view did
    // not yet exist.  onLayerDataLoaded sets the scene origin on the first valid
    // track, so subsequent layers are positioned correctly.
    for (const auto& lid : m_view2d->layerDataIds()) {
        if (const LayerMapData* data = m_view2d->layerData(lid))
            onLayerDataLoaded(lid, *data, colorForLayer(lid));
    }

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

void MapViewportHost::setLayerOpacity(const std::string& layer_id, float opacity)
{
    m_view2d->setLayerOpacity(layer_id, opacity);
    if (m_view3d) m_view3d->setLayerOpacity(layer_id, opacity);
}

void MapViewportHost::setLayerBlendMode(const std::string& layer_id, int blend_mode)
{
    // Mosaic compositing is a 2D concern only; the 3D drape/curtain draw one
    // texture per layer with no overlap blending, so there is no 3D equivalent.
    m_view2d->setLayerBlendMode(layer_id, blend_mode);
}

void MapViewportHost::setLayerClipPolygons(const std::string& layer_id, bool clip)
{
    m_view2d->setLayerClipPolygons(layer_id, clip);   // 2D mosaic only
}

void MapViewportHost::setLayerShowBeams(const std::string& layer_id, bool show)
{
    m_view2d->setLayerShowBeams(layer_id, show);       // 2D mosaic only
}

void MapViewportHost::setLayerBeamSpacing(const std::string& layer_id, int spacing)
{
    m_view2d->setLayerBeamSpacing(layer_id, spacing);  // 2D mosaic only
}

void MapViewportHost::setNavTrackVisible(const std::string& layer_id, bool visible)
{
    m_nav_track_visibility[layer_id] = visible;
    m_view2d->setNavTrackVisible(layer_id, visible);
    if (m_view3d) m_view3d->setNavTrackVisible(layer_id, visible);
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
        auto* view3d = ensureView3D();

        if (!view3d->isGLReady()) {
            // First switch: the QOpenGLWidget paints a black zero-frame before
            // initializeGL() runs (and the window enters GL composition). Raise an
            // opaque cover BEFORE swapping the stack, so the 3D widget never appears
            // uncovered, and keep it up until the first real frame has been presented
            // (deferred hide — firstFrameReady fires inside paintGL, before the frame
            // reaches the screen, so hiding synchronously there unmasks too early).
            const QColor bg = m_map_bg_color.isValid() ? m_map_bg_color : QColor(Theme::kBg);
            m_transition_cover->setStyleSheet(
                "#mapTransitionCover { background:" + bg.name(QColor::HexRgb) + "; }");
            m_transition_cover->setGeometry(m_stack->geometry());
            m_transition_cover->show();
            m_transition_cover->raise();

            m_stack->setCurrentWidget(m_view3d_container);
            m_transition_cover->raise();

            connect(view3d, &MapView3D::firstFrameReady, this, [this]() {
                // Defer past the present that completes the first frame.
                QTimer::singleShot(0, this, [this]() {
                    if (m_transition_cover) m_transition_cover->hide();
                });
            }, Qt::SingleShotConnection);
        } else {
            m_stack->setCurrentWidget(m_view3d_container);
        }
    } else {
        m_stack->setCurrentWidget(m_view2d);
    }

    // Push cached viewport state into the newly-active view so the status bar
    // stays accurate and the new view opens at the same scale/rotation.
    if (m_last_mpp > 0.0) {
        if (on && m_view3d) {
            const float h = static_cast<float>(m_view3d->height());
            if (h > 0.f)
                m_view3d->setDistance(static_cast<float>(m_last_mpp) * h * 0.5f);
            m_view3d->setYaw(m_last_rot_deg);
        } else if (!on && m_view2d) {
            m_view2d->setZoomFromMpp(m_last_mpp);
            m_view2d->setRotationDeg(m_last_rot_deg);
        }
        emit viewportChanged(m_last_mpp, m_last_rot_deg);
    }

    emit modeChanged(on);
}

void MapViewportHost::loadTerrainPath(const QString& path)
{
    // Works from either mode: the 3D view is pre-created, data uploads when
    // it next paints. File path doubles as the terrain layer ID.
    // z_is_depth = true: common convention for bathy XYZ files (Z = positive depth).
    ensureView3D()->loadTerrainFile(path.toStdString(), path, /*z_is_depth=*/true);
}

void MapViewportHost::removeTerrainPath(const QString& path)
{
    if (m_view3d) m_view3d->removeTerrainLayer(path.toStdString());
}

void MapViewportHost::setSbpCurtainPalette(int palette_index)
{
    if (m_view3d) m_view3d->setCurtainPalette(palette_index);
}

void MapViewportHost::setRecentProjects(const QStringList& names,
                                        const QStringList& paths)
{
    if (!m_recent_items_l) return;

    while (auto* item = m_recent_items_l->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }

    const int n = std::min({ int(names.size()), int(paths.size()), 5 });
    for (int i = 0; i < n; ++i) {
        const QString path = paths[i];

        // Row = a flat button carrying its own layout (icon chip + name +
        // last-opened date). Children are mouse-transparent so the whole row
        // stays one click target.
        auto* btn = new QPushButton(m_recent_box);
        btn->setObjectName("mapRecentBtn");
        btn->setFlat(true);   // suppress native chrome; QSS owns the look
        btn->setFixedHeight(46);
        btn->setToolTip(path);
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked,
                this, [this, path]() { emit openProjectRequested(path); });

        auto* rl = new QHBoxLayout(btn);
        rl->setContentsMargins(8, 0, 10, 0);
        rl->setSpacing(10);

        auto* chip = new QLabel(btn);
        chip->setObjectName("mapRecentChip");
        chip->setFixedSize(28, 28);
        chip->setAlignment(Qt::AlignCenter);
        chip->setPixmap(Theme::icon(
            QStringLiteral(":/icons/recent_projects.svg")).pixmap(14, 14));
        chip->setAttribute(Qt::WA_TransparentForMouseEvents);

        auto* text_col = new QWidget(btn);
        text_col->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* tc = makeCompactLayout<QVBoxLayout>(text_col);
        tc->setSpacing(1);
        auto* name_lbl = new QLabel(names[i], text_col);
        name_lbl->setObjectName("mapRecentName");
        const QFileInfo fi(path);
        auto* meta_lbl = new QLabel(
            fi.exists() ? QLocale().toString(fi.lastModified().date(),
                                             QLocale::ShortFormat)
                        : tr("Not found"),
            text_col);
        meta_lbl->setObjectName("mapRecentMeta");
        tc->addStretch(1);
        tc->addWidget(name_lbl);
        tc->addWidget(meta_lbl);
        tc->addStretch(1);

        rl->addWidget(chip);
        rl->addWidget(text_col, 1);

        m_recent_items_l->addWidget(btn);
    }
    m_recent_box->setVisible(n > 0);
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
                                    std::move(hull_geo), data.opacity);
        }

        // Replay visibility after every representation exists. Whole-layer
        // visibility controls tracks, curtains, drapes, and terrain; the nav
        // toggle controls only the centreline.
        if (const auto it = m_layer_visibility.find(layer_id);
            it != m_layer_visibility.end()) {
            m_view3d->setLayerVisible(layer_id, it->second);
        }
        if (const auto it = m_nav_track_visibility.find(layer_id);
            it != m_nav_track_visibility.end()) {
            m_view3d->setNavTrackVisible(layer_id, it->second);
        }
    }
}

void MapViewportHost::loadRasterTerrain(const std::string& layer_id,
                                        const core::RasterGrid& grid)
{
    ensureView3D();
    if (m_view3d) m_view3d->loadTerrainGrid(layer_id, grid, /*z_is_depth=*/true);
}

void MapViewportHost::onLayerRemoved(const std::string& layer_id)
{
    m_layer_visibility.erase(layer_id);
    m_nav_track_visibility.erase(layer_id);
    m_view2d->removeLayerData(layer_id);
    m_view3d->removeLayer(layer_id);
    m_view3d->removeProfileCurtain(layer_id);
    m_view3d->removeSonarDrape(layer_id);
    m_view3d->removeTerrainLayer(layer_id);
}

void MapViewportHost::clearScene()
{
    m_layer_visibility.clear();
    m_nav_track_visibility.clear();
    m_view2d->clearAllLayerData();
    if (m_view3d) m_view3d->clearScene();
}

void MapViewportHost::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (m_empty_state) m_empty_state->setGeometry(rect());
    if (m_transition_cover && m_transition_cover->isVisible())
        m_transition_cover->setGeometry(m_stack->geometry());
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

void MapViewportHost::setViewportScale(double mpp)
{
    if (mpp <= 0.0 || std::isnan(mpp)) return;
    m_last_mpp = mpp;
    if (m_is_3d && m_view3d) {
        // In 3D, mpp ≈ distance * 2 / height — invert to set camera distance.
        const float h = static_cast<float>(m_view3d->height());
        if (h > 0.f) m_view3d->setDistance(static_cast<float>(mpp) * h * 0.5f);
    } else {
        if (m_view2d) m_view2d->setZoomFromMpp(mpp);
    }
}

void MapViewportHost::setRotationDeg(double deg)
{
    m_last_rot_deg = deg;
    if (m_is_3d && m_view3d) m_view3d->setYaw(deg);
    else if (m_view2d)        m_view2d->setRotationDeg(deg);
}

void MapViewportHost::panByPixels(int dx, int dy)
{
    if (m_view2d) m_view2d->panByPixels(dx, dy);
}


} // namespace dolphin::ui
