#include "ui/features/export/ExportManagerWindow.h"
#include "ui/shell/Theme.h"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace dolphin::ui {

namespace {

// One export target shown in the nav tree.
struct Target {
    QString     id;        // "contacts", "screenshot", …
    QString     title;
    QString     desc;
    QString     icon;
    QStringList formats;   // selectable formats; empty + !enabled = coming soon
    bool        enabled;
};

const QList<Target>& targets()
{
    static const QList<Target> kTargets = {
        { "contacts",   QObject::tr("Contacts"),
          QObject::tr("All contacts in the project, as a spreadsheet (CSV) or a formatted report."),
          ":/icons/contacts.svg", { "CSV", "PDF", "Word" }, true },
        { "screenshot", QObject::tr("Screenshot"),
          QObject::tr("A PNG image of the current map / application view."),
          ":/icons/export.svg", { "PNG" }, true },
        { "geotiff",    QObject::tr("GeoTIFF Mosaic"),
          QObject::tr("Georeferenced raster mosaic of the sidescan coverage."),
          ":/icons/export_geotiff.svg", {}, false },
        { "kmz",        QObject::tr("Google Earth (KMZ)"),
          QObject::tr("Contacts and track lines as a KMZ overlay."),
          ":/icons/export_kmz.svg", {}, false },
        { "nav",        QObject::tr("Navigation Track"),
          QObject::tr("Per-line navigation as CSV / GPX."),
          ":/icons/export_nav.svg", {}, false },
    };
    return kTargets;
}

} // namespace

ExportManagerWindow::ExportManagerWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Export Manager"));
    setObjectName("exportManagerWindow");
    resize(880, 540);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName("exportSplitter");
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);
    splitter->addWidget(buildNavPane());
    splitter->addWidget(buildCentrePane());
    splitter->addWidget(buildPreviewPane());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({ 220, 430, 230 });
    setCentralWidget(splitter);

    auto* status = new QLabel(tr("Choose what to export."), this);
    status->setObjectName("exportStatus");
    statusBar()->addWidget(status);
    statusBar()->setSizeGripEnabled(false);

    connect(m_nav, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* cur, QTreeWidgetItem*) {
                selectTarget(cur ? cur->data(0, Qt::UserRole).toInt() : -1);
            });
    connect(m_export, &QPushButton::clicked, this, [this]() { doExport(); });

    if (m_nav->topLevelItemCount() > 0)
        m_nav->setCurrentItem(m_nav->topLevelItem(0));
}

QWidget* ExportManagerWindow::buildNavPane()
{
    m_nav = new QTreeWidget(this);
    m_nav->setObjectName("exportNavTree");
    m_nav->setHeaderHidden(true);
    m_nav->setColumnCount(1);
    m_nav->setMinimumWidth(200);
    m_nav->setIndentation(8);
    m_nav->setUniformRowHeights(true);

    const auto& ts = targets();
    for (int i = 0; i < ts.size(); ++i) {
        auto* it = new QTreeWidgetItem(m_nav);
        it->setText(0, ts[i].enabled ? ts[i].title
                                     : ts[i].title + tr("  · soon"));
        it->setIcon(0, QIcon(ts[i].icon));
        it->setData(0, Qt::UserRole, i);
        if (!ts[i].enabled) {
            it->setFlags(it->flags() & ~Qt::ItemIsSelectable);
            it->setForeground(0, QColor(Theme::kTextDisabled));
        }
    }
    return m_nav;
}

QWidget* ExportManagerWindow::buildCentrePane()
{
    auto* centre = new QWidget(this);
    centre->setObjectName("exportCentre");
    auto* col = new QVBoxLayout(centre);
    col->setContentsMargins(Theme::kSpacing6, Theme::kSpacing5, Theme::kSpacing6, Theme::kSpacing5);
    col->setSpacing(Theme::kSpacing3);

    m_title = new QLabel(centre);
    m_title->setObjectName("exportTitle");
    col->addWidget(m_title);

    m_desc = new QLabel(centre);
    m_desc->setObjectName("exportDesc");
    m_desc->setWordWrap(true);
    col->addWidget(m_desc);

    auto* fmt_lbl = new QLabel(tr("Format"), centre);
    fmt_lbl->setObjectName("exportFieldLabel");
    col->addSpacing(Theme::kSpacing2);
    col->addWidget(fmt_lbl);

    auto* fmt_host = new QWidget(centre);
    m_formats = new QHBoxLayout(fmt_host);
    m_formats->setContentsMargins(0, 0, 0, 0);
    m_formats->setSpacing(Theme::kSpacing2);
    m_formats->addStretch(1);
    m_fmt_grp = new QButtonGroup(this);
    m_fmt_grp->setExclusive(true);
    col->addWidget(fmt_host);

    col->addStretch(1);

    m_export = new QPushButton(tr("Export…"), centre);
    m_export->setObjectName("dlgBtnPrimary");
    m_export->setMinimumHeight(Theme::kDialogBtnH);
    col->addWidget(m_export, 0, Qt::AlignRight);

    return centre;
}

