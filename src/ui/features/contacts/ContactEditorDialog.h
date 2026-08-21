#pragma once
#include <QColor>
#include <QDialog>
#include <QPixmap>
#include <cstdint>
#include <functional>
#include <vector>
#include "core/Contact.h"
#include "ui/features/contacts/ContactSnapshotData.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QToolButton;

namespace dolphin::app { class Project; }

namespace dolphin::ui {

class ContactSnapshotView;

// -----------------------------------------------------------------------------
//  ContactEditorDialog — "Edit contact details".
//
//  A focused, modeless-but-single editor for one contact at a time, with the
//  full attribute form on the left and the picked source image on the right,
//  matching the SonarWiz / SeaView contact editor:
//
//    Name · Symbol · Class · Colour · Coordinates · Height (+ Not measurable) ·
//    Shadow · Width · Length · Depth · Burial depth · Confidence · Tags ·
//    Description · Use for report
//
//  The operator steps through the current contact set with Prev / Next; each
//  edit is committed as an undoable change routed to the owner via
//  contactSaveRequested (before/after) when navigating away, deleting, or
//  closing. Delete and Export are surfaced on the command row.
// -----------------------------------------------------------------------------
class ContactEditorDialog : public QDialog {
    Q_OBJECT
public:
    ContactEditorDialog(app::Project* project,
                        std::vector<uint64_t> ordered_ids,
                        uint64_t current_id,
                        QWidget* parent = nullptr);

    uint64_t currentId() const;

    // Re-target an already-open editor at a new contact set / selection (commits
    // the current edit first). Used when the operator double-clicks another row.
    void showContact(std::vector<uint64_t> ordered_ids, uint64_t current_id);

    // Optional fetch-from-source fallback: called when a contact has no persisted
    // snapshot PNG so the owner can render one from the source pings on demand.
    // Re-attempts the fetch for the already-loaded contact (the constructor loads
    // the first contact before the owner can install the provider).
    void setSnapshotProvider(std::function<ContactSnapshotData(const core::Contact&)> fn);

    // Re-sync after the project changed underneath us (edit committed, contact
    // removed elsewhere, project replaced). Keeps the current contact selected
    // when it still exists; otherwise clamps to a neighbour or closes.
    void refresh(app::Project* project);

signals:
    // Undoable edit: apply the diff from `before` to `after` (same id).
    void contactSaveRequested(const core::Contact& before, const core::Contact& after);
    void removeContactRequested(uint64_t id);
    void exportRequested(uint64_t id);
    // The editor moved to a different contact (owner may sync map/list selection).
    void contactActivated(uint64_t id);

protected:
    // All close paths (Close button → accept(), Esc → reject(), titlebar X →
    // closeEvent → reject()) funnel through done(); commit pending edits there.
    void done(int r) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* buildForm();
    QWidget* buildImagePane();
    QWidget* buildCommandRow();

    void loadIndex(int index);          // commit current, then show contacts[index]
    void loadContactIntoForm(const core::Contact& c);
    core::Contact readForm() const;     // form → contact (preserves untouched fields)
    void commitIfChanged();             // emit save if the form differs from m_before
    void updateNavState();

    const core::Contact* findContact(uint64_t id) const;
    QColor effectiveColor() const;
    void   setColorSwatch(const QColor& c);
    // Refresh the geographic echo labels + the footer readout from the
    // (possibly edited) coordinate spin boxes.
    void   updateCoordEchoes();
    void   addTagFromCombo();

    app::Project*         m_project = nullptr;
    std::vector<uint64_t> m_ids;
    int                   m_index = -1;
    core::Contact         m_before;      // snapshot of the contact as loaded
    bool                  m_loading = false;
    std::function<ContactSnapshotData(const core::Contact&)> m_snapshot_provider;
    float m_measure_across_m_per_px = 0.f;
    float m_measure_along_m_per_px  = 0.f;
    float m_measure_altitude_m      = 0.f;

    // -- Form ------------------------------------------------------------------
    QLineEdit*      m_name       = nullptr;
    QComboBox*      m_symbol     = nullptr;
    QComboBox*      m_class      = nullptr;
    QPushButton*    m_color_btn  = nullptr;
    QColor          m_color;                    // invalid = auto
    // Editable position: Northing/Easting (projected) or Lat/Lon (geographic).
    // The row labels + spin formats are reconfigured per contact CRS on load;
    // the echo labels show the WGS84 equivalent when the CRS is transformable.
    QLabel*         m_coord_n_label = nullptr;
    QLabel*         m_coord_e_label = nullptr;
    QDoubleSpinBox* m_coord_n    = nullptr;     // northing / latitude
    QDoubleSpinBox* m_coord_e    = nullptr;     // easting / longitude
    QLabel*         m_coord_n_echo = nullptr;
    QLabel*         m_coord_e_echo = nullptr;
    QDoubleSpinBox* m_height     = nullptr;
    QLabel*         m_height_label = nullptr;
    QCheckBox*      m_height_nm  = nullptr;
    QDoubleSpinBox* m_shadow     = nullptr;
    QLabel*         m_shadow_label = nullptr;
    QDoubleSpinBox* m_width      = nullptr;
    QLabel*         m_width_label = nullptr;
    QDoubleSpinBox* m_length     = nullptr;     // length_m (object length, not pick range)
    QLabel*         m_length_label = nullptr;
    QDoubleSpinBox* m_depth      = nullptr;
    QDoubleSpinBox* m_burial     = nullptr;
    QComboBox*      m_confidence = nullptr;
    QComboBox*      m_tags_combo = nullptr;     // editable, with project-wide suggestions
    QListWidget*    m_tags_list  = nullptr;     // this contact's tags
    QPlainTextEdit* m_desc       = nullptr;
    QCheckBox*      m_use_report = nullptr;

    // -- Image pane ------------------------------------------------------------
    ContactSnapshotView* m_snap  = nullptr;
    QComboBox*      m_source_combo = nullptr;   // "Source image:" caption
    QPushButton*    m_export_btn   = nullptr;   // header-row Export (this contact)
    QLabel*         m_img_coords   = nullptr;   // footer position readout
    QCheckBox*      m_show_icon    = nullptr;   // "Show / hide contact icon"
    QSlider*        m_scale_sl    = nullptr;
    QSlider*        m_rot_sl      = nullptr;
    QLabel*         m_scale_lbl   = nullptr;
    QLabel*         m_rot_lbl     = nullptr;

    // -- Command row -----------------------------------------------------------
    QToolButton*    m_prev_btn    = nullptr;
    QToolButton*    m_next_btn    = nullptr;
    QPushButton*    m_delete_btn  = nullptr;
    QLabel*         m_title_lbl   = nullptr;
};

} // namespace dolphin::ui
