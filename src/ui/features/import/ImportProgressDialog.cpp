#include "ui/features/import/ImportProgressDialog.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace dolphin::ui {

static constexpr int kDialogW  = 520;  // fixed dialog width
static constexpr int kBadgeSz  = 32;   // format-type badge square size

namespace {

QString sizeMbStr(float mb)
{
    if (mb <= 0.f) return {};
    return mb >= 1000.f
        ? QString("%1 GB").arg(mb / 1024.f, 0, 'f', 2)
        : QString("%1 MB").arg(mb, 0, 'f', 1);
}

} // namespace

// -- Constructor ----------------------------------------------------------------

ExecutionProgressDialog::ExecutionProgressDialog(QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::CustomizeWindowHint
                     | Qt::WindowTitleHint | Qt::WindowCloseButtonHint)
{
    setWindowTitle(tr("Background Tasks"));
    setModal(false);
    setFixedWidth(kDialogW);


    // -- Root layout -----------------------------------------------------------
    auto* root = makeCompactLayout<QVBoxLayout>(this);

    // -- Header ----------------------------------------------------------------
    auto* header = new QWidget(this);
    header->setObjectName("epdHeader");
    auto* hdr_lay = new QVBoxLayout(header);
    hdr_lay->setContentsMargins(Theme::kSpacing5, 12, Theme::kSpacing5, Theme::kSpacing3);
    hdr_lay->setSpacing(Theme::kSpacing1);

    m_title_lbl = new QLabel(tr("Background Tasks"), header);
    m_title_lbl->setObjectName("titleLabel");
    hdr_lay->addWidget(m_title_lbl);

    m_sub_lbl = new QLabel(tr("Starting…"), header);
    m_sub_lbl->setObjectName("subtitleLabel");
    hdr_lay->addWidget(m_sub_lbl);

    hdr_lay->addSpacing(4);

    m_overall_bar = new QProgressBar(header);
    m_overall_bar->setObjectName("overallBar");
    m_overall_bar->setRange(0, 100);
    m_overall_bar->setValue(0);
    m_overall_bar->setTextVisible(false);
    hdr_lay->addWidget(m_overall_bar);

    root->addWidget(header);

    // -- Scrollable file list --------------------------------------------------
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setMinimumHeight(60);
    m_scroll->setMaximumHeight(260);

    m_list_body = new QWidget();
    m_list_body->setObjectName("epdListBody");
    m_list_lay = new QVBoxLayout(m_list_body);
    m_list_lay->setContentsMargins(Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3, Theme::kSpacing3);
    m_list_lay->setSpacing(Theme::kSpacing2);
    m_list_lay->addStretch(1);

    m_scroll->setWidget(m_list_body);
    root->addWidget(m_scroll, 1);

    // -- Footer ----------------------------------------------------------------
    auto* footer = new QWidget(this);
    footer->setObjectName("epdFooter");
    auto* foot_lay = new QHBoxLayout(footer);
    foot_lay->setContentsMargins(Theme::kSpacing4, 8, Theme::kSpacing4, 8);
    foot_lay->setSpacing(Theme::kSpacing3);

    m_elapsed_lbl = new QLabel(tr("Elapsed: 0:00"), footer);
    m_elapsed_lbl->setObjectName("elapsedLabel");
    foot_lay->addWidget(m_elapsed_lbl, 1);

    m_bg_btn = new QPushButton(tr("Run in Background"), footer);
    m_bg_btn->setObjectName("bgBtn");
    connect(m_bg_btn, &QPushButton::clicked, this, &QDialog::hide);
    foot_lay->addWidget(m_bg_btn);

    m_close_btn = new QPushButton(tr("Close"), footer);
    m_close_btn->setObjectName("closeBtn");
    m_close_btn->setEnabled(false);
    connect(m_close_btn, &QPushButton::clicked, this, &QDialog::accept);
    foot_lay->addWidget(m_close_btn);

    root->addWidget(footer);

    // -- Elapsed timer ---------------------------------------------------------
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &ExecutionProgressDialog::onTick);
}

// -- Public API -----------------------------------------------------------------

void ExecutionProgressDialog::setQueueTotal(int n)
{
    m_queue_total = n;
    updateHeader();
}

