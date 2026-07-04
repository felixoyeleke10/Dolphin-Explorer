// MapView3D.Load.cpp — XYZ/CSV terrain file loading (background thread).

#include "ui/features/map/MapView3D.h"

#include <QFile>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QString>
#include <QTextStream>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  Thread-safe terrain build result
// -----------------------------------------------------------------------------

struct TerrainBuildResult {
    std::vector<float> vertices;      // full-res xyz triples, GL_TRIANGLES
    std::vector<float> lod_vertices;  // half-res xyz triples, GL_TRIANGLES
    float z_min   = 0.f;
    float z_max   = 0.f;
    float radius  = 0.f;
    float x_min_local = 0.f, x_max_local = 0.f;
    float y_min_local = 0.f, y_max_local = 0.f;
    double auto_origin_x  = 0.0;
    double auto_origin_y  = 0.0;
    bool   is_projected   = false;
    bool   has_auto_origin = false;
    QString error;
};

// buildTerrainFromFile — pure function, runs on a background thread.
// All inputs captured by value; no Qt widgets are touched.
// VE is NOT applied here — the terrain shader handles it as a uniform so
// the user can change VE without reloading the file.
// When has_origin=false the function auto-detects an origin from the data centre.
static TerrainBuildResult buildTerrainFromFile(
    const QString& path,
    bool   has_origin,
    double origin_x, double origin_y,
    bool   is_projected,
    bool   z_is_depth)
{
    TerrainBuildResult result;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error = QStringLiteral("Cannot open: ") + path;
        return result;
    }

    // -- Parse XYZ / CSV (3 numeric columns; skip header/comment lines) --------
    struct RawPt { double x, y, z; };
    std::vector<RawPt> pts;
    pts.reserve(300000);

    static const QRegularExpression kSplit(QStringLiteral("[,\\s]+"));
    QTextStream ts(&file);
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith('!'))
            continue;
        const QStringList parts = line.split(kSplit, Qt::SkipEmptyParts);
        if (parts.size() < 3) continue;

        bool ok1, ok2, ok3;
        const double fx = parts[0].toDouble(&ok1);
        const double fy = parts[1].toDouble(&ok2);
              double fz = parts[2].toDouble(&ok3);
        if (!ok1 || !ok2 || !ok3) continue;

        if (z_is_depth) fz = -fz;   // positive depth → negative altitude
        pts.push_back({fx, fy, fz});
    }

    if (pts.size() < 3) {
        result.error = QStringLiteral("Too few valid XYZ points in file.");
        return result;
    }

    // -- Auto-detect origin when none is set -----------------------------------
    if (!has_origin) {
        double xmin = pts[0].x, xmax = pts[0].x;
        double ymin = pts[0].y, ymax = pts[0].y;
        for (const auto& p : pts) {
            xmin = std::min(xmin, p.x);  xmax = std::max(xmax, p.x);
            ymin = std::min(ymin, p.y);  ymax = std::max(ymax, p.y);
        }
        origin_x = (xmin + xmax) * 0.5;
        origin_y = (ymin + ymax) * 0.5;
        // Heuristic: lon/lat if all X ∈ [−180,180] and Y ∈ [−90,90]
        is_projected = !(xmin >= -180.0 && xmax <= 180.0 &&
                         ymin >=  -90.0 && ymax <=  90.0);
        result.auto_origin_x   = origin_x;
        result.auto_origin_y   = origin_y;
        result.is_projected    = is_projected;
        result.has_auto_origin = true;
    }

    // -- Convert to local metres -----------------------------------------------
    std::vector<float> raw;
    raw.reserve(pts.size() * 3);
    for (const auto& p : pts) {
        double lx, ly;
        if (is_projected) {
            lx = p.x - origin_x;
            ly = p.y - origin_y;
        } else {
            static constexpr double kMPD = 111320.0;
            const double cos_lat = std::cos(origin_y * M_PI / 180.0);
            lx = (p.x - origin_x) * cos_lat * kMPD;
            ly = (p.y - origin_y)            * kMPD;
        }
        raw.push_back(static_cast<float>(lx));
        raw.push_back(static_cast<float>(ly));
        raw.push_back(static_cast<float>(p.z));
    }

    if (raw.size() < 9) {
        result.error = QStringLiteral("All points collapsed after coordinate conversion.");
        return result;
    }

    // -- Bounding box ----------------------------------------------------------
    float xmin = raw[0], xmax = raw[0];
    float ymin = raw[1], ymax = raw[1];
    result.z_min = raw[2];
    result.z_max = raw[2];

    for (size_t i = 0; i < raw.size(); i += 3) {
        xmin = std::min(xmin, raw[i]);     xmax = std::max(xmax, raw[i]);
        ymin = std::min(ymin, raw[i+1]);   ymax = std::max(ymax, raw[i+1]);
        result.z_min = std::min(result.z_min, raw[i+2]);
        result.z_max = std::max(result.z_max, raw[i+2]);
    }

    const float span_x = xmax - xmin;
    const float span_y = ymax - ymin;
    if (span_x <= 0.f || span_y <= 0.f) {
        result.error = QStringLiteral("Terrain data has zero extent in at least one axis — "
                                      "all points must vary in both X and Y.");
        return result;
    }
    result.radius      = 0.5f * std::sqrt(span_x * span_x + span_y * span_y);
    result.x_min_local = xmin;  result.x_max_local = xmax;
    result.y_min_local = ymin;  result.y_max_local = ymax;

    // -- Build regular grid -----------------------------------------------------
    // Aim for ~4 points per cell; resolution clamped to [64, 512].
    const int n_pts = static_cast<int>(raw.size()) / 3;
    const int GRID  = std::clamp(static_cast<int>(std::sqrt(n_pts / 4.0)), 64, 512);

    const float cell_x = (GRID > 1) ? span_x / (GRID - 1) : 1.f;
    const float cell_y = (GRID > 1) ? span_y / (GRID - 1) : 1.f;

    std::vector<double> grid_z_sum(GRID * GRID, 0.0);
    std::vector<int>    grid_cnt  (GRID * GRID, 0);

    for (size_t i = 0; i < raw.size(); i += 3) {
        const int gx = std::clamp(
            static_cast<int>((raw[i]   - xmin) / cell_x), 0, GRID - 1);
        const int gy = std::clamp(
            static_cast<int>((raw[i+1] - ymin) / cell_y), 0, GRID - 1);
        const int idx = gy * GRID + gx;
        grid_z_sum[idx] += raw[i+2];
        grid_cnt[idx]++;
    }

    std::vector<float> grid_z(GRID * GRID, std::numeric_limits<float>::quiet_NaN());
    for (int i = 0; i < GRID * GRID; ++i)
        if (grid_cnt[i] > 0)
            grid_z[i] = static_cast<float>(grid_z_sum[i] / grid_cnt[i]);

    // -- Emit GL_TRIANGLES: two per 2×2 quad with all four corners present -----
    auto has = [&](int gx, int gy) -> bool {
        return gx >= 0 && gx < GRID && gy >= 0 && gy < GRID
            && grid_cnt[gy * GRID + gx] > 0;
    };
    auto V = [&](int gx, int gy) -> std::array<float,3> {
        return { xmin + gx * cell_x, ymin + gy * cell_y, grid_z[gy * GRID + gx] };
    };

    result.vertices.reserve(GRID * GRID * 6 * 3);
    for (int gy = 0; gy < GRID - 1; ++gy) {
        for (int gx = 0; gx < GRID - 1; ++gx) {
            if (has(gx, gy) && has(gx+1, gy) && has(gx, gy+1)) {
                auto a = V(gx, gy), b = V(gx+1, gy), c = V(gx, gy+1);
                result.vertices.insert(result.vertices.end(),
                    {a[0],a[1],a[2], b[0],b[1],b[2], c[0],c[1],c[2]});
            }
            if (has(gx+1, gy) && has(gx+1, gy+1) && has(gx, gy+1)) {
                auto b = V(gx+1, gy), d = V(gx+1, gy+1), c = V(gx, gy+1);
                result.vertices.insert(result.vertices.end(),
                    {b[0],b[1],b[2], d[0],d[1],d[2], c[0],c[1],c[2]});
            }
        }
    }

    // -- Build half-resolution LOD mesh ---------------------------------------
    {
        const int LGRID = std::clamp(GRID / 2, 32, 256);
        const float lcell_x = (LGRID > 1) ? span_x / (LGRID - 1) : span_x;
        const float lcell_y = (LGRID > 1) ? span_y / (LGRID - 1) : span_y;

        // Map LOD grid index to nearest full-res grid index
        auto fg = [&](int li) -> int {
            return (LGRID > 1)
                ? std::clamp(li * (GRID - 1) / (LGRID - 1), 0, GRID - 1)
                : 0;
        };
        auto lhas = [&](int lx, int ly) -> bool {
            return grid_cnt[fg(ly) * GRID + fg(lx)] > 0;
        };
        auto LV = [&](int lx, int ly) -> std::array<float, 3> {
            const int fx = fg(lx), fy = fg(ly);
            return { xmin + lx * lcell_x, ymin + ly * lcell_y, grid_z[fy * GRID + fx] };
        };

        result.lod_vertices.reserve(static_cast<size_t>(LGRID * LGRID * 6 * 3));
        for (int ly = 0; ly < LGRID - 1; ++ly) {
            for (int lx = 0; lx < LGRID - 1; ++lx) {
                if (lhas(lx, ly) && lhas(lx+1, ly) && lhas(lx, ly+1)) {
                    auto a = LV(lx, ly), b = LV(lx+1, ly), c = LV(lx, ly+1);
                    result.lod_vertices.insert(result.lod_vertices.end(),
                        {a[0],a[1],a[2], b[0],b[1],b[2], c[0],c[1],c[2]});
                }
                if (lhas(lx+1, ly) && lhas(lx+1, ly+1) && lhas(lx, ly+1)) {
                    auto b = LV(lx+1, ly), d = LV(lx+1, ly+1), c = LV(lx, ly+1);
                    result.lod_vertices.insert(result.lod_vertices.end(),
                        {b[0],b[1],b[2], d[0],d[1],d[2], c[0],c[1],c[2]});
                }
            }
        }
    }

    return result;
}

