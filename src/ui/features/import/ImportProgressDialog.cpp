#include "ui/features/import/ImportProgressDialog.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QColor>
#include <QDateTime>
#include <QCloseEvent>
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
static constexpr int kListMinH = 140;  // task list min height (comfortable with few tasks)
static constexpr int kListMaxH = 440;  // task list max height before it scrolls (~9 rows)
// Format-badge size is a design token (Theme::kFormatBadgeSize / @badgeSize in QSS)
// so the C++ setFixedSize and the stylesheet min/max stay in sync from one source.

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

    // Stage pipeline (e.g. Reading → Building map). Chips are created on the first
    // job once the operation kind is known, and refreshed by updateStages().
    m_stage_box = new QWidget(header);
    m_stage_box->setObjectName("epdStages");
    m_stage_lay = new QHBoxLayout(m_stage_box);
    m_stage_lay->setContentsMargins(0, 2, 0, 2);
    m_stage_lay->setSpacing(Theme::kSpacing2);
    m_stage_lay->addStretch(1);
    hdr_lay->addWidget(m_stage_box);

    hdr_lay->addSpacing(4);

    m_overall_bar = new QProgressBar(header);
    m_overall_bar->setObjectName("overallBar");
    m_overall_bar->setRange(0, 100);
    m_overall_bar->setValue(0);
    m_overall_bar->setTextVisible(false);
    hdr_lay->addWidget(m_overall_bar);

    // Current-activity line ("Reading …" / "Building map — X of Y" / "… complete").
    m_sub_lbl = new QLabel(tr("Starting…"), header);
    m_sub_lbl->setObjectName("subtitleLabel");
    hdr_lay->addWidget(m_sub_lbl);

    root->addWidget(header);

    // -- Scrollable file list --------------------------------------------------
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setMinimumHeight(kListMinH);
    m_scroll->setMaximumHeight(kListMaxH);

    m_list_body = new QWidget();
    m_list_body->setObjectName("epdListBody");
    m_list_lay = new QVBoxLayout(m_list_body);
    m_list_lay->setContentsMargins(Theme::kSpacing3, Theme::kSpacing2, Theme::kSpacing3, Theme::kSpacing2);
    m_list_lay->setSpacing(0);   // rows are flush; separated by each row's bottom border
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
    connect(m_bg_btn, &QPushButton::clicked, this, &ExecutionProgressDialog::runInBackground);
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
        m_backgrounded = false;
        m_pending_map_loads = 0;
        m_map_total = 0;
        m_has_map_phase = false;
        m_stages_built = false;   // rebuild chips for the new batch's operation kind
        m_title_lbl->setText(tr("Background Tasks"));
        m_close_btn->setEnabled(false);
        m_bg_btn->setEnabled(true);
        m_start_ms = QDateTime::currentMSecsSinceEpoch();
        m_timer->start();
    }

    // Determine the operation kind (and build the stage chips) from the first job:
    // correction/processing tags have no map phase; everything else is an import.
    if (!m_stages_built) {
        const QString f = format.toUpper();
        m_op_is_processing = (f == "COR" || f == "SBP" || f == "RUN");
        buildStageChips(m_op_is_processing ? 1 : 2);
        m_stages_built = true;
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
        QFontMetrics(fn).elidedText(filename, Qt::ElideMiddle, 260);

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

    showForActiveBatch();

    if (!m_backgrounded) {
        raise();
        activateWindow();
    }
}