QWidget* ExportManagerWindow::buildPreviewPane()
{
    auto* pane = new QWidget(this);
    pane->setObjectName("exportPreview");
    pane->setMinimumWidth(210);
    auto* col = new QVBoxLayout(pane);
    col->setContentsMargins(Theme::kSpacing5, Theme::kSpacing6, Theme::kSpacing5, Theme::kSpacing5);
    col->setSpacing(Theme::kSpacing3);

    m_pv_icon = new QLabel(pane);
    m_pv_icon->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    col->addWidget(m_pv_icon, 0, Qt::AlignHCenter);

    m_pv_title = new QLabel(pane);
    m_pv_title->setObjectName("exportPreviewTitle");
    m_pv_title->setAlignment(Qt::AlignHCenter);
    m_pv_title->setWordWrap(true);
    col->addWidget(m_pv_title);

    m_pv_detail = new QLabel(pane);
    m_pv_detail->setObjectName("exportPreviewDetail");
    m_pv_detail->setWordWrap(true);
    m_pv_detail->setAlignment(Qt::AlignTop);
    col->addWidget(m_pv_detail);

    col->addStretch(1);
    return pane;
}

void ExportManagerWindow::selectTarget(int index)
{
    m_current = index;
    const auto& ts = targets();

    // Clear the format buttons.
    for (auto* b : m_fmt_grp->buttons()) { m_fmt_grp->removeButton(b); b->deleteLater(); }
    while (m_formats->count() > 1) {                 // keep the trailing stretch
        QLayoutItem* it = m_formats->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }

    if (index < 0 || index >= ts.size()) {
        m_title->clear(); m_desc->clear();
        m_pv_icon->clear(); m_pv_title->clear(); m_pv_detail->clear();
        m_export->setEnabled(false);
        return;
    }

    const Target& t = ts[index];
    m_title->setText(t.title);
    m_desc->setText(t.desc);
    m_pv_icon->setPixmap(QIcon(t.icon).pixmap(64, 64));
    m_pv_title->setText(t.title);

    int idx = 0;
    for (const QString& f : t.formats) {
        auto* b = new QPushButton(f);
        b->setObjectName("exportFmtBtn");
        b->setCheckable(true);
        b->setChecked(idx == 0);
        m_fmt_grp->addButton(b, idx++);
        m_formats->insertWidget(m_formats->count() - 1, b);   // before the stretch
    }

    m_export->setEnabled(t.enabled && !t.formats.isEmpty());
    if (t.enabled)
        m_pv_detail->setText(tr("Exports the whole project's %1.\nClick Export to choose a file.")
                                 .arg(t.title.toLower()));
    else
        m_pv_detail->setText(tr("Not yet available in this version."));
}

void ExportManagerWindow::doExport()
{
    if (m_current < 0) return;
    const auto& ts = targets();
    if (m_current >= ts.size()) return;
    const Target& t = ts[m_current];

    auto* checked = m_fmt_grp->checkedButton();
    const QString fmt = checked ? checked->text() : QString();

    if (t.id == QLatin1String("contacts")) {
        if (fmt == QLatin1String("CSV"))  emit exportContactsCsvRequested();
        else if (fmt == QLatin1String("PDF"))  emit exportContactsPdfRequested();
        else if (fmt == QLatin1String("Word")) emit exportContactsWordRequested();
    } else if (t.id == QLatin1String("screenshot")) {
        emit exportScreenshotRequested();
    }
}

} // namespace dolphin::ui