// -----------------------------------------------------------------------------
//  loadTerrainFile — public API, dispatches background parse + GL upload
// -----------------------------------------------------------------------------

// buildTerrainFromGrid — pure function, runs on a background thread.
// A depth raster is already a regular grid, so it triangulates directly (no
// resampling). Decimated to <= ~512 nodes/axis so a large GeoTIFF still yields a
// manageable mesh. No-data / non-finite cells are skipped (holes in the mesh).
static TerrainBuildResult buildTerrainFromGrid(
    const core::RasterGrid& grid,
    bool   has_origin,
    double origin_x, double origin_y,
    bool   is_projected,
    bool   z_is_depth)
{
    TerrainBuildResult result;
    const uint32_t cols = grid.cols, rows = grid.rows;
    if (cols < 2 || rows < 2 ||
        grid.data.size() != static_cast<size_t>(cols) * rows) {
        result.error = QStringLiteral("Raster grid is empty or malformed.");
        return result;
    }

    const double* gt = grid.geo_transform;
    auto worldX = [&](double c, double r) { return gt[0] + c * gt[1] + r * gt[2]; };
    auto worldY = [&](double c, double r) { return gt[3] + c * gt[4] + r * gt[5]; };

    // World extent from the four corners.
    const double cx[4] = { worldX(0,0), worldX(cols-1,0), worldX(0,rows-1), worldX(cols-1,rows-1) };
    const double cy[4] = { worldY(0,0), worldY(cols-1,0), worldY(0,rows-1), worldY(cols-1,rows-1) };
    double wxmin = cx[0], wxmax = cx[0], wymin = cy[0], wymax = cy[0];
    for (int i = 1; i < 4; ++i) {
        wxmin = std::min(wxmin, cx[i]); wxmax = std::max(wxmax, cx[i]);
        wymin = std::min(wymin, cy[i]); wymax = std::max(wymax, cy[i]);
    }

    if (!has_origin) {
        origin_x = (wxmin + wxmax) * 0.5;
        origin_y = (wymin + wymax) * 0.5;
        is_projected = !(wxmin >= -180.0 && wxmax <= 180.0 &&
                         wymin >=  -90.0 && wymax <=  90.0);
        result.auto_origin_x   = origin_x;
        result.auto_origin_y   = origin_y;
        result.is_projected    = is_projected;
        result.has_auto_origin = true;
    }

    static constexpr double kMPD = 111320.0;
    const double cos_lat = std::cos(origin_y * M_PI / 180.0);
    auto toLocal = [&](double wx, double wy, float& lx, float& ly) {
        if (is_projected) { lx = float(wx - origin_x); ly = float(wy - origin_y); }
        else { lx = float((wx - origin_x) * cos_lat * kMPD);
               ly = float((wy - origin_y)            * kMPD); }
    };

    const float nodata = grid.no_data_value;

    // Build a decimated triangle mesh at the given step; valid cells only.
    auto buildMesh = [&](uint32_t sx, uint32_t sy, std::vector<float>& out) {
        const uint32_t NX = (cols - 1) / sx + 1;
        const uint32_t NY = (rows - 1) / sy + 1;
        std::vector<float> lx(static_cast<size_t>(NX) * NY);
        std::vector<float> ly(static_cast<size_t>(NX) * NY);
        std::vector<float> lz(static_cast<size_t>(NX) * NY);
        std::vector<char>  ok(static_cast<size_t>(NX) * NY, 0);

        for (uint32_t j = 0; j < NY; ++j)
            for (uint32_t i = 0; i < NX; ++i) {
                const uint32_t c = std::min(i * sx, cols - 1);
                const uint32_t r = std::min(j * sy, rows - 1);
                const float z = grid.data[static_cast<size_t>(r) * cols + c];
                const size_t k = static_cast<size_t>(j) * NX + i;
                if (std::isfinite(z) && z != nodata) {
                    toLocal(worldX(c, r), worldY(c, r), lx[k], ly[k]);
                    lz[k] = z_is_depth ? -z : z;
                    ok[k] = 1;
                }
            }

        out.reserve(static_cast<size_t>(NX - 1) * (NY - 1) * 6 * 3);
        for (uint32_t j = 0; j < NY - 1; ++j)
            for (uint32_t i = 0; i < NX - 1; ++i) {
                const size_t a = static_cast<size_t>(j) * NX + i;
                const size_t b = a + 1, c2 = a + NX, d = c2 + 1;
                if (ok[a] && ok[b] && ok[c2])
                    out.insert(out.end(), { lx[a],ly[a],lz[a], lx[b],ly[b],lz[b], lx[c2],ly[c2],lz[c2] });
                if (ok[b] && ok[d] && ok[c2])
                    out.insert(out.end(), { lx[b],ly[b],lz[b], lx[d],ly[d],lz[d], lx[c2],ly[c2],lz[c2] });
            }
    };

    constexpr uint32_t kTarget = 512;
    const uint32_t sx = std::max<uint32_t>(1, (cols + kTarget - 1) / kTarget);
    const uint32_t sy = std::max<uint32_t>(1, (rows + kTarget - 1) / kTarget);

    buildMesh(sx, sy, result.vertices);
    if (result.vertices.empty()) return result;   // all no-data → applyTerrainResult reports it

    buildMesh(sx * 2, sy * 2, result.lod_vertices);

    // Bounding box + z range from the emitted vertices (xyz triples).
    float xmin = result.vertices[0], xmax = result.vertices[0];
    float ymin = result.vertices[1], ymax = result.vertices[1];
    result.z_min = result.vertices[2]; result.z_max = result.vertices[2];
    for (size_t i = 0; i < result.vertices.size(); i += 3) {
        xmin = std::min(xmin, result.vertices[i]);     xmax = std::max(xmax, result.vertices[i]);
        ymin = std::min(ymin, result.vertices[i+1]);   ymax = std::max(ymax, result.vertices[i+1]);
        result.z_min = std::min(result.z_min, result.vertices[i+2]);
        result.z_max = std::max(result.z_max, result.vertices[i+2]);
    }
    const float span_x = xmax - xmin, span_y = ymax - ymin;
    result.radius      = 0.5f * std::sqrt(span_x * span_x + span_y * span_y);
    result.x_min_local = xmin;  result.x_max_local = xmax;
    result.y_min_local = ymin;  result.y_max_local = ymax;
    return result;
}

