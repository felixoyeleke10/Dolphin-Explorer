#pragma once
#include <QWidget>
#include "app/layers/DataLayer.h"
#include "ui/shared/widgets/CollapsibleSection.h"

class QLabel;
class QComboBox;
class QVBoxLayout;

namespace dolphin::ui {

// Self-contained page widget shown in InspectorPanel when a DataLayer is selected.
class LayerInspectorPage : public QWidget {
    Q_OBJECT
public:
    explicit LayerInspectorPage(QWidget* parent = nullptr);
    void refresh(app::DataLayer* layer);
    int  currentPaletteIndex() const;
    void setPalette(int idx);   // external sync — does NOT re-emit

signals:
    void paletteChanged(int idx);

private:
    void build();
    void makeRow(QVBoxLayout* vl, const QString& key, QLabel*& val_out);

    QLabel*    m_pings_key    = nullptr;  // dynamic: "Pings" / "Traces" / "Samples" / "Records"
    QLabel*    m_pings_val    = nullptr;
    QLabel*    m_modality_val = nullptr;
    QLabel*    m_freq_val     = nullptr;
    QLabel*    m_duration_val = nullptr;
    QLabel*    m_sonar_val    = nullptr;
    QLabel*    m_date_val     = nullptr;
    QLabel*    m_survey_val   = nullptr;
    QLabel*    m_vessel_val   = nullptr;
    QLabel*    m_crs_val      = nullptr;
    QWidget*            m_palette_row     = nullptr;
    QComboBox*          m_palette         = nullptr;
    CollapsibleSection* m_display_section = nullptr;
};

} // namespace dolphin::ui
