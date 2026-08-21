// ImportProgressDialog.Jobs.cpp — the job/state model and progress logic:
// add/update/finish/fail jobs, row bookkeeping, header + stage + overall-progress
// updates, all-done detection, and the map-load phase counters. Construction,
// card/chip building, and window chrome live in ImportProgressDialog.cpp.
#include "ui/features/import/ImportProgressDialog.h"
#include "ui/shell/Theme.h"

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStringList>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace dolphin::ui {

namespace {

QString sizeMbStr(float mb)
{
    if (mb <= 0.f) return {};
    return mb >= 1000.f
        ? QString("%1 GB").arg(mb / 1024.f, 0, 'f', 2)
        : QString("%1 MB").arg(mb, 0, 'f', 1);
}

} // namespace

// -- Public API -----------------------------------------------------------------

void ExecutionProgressDialog::setQueueTotal(int n)
{
    m_queue_total = std::max(0, n);
    m_queue_total_armed = true;
    updateHeader();
}

void ExecutionProgressDialog::addToQueueTotal(int n)
{
    const int base = m_all_done
        ? 0 : std::max(m_queue_total, static_cast<int>(m_rows.size()));
    m_queue_total = base + std::max(0, n);
    m_queue_total_armed = true;
    updateHeader();
}

void ExecutionProgressDialog::addJob(const std::string& layer_id,
                                  const QString&     filename,
                                  const QString&     format,
                                  float              size_mb,
                                  bool               reveal)
{
    // New batch starting: purge stale Done/Failed rows so prior results don't block
    if (m_all_done) {
        clearFinishedRows();
        if (!m_queue_total_armed)
            m_queue_total = 0;
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
    m_queue_total_armed = false;

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
    row.full_name    = filename;
    row.display_name = elided;
    row.format       = format.toUpper();
    row.size_mb      = size_mb;
    row.started_ms   = QDateTime::currentMSecsSinceEpoch();
    row.last_status  = tr("Queued");

    m_rows.push_back(std::move(row));
    if (m_scroll) m_scroll->show();

    FileRow& r = m_rows.back();
    r.card = buildCard(r, m_list_body);
    // Insert before the trailing stretch item
    m_list_lay->insertWidget(m_list_lay->count() - 1, r.card);

    updateHeader();

    if (reveal) showForActiveBatch();

    // Bring it above siblings but do NOT activateWindow(): stealing the foreground
    // from the frameless main window makes it blink. WA_ShowWithoutActivating + this
    // keeps the main window in front; the user can still click the dialog.
    if (!m_backgrounded) raise();
}

void ExecutionProgressDialog::updateJob(const std::string& layer_id, int percent)
{
    auto* r = findRow(layer_id);
    if (!r) return;
    percent = std::clamp(percent, 0, 100);
    r->percent = percent;
    if (r->state == FileRow::State::Active) {
        r->last_status = tr("Reading data");
        if (r->status_lbl) r->status_lbl->setText(r->last_status);
        if (r->result_lbl) r->result_lbl->setText(tr("%1%").arg(percent));
        if (r->bar) r->bar->setValue(percent);
    }
    updateOverallProgress();
}

void ExecutionProgressDialog::updateJob(const std::string& layer_id, int percent,
                                        const QString& status)
{
    auto* r = findRow(layer_id);
    if (!r) return;
    percent = std::clamp(percent, 0, 100);
    r->percent = percent;
    if (r->state == FileRow::State::Active) {
        QString phase = status.trimmed();
        phase.remove(QRegularExpression(QStringLiteral("\\s+\\d+%$")));
        r->last_status = phase.isEmpty() ? tr("Working") : phase;
        if (r->status_lbl) r->status_lbl->setText(r->last_status);
        if (r->result_lbl) r->result_lbl->setText(tr("%1%").arg(percent));
        if (r->bar) r->bar->setValue(percent);
    }
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
    r->percent = 100;
    if (r->bar) r->bar->setValue(100);
    if (r->status_lbl) r->status_lbl->setText(tr("Completed"));

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
    r->percent = 100;
    if (r->bar) r->bar->setValue(100);
    if (r->status_lbl) r->status_lbl->setText(tr("Completed"));

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
    if (r->status_lbl) {
        r->status_lbl->setText(error.isEmpty() ? tr("Task failed") : error);
        r->status_lbl->setToolTip(error);
        r->status_lbl->setWordWrap(true);
    }
    if (r->bar) r->bar->setValue(r->percent);

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

void ExecutionProgressDialog::cancelJob(const std::string& layer_id)
{
    auto* r = findRow(layer_id);
    if (!r) return;
    applyCardState(*r, FileRow::State::Cancelled);
    if (r->status_lbl) r->status_lbl->setText(tr("Cancelled"));
    if (r->result_lbl) {
        r->result_lbl->setText(tr("Cancelled"));
        r->result_lbl->setProperty("state", "cancelled");
        r->result_lbl->show();
    }
    updateHeader();
    updateOverallProgress();
    checkAllDone();
}

// -- Row bookkeeping ------------------------------------------------------------

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

// -- Header / stages / overall progress -----------------------------------------

void ExecutionProgressDialog::updateHeader()
{
    const int total = std::max(m_queue_total, static_cast<int>(m_rows.size()));

    if (m_all_done)
        m_title_lbl->setText(tr("All Done"));
    else if (total <= 0)
        // No per-file rows: either idle, or a map-only phase (opening a project).
        m_title_lbl->setText(m_has_map_phase ? tr("Building map")
                                             : tr("Background Tasks"));
    else
        m_title_lbl->setText(m_op_is_processing
            ? tr("Processing %1 line(s)").arg(total)
            : tr("Importing %1 line(s)").arg(total));

    // The stepper + the sub-label ("now" line) are owned by updateStages().
    updateStages();
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
                                   : Theme::textMutedColor();
        m_stage_lbls[idx]->setText(icon + "  " + name);
        m_stage_lbls[idx]->setStyleSheet(QString("color:%1; font-weight:%2;")
            .arg(c.name(), st == 1 ? "600" : "400"));
    };

    // Map-only phase (opening/reloading a project): no per-file rows, just background
    // map (re)builds. One "Building map" chip + an X-of-Y sub-line.
    if (total == 0 && m_has_map_phase) {
        const int done = std::max(0, m_map_total - m_pending_map_loads);
        const bool map_done = (m_pending_map_loads == 0);
        setChip(0, tr("Building map"), map_done ? 2 : 1);
        if (m_sub_lbl)
            m_sub_lbl->setText(map_done
                ? tr("Maps ready")
                : tr("Building map — %1 of %2").arg(done).arg(m_map_total));
        return;
    }

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
    const int total = std::max(m_queue_total, static_cast<int>(m_rows.size()));
    if (total <= 0) return;
    int sum = 0;
    for (const auto& r : m_rows)
        sum += (r.state != FileRow::State::Active) ? 100 : r.percent;
    m_overall_bar->setValue(sum / total);
    updateStages();
}

void ExecutionProgressDialog::checkAllDone()
{
    if (m_all_done) return;
    const int expected = std::max(m_queue_total, static_cast<int>(m_rows.size()));
    int terminal = 0;
    for (const auto& r : m_rows)
        if (r.state == FileRow::State::Active) return;
        else ++terminal;

    if (terminal < expected) return;

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

// -- Map-load phase -------------------------------------------------------------

// Delay before a map-only phase (project open) surfaces the panel — opens that finish
// within this window (fully-cached, no rebuild) never flash a panel that immediately
// closes; slower (re)builds cross it and show progress.
static constexpr int kMapPhaseShowDelayMs = 350;

void ExecutionProgressDialog::onMapLoadPending(uint64_t task_id,
                                                const QString& layer_name)
{
    if (task_id == 0 || !m_pending_map_task_ids.insert(task_id).second)
        return;
    // Starting a fresh map batch after a previous one finished: reset the map-phase
    // counters (mirrors addJob's reset) so any later "Building map — X of Y" counts
    // this open, not the accumulation of every open this session.
    if (m_all_done) {
        clearFinishedRows();
        m_all_done = false;
        m_backgrounded = false;
        m_queue_total = 0;
        m_pending_map_loads = 0;
        m_map_total = 0;
        m_has_map_phase = false;
        m_op_is_processing = false;
        m_stages_built = false;
        m_queue_total_armed = false;
        buildStageChips(0);
        m_close_btn->setEnabled(false);
        m_bg_btn->setEnabled(true);
    }
    ++m_pending_map_loads;
    ++m_map_total;
    m_map_task_names[task_id] = layer_name;
    // loadingProgress belongs to the active/first requested map build. Do not
    // relabel that percentage with a later concurrently queued line.
    if (!layer_name.isEmpty() && m_active_map_name.isEmpty())
        m_active_map_name = layer_name;
    m_map_percent = 0;
    m_has_map_phase = true;
    if (m_rows.empty() && m_scroll) m_scroll->hide();

    // Map-only phase (project open / reload): give the panel a "Building map" stage so
    // it reads sensibly even with no per-file rows.
    if (m_rows.empty() && m_stage_lbls.empty()) {
        m_op_is_processing = false;
        buildStageChips(1);
        m_start_ms = QDateTime::currentMSecsSinceEpoch();
        m_timer->start();
    }
    updateHeader();

    // Surface the panel after a short delay. Safe now that the 3D view is a native
    // QOpenGLWindow: in 2D the main window is no longer GL-composited, so showing the
    // embedded panel over the (QPainter) map no longer flickers the whole window. The
    // delay means instant, fully-cached opens that finish first never flash a panel.
    if (!m_backgrounded && !isVisible()) {
        QTimer::singleShot(kMapPhaseShowDelayMs, this, [this]() {
            if (!m_backgrounded && !isVisible()
                && !m_all_done && m_pending_map_loads > 0)
                showForActiveBatch();
        });
    }
}

void ExecutionProgressDialog::onMapLoadProgress(int percent)
{
    if (!m_has_map_phase || m_pending_map_loads <= 0) return;
    m_map_percent = std::clamp(percent, 0, 100);
    if (m_map_total > 0) {
        const int completed = std::max(0, m_map_total - m_pending_map_loads);
        m_overall_bar->setValue(std::clamp(
            (completed * 100 + m_map_percent) / m_map_total, 0, 99));
    }
    updateStages();
    if (m_sub_lbl) {
        const QString phase = m_map_percent < 55 ? tr("Reading pings")
            : m_map_percent < 70 ? tr("Applying corrections")
            : m_map_percent < 85 ? tr("Georeferencing")
            : m_map_percent < 100 ? tr("Building mosaic") : tr("Finishing");
        const QString item = m_active_map_name.isEmpty()
            ? QString() : tr("  ·  %1").arg(m_active_map_name);
        const int completed = std::max(0, m_map_total - m_pending_map_loads);
        m_sub_lbl->setText(tr("%1  %2%  ·  %3 of %4%5")
            .arg(phase).arg(m_map_percent).arg(completed).arg(m_map_total).arg(item));
    }
}

void ExecutionProgressDialog::onMapLoadDone(uint64_t task_id)
{
    // Only a task registered in the current batch may advance its counters.
    if (task_id == 0 || m_pending_map_task_ids.erase(task_id) == 0) return;
    m_map_task_names.erase(task_id);
    if (m_pending_map_loads <= 0) return;
    --m_pending_map_loads;
    if (!m_map_task_names.empty())
        m_active_map_name = m_map_task_names.begin()->second;
    else
        m_active_map_name.clear();
    checkAllDone();
}

void ExecutionProgressDialog::resetState()
{
    m_timer->stop();

    // Any in-flight map builds were just cancelled by the caller; their done events
    // will still arrive — remember how many to absorb.
    m_pending_map_task_ids.clear();
    m_map_task_names.clear();
    m_active_map_name.clear();
    m_map_percent = 0;

    // Remove every card (active + finished) and reset all batch/map-phase state.
    for (auto& r : m_rows)
        if (r.card) { m_list_lay->removeWidget(r.card); r.card->deleteLater(); }
    m_rows.clear();

    m_queue_total       = 0;
    m_pending_map_loads = 0;
    m_map_total         = 0;
    m_has_map_phase     = false;
    m_all_done          = false;
    m_backgrounded      = false;
    m_op_is_processing  = false;
    m_stages_built      = false;
    m_queue_total_armed = false;
    buildStageChips(0);          // clear the stage chips
    m_overall_bar->setValue(0);

    hide();
}

} // namespace dolphin::ui
