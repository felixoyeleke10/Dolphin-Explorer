#pragma once
#include "app/display/NavProcessingParams.h"
#include "ui/features/waterfall/PingRow.h"
#include "ui/features/waterfall/processing/SeabedAutoDetector.h"
#include "ui/features/waterfall/processing/WaterfallAmpProfile.h"
#include "ui/features/waterfall/WaterfallContact.h"
#include "core/Contact.h"
#include "ui/features/waterfall/WaterfallParams.h"
#include "ui/features/waterfall/WaterfallOverlayParams.h"
#include "ui/features/waterfall/rendering/WaterfallGLRenderer.h"
#include "ui/features/waterfall/rendering/WaterfallRenderer.h"
#include "ui/features/waterfall/processing/WaterfallSeabedTracker.h"
#include "ui/features/waterfall/WaterfallScrollController.h"
#include "core/SidescanPing.h"

#include <QOpenGLWidget>
#include <QPoint>
#include <QPointF>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

namespace dolphin::ui {
namespace imaging { struct SssAmplitudeContext; }

// -----------------------------------------------------------------------------
//  WaterfallView — scrolling sidescan waterfall canvas (thin orchestrator).
//
//  All rendering logic lives in WaterfallRenderer.
//  All scroll / zoom / pan state lives in WaterfallScrollController.
//  All seabed detection and drag-edit lives in WaterfallSeabedTracker.
//  All colour table logic lives in WaterfallPalette (owned by WaterfallRenderer).
//
//  This class owns the data (m_rows), wires the modules together, and handles
//  Qt event dispatch.
// -----------------------------------------------------------------------------

class WaterfallView : public QOpenGLWidget {
    Q_OBJECT
public:
    // -- Compatibility aliases ----------------------------------------------
    // WaterfallWindow still uses Params and kPalette* in a few call sites.
    // These are forwarding aliases — remove once all callers use the module types.
    using Params = WaterfallParams;
    static constexpr int kPaletteThermal   = PaletteIndex::Thermal;
    static constexpr int kPaletteGreyscale = PaletteIndex::Greyscale;
    static constexpr int kPaletteOcean     = PaletteIndex::Ocean;
    static constexpr int kPaletteCopper    = PaletteIndex::Copper;
    static constexpr int kPaletteCount     = PaletteIndex::Count;
    static const char*   paletteName(int idx);  // delegates to WaterfallPalette::name()

    explicit WaterfallView(QWidget* parent = nullptr);
    ~WaterfallView() override;

    // -- Data API -----------------------------------------------------------
    // preserve_view: keep h-zoom / h-pan state (set false on new file open).
    void setPings(const std::vector<core::SidescanPing>& pings,
                  bool preserve_view = false);

    // Result of a full display pipeline run — background-safe, no QObject access.
    struct WfPipelineResult {
        std::vector<PingRow> rows;
        float stretch_low  = 0.f;
        float stretch_high = 1.f;
    };

    // Run the canonical SSS display pipeline off-thread. Display amplitudes use
    // the same calibration/enhancement/stretch contract as the map; seabed
    // detection uses a clean calibrated copy.
    // pings are read-only; an internal working copy is made.
    // Deliver result via setPreassembledRows.
    static WfPipelineResult runPipeline(const std::vector<core::SidescanPing>& pings,
                                        const WaterfallParams&                 params,
                                        const SeabedAutoParams&                seabed_params,
                                        bool                                   seabed_enabled,
                                        const imaging::SssAmplitudeContext*    amplitude_context = nullptr);

    void setAmplitudeContext(
        std::shared_ptr<const imaging::SssAmplitudeContext> context);
    std::shared_ptr<const imaging::SssAmplitudeContext> amplitudeContext() const
        { return m_amplitude_context; }

    // Install pre-built pipeline result on the UI thread.
    // Applies manual seabed picks, updates renderer stretch, scrolls to end.
    void setPreassembledRows(std::vector<core::SidescanPing> raw_pings,
                             WfPipelineResult                result,
                             bool                            preserve_view = false);

    void clear();

    // -- Display API --------------------------------------------------------
    void setParams(const WaterfallParams& p);
    // Store params without triggering a pipeline rebuild.
    // Use when the caller is about to schedule an async re-pipeline.
    void setParamsNoRebuild(const WaterfallParams& p);