void ExecutionProgressDialog::addJob(const std::string& layer_id,
                                  const QString&     filename,
                                  const QString&     format,
                                  float              size_mb)
{
    // New batch starting: purge stale Done/Failed rows so prior results don't block
    if (m_all_done) {
        clearFinishedRows();
        m_all_done = false;
        m_title_lbl->setText(tr("Background Tasks"));
        m_close_btn->setEnabled(false);
        m_bg_btn->setEnabled(true);
        m_start_ms = QDateTime::currentMSecsSinceEpoch();
        m_timer->start();
    }

    // Duplicate check: skip Active (already in-flight), replace Done/Failed (re-import)
    if (auto* existing = findRow(layer_id)) {
        if (existing->state == FileRow::State::Active)
            return;
        removeRowById(layer_id);
    }

    QFont fn = font();
    fn.setPixelSize(12);
    const QString elided =
        QFontMetrics(fn).elidedText(filename, Qt::ElideMiddle, 320);

    FileRow row;
    row.layer_id     = layer_id;
    row.display_name = elided;
    row.format       = format.toUpper();
    row.size_mb      = size_mb;

    m_rows.push_back(std::move(row));

    FileRow& r = m_rows.back();
    r.card = buildCard(r, m_list_body);
    // Insert before the trailing stretch item
    m_list_lay->insertWidget(m_list_lay->count() - 1, r.card);

    updateHeader();

    if (!isVisible()) {
        if (parentWidget()) {
            const QRect pr = parentWidget()->geometry();
            move(pr.center().x() - width() / 2,
                 pr.center().y() - height() / 2);
        }
        show();
        m_start_ms = QDateTime::currentMSecsSinceEpoch();
        m_timer->start();
    }

    raise();
    activateWindow();
}

void ExecutionProgressDialog::updateJob(const std::string& layer_id, int percent)
{
    auto* r = findRow(layer_id);
    if (!r) return;
    if (r->bar)
        r->bar->setValue(percent);
    updateOverallProgress();
}

void ExecutionProgressDialog::finishJob(const std::string& layer_id,
                                     int                artifact_count,
                                     float              freq_khz,
                                     const QString&     coord_sys)
{
    auto* r = findRow(layer_id);
    if (!r) return;

    applyCardState(*r, FileRow::State::Done);

    if (r->bar)        { r->bar->setValue(100); r->bar->hide(); }
    if (r->status_lbl) { r->status_lbl->hide(); }

    QStringList parts;
    parts << (artifact_count >= 1000
        ? QString("%1k artifacts").arg(artifact_count / 1000.0, 0, 'f', 1)
        : QString("%1 artifacts").arg(artifact_count));
    if (!coord_sys.isEmpty() && coord_sys != "—")
        parts << coord_sys;
    if (freq_khz > 0.f)
        parts << QString("%1 kHz").arg(static_cast<int>(freq_khz));

    if (r->result_lbl) {
        r->result_lbl->setText("✔  " + parts.join("  ·  "));
        r->result_lbl->setProperty("state", "done");
        r->result_lbl->style()->unpolish(r->result_lbl);
        r->result_lbl->style()->polish(r->result_lbl);
        r->result_lbl->show();
    }

    updateHeader();
    updateOverallProgress();
    checkAllDone();
}

void ExecutionProgressDialog::finishJob(const std::string& layer_id,
                                     const QString&     result_text)
{
    auto* r = findRow(layer_id);
    if (!r) return;

    applyCardState(*r, FileRow::State::Done);

    if (r->bar)        { r->bar->setValue(100); r->bar->hide(); }
    if (r->status_lbl) { r->status_lbl->hide(); }

    if (r->result_lbl) {
        r->result_lbl->setText("✔  " + result_text);
        r->result_lbl->setProperty("state", "done");
        r->result_lbl->style()->unpolish(r->result_lbl);
        r->result_lbl->style()->polish(r->result_lbl);
        r->result_lbl->show();
    }

    updateHeader();
    updateOverallProgress();
    checkAllDone();
}

void ExecutionProgressDialog::failJob(const std::string& layer_id,
                                   const QString&     error)
{
    auto* r = findRow(layer_id);
    if (!r) {
        // Synchronous failure before indexingStarted — create a stub row first
        const QString display = layer_id.empty()
            ? tr("Unknown file") : QString::fromStdString(layer_id);
        addJob(layer_id, display, "?", 0.f);
        r = findRow(layer_id);
        if (!r) return;
    }

    applyCardState(*r, FileRow::State::Failed);

    if (r->bar)        { r->bar->hide(); }
    if (r->status_lbl) { r->status_lbl->hide(); }

    if (r->result_lbl) {
        const QString msg = error.length() > 60
            ? error.left(57) + "…" : error;
        r->result_lbl->setText("✕  " + msg);
        r->result_lbl->setProperty("state", "failed");
        r->result_lbl->style()->unpolish(r->result_lbl);
        r->result_lbl->style()->polish(r->result_lbl);
        r->result_lbl->show();
    }

    updateHeader();
    updateOverallProgress();
    checkAllDone();
}

// -- Private helpers ------------------------------------------------------------

ExecutionProgressDialog::FileRow*
ExecutionProgressDialog::findRow(const std::string& id)
{
    for (auto& r : m_rows)
        if (r.layer_id == id) return &r;
    return nullptr;
}

void ExecutionProgressDialog::removeRowById(const std::string& id)
{
    for (int i = static_cast<int>(m_rows.size()) - 1; i >= 0; --i) {
        if (m_rows[i].layer_id == id) {
            if (m_rows[i].card) {
                m_list_lay->removeWidget(m_rows[i].card);
                m_rows[i].card->deleteLater();
            }
            m_rows.erase(m_rows.begin() + i);
            return;
        }
    }
}