void MapView3D::loadTerrainFile(const std::string& layer_id,
                                 const QString& path,
                                 bool z_is_depth)
{
    // Capture origin state by value — background thread must not access Qt objects.
    const bool   has_orig = m_has_origin;
    const double ox       = m_origin_x;
    const double oy       = m_origin_y;
    const bool   is_proj  = m_is_projected;

    m_terrain_loading = true;   // shows the in-HUD "Loading terrain…" chip
    update();

    auto* watcher = new QFutureWatcher<TerrainBuildResult>(this);
    connect(watcher, &QFutureWatcher<TerrainBuildResult>::finished, this,
            [this, watcher, layer_id]() {
                TerrainBuildResult res = watcher->result();
                watcher->deleteLater();
                applyTerrainResult(layer_id, std::move(res));
            });

    watcher->setFuture(QtConcurrent::run(
        [path, has_orig, ox, oy, is_proj, z_is_depth]() {
            return buildTerrainFromFile(path, has_orig, ox, oy, is_proj, z_is_depth);
        }));
}

// Shared apply path for both the file and grid terrain loaders.
void MapView3D::applyTerrainResult(const std::string& layer_id, TerrainBuildResult&& res)
{
    if (m_terrain_loading) { m_terrain_loading = false; update(); }
    if (!res.error.isEmpty()) {
        emit terrainLoadFinished(layer_id, false, res.error);
        return;
    }
    if (res.vertices.empty()) {
        emit terrainLoadFinished(layer_id, false,
            tr("No triangles built — data may be too sparse or flat."));
        return;
    }

    // Adopt auto-detected origin if we had none.
    if (res.has_auto_origin && !m_has_origin)
        setSceneOrigin(res.auto_origin_x, res.auto_origin_y, res.is_projected);

    auto it = std::find_if(m_terrain_layers.begin(), m_terrain_layers.end(),
        [&](const TerrainMesh3D& T){ return T.id == layer_id; });
    if (it == m_terrain_layers.end()) {
        m_terrain_layers.push_back(TerrainMesh3D{});
        it = m_terrain_layers.end() - 1;
        it->id = layer_id;
    }
    it->cpu_verts     = std::move(res.vertices);
    it->lod_cpu_verts = std::move(res.lod_vertices);
    it->z_min         = res.z_min;
    it->z_max         = res.z_max;
    it->bbox_xmin  = res.x_min_local;  it->bbox_xmax = res.x_max_local;
    it->bbox_ymin  = res.y_min_local;  it->bbox_ymax = res.y_max_local;
    it->dirty      = true;
    m_terrain_dirty = true;
    m_survey_dirty  = true;

    if (res.radius > m_scene_radius) {
        m_scene_radius = res.radius;
        if (!m_camera_user_moved) fitToScene();
    }
    update();
    emit terrainLoadFinished(layer_id, true, {});
}