    // Apply nav corrections (layback / smoothing / offsets) to a copy of pings.
    // Background-safe; no Qt dependency.  Used by WaterfallWindow async nav path.
    static std::vector<core::SidescanPing>
    runNavCorrections(std::vector<core::SidescanPing> pings,
                      const NavProcessingParams& params);
    const WaterfallParams& params() const { return m_params; }

    // -- Scale API ---------------------------------------------------------
    // Set vertical (along-track) scale.  0 = auto (kWfRowHeight default).
    void setVerticalScale(float pings_per_cm);
    // Set horizontal (across-track) scale.  0 = auto-fit all samples.
    void setHorizontalScale(float samples_per_cm);

    // -- Seabed API --------------------------------------------------------
    // Set the active manual tool: 0=none, 1=pen (drag), 2=insert, 3=remove.
    void setSeabedTool(int tool);
    // Set which side manual seabed tools edit: 0=Both, 1=Port, 2=Starboard.
    void setSeabedChannel(int ch);
    // Re-run auto-detection on every currently-loaded row using the given params.
    // Also sets the internal enabled flag so subsequent window loads re-apply.
    void redetectSeabed(const SeabedAutoParams& params);
    // Update the cached seabed params without re-running detection.
    // Called by the loader after it has already baked picks into pre-assembled rows.
    void setSeabedAutoParamsOnly(const SeabedAutoParams& p, bool enabled) {
        m_seabed_auto_params = p;
        m_seabed_enabled     = enabled;
    }
    // Clear auto-detection results and disable auto-re-apply on window loads.
    void clearSeabedDetection();
    // Clear seabed picks for a new layer without disabling auto-detection.
    void resetSeabedForNewLayer();
    // Show or hide the seabed overlay line when no editing tool is active.
    void setShowSeabedLine(bool show);
    // Show or hide the amplitude profile chart at the bottom of the canvas.
    void setShowAmpBar(bool show);

    // Overlay style — crosshair colour/style and range grid.
    const WfOverlayParams& overlayParams() const { return m_overlay_params; }
    void setOverlayParams(const WfOverlayParams& p);

    // -- Contact API -------------------------------------------------------
    // Set the active contact tool: 0=none, 1=pick.
    // Activating any contact tool deactivates the seabed tool and vice versa.
    void setContactTool(int tool);
    // Classification applied to the next pick (kept in sync with the panel combo).
    void setContactClass(ContactClass cls) { m_contact_class = cls; }
    // Remove all picked contacts from the current view.
    void clearContacts();
    // Scan the loaded window for isolated bright targets followed by a darker
    // acoustic shadow. sensitivity: 0=conservative, 1=balanced, 2=sensitive.
    // Returns the number of candidates promoted through contactPicked().
    int detectContactCandidates(int sensitivity);
    // Read back all contacts placed in the current window.
    const std::vector<WfContact>& contacts() const { return m_contacts; }

    // -- Feature API -------------------------------------------------------
    // Set the active feature draw tool: 0=none, 1=polygon, 2=line.
    // Activating it deactivates the contact and seabed tools (and vice versa).
    void setFeatureTool(int tool);
    // Set externally-supplied project contacts for the current window.
    // Replaces the existing external list and triggers a repaint.
    // window_first_row: absolute row index of the first row in the current window.
    void refreshExternalContacts(const std::vector<core::Contact>& contacts,
                                  int window_first_row);

    // -- Query API ---------------------------------------------------------
    int   rowCount()       const;
    // Returns the raw ping at index idx, or nullptr if out of range.
    const core::SidescanPing* pingAt(int idx) const;
    // Read-only access to all raw pings (for external re-pipeline without disk I/O).
    const std::vector<core::SidescanPing>& rawPings() const { return m_raw_pings; }
    // Populate bottom_pick on each of the supplied pings from the viewer's current
    // seabed detection state (m_rows for auto-detected, m_manual_seabed for user edits).
    // source=1 (auto), source=2 (manual).  Pings not in either map are left unchanged.
    void applySeabedPicksToPings(std::vector<core::SidescanPing>& pings) const;
    int   scrollRow()      const { return m_scroll.scrollRow(); }
    int   visibleRows()    const { return m_renderer.maxVisible(); }
    int   samplesPerPing() const;
    float lineLengthMetres()  const;   // cumulative GPS track length of loaded pings
    float frequencyHz()       const;   // first non-zero frequency from loaded pings
    float soundVelocityMs()   const;   // first non-zero sound velocity from loaded pings
    const SeabedAutoParams& seabedAutoParams() const { return m_seabed_auto_params; }
    bool seabedEnabled() const { return m_seabed_enabled; }
    // Slant Range Correction diagnostics. SRC remaps each column against the seabed
    // altitude (detected seabed range, falling back to the sensor's nav altitude).
    // Reports how many rows have a usable altitude and the altitude/swath span, so
    // the UI can show what SRC is actually working with (and how large the effect is).
    struct SrcStats {
        int   total      = 0;     // loaded rows
        int   with_alt   = 0;     // rows with a usable altitude reference
        float alt_min_m  = 0.f;   // min/max altitude across rows that have one
        float alt_max_m  = 0.f;
        float range_max_m = 0.f;  // max slant range (swath edge)
    };
    SrcStats srcStats() const;

