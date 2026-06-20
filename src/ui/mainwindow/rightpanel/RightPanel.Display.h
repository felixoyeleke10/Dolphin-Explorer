#pragma once
#include "ui/mainwindow/rightpanel/IRightPanelModule.h"
#include "app/layers/DataLayer.h"
#include <QWidget>

class QComboBox;

namespace dolphin::ui {

class DisplayModule : public QWidget, public IRightPanelModule {
    Q_OBJECT
public:
    explicit DisplayModule(QWidget* parent = nullptr);

    QString  title()                               const override { return tr("Display"); }
    QString  badge()                               const override { return QStringLiteral("SSS"); }
    QString  icon()                                const override { return QStringLiteral(":/icons/panel_display.svg"); }
    QString  notApplicableHint()                   const override { return tr("Select a Sidescan layer in the Explorer to use this section."); }
    app::Modality primaryModality()                const override { return app::Modality::Sidescan; }
    bool     supports(const app::DataLayer& layer) const override {
        return layer.modality == app::Modality::Sidescan;
    }
    void     setLayer(app::DataLayer*)                   override;
    QWidget* widget()                                    override { return this; }

    int  currentPaletteIndex() const;
    void setPalette(int idx);

signals:
    void paletteChanged(int idx);

private:
    QComboBox* m_palette  = nullptr;
};

} // namespace dolphin::ui
