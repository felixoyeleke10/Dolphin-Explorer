// ImportSetupDialog.cpp — sensor-type selection screen (step 1 of import flow).
// Radio buttons enforce one sensor type per import session, preventing mixed batches.
#include "ui/features/import/ImportSetupDialog.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"
#include "app/layers/LayerUtils.h"

#include <QButtonGroup>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QStyle>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace dolphin::ui {

namespace {

struct SensorEntry {
    const char* name;
    const char* description;  // one-line hint shown below the name
    const char* formats;
};

static constexpr SensorEntry kSensors[4] = {
    { QT_TR_NOOP("Sidescan Sonar"),
      QT_TR_NOOP("Acoustic images of the seabed"),
      "XTF · JSF · DLPD" },
    { QT_TR_NOOP("Sub-Bottom Profiler"),
      QT_TR_NOOP("Sediment layer profiles"),
      "SEG-Y · DLPD" },
    { QT_TR_NOOP("Magnetometer"),
      QT_TR_NOOP("Magnetic anomaly survey data"),
      "XTF · DLPD" },
    { QT_TR_NOOP("Multibeam Bathymetry"),
      QT_TR_NOOP("3D seabed depth point cloud"),
      "DLPD" },
};

} // namespace

ImportSetupDialog::ImportSetupDialog(QWidget* parent)
    : QDialog(parent, Qt::Dialog)
{
    setWindowTitle(tr("Import Survey Files"));
    setModal(true);

    auto* root = makeCompactLayout<QVBoxLayout>(this);
    // Derive the dialog's minimum size from its content: the layout's minimum
    // becomes the window's minimum, so it always opens at its natural size and is
    // never created smaller than its content. This replaces a hard-coded minimum
    // width (which didn't match the cards' real width) and avoids the
    // QWindowsWindow::setGeometry clamp warning at the source.
    root->setSizeConstraint(QLayout::SetMinimumSize);

    // -- Header ----------------------------------------------------------------
    {
        auto* hdr = new QFrame(this);
        hdr->setObjectName("dlgHeader");
        auto* hl = new QHBoxLayout(hdr);
        hl->setContentsMargins(Theme::kSpacing7, Theme::kSpacing4, Theme::kSpacing7, 10);

        auto* title = new QLabel(tr("What are you importing?"), hdr);
        title->setObjectName("dlgTitle");
        auto* sub = new QLabel(
            tr("Choose the sensor type. Each import session handles one type."), hdr);
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

    // -- Body ------------------------------------------------------------------
    {
        auto* body = new QWidget(this);
        body->setObjectName("dlgBody");
        auto* bl = new QVBoxLayout(body);
        bl->setContentsMargins(Theme::kSpacing6, Theme::kSpacing4, Theme::kSpacing6, Theme::kSpacing3);
        bl->setSpacing(Theme::kSpacing2);

        // QButtonGroup enforces mutual exclusivity across cards (radio buttons
        // parented to different QFrames are not auto-exclusive by default).
        auto* btn_group = new QButtonGroup(this);
        btn_group->setExclusive(true);

        for (int i = 0; i < kModuleCount; ++i) {
            auto* card = new QFrame(body);
            card->setObjectName("importActionBtn");
            card->setCursor(Qt::PointingHandCursor);

            auto* hl = new QHBoxLayout(card);
            hl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing3,
                                   Theme::kSpacing4, Theme::kSpacing3);
            hl->setSpacing(Theme::kSpacing3);

            auto* rb = new QRadioButton(card);
            rb->setChecked(i == 0);  // Sidescan selected by default
            btn_group->addButton(rb, i);
            m_radios[i] = rb;

            auto* text_col = makeCompactLayout<QVBoxLayout>(nullptr, 1);

            auto* name_lbl = new QLabel(tr(kSensors[i].name), card);
            name_lbl->setObjectName("dlgRadioLabel");

            auto* desc_lbl = new QLabel(tr(kSensors[i].description), card);
            desc_lbl->setObjectName("dlgLabelMeta");

            text_col->addWidget(name_lbl);
            text_col->addWidget(desc_lbl);

            auto* fmt_lbl = new QLabel(QString::fromUtf8(kSensors[i].formats), card);
            fmt_lbl->setObjectName("dlgLabelMono");
            fmt_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

            hl->addWidget(rb);
            hl->addLayout(text_col, 1);
            hl->addWidget(fmt_lbl);

            // Clicking anywhere on the card selects the radio
            connect(rb, &QRadioButton::toggled, card, [card](bool checked) {
                card->setProperty("selected", checked);
                card->style()->unpolish(card);
                card->style()->polish(card);
            });
            // Forward card click to the radio button
            card->installEventFilter(this);

            bl->addWidget(card);
        }

        bl->addStretch();
        root->addWidget(body, 1);
    }

    // -- Footer ----------------------------------------------------------------
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

    // Trigger initial card highlight for the default-selected radio
    if (m_radios[0]) {
        auto* card = qobject_cast<QFrame*>(m_radios[0]->parentWidget());
        if (card) {
            card->setProperty("selected", true);
            card->style()->unpolish(card);
            card->style()->polish(card);
        }
    }
}

bool ImportSetupDialog::eventFilter(QObject* obj, QEvent* ev)
{
    if (ev->type() == QEvent::MouseButtonRelease) {
        for (int i = 0; i < kModuleCount; ++i) {
            if (m_radios[i] && m_radios[i]->parentWidget() == obj) {
                m_radios[i]->setChecked(true);
                return true;
            }
        }
    }
    return QDialog::eventFilter(obj, ev);
}

std::vector<core::ArtifactType> ImportSetupDialog::moduleFilter() const
{
    for (int i = 0; i < kModuleCount; ++i)
        if (m_radios[i] && m_radios[i]->isChecked())
            return { app::kModuleArtifactTypes[i] };
    return {};
}

} // namespace dolphin::ui
