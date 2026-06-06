// ImportSetupDialog.cpp — sensor-type selection screen (step 1 of import flow).
// Text-based rows: no custom-painted icons, no tile frames.
#include "ui/features/import/ImportSetupDialog.h"
#include "ui/shell/Theme.h"
#include "app/layers/LayerUtils.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace dolphin::ui {

namespace {

struct SensorEntry {
    const char* name;
    const char* formats;  // shown right-aligned in muted text
};

static constexpr SensorEntry kSensors[4] = {
    { QT_TR_NOOP("Sidescan Sonar"),       "XTF · JSF · DLPD" },
    { QT_TR_NOOP("Sub-Bottom Profiler"),  "SEG-Y"             },
    { QT_TR_NOOP("Magnetometer"),         "XTF"               },
    { QT_TR_NOOP("Multibeam Bathymetry"), "ALL · KMALL · 7K"  },
};

} // namespace

static constexpr int kMinW = 420;

ImportSetupDialog::ImportSetupDialog(QWidget* parent)
    : QDialog(parent, Qt::Dialog)
{
    setWindowTitle(tr("Import Survey Files"));
    setModal(true);
    setMinimumWidth(kMinW);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // -- Header ------------------------------------------------------------
    {
        auto* hdr = new QFrame(this);
        hdr->setObjectName("dlgHeader");
        auto* hl = new QHBoxLayout(hdr);
        hl->setContentsMargins(Theme::kSpacing7, Theme::kSpacing4, Theme::kSpacing7, 10);

        auto* title = new QLabel(tr("What are you importing?"), hdr);
        title->setObjectName("dlgTitle");
        auto* sub = new QLabel(
            tr("Select the sensor type(s). The file browser and options will adapt."), hdr);
        sub->setObjectName("dlgSubtitle");

        auto* vl = new QVBoxLayout;
        vl->setSpacing(2);
        vl->addWidget(title);
        vl->addWidget(sub);
        hl->addLayout(vl, 1);
        root->addWidget(hdr);
    }

    {
        auto* div = new QFrame(this);
        div->setFixedHeight(Theme::kSepSz);
        div->setObjectName("dlgDivider");
        root->addWidget(div);
    }

    // -- Body --------------------------------------------------------------
    {
        auto* body = new QWidget(this);
        body->setObjectName("dlgBody");
        auto* bl = new QVBoxLayout(body);
        bl->setContentsMargins(Theme::kSpacing6, Theme::kSpacing4, Theme::kSpacing6, Theme::kSpacing3);
        bl->setSpacing(10);

        auto* sect = new QFrame(body);
        sect->setObjectName("dlgSection");
        auto* vl = new QVBoxLayout(sect);
        vl->setContentsMargins(Theme::kSpacing4, 10, Theme::kSpacing4, 10);
        vl->setSpacing(Theme::kSpacing1);

        auto* sect_lbl = new QLabel(tr("Sensor Types"), sect);
        sect_lbl->setObjectName("dlgSectionLabel");
        vl->addWidget(sect_lbl);

        for (int i = 0; i < kModuleCount; ++i) {
            auto* row = new QWidget(sect);
            auto* hl  = new QHBoxLayout(row);
            hl->setContentsMargins(0, 2, 0, 2);
            hl->setSpacing(Theme::kSpacing3);

            auto* cb = new QCheckBox(tr(kSensors[i].name), row);
            cb->setObjectName("dlgCheckBox");
            cb->setChecked(true);

            auto* fmt = new QLabel(QString::fromLatin1(kSensors[i].formats), row);
            fmt->setObjectName("dlgLabelMeta");

            hl->addWidget(cb, 1);
            hl->addWidget(fmt);
            m_checkboxes[i] = cb;
            vl->addWidget(row);
        }

        bl->addWidget(sect);
        bl->addStretch();
        root->addWidget(body, 1);
    }

    // -- Footer ------------------------------------------------------------
    {
        auto* div = new QFrame(this);
        div->setFixedHeight(Theme::kSepSz);
        div->setObjectName("dlgDivider");
        root->addWidget(div);

        auto* foot = new QFrame(this);
        foot->setObjectName("dlgFooter");
        auto* fl = new QHBoxLayout(foot);
        fl->setContentsMargins(Theme::kSpacing7, 10, Theme::kSpacing7, Theme::kSpacing4);
        fl->setSpacing(10);
        fl->addStretch();

        auto* cancel_btn = new QPushButton(tr("Cancel"), foot);
        cancel_btn->setObjectName("dlgBtnSecondary");
        cancel_btn->setFixedHeight(Theme::kDialogBtnH);
        cancel_btn->setMinimumWidth(80);
        fl->addWidget(cancel_btn);

        auto* next_btn = new QPushButton(tr("Select Files →"), foot);
        next_btn->setObjectName("dlgBtnPrimary");
        next_btn->setFixedHeight(Theme::kDialogBtnH);
        next_btn->setMinimumWidth(120);
        next_btn->setDefault(true);
        fl->addWidget(next_btn);

        root->addWidget(foot);

        connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
        connect(next_btn,   &QPushButton::clicked, this, &QDialog::accept);
    }
}

std::vector<core::ArtifactType> ImportSetupDialog::moduleFilter() const
{
    bool all = true;
    for (int i = 0; i < kModuleCount; ++i)
        if (!m_checkboxes[i]->isChecked()) { all = false; break; }
    if (all) return {};

    std::vector<core::ArtifactType> f;
    for (int i = 0; i < kModuleCount; ++i)
        if (m_checkboxes[i]->isChecked())
            f.push_back(app::kModuleArtifactTypes[i]);
    return f;
}

} // namespace dolphin::ui
