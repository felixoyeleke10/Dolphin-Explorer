#pragma once
#include "ui/mainwindow/rightpanel/IRightPanelModule.h"
#include "app/layers/DataLayer.h"
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace dolphin::ui {

class ElidingLabel;

class InfoModule : public QWidget, public IRightPanelModule {
    Q_OBJECT
public:
    explicit InfoModule(QWidget* parent = nullptr);

    QString  title()                               const override { return tr("Info"); }
    QString  icon()                                const override { return QStringLiteral(":/icons/panel_info.svg"); }
    bool     supports(const app::DataLayer&)       const override { return true; }
    void     setLayer(app::DataLayer* layer)             override;
    QWidget* widget()                                    override { return this; }

private:
    QWidget* makeRow(QVBoxLayout* vl, const QString& key, ElidingLabel*& val_out);
    void setOptionalRow(QWidget* row, ElidingLabel* value_label, const QString& value);

    QLabel*       m_pings_key    = nullptr;
    ElidingLabel* m_pings_val    = nullptr;
    ElidingLabel* m_modality_val = nullptr;
    ElidingLabel* m_freq_val     = nullptr;
    ElidingLabel* m_duration_val = nullptr;
    ElidingLabel* m_sonar_val    = nullptr;
    ElidingLabel* m_date_val     = nullptr;
    ElidingLabel* m_survey_val   = nullptr;
    ElidingLabel* m_vessel_val   = nullptr;
    ElidingLabel* m_crs_val      = nullptr;

    QWidget* m_freq_row     = nullptr;
    QWidget* m_duration_row = nullptr;
    QWidget* m_sonar_row    = nullptr;
    QWidget* m_date_row     = nullptr;
    QWidget* m_survey_row   = nullptr;
    QWidget* m_vessel_row   = nullptr;
    QWidget* m_crs_row      = nullptr;
};

} // namespace dolphin::ui
