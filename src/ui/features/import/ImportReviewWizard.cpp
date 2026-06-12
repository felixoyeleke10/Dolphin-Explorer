// ImportReviewWizard.cpp — constructor, drag-and-drop, file management, accept.
#include "ui/features/import/ImportReviewWizard.h"
#include "ui/shared/UiUtils.h"
#include "app/import/ImportClassifier.h"
#include "ui/shell/Theme.h"
#include "io/ProbeDispatch.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {
void applyFileStatus(QLabel* lbl, const char* state)
{
    lbl->setProperty("state", QLatin1String(state));
    lbl->style()->unpolish(lbl);
    lbl->style()->polish(lbl);
}
} // namespace

namespace dolphin::ui {

static constexpr int kMinW  = 700;
static constexpr int kMinH  = 560;
static constexpr int kInitW = 760;
static constexpr int kInitH = 600;

// -----------------------------------------------------------------------------
//  Construction
// -----------------------------------------------------------------------------

ImportReviewWizard::ImportReviewWizard(app::Project*           current_project,
                                       const core::SpatialRef& suggested_crs,
                                       QWidget*                parent)
    : QDialog(parent, Qt::Dialog)
    , m_current_project(current_project)
    , m_suggested_crs(suggested_crs)
    , m_project_crs(suggested_crs)
{
    setWindowTitle(tr("Import Survey Files"));
    setModal(true);
    setMinimumSize(kMinW, kMinH);
    resize(kInitW, kInitH);
    setAcceptDrops(true);

    auto* root = makeCompactLayout<QVBoxLayout>(this);

    // -- Header ------------------------------------------------------------
    {
        auto* hdr = new QFrame(this);
        hdr->setObjectName("dlgHeader");
        auto* hl = new QHBoxLayout(hdr);
        hl->setContentsMargins(Theme::kSpacing7, Theme::kSpacing4, Theme::kSpacing7, 10);

        m_hdr_title = new QLabel(tr("Import Survey Files"), hdr);
        m_hdr_title->setObjectName("dlgTitle");
        m_hdr_subtitle = new QLabel(
            tr("Add files below, review CRS and modality, then click Import."), hdr);
        m_hdr_subtitle->setObjectName("dlgSubtitle");
        auto* title = m_hdr_title;
        auto* sub   = m_hdr_subtitle;

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
    auto* body = new QWidget(this);
    body->setObjectName("dlgBody");
    auto* bl = new QVBoxLayout(body);
    bl->setContentsMargins(Theme::kSpacing6, Theme::kSpacing4, Theme::kSpacing6, Theme::kSpacing3);
    bl->setSpacing(10);

    buildFileSection(bl);
    buildTabSection(bl);
    buildProjectSection(bl);

    root->addWidget(body, 1);

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

        m_status_label = new QLabel(foot);
        m_status_label->setObjectName("dlgSummary");
        fl->addWidget(m_status_label, 1);

        auto* cancel_btn = new QPushButton(tr("Cancel"), foot);
        cancel_btn->setObjectName("dlgBtnSecondary");
        cancel_btn->setFixedHeight(Theme::kDialogBtnH);
        cancel_btn->setMinimumWidth(80);
        fl->addWidget(cancel_btn);

        m_import_btn = new QPushButton(tr("Import"), foot);
        m_import_btn->setObjectName("dlgBtnPrimary");
        m_import_btn->setFixedHeight(Theme::kDialogBtnH);
        m_import_btn->setMinimumWidth(100);
        m_import_btn->setDefault(true);
        m_import_btn->setEnabled(false);
        fl->addWidget(m_import_btn);

        root->addWidget(foot);

        connect(cancel_btn,  &QPushButton::clicked, this, &QDialog::reject);
        connect(m_import_btn, &QPushButton::clicked, this, &ImportReviewWizard::onAccept);
    }

    updateImportButton();
}

// -----------------------------------------------------------------------------
//  Drag-and-drop
// -----------------------------------------------------------------------------

void ImportReviewWizard::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void ImportReviewWizard::dropEvent(QDropEvent* e)
{
    QStringList paths;
    for (const QUrl& url : e->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (!path.isEmpty()) paths << path;
    }
    if (!paths.isEmpty()) addFiles(paths);
}

// -----------------------------------------------------------------------------
//  File management
// -----------------------------------------------------------------------------

void ImportReviewWizard::onAddFiles()
{
    const QString filter = QString::fromLatin1(io::kSupportedFileFilter);
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add Survey Files"), QDir::homePath(), filter);
    if (!paths.isEmpty())
        addFiles(paths, m_module_filter);
}

void ImportReviewWizard::setModuleFilter(std::vector<core::ArtifactType> filter)
{
    m_module_filter = std::move(filter);
    updateSensorHeader();
}

bool ImportReviewWizard::fileMatchesSensorFilter(const io::ProbeResult& r) const
{
    if (m_module_filter.empty()) return true;  // drag-drop path — accept all types
    for (const auto type : m_module_filter) {
        switch (type) {
            case core::ArtifactType::Sidescan:     if (r.has_sidescan)     return true; break;
            case core::ArtifactType::SubBottom:    if (r.has_subbottom)    return true; break;
            case core::ArtifactType::Magnetometer: if (r.has_magnetometer) return true; break;
            case core::ArtifactType::Multibeam:    if (r.has_multibeam)    return true; break;
            default: break;
        }
    }
    return false;
}

void ImportReviewWizard::updateSensorHeader()
{
    if (!m_hdr_title || !m_hdr_subtitle) return;
    if (m_module_filter.empty()) {
        m_hdr_title->setText(tr("Import Survey Files"));
        m_hdr_subtitle->setText(
            tr("Add files below, review CRS and modality, then click Import."));
        return;
    }

    QString sensor;
    switch (m_module_filter[0]) {
        case core::ArtifactType::Sidescan:     sensor = tr("Sidescan Sonar");      break;
        case core::ArtifactType::SubBottom:    sensor = tr("Sub-Bottom Profiler"); break;
        case core::ArtifactType::Magnetometer: sensor = tr("Magnetometer");        break;
        case core::ArtifactType::Multibeam:    sensor = tr("Multibeam Bathymetry"); break;
        default:                               sensor = tr("Survey Files");         break;
    }

    m_hdr_title->setText(tr("Import %1").arg(sensor));
    m_hdr_subtitle->setText(
        tr("Only %1 files will be accepted. Review settings, then click Import.")
            .arg(sensor));
    setWindowTitle(tr("Import %1").arg(sensor));
}

void ImportReviewWizard::addFiles(const QStringList& paths,
                                  std::vector<core::ArtifactType> module_filter)
{
    for (const QString& path : paths) {
        // Skip duplicates
        bool dup = false;
        for (const auto& e : m_entries) { if (e.path == path) { dup = true; break; } }
        if (dup) continue;

        const int idx = static_cast<int>(m_entries.size());

        FileEntry entry;
        entry.path          = path;
        entry.file_name     = QFileInfo(path).fileName();
        entry.probing       = true;
        entry.module_filter = module_filter;

        // Build a file row widget
        auto* row = new QFrame(m_file_content);
        row->setObjectName("dlgSection");
        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(Theme::kSpacing3, Theme::kSpacing1, Theme::kSpacing3, Theme::kSpacing1);
        hl->setSpacing(Theme::kSpacing3);

        entry.name_label = new QLabel("<b>" + entry.file_name.toHtmlEscaped() + "</b>", row);

        entry.detail_label = new QLabel(row);
        entry.detail_label->setObjectName("dlgLabelMeta");

        entry.status_label = new QLabel(tr("Scanning..."), row);
        entry.status_label->setObjectName("importFileStatus");

        hl->addWidget(entry.name_label);
        hl->addWidget(entry.detail_label, 1);
        hl->addWidget(entry.status_label);

        // Insert before stretch
        const int stretch_pos = m_file_layout->count() - 1;  // before the trailing addStretch
        m_file_layout->insertWidget(stretch_pos, row);

        m_entries.append(std::move(entry));
        startProbe(idx);
    }

    // Hide drop hint once files are present (safe to call multiple times)
    if (!m_entries.isEmpty()) {
        m_drop_hint->hide();
        m_file_layout->removeWidget(m_drop_hint);
    }

    updateImportButton();
}

void ImportReviewWizard::startProbe(int idx)
{
    FileEntry& e = m_entries[idx];
    const std::string path = e.path.toStdString();

    auto* watcher = new QFutureWatcher<io::ProbeResult>(this);
    e.watcher = watcher;

    connect(watcher, &QFutureWatcher<io::ProbeResult>::finished, this,
            [this, idx, watcher]() {
                m_entries[idx].result  = watcher->result();
                m_entries[idx].done    = true;
                m_entries[idx].probing = false;
                watcher->deleteLater();
                m_entries[idx].watcher = nullptr;
                onProbeFinished(idx);
            });

    watcher->setFuture(QtConcurrent::run([path]() {
        return io::probeFile(path);
    }));
}

void ImportReviewWizard::onProbeFinished(int idx)
{
    FileEntry& e = m_entries[idx];

    if (e.done && e.result.success) {
        // Classify against the current project (Reuse / Rebuild / ImportNew).
        e.classify_kind = app::classifyImportAction(e.path, m_current_project).kind;
        // Check whether the file's modality matches the wizard's sensor filter.
        e.modality_mismatch = !fileMatchesSensorFilter(e.result);
    }
    updateFileRow(idx);
    updateImportButton();
    if (!m_rebuild_pending) {
        m_rebuild_pending = true;
        QTimer::singleShot(0, this, [this]() {
            m_rebuild_pending = false;
            rebuildSummaryTab();
            rebuildNavTab();
            rebuildCrsTab();
            rebuildHeadingTab();
            rebuildChannelsTab();
            rebuildHeaderTab();
            updateTabVisibility();
        });
    }
}

void ImportReviewWizard::updateFileRow(int idx)
{
    FileEntry& e = m_entries[idx];
    if (!e.name_label) return;

    if (e.probing) {
        e.status_label->setText(tr("Scanning..."));
        return;
    }

    const io::ProbeResult& r = e.result;

    if (!r.success) {
        e.status_label->setText(tr("Error"));
        applyFileStatus(e.status_label, "error");
        e.detail_label->setText(QString::fromStdString(r.error_message));
        return;
    }

    // Detail: "SEG-Y · Sub-Bottom"
    e.detail_label->setText(
        QString::fromStdString(r.format_name) + "  \xC2\xB7  " + modalityString(r));

    // Reject files whose modality doesn't match the selected sensor type.
    if (e.modality_mismatch) {
        e.status_label->setText(tr("Wrong sensor type"));
        applyFileStatus(e.status_label, "error");
        return;
    }

    // Show import decision badge, overriding CRS state when the file is recognised.
    using Kind = FileImportAction::Kind;
    if (e.classify_kind == Kind::ReuseExisting) {
        e.status_label->setText(tr("Already indexed"));
        applyFileStatus(e.status_label, "ok");
    } else if (e.classify_kind == Kind::RebuildExisting) {
        e.status_label->setText(tr("Rebuild needed"));
        applyFileStatus(e.status_label, "caution");
    } else {
        const bool crs_resolved = !r.needs_crs_review || !m_project_crs.empty();
        if (!crs_resolved) {
            e.status_label->setText(tr("Needs CRS"));
            applyFileStatus(e.status_label, "caution");
        } else {
            e.status_label->setText(r.is_projected ? tr("Projected") : tr("Geographic"));
            applyFileStatus(e.status_label, "ok");
        }
    }
}

// -----------------------------------------------------------------------------
//  Accept — build result and emit
// -----------------------------------------------------------------------------

void ImportReviewWizard::onAccept()
{
    m_result = ImportDialogResult{};
    m_result.accepted = true;

    // Project target
    if (m_radio_new && m_radio_new->isChecked()) {
        m_result.target = ImportDialogResult::ProjectTarget::New;
        m_result.new_project_name   = m_name_edit ? m_name_edit->text() : QString();
        m_result.new_project_folder = m_browse_folder + "/"
                                    + m_result.new_project_name;
    } else {
        m_result.target = ImportDialogResult::ProjectTarget::Current;
    }

    // Build per-file actions — classifier decides Reuse/Rebuild/ImportNew.
    for (const FileEntry& e : m_entries) {
        if (!e.done || !e.result.success) continue;

        // Base action from the classifier (kind + existing ids already set).
        FileImportAction action = app::classifyImportAction(e.path, m_current_project);

        // Source CRS: project-level selection takes priority (confirmed by the user
        // in the CRS tab), then the exact declared CRS from the probe (e.g. EPSG:4326
        // embedded in the file header).  m_project_crs is only non-empty for files
        // that need CRS review; geographic files resolve without it.
        if (!m_project_crs.empty() && e.result.needs_crs_review)
            action.source_crs = m_project_crs;
        else if (!e.result.declared_crs.empty())
            action.source_crs = e.result.declared_crs;

        action.module_filter = e.module_filter;
        m_result.files.append(action);
    }

    if (m_result.files.isEmpty()) {
        reject();
        return;
    }

    // Propagate confirmed project CRS to m_pending_crs in MainWindow.
    m_result.source_crs = m_project_crs;

    emit importConfirmed(m_result);
    accept();
}

} // namespace dolphin::ui
