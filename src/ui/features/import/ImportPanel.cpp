#include "ui/features/import/ImportPanel.h"
#include "ui/shell/Theme.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>

namespace dolphin::ui {

static QPushButton* makeBtn(const QString& text, const QString& sub, QWidget* parent)
{
    auto* btn = new QPushButton(parent);
    btn->setObjectName("importActionBtn");
    btn->setText(text);
    btn->setToolTip(sub);
    return btn;
}

ImportPanel::ImportPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(Theme::kSpacing3, 10, Theme::kSpacing3, Theme::kSpacing3);
    layout->setSpacing(Theme::kSpacing2);

    // Project status
    auto* hdr = new QLabel(tr("Project"), this);
    hdr->setObjectName("sectionLabel");
    layout->addWidget(hdr);

    m_project_label = new QLabel(tr("No project open"), this);
    m_project_label->setObjectName("importProjectName");
    m_project_label->setWordWrap(true);
    layout->addWidget(m_project_label);

    // Divider
    auto* line1 = new QFrame(this);
    line1->setFrameShape(QFrame::HLine);
    layout->addWidget(line1);

    // Actions
    auto* hdr2 = new QLabel(tr("Actions"), this);
    hdr2->setObjectName("sectionLabel");
    layout->addWidget(hdr2);

    m_btn_new = makeBtn(tr("  New Project"), tr("Create a new .dlp project"), this);
    layout->addWidget(m_btn_new);

    m_btn_open = makeBtn(tr("  Open Project"), tr("Open existing .dlp project"), this);
    layout->addWidget(m_btn_open);

    // Divider
    auto* line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    layout->addWidget(line2);

    auto* hdr3 = new QLabel(tr("Import"), this);
    hdr3->setObjectName("sectionLabel");
    layout->addWidget(hdr3);

    m_btn_import = makeBtn(tr("  Import Survey File"), tr("Load a survey file (XTF, JSF, DLPD\u2026)"), this);
    m_btn_import->setObjectName("importPrimaryBtn");
    layout->addWidget(m_btn_import);

    // Drop hint
    auto* hint = new QLabel(tr("or drag & drop a survey file\n(XTF, JSF, DLPD) onto the map view"), this);
    hint->setObjectName("inspectorEmpty");
    hint->setAlignment(Qt::AlignCenter);
    layout->addWidget(hint);

    layout->addStretch();

    connect(m_btn_new,    &QPushButton::clicked, this, &ImportPanel::newProjectRequested);
    connect(m_btn_open,   &QPushButton::clicked, this, &ImportPanel::openProjectRequested);
    connect(m_btn_import, &QPushButton::clicked, this, &ImportPanel::importFileRequested);
}

void ImportPanel::setProjectName(const QString& name)
{
    m_project_label->setText(name.isEmpty() ? tr("No project open") : name);
}

} // namespace dolphin::ui