void ExecutionProgressDialog::updateJob(const std::string& layer_id, int percent)
{
    auto* r = findRow(layer_id);
    if (!r) return;
    r->percent = percent;
    if (r->result_lbl && r->state == FileRow::State::Active)
        r->result_lbl->setText(tr("Reading %1%").arg(percent));
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

    (void)freq_khz; (void)coord_sys;  // CRS/freq detail belongs in the layer inspector

    QStringList parts;
    parts << (artifact_count >= 1000
        ? QString("%1k records").arg(artifact_count / 1000.0, 0, 'f', 1)
        : QString("%1 records").arg(artifact_count));
    if (r->size_mb > 0.f)
        parts << sizeMbStr(r->size_mb);

    if (r->result_lbl) {
        r->result_lbl->setText(parts.join("  ·  "));
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

    if (r->result_lbl) {
        r->result_lbl->setText(result_text);
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

    if (r->result_lbl) {
        const QString msg = error.length() > 38 ? error.left(35) + "…"
                          : error.isEmpty()     ? tr("Failed") : error;
        r->result_lbl->setText(msg);
        r->result_lbl->setToolTip(error);   // full error on hover
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

    // One compact line: [status icon] [name (elided, stretch)] [status/result (right)].
    // No nested vertical layout and no per-row bar, so a row is always a single line.
    auto* h = new QHBoxLayout(card);
    h->setContentsMargins(Theme::kSpacing3, Theme::kSpacing2, Theme::kSpacing3, Theme::kSpacing2);
    h->setSpacing(Theme::kSpacing2);

    // Status icon (replaces the old DLP badge): ● reading / ✓ done / ✕ failed.
    row.badge = new QLabel(QStringLiteral("●"), card);
    row.badge->setObjectName("rowStatusIcon");
    row.badge->setAlignment(Qt::AlignCenter);
    row.badge->setFixedWidth(16);
    row.badge->setStyleSheet(QString("color:%1; font-weight:700;")
        .arg(QColor(Theme::kAccent).name()));
    h->addWidget(row.badge);

    row.name_lbl = new QLabel(row.display_name, card);
    row.name_lbl->setObjectName("fileName");
    h->addWidget(row.name_lbl, 1);

    // Live status while active ("Reading X%"), then the result / error. Right-aligned
    // and kept short (name pre-elided) so the row never overflows — no h-scroll.
    row.result_lbl = new QLabel(tr("Reading…"), card);
    row.result_lbl->setObjectName("fileResult");
    row.result_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    h->addWidget(row.result_lbl);

    row.bar        = nullptr;   // overall bar + per-row "Reading X%" instead
    row.meta_lbl   = nullptr;
    row.status_lbl = nullptr;
    return card;
}

void ExecutionProgressDialog::applyCardState(FileRow& row, FileRow::State s)
{
    row.state = s;
    if (!row.badge) return;
    const QString glyph = (s == FileRow::State::Done)   ? QStringLiteral("✓")
                        : (s == FileRow::State::Failed) ? QStringLiteral("✕")
                                                        : QStringLiteral("●");
    const QColor c = (s == FileRow::State::Done)   ? QColor(Theme::kSuccess)
                   : (s == FileRow::State::Failed) ? QColor(Theme::kDanger)
                                                   : QColor(Theme::kAccent);
    row.badge->setText(glyph);
    row.badge->setStyleSheet(QString("color:%1; font-weight:600;").arg(c.name()));
}

void ExecutionProgressDialog::updateHeader()
{
    const int total = std::max(m_queue_total, static_cast<int>(m_rows.size()));

    if (m_all_done)
        m_title_lbl->setText(tr("All Done"));
    else if (total <= 0)
        m_title_lbl->setText(tr("Background Tasks"));
    else
        m_title_lbl->setText(m_op_is_processing
            ? tr("Processing %1 line(s)").arg(total)
            : tr("Importing %1 line(s)").arg(total));

    // The stepper + the sub-label ("now" line) are owned by updateStages().
    updateStages();
}

void ExecutionProgressDialog::buildStageChips(int n)
{
    if (!m_stage_lay) return;
    // Wipe any existing chips / arrows / stretch, then build n fresh chips.
    QLayoutItem* it;
    while ((it = m_stage_lay->takeAt(0)) != nullptr) {
        if (auto* w = it->widget()) w->deleteLater();
        delete it;
    }
    m_stage_lbls.clear();
    for (int i = 0; i < n; ++i) {
        if (i > 0) {
            auto* arrow = new QLabel(QStringLiteral("→"), m_stage_box);
            arrow->setStyleSheet(QString("color:%1;")
                .arg(QColor(Theme::kTextMuted).name()));
            m_stage_lay->addWidget(arrow);
        }
        auto* chip = new QLabel(m_stage_box);
        chip->setObjectName("stageChip");
        m_stage_lbls.push_back(chip);
        m_stage_lay->addWidget(chip);
    }
    m_stage_lay->addStretch(1);
}

void ExecutionProgressDialog::updateStages()
{
    if (m_stage_lbls.empty()) return;

    // Total = the whole batch (queued + dispatched), not just the rows visible so
    // far. With the D-14 import cap (2 concurrent), only 2 of N rows exist while the
    // rest are queued; using m_rows.size() here under-reported the total ("of 2" for
    // a 3-line import) and let reading_done flip true before the queued lines were
    // even added. Mirror updateHeader().
    const int rows  = static_cast<int>(m_rows.size());
    const int total = std::max(m_queue_total, rows);
    int done = 0, failed = 0;
    for (int i = 0; i < rows; ++i) {
        if      (m_rows[i].state == FileRow::State::Done)   ++done;
        else if (m_rows[i].state == FileRow::State::Failed) ++failed;
    }
    const int  parsed       = done + failed;
    // Reading is only "done" once every batch line has produced a finished row —
    // not when the first 2 dispatched lines finish ahead of the queued remainder.
    const bool reading_done = total > 0 && (parsed == total);

    // st: 0 = pending · 1 = active · 2 = done
    auto setChip = [&](int idx, const QString& name, int st) {
        if (idx < 0 || idx >= static_cast<int>(m_stage_lbls.size())) return;
        const QString icon = (st == 2) ? QStringLiteral("✓")
                           : (st == 1) ? QStringLiteral("●")
                                       : QStringLiteral("○");
        const QColor c = (st == 2) ? QColor(Theme::kSuccess)
                       : (st == 1) ? QColor(Theme::kAccent)
                                   : QColor(Theme::kTextMuted);
        m_stage_lbls[idx]->setText(icon + "  " + name);
        m_stage_lbls[idx]->setStyleSheet(QString("color:%1; font-weight:%2;")
            .arg(c.name(), st == 1 ? "600" : "400"));
    };

    // Sub-line is an aggregate summary — per-line detail lives in the rows below.
    QString now;
    if (m_op_is_processing) {
        setChip(0, tr("Processing"), reading_done ? 2 : 1);
        now = reading_done ? tr("%1 line(s) complete").arg(total)
                           : tr("Processing… %1 of %2").arg(parsed).arg(total);
    } else {
        setChip(0, tr("Reading"), reading_done ? 2 : 1);
        const bool map_done = reading_done && m_pending_map_loads == 0;
        setChip(1, tr("Building map"),
                !reading_done ? 0 : (map_done ? 2 : 1));
        if (!reading_done)
            now = tr("Reading… %1 of %2 lines").arg(parsed).arg(total);
        else if (m_pending_map_loads > 0)
            now = tr("Building map — %1 of %2")
                    .arg(m_map_total - m_pending_map_loads).arg(m_map_total);
        else
            now = tr("%1 line(s) complete").arg(total);
    }
    if (failed > 0) now += tr("   ·   %1 failed").arg(failed);
    if (m_sub_lbl) m_sub_lbl->setText(now);
}

void ExecutionProgressDialog::updateOverallProgress()
{
    if (m_rows.empty()) return;
    const int total = static_cast<int>(m_rows.size());
    int sum = 0;
    for (const auto& r : m_rows)
        sum += (r.state != FileRow::State::Active) ? 100 : r.percent;
    m_overall_bar->setValue(sum / total);
    updateStages();
}

void ExecutionProgressDialog::checkAllDone()
{
    if (m_all_done) return;
    for (const auto& r : m_rows)
        if (r.state == FileRow::State::Active) return;

    // All rows parsed — wait for the rasteriser before declaring "All Done".
    // updateStages() shows "Building map — X of Y" on the sub-line meanwhile.
    if (m_pending_map_loads > 0) {
        updateStages();
        return;
    }

    m_all_done = true;
    m_timer->stop();
    m_overall_bar->setValue(100);
    updateHeader();

    // Map-only phase (opening a project — no import/reindex rows to review): this
    // panel was pure loading feedback, so auto-dismiss instead of forcing a manual
    // close on every open. Import/reindex (rows present) keeps the manual close so
    // the user can review per-file results.
    if (m_rows.empty()) {
        hide();
        return;
    }

    m_bg_btn->setEnabled(false);
    m_close_btn->setEnabled(true);
    m_close_btn->setDefault(true);
    m_close_btn->setFocus();
}

void ExecutionProgressDialog::onMapLoadPending()
{
    if (m_all_done) {
        m_all_done = false;
        m_backgrounded = false;
        m_close_btn->setEnabled(false);
        m_bg_btn->setEnabled(true);
    }
    const bool phase_starting = (m_pending_map_loads == 0);
    ++m_pending_map_loads;
    ++m_map_total;
    m_has_map_phase = true;
    updateStages();

    // Surface the panel for map-only work (e.g. opening a recent project, which has
    // no import/reindex rows to call addJob()). Deferred so a fast cached open that
    // finishes in well under the delay never flashes a dialog — it only appears when
    // loading actually takes a moment. Auto-dismissed in checkAllDone() when a
    // map-only phase completes.
    if (phase_starting && !isVisible() && !m_backgrounded) {
        QTimer::singleShot(400, this, [this] {
            if (m_pending_map_loads > 0 && !m_all_done)
                showForActiveBatch();
        });
    }
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

void ExecutionProgressDialog::runInBackground()
{
    m_backgrounded = true;
    hide();
}

void ExecutionProgressDialog::showForActiveBatch()
{
    if (m_backgrounded || isVisible()) return;

    if (parentWidget()) {
        const QRect pr = parentWidget()->geometry();
        move(pr.center().x() - width() / 2,
             pr.center().y() - height() / 2);
    }
    show();
    m_start_ms = QDateTime::currentMSecsSinceEpoch();
    m_timer->start();
}

void ExecutionProgressDialog::closeEvent(QCloseEvent* event)
{
    if (!m_all_done) {
        runInBackground();
        event->ignore();
        return;
    }

    QDialog::closeEvent(event);
}

} // namespace dolphin::ui
