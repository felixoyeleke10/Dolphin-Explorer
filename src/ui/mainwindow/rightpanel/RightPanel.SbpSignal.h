#pragma once
#include "ui/mainwindow/rightpanel/IRightPanelModule.h"
#include "ui/features/subbottom/SbpSignalParams.h"
#include "app/layers/DataLayer.h"
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QPushButton;

namespace dolphin::ui {

// Right-panel Signal processing module for SubBottom layers.
// Envelope, DC Removal, and Bandpass — following ImagingControlPanel pattern.
// Processing is not applied in real-time; Apply buttons fire signals handled
// by MainWindow.SubBottomCoordinator.
class SbpSignalModule : public QWidget, public IRightPanelModule {
    Q_OBJECT
public:
    explicit SbpSignalModule(QWidget* parent = nullptr);

    QString  title()                               const override { return tr("Signal"); }
    QString  badge()                               const override { return QStringLiteral("SBP"); }
    QString  icon()                                const override { return QStringLiteral(":/icons/panel_signal.svg"); }
    QString  notApplicableHint()                   const override { return tr("Select a Sub-Bottom layer in the Explorer to use this section."); }
    app::Modality primaryModality()                const override { return app::Modality::SubBottom; }
    bool     supports(const app::DataLayer& layer) const override {
        return layer.modality == app::Modality::SubBottom;
    }
    void     setLayer(app::DataLayer*)                   override {}
    QWidget* widget()                                    override { return this; }

    SbpSignalParams currentParams() const;
    void            setParams(const SbpSignalParams& p);

private:
    void updateControlStates();

    QCheckBox*      m_envelope_en    = nullptr;
    QCheckBox*      m_dc_removal_en  = nullptr;
    QCheckBox*      m_bandpass_en    = nullptr;
    QDoubleSpinBox* m_bp_lo_hz       = nullptr;
    QDoubleSpinBox* m_bp_hi_hz       = nullptr;
};

} // namespace dolphin::ui