    void scrollToRow(int row);
    void scrollToEnd();

    // Returns true if the OpenGL GPU path is active; false if on the CPU fallback.
    bool isGpuRendering() const;
    // Enable or disable GPU rendering. false = force CPU fallback immediately.
    // true = re-initialize the GL renderer if a valid context is available.
    void setGpuAccel(bool enabled);

signals:
    // Status bar: cursor range/depth/nav info
    void cursorMoved(int ping_idx, float range_port_m, float range_stbd_m,
                     float depth_port_m, float depth_stbd_m,
                     double lat, double lon, bool is_projected);
    // User click (not a drag-edit end)
    void pingClicked(int ping_idx, core::SidescanChannel channel, float range_m);
    // Contact placed by the user with the contact pick tool.
    // lat/lon are 0.0 when nav is unavailable for that row.
    // snapshot is a square grab of the waterfall around the pick (may be null).
    void contactPicked(int row_idx, core::SidescanChannel ch,
                       float range_m, double lat, double lon, bool is_projected,
                       const QPixmap& snapshot);
    // Feature (polygon/polyline) drawn on the waterfall. vertices are (lon,lat);
    // polygon=true closes the shape. The window attaches classification + line.
    void featureDrawn(const std::vector<QPointF>& lonlat_vertices, bool polygon,
                      bool is_projected);
    // Double-click on a project-contact marker (no tool active) — open the
    // "Edit contact details" editor for this contact.
    void contactEditRequested(uint64_t contact_id);
    // Scroll position changed — drives the external QScrollBar
    void scrollChanged(int scroll_row, int total_rows, int visible_rows);
    // Scroll attempt past data boundary; direction = -1 (top) or +1 (bottom)
    void scrollBeyondBounds(int direction);
    // Right-click; globalPos is in screen coordinates for QMenu::exec().
    void contextMenuRequested(QPoint globalPos);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void leaveEvent(QEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;

private slots:
    void cleanupGL();

private:
    // Recompute WfLayout from current widget dimensions; called on resize.
    void updateLayout();

    // Commit the in-progress feature draft (double-click / Enter). Strips the
    // near-duplicate final vertex a double-click's own press adds, enforces the
    // minimum vertex count, emits featureDrawn, and clears the draft.
    void commitFeatureDraft();

    // Single-argument wrappers around the anonymous free functions in
    // WaterfallViewProcessing.cpp — use m_params implicitly so that
    // setPings() doesn't have to name the params directly.

    // Overlay m_manual_seabed onto the current m_rows after assembly/detection.
    void applyManualSeabedPicks();

    // After an eraser pass, fill every gap in seabed.range_m with a straight
    // line interpolated between the nearest valid picks on each side.
    void interpolateSeabedGaps();

    // Box tool: threshold-detect seabed within rows [r0,r1] constrained to the
    // range window [range_min_m, range_max_m], followed by a light smoothing pass.
    void detectSeabedInBox(int r0, int r1, float range_min_m, float range_max_m);

    // Pen smart snap: given a cursor screen_x on a row, return the range_m of the
    // peak amplitude sample within ±kPenSnapRadius samples of the cursor position.
    float smartPenRange(int row, int screen_x) const;

    // Map a click (row + across-track channel/range) to a geographic position by
    // offsetting the row's nav point by ground range perpendicular to heading.
    // Shared by contact picking and feature drawing. Returns false with no nav.
    bool rangeToGeo(int row, core::SidescanChannel ch, float range_m,
                    double& lat, double& lon, bool& is_projected) const;

    // Overlay painting shared between GPU and CPU paths.
    void paintOverlays(QPainter& p, const WfLayout& lay);

    // Scan m_rows, compute the 1st/99th percentile of assembled uint16 amplitudes,
    // and store the result in m_stretch_low/high so the viewer maps the data's
    // actual dynamic range to the full palette range.
    void computeAutoStretch();

    // -- Members ------------------------------------------------------------
    std::vector<core::SidescanPing>   m_raw_pings;          // stored for re-assembly on Apply
    std::shared_ptr<const imaging::SssAmplitudeContext> m_amplitude_context;
    std::vector<PingRow>              m_rows;
    std::unordered_map<int64_t,float> m_manual_seabed;     // timestamp_us → range_m; survives window changes
    WaterfallParams                 m_params;
    SeabedAutoParams                m_seabed_auto_params;  // last-used detection params
    WaterfallRenderer               m_renderer;
    WaterfallSeabedTracker          m_seabed;
    WaterfallScrollController       m_scroll;

    WaterfallGLRenderer       m_gl_renderer;
    bool  m_gl_initialized    = false;
    bool  m_gl_data_dirty     = false;  // amplitude texture needs re-upload
    bool  m_gl_src_dirty      = false;  // SRC params need re-upload (seabed edit)

    bool  m_dirty            = true;
    bool  m_layout_dirty     = false;  // force updateLayout even when dimensions unchanged
    bool  m_seabed_enabled   = true;   // always auto-detect; cleared only on explicit reset
    int   m_seabed_channel   = 0;      // 0=Both, 1=Port, 2=Starboard (manual tool filter)
    float m_v_pings_per_cm   = 0.f;   // 0 = auto (kWfRowHeight)
    float m_h_samples_per_cm = 0.f;   // 0 = auto-fit
    bool m_show_seabed_line = true;    // hidden only when SRC is active
    bool m_show_amp_bar     = true;    // amplitude profile chart at bottom
    WfOverlayParams m_overlay_params;
    int  m_seabed_tool  = 0;   // 0=none, 1=pen (drag), 2=box (drag straight line), 3=eraser

    // -- Box tool drag state (tool == 2) -----------------------------------
    int   m_box_anchor_row   = -1;   // row at press; -1 when not dragging
    float m_box_anchor_range = 0.f;
    int   m_box_press_sx     = -1;   // screen coords at press (rubber-band preview)
    int   m_box_press_sy     = -1;

    // -- Pen last position for gap interpolation (tool == 1) --------------
    int   m_pen_last_row     = -1;
    float m_pen_last_range   = 0.f;
    int          m_contact_tool  = 0;               // 0=none, 1=pick
    ContactClass m_contact_class = ContactClass::Unknown;  // applied to next pick

    // -- Feature draw tool state (polygon/polyline) ------------------------
    int                  m_feature_tool = 0;   // 0=none, 1=polygon, 2=line, 3=pen
    std::vector<QPointF> m_feature_pts;        // committed vertices (lon,lat)
    std::vector<QPoint>  m_feature_px;         // matching screen points for the draft
    bool                 m_feature_proj = false;  // is_projected of the draft rows
    bool                 m_feature_pen_down = false;  // pen freehand capture in progress
    int  m_cursor_x = -1;
    int  m_cursor_y = -1;
    int   m_last_w   = -1;   // cached width to detect resize in paintEvent
    int   m_last_h   = -1;
    qreal m_last_dpr = -1.0;

    // Percentile stretch values derived from the last loaded data set.
    // Persists across palette / gain / contrast changes so the stretch
    // is not reset every time the user tweaks a display knob.
    float m_stretch_low  = 0.f;
    float m_stretch_high = 1.f;

    // Generation counter: incremented whenever a new background rebuild is started
    // or new ping data is loaded.  The finished handler discards results whose
    // generation no longer matches, so rapid param changes don't produce flicker.
    std::atomic<uint64_t> m_rebuild_gen{0};

    // Cached amplitude profile — only recomputed when scroll row changes or rows are rebuilt.
    WaterfallAmpProfile m_cached_amp_profile;
    int  m_amp_profile_scroll_row = -1;  // scroll row the cache was built for
    bool m_amp_profile_dirty      = true;

    std::vector<WfContact> m_contacts;
    std::vector<WfContact> m_external_contacts;  // synced from project contacts
};

} // namespace dolphin::ui