void ExecutionProgressDialog::clearFinishedRows()
{
    for (int i = static_cast<int>(m_rows.size()) - 1; i >= 0; --i) {
        if (m_rows[i].state != FileRow::State::Active) {
            if (m_rows[i].card) {
                m_list_lay->removeWidget(m_rows[i].card);
                m_rows[i].card->deleteLater();
            }
            m_rows.erase(m_rows.begin() + i);
        }
    }
}

QFrame* ExecutionProgressDialog::buildCard(FileRow& row, QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName("fileCard");

    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(Theme::kSpacing4, Theme::kSpacing2, Theme::kSpacing4, Theme::kSpacing2);
    v->setSpacing(Theme::kSpacing1);

    // Single row: format badge · filename · size · result
    auto* top = new QWidget(card);
    auto* h = makeCompactLayout<QHBoxLayout>(top, Theme::kSpacing2);

    row.badge = new QLabel(row.format.left(3), top);
    row.badge->setObjectName("formatBadge");
    row.badge->setAlignment(Qt::AlignCenter);
    row.badge->setFixedSize(kBadgeSz, kBadgeSz);
    h->addWidget(row.badge);

    row.name_lbl = new QLabel(row.display_name, top);
    row.name_lbl->setObjectName("fileName");
    h->addWidget(row.name_lbl, 1);

    if (row.size_mb > 0.f) {
        row.meta_lbl = new QLabel(sizeMbStr(row.size_mb), top);
        row.meta_lbl->setObjectName("fileMeta");
        h->addWidget(row.meta_lbl);
    }

    row.result_lbl = new QLabel("", top);
    row.result_lbl->setObjectName("fileResult");
    row.result_lbl->hide();
    h->addWidget(row.result_lbl);

    v->addWidget(top);

    // Thin progress bar — hides when done/failed, result_lbl appears instead
    row.bar = new QProgressBar(card);
    row.bar->setObjectName("fileBar");
    row.bar->setRange(0, 100);
    row.bar->setValue(0);
    row.bar->setTextVisible(false);
    v->addWidget(row.bar);

    row.status_lbl = nullptr;  // not used in compact layout

    return card;
}

void ExecutionProgressDialog::applyCardState(FileRow& row, FileRow::State s)
{
    row.state = s;
    if (!row.badge) return;

    const char* sv = (s == FileRow::State::Done)   ? "done"
                   : (s == FileRow::State::Failed) ? "failed"
                   :                                  "active";
    row.badge->setProperty("state", sv);
    row.badge->style()->unpolish(row.badge);
    row.badge->style()->polish(row.badge);
    row.badge->update();
}

void ExecutionProgressDialog::updateHeader()
{
    const int done = static_cast<int>(std::count_if(
        m_rows.begin(), m_rows.end(),
        [](const FileRow& r) { return r.state != FileRow::State::Active; }));
    const int total = std::max(m_queue_total, static_cast<int>(m_rows.size()));

    if (total <= 0) {
        m_sub_lbl->setText(tr("Starting…"));
    } else if (m_all_done) {
        m_title_lbl->setText(tr("All Done"));
        m_sub_lbl->setText(total == 1
            ? tr("1 task completed")
            : tr("%1 tasks completed").arg(total));
    } else {
        m_sub_lbl->setText(total == 1
            ? tr("Running 1 task…")
            : tr("Task %1 of %2").arg(done + 1).arg(total));
    }
}

void ExecutionProgressDialog::updateOverallProgress()
{
    if (m_rows.empty()) return;
    const int total = static_cast<int>(m_rows.size());
    int sum = 0;
    for (const auto& r : m_rows) {
        if (r.state != FileRow::State::Active)
            sum += 100;
        else if (r.bar)
            sum += r.bar->value();
    }
    m_overall_bar->setValue(sum / total);
}

void ExecutionProgressDialog::checkAllDone()
{
    if (m_all_done) return;
    for (const auto& r : m_rows)
        if (r.state == FileRow::State::Active) return;

    // All rows parsed — wait for the rasteriser before declaring "All Done".
    if (m_pending_map_loads > 0) {
        m_sub_lbl->setText(tr("Loading into map…"));
        return;
    }

    m_all_done = true;
    m_timer->stop();
    m_overall_bar->setValue(100);
    updateHeader();

    m_bg_btn->setEnabled(false);
    m_close_btn->setEnabled(true);
    m_close_btn->setDefault(true);
    m_close_btn->setFocus();
}

void ExecutionProgressDialog::onMapLoadPending()
{
    ++m_pending_map_loads;
}

void ExecutionProgressDialog::onMapLoadDone()
{
    if (m_pending_map_loads <= 0) return;
    --m_pending_map_loads;
    checkAllDone();
}

void ExecutionProgressDialog::onTick()
{
    const qint64 elapsed = (QDateTime::currentMSecsSinceEpoch() - m_start_ms) / 1000;
    const int mins = static_cast<int>(elapsed / 60);
    const int secs = static_cast<int>(elapsed % 60);
    m_elapsed_lbl->setText(
        QString("Elapsed: %1:%2").arg(mins).arg(secs, 2, 10, QChar('0')));
}

} // namespace dolphin::ui
