#pragma once
#include "ui/mainwindow/rightpanel/IRightPanelModule.h"
#include "app/layers/DataLayer.h"
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace dolphin::ui {

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
    void makeRow(QVBoxLayout* vl, const QString& key, QLabel*& val_out);

    QLabel* m_pings_key    = nullptr;
    QLabel* m_pings_val    = nullptr;
    QLabel* m_modality_val = nullptr;
    QLabel* m_freq_val     = nullptr;
    QLabel* m_duration_val = nullptr;
    QLabel* m_sonar_val    = nullptr;
    QLabel* m_date_val     = nullptr;
    QLabel* m_survey_val   = nullptr;
    QLabel* m_vessel_val   = nullptr;
    QLabel* m_crs_val      = nullptr;
};

} // namespace dolphin::ui
