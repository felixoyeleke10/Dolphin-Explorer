#pragma once
#include "ui/features/waterfall/NavProcessingParams.h"
#include "ui/features/waterfall/processing/SeabedAutoDetector.h"
#include "ui/features/waterfall/WaterfallContact.h"
#include "ui/features/waterfall/WaterfallParams.h"
#include <QFrame>

class QComboBox;
class QPushButton;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace dolphin::ui {

class WfValueRow;
class WfToggleRow;

// -----------------------------------------------------------------------------
//  WaterfallAnalysisPanel — right image-processing panel in the Waterfall window.
//
//  All numeric parameters use WfValueRow: drag up/down to change, click to
//  type, wheel always ignored — impossible to change a value by accident.
//
//  Three categories of controls:
//    • Image Processing (WaterfallParams) — applied via "Apply to This/All Lines"
//    • Processing Tools (WaterfallParams) — applied via same Apply buttons
//    • Navigation Processing (NavProcessingParams) — applied via dedicated
//      "Run on This Line / All Lines" buttons; modifies stored raw ping nav.
// -----------------------------------------------------------------------------

class WaterfallAnalysisPanel : public QFrame {
    Q_OBJECT
public:
    enum SeabedTool { ToolNone = 0, ToolPen = 1, ToolBox = 2, ToolEraser = 3 };

    explicit WaterfallAnalysisPanel(QWidget* parent = nullptr);

    WaterfallParams      currentParams(int palette_index) const;
    NavProcessingParams  currentNavParams() const;
    void                 setParams(const WaterfallParams& p);
    void                 setContactPickActive(bool active);
    void                 setSeabedToolActive(int tool);
    SeabedAutoParams     currentSeabedAutoParams() const;
    int                  currentSeabedChannel() const;  // 0=Both, 1=Port, 2=Starboard
    QString              currentContactClassText() const;

signals:
    void applyToLineRequested();
    void applyToAllLinesRequested();
    void slantRangeCorrectionChanged(bool enabled); // fired immediately on SRC toggle
    void seabedChannelChanged(int channel);         // 0=Both, 1=Port, 2=Starboard
    void seabedToolChanged(int tool);
    void contactToolChanged(int tool);
    void contactClassChanged(dolphin::ui::ContactClass cls);
    void clearContactsRequested();
    // Nav processing — emitted when user clicks Run in the Navigation section.
    // Modifies stored raw ping nav; never auto-fired on data load.
    void navProcessThisLineRequested(dolphin::ui::NavProcessingParams params);
    void navProcessAllLinesRequested(dolphin::ui::NavProcessingParams params);

private:
    static QVBoxLayout* makeSection(const QString& title, bool expanded,
                                    QWidget* parent, QVBoxLayout* parent_layout,
                                    QPushButton** hdr_out = nullptr);

    // Creates a WfValueRow and appends it to bl.
    void addValueRow(QVBoxLayout* bl, const QString& lbl,
                     WfValueRow*& row_out,
                     double lo, double hi, double def,
                     double step, int decimals,
                     const QString& suffix = {});

    void buildImageSection      (QVBoxLayout* vl, QWidget* container);
    void buildSeabedSection     (QVBoxLayout* vl, QWidget* container);
    void buildContactSection    (QVBoxLayout* vl, QWidget* container);
    void buildFeatureSection    (QVBoxLayout* vl, QWidget* container);
    void buildProcessingSection (QVBoxLayout* vl, QWidget* container);
    void buildNavSection        (QVBoxLayout* vl, QWidget* container);

    void updateRangeCompVisibility();
    void updateAgcVisibility();
    void updateDestripeVisibility();
    void updateProcessingVisibility();
    void updateNavVisibility();
    void refreshImageDirty();
    void refreshSeabedDirty();
    void refreshProcessingDirty();
    void refreshNavDirty();

    // -- Section header buttons (for dirty indicators) ---------------------
    QPushButton* m_image_hdr      = nullptr;
    QPushButton* m_seabed_hdr     = nullptr;
    QPushButton* m_processing_hdr = nullptr;
    QPushButton* m_nav_hdr        = nullptr;

