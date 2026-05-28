// SettingsDialog.cpp — application preferences (QSettings-backed).

#include "ui/shared/dialogs/SettingsDialog.h"
#include "ui/shell/Theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSettings>
#include <QTabWidget>
#include <QVBoxLayout>

namespace dolphin::ui {

static constexpr int kDialogW = 420;  // settings dialog fixed width
static constexpr int kDialogH = 320;  // settings dialog fixed height

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setFixedSize(kDialogW, kDialogH);

    QSettings s;
    auto* root = new QVBoxLayout(this);
    root->setSpacing(Theme::kSpacing3);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(buildDisplayTab(tabs),    tr("Display"));
    tabs->addTab(buildProcessingTab(tabs), tr("Processing"));
    root->addWidget(tabs, 1);

    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btns->button(QDialogButtonBox::Ok)->setObjectName("dlgBtnOk");
    root->addWidget(btns);

    // Load saved values
    const QString units = s.value(kKeyUnits, "m").toString();
    m_units_combo->setCurrentIndex(units == "ft" ? 1 : 0);

    const QString palette = s.value(kKeyDefaultPalette, "Gray").toString();
    const int pi = m_palette_combo->findText(palette);
    if (pi >= 0) {
        m_palette_combo->setCurrentIndex(pi);
    } else {
        // Stored as integer index (written by setPaletteIndex); map to combo text.
        bool ok = false;
        const int idx = palette.toInt(&ok);
        if (ok) {
            // Matches PaletteIndex constants in SonarDisplayParams.h
            const char* name = nullptr;
            switch (idx) {
            case 0: name = "Hot";    break;
            case 1: name = "Gray";   break;
            case 3: name = "Copper"; break;
            case 5: name = "Viridis";break;
            case 9: name = "Turbo";  break;
            default: break;
            }
            if (name) {
                const int found = m_palette_combo->findText(QLatin1String(name));
                if (found >= 0) m_palette_combo->setCurrentIndex(found);
            }
        }
    }

    m_tvg_spread->setValue(s.value(kKeyTvgSpreading,  20.0).toDouble());
    m_tvg_absorb->setValue(s.value(kKeyTvgAbsorption,  0.0).toDouble());
    m_tvg_blank ->setValue(s.value(kKeyTvgBlanking,    1.0).toDouble());

    connect(btns, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccepted);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QWidget* SettingsDialog::buildDisplayTab(QWidget* parent)
{
    auto* w  = new QWidget(parent);
    auto* fl = new QFormLayout(w);
    fl->setSpacing(10);
    fl->setContentsMargins(Theme::kSpacing5, Theme::kSpacing5, Theme::kSpacing5, Theme::kSpacing5);

    m_units_combo = new QComboBox(w);
    m_units_combo->addItems({tr("Metres (m)"), tr("Feet (ft)")});
    fl->addRow(tr("Distance units:"), m_units_combo);

    m_palette_combo = new QComboBox(w);
    m_palette_combo->addItems({"Gray", "Hot", "Copper", "Viridis", "Turbo"});
    fl->addRow(tr("Default palette:"), m_palette_combo);

    return w;
}

QWidget* SettingsDialog::buildProcessingTab(QWidget* parent)
{
    auto* w  = new QWidget(parent);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(Theme::kSpacing5, Theme::kSpacing4, Theme::kSpacing5, Theme::kSpacing5);
    vl->setSpacing(Theme::kSpacing3);

    auto* grp = new QGroupBox(tr("Default TVG Parameters"), w);
    auto* fl  = new QFormLayout(grp);
    fl->setSpacing(Theme::kSpacing3);

    m_tvg_spread = new QDoubleSpinBox(grp);
    m_tvg_spread->setRange(0.0, 40.0);
    m_tvg_spread->setSingleStep(1.0);
    m_tvg_spread->setSuffix(" dB/decade");
    fl->addRow(tr("Spreading:"), m_tvg_spread);

    m_tvg_absorb = new QDoubleSpinBox(grp);
    m_tvg_absorb->setRange(0.0, 2.0);
    m_tvg_absorb->setSingleStep(0.01);
    m_tvg_absorb->setDecimals(3);
    m_tvg_absorb->setSuffix(" dB/m");
    fl->addRow(tr("Absorption:"), m_tvg_absorb);

    m_tvg_blank = new QDoubleSpinBox(grp);
    m_tvg_blank->setRange(0.0, 50.0);
    m_tvg_blank->setSingleStep(0.5);
    m_tvg_blank->setSuffix(" m");
    fl->addRow(tr("Blanking:"), m_tvg_blank);

    vl->addWidget(grp);
    vl->addStretch(1);
    return w;
}

void SettingsDialog::onAccepted()
{
    QSettings s;
    s.setValue(kKeyUnits,         m_units_combo->currentIndex() == 0 ? "m" : "ft");
    s.setValue(kKeyDefaultPalette, m_palette_combo->currentText());
    s.setValue(kKeyTvgSpreading,   m_tvg_spread->value());
    s.setValue(kKeyTvgAbsorption,  m_tvg_absorb->value());
    s.setValue(kKeyTvgBlanking,    m_tvg_blank->value());
    accept();
}

} // namespace dolphin::ui
