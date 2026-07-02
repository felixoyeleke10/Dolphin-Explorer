#pragma once
#include "ui/mainwindow/rightpanel/IRightPanelModule.h"
#include "ui/features/subbottom/SbpGainParams.h"
#include "app/layers/DataLayer.h"
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;

namespace dolphin::ui {

// Right-panel Gain processing module for SubBottom layers.
// Static Gain, AGC, and per-trace Normalize — following GainControlPanel pattern.
// Processing is not applied in real-time; Apply buttons fire signals handled
// by MainWindow.SubBottomCoordinator.
class SbpGainModule : public QWidget, public IRightPanelModule {
    Q_OBJECT
public:
    explicit SbpGainModule(QWidget* parent = nullptr);

    QString  title()                               const override { return tr("Gain"); }
    QString  badge()                               const override { return QStringLiteral("SBP"); }
    QString  icon()                                const override { return QStringLiteral(":/icons/panel_gain.svg"); }
    QString  notApplicableHint()                   const override { return tr("Select a Sub-Bottom layer in the Explorer to use this section."); }
    app::Modality primaryModality()                const override { return app::Modality::SubBottom; }
    bool     supports(const app::DataLayer& layer) const override {
        return layer.modality == app::Modality::SubBottom;
    }
    void     setLayer(app::DataLayer*)                   override {}
    QWidget* widget()                                    override { return this; }

    SbpGainParams currentParams() const;
    void          setParams(const SbpGainParams& p);

private:
    void updateControlStates();

    QCheckBox*      m_static_en      = nullptr;
    QDoubleSpinBox* m_static_db      = nullptr;
    QCheckBox*      m_agc_en         = nullptr;
    QSpinBox*       m_agc_window     = nullptr;
    QCheckBox*      m_normalize_en   = nullptr;
};

} // namespace dolphin::ui
