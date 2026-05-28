#pragma once
#include "ui/mainwindow/rightpanel/IRightPanelModule.h"
#include "ui/features/subbottom/panels/SubBottomDisplayPanel.h"  // SubBottomDisplayParams
#include "app/layers/DataLayer.h"
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;

namespace dolphin::ui {

// Right-panel Display module for SubBottom layers.
// Shows palette, gain and bottom-track controls in the main window inspector.
// Mirrors SubBottomDisplayPanel (inside SubBottomWindow) but lives in the
// persistent properties panel so the user can adjust it without opening the
// dedicated viewer.
class SubBottomDisplayModule : public QWidget, public IRightPanelModule {
    Q_OBJECT
public:
    explicit SubBottomDisplayModule(QWidget* parent = nullptr);

    QString  title()                               const override { return tr("Display"); }
    QString  badge()                               const override { return QStringLiteral("SBP"); }
    QString  icon()                                const override { return QStringLiteral(":/icons/panel_display.svg"); }
    QString  notApplicableHint()                   const override { return tr("Select a Sub-Bottom layer in the Explorer to use this section."); }
    app::Modality primaryModality()                const override { return app::Modality::SubBottom; }
    bool     supports(const app::DataLayer& layer) const override {
        return layer.modality == app::Modality::SubBottom;
    }
    void     setLayer(app::DataLayer*)                   override {}
    QWidget* widget()                                    override { return this; }

    SubBottomDisplayParams currentParams() const;
    void setParams(const SubBottomDisplayParams& p);

signals:
    void paramsChanged(dolphin::ui::SubBottomDisplayParams params);

private:
    void emitAndSave();

    QComboBox*      m_palette   = nullptr;
    QDoubleSpinBox* m_gain      = nullptr;
    QDoubleSpinBox* m_contrast  = nullptr;
    QCheckBox*      m_polarity  = nullptr;
    QCheckBox*      m_bt_check  = nullptr;
};

} // namespace dolphin::ui