    // -- TVG ---------------------------------------------------------------
    WfToggleRow* m_tvg_toggle       = nullptr;
    QWidget*     m_tvg_body         = nullptr;
    WfValueRow*  m_tvg_spreading    = nullptr;
    WfValueRow*  m_tvg_absorption   = nullptr;

    // -- ARN ---------------------------------------------------------------
    WfToggleRow* m_arn_toggle       = nullptr;
    QWidget*     m_arn_body         = nullptr;
    WfValueRow*  m_arn_strength     = nullptr;
    WfValueRow*  m_arn_gain_cap     = nullptr;

    // -- AGC ---------------------------------------------------------------
    WfToggleRow* m_agc_enable_toggle    = nullptr;
    QWidget*     m_agc_body             = nullptr;
    QComboBox*   m_agc_mode_combo       = nullptr;
    QWidget*     m_agc_along_track_row  = nullptr;
    WfValueRow*  m_agc_strength         = nullptr;
    WfValueRow*  m_agc_along_track      = nullptr;
    QComboBox*   m_agc_smooth_type_combo = nullptr;
    WfValueRow*  m_agc_smooth_win       = nullptr;
    WfValueRow*  m_agc_edge_skip        = nullptr;
    WfValueRow*  m_agc_noise_floor      = nullptr;

    // -- Destripe ----------------------------------------------------------
    WfToggleRow* m_destripe_toggle  = nullptr;
    QWidget*     m_destripe_body    = nullptr;
    WfValueRow*  m_destripe_window  = nullptr;
    WfValueRow*  m_destripe_subdiv  = nullptr;
    WfValueRow*  m_destripe_cap     = nullptr;

    // -- Slant Range Correction --------------------------------------------
    WfToggleRow* m_src_toggle       = nullptr;

    // -- Seabed auto params ------------------------------------------------
    QComboBox*   m_seabed_channel_combo = nullptr;
    QComboBox*   m_seabed_method_combo  = nullptr;
    WfValueRow*  m_seabed_blank         = nullptr;
    WfValueRow*  m_seabed_thresh        = nullptr;
    WfValueRow*  m_seabed_snr           = nullptr;
    WfValueRow*  m_seabed_outlier       = nullptr;
    WfValueRow*  m_seabed_ch_agree      = nullptr;
    WfValueRow*  m_seabed_smooth        = nullptr;

    // -- Seabed manual tools -----------------------------------------------
    QToolButton* m_tool_pen         = nullptr;
    QToolButton* m_tool_box         = nullptr;
    QToolButton* m_tool_erase       = nullptr;

    // -- Contact picking ---------------------------------------------------
    QToolButton* m_contact_pick_btn    = nullptr;
    QComboBox*   m_contact_class_combo = nullptr;

    // -- Processing Tools (image enhancement, applied via main Apply btns) -
    WfToggleRow* m_bpn_toggle        = nullptr;
    WfValueRow*  m_bpn_strength      = nullptr;
    WfValueRow*  m_bpn_smooth        = nullptr;
    QWidget*     m_bpn_body          = nullptr;

    WfToggleRow* m_arc_toggle        = nullptr;
    WfValueRow*  m_arc_exponent      = nullptr;
    WfValueRow*  m_arc_gain_cap      = nullptr;
    QWidget*     m_arc_body          = nullptr;

    WfToggleRow* m_mle_toggle        = nullptr;
    WfValueRow*  m_mle_tile_pings    = nullptr;
    WfValueRow*  m_mle_tile_samps    = nullptr;
    WfValueRow*  m_mle_clip_limit    = nullptr;
    QWidget*     m_mle_body          = nullptr;

    // -- Navigation Processing (applied via dedicated Run buttons) ---------
    WfToggleRow* m_nav_smooth_toggle  = nullptr;
    QWidget*     m_nav_smooth_body    = nullptr;
    WfValueRow*  m_nav_smooth_window  = nullptr;

    WfToggleRow* m_nav_layback_toggle = nullptr;
    QWidget*     m_nav_layback_body   = nullptr;
    WfValueRow*  m_nav_layback_m      = nullptr;
};

} // namespace dolphin::ui
