// GL initialization smoke test.
//
// Verifies WaterfallView and MapView3D start up without shader errors.
// Run with QT_QPA_PLATFORM=offscreen for headless environments (Mesa software
// renderer); on a developer machine the native platform is used automatically.
//
// Failure modes caught:
//   - Shader compilation/link error (MapView3D::glInitError fires)
//   - WaterfallGLRenderer::initialize() returns false (WaterfallView falls back
//     to CPU — logged as a note, not a test failure, since CPU path is valid)
//   - Hard crash during initializeGL (would propagate as a non-zero exit code)

#include "ui/features/waterfall/WaterfallView.h"
#include "ui/features/map/MapView3D.h"
#include "ui/features/map/MapViewportHost.h"
#include "ui/features/map/MapView.h"

#include <QApplication>
#include <QMouseEvent>
#include <cstdio>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); \
        ++g_fail; \
    } else { \
        ++g_pass; \
        std::fprintf(stdout, "PASS  %s\n", #expr); \
    } \
} while (false)

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // ── WaterfallView: GPU path initializes without crashing ──────────────────
    {
        dolphin::ui::WaterfallView view;
        view.resize(400, 300);
        view.show();
        view.repaint();  // force initializeGL to fire synchronously
        QApplication::processEvents();

        if (view.isGpuRendering()) {
            ++g_pass;
            std::fprintf(stdout, "PASS  WaterfallView GPU path active (GL 3.3 available)\n");
        } else {
            std::fprintf(stdout, "NOTE  WaterfallView using CPU fallback (GL 3.3 unavailable — acceptable)\n");
        }
    }

    // ── MapView3D: no shader errors on initialization ─────────────────────────
    {
        int gl_errors = 0;
        dolphin::ui::MapView3D view;
        QObject::connect(&view, &dolphin::ui::MapView3D::glInitError,
                         [&gl_errors](const QString& msg) {
            std::fprintf(stderr, "GL shader error: %s\n", msg.toUtf8().constData());
            ++gl_errors;
        });
        view.resize(800, 600);
        view.show();
        view.update();  // MapView3D is a QOpenGLWindow — schedule a frame
        QApplication::processEvents();
        CHECK(gl_errors == 0);

        int hover_events = 0;
        QObject::connect(&view, &dolphin::ui::MapView3D::hoverLayerChanged,
                         [&hover_events](const std::string&, QPoint) { ++hover_events; });
        view.setHoverTooltipsEnabled(true);
        view.setHoverHighlightEnabled(true);
        QMouseEvent move(QEvent::MouseMove, QPointF(100, 100), QPointF(100, 100),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&view, &move);
        CHECK(hover_events >= 1);
    }

    // One shared 2D model is authoritative for both modes. Two inserted survey
    // lines must therefore produce two nav layers and two mosaics in 3D as well.
    {
        dolphin::ui::MapViewportHost host;
        host.resize(800, 600);
        host.show();

        auto addLine = [&](const std::string& id, double offset) {
            dolphin::ui::LayerMapData data;
            data.kind = dolphin::ui::LayerMapKind::Swath;
            data.is_projected = true;
            data.nav_track = {QPointF(offset, 0.0), QPointF(offset + 10.0, 20.0)};
            data.lon_min = offset;
            data.lon_max = offset + 10.0;
            data.lat_min = 0.0;
            data.lat_max = 20.0;
            data.preview_image = QImage(4, 4, QImage::Format_RGBA8888);
            data.preview_image.fill(Qt::white);
            host.view2D()->setLayerMapData(id, std::move(data));
        };
        addLine("line-a", 0.0);
        addLine("line-b", 30.0);
        QApplication::processEvents();

        CHECK(host.view2D()->layerDataIds().size() == 2);
        CHECK(host.view3D()->navLayerCount() == 2);
        CHECK(host.view3D()->drapeLayerCount() == 2);
    }

    std::fprintf(stdout, "\n%d/%d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail != 0 ? 1 : 0;
}