void MapView3D::loadTerrainGrid(const std::string& layer_id,
                                const core::RasterGrid& grid,
                                bool z_is_depth)
{
    const bool   has_orig = m_has_origin;
    const double ox       = m_origin_x;
    const double oy       = m_origin_y;
    const bool   is_proj  = m_is_projected;

    auto* watcher = new QFutureWatcher<TerrainBuildResult>(this);
    connect(watcher, &QFutureWatcher<TerrainBuildResult>::finished, this,
            [this, watcher, layer_id]() {
                TerrainBuildResult res = watcher->result();
                watcher->deleteLater();
                applyTerrainResult(layer_id, std::move(res));
            });

    // Copy the grid by value into the task — the background thread must not race
    // the caller's RasterGrid lifetime.
    watcher->setFuture(QtConcurrent::run(
        [grid, has_orig, ox, oy, is_proj, z_is_depth]() {
            return buildTerrainFromGrid(grid, has_orig, ox, oy, is_proj, z_is_depth);
        }));
}

void MapView3D::removeTerrainLayer(const std::string& layer_id)
{
    auto it = std::find_if(m_terrain_layers.begin(), m_terrain_layers.end(),
                           [&](const TerrainMesh3D& T){ return T.id == layer_id; });
    if (it == m_terrain_layers.end()) return;
    if (m_gl_ready) {
        makeCurrent();
        it->vbo.destroy();
        it->vbo_lod.destroy();
        doneCurrent();
    }
    m_terrain_layers.erase(it);
    m_survey_dirty = true;
    update();
}

} // namespace dolphin::ui
