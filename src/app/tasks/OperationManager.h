#pragma once
#include "app/tasks/CancellationToken.h"
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QtConcurrent>
#include <deque>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace dolphin::app {

// Central owner for background operations.
//
// Wraps QtConcurrent::run with automatic lifecycle signals, cancellation
// tracking, stale-result guards, keyed supersession, and a heavy-job
// concurrency cap.  All methods must be called from the main thread.
//
// UI layer usage:
//   Connect operationStarted/Completed/Failed/Cancelled to DiagnosticsHub
//   beginJob/endJob/failJob/cancelJob to get structured job tracking in the
//   bottom panel without each call-site knowing about DiagnosticsHub.
class OperationManager : public QObject {
    Q_OBJECT
public:
    explicit OperationManager(QObject* parent = nullptr);
    ~OperationManager() override;

    struct Handle {
        uint32_t          op_id = 0;
        CancellationToken token;
    };

    // Schedule work on the global thread pool.
    //   name:    human-readable label (forwarded via operationStarted, e.g. to
    //            DiagnosticsHub).
    //   fn:      runs on a background thread; receives this op's CancellationToken
    //            so long-running work can abort mid-flight; its result is forwarded
    //            to on_done.
    //   on_done: runs on the main thread with fn's result; skipped if cancelled.
    //   key:     optional supersession key. Launching a new op with the same key
    //            cancels the previous one — this replaces the per-call-site
    //            generation-counter + cancel-flag pattern (e.g. one key per
    //            viewer-layer load).
    //   heavy:   when true the op is subject to the heavy-job concurrency cap
    //            (D-14). Excess heavy ops queue — visibly, since operationStarted
    //            still fires immediately — instead of launching unbounded.
    //   on_finally: optional; runs on the main thread on EVERY outcome (success,
    //            failure, supersession, or cancel-while-queued). Use it to release
    //            caller-side state (e.g. a viewer-busy counter) that must balance
    //            regardless of how the op ended — on_done only runs on success.
    // Returns a Handle with the op_id and a copy of the cancellation token.
    template<typename T>
    Handle run(const QString&                      name,
               std::function<T(CancellationToken)> fn,
               std::function<void(T)>              on_done,
               const std::string&                  key        = {},
               bool                                heavy      = false,
               std::function<void()>               on_finally = {},
               const std::string&                  lane       = {})
    {
        // Supersede a prior op sharing this key (if still queued it is dropped
        // when the lane is next pumped).
        if (!key.empty()) {
            const auto it = m_keyed.find(key);
            if (it != m_keyed.end()) cancel(it->second);
        }

        const uint32_t id = m_next_id++;
        CancellationToken tok;
        // Resolve the concurrency lane: an explicit lane wins; otherwise the
        // "heavy" lane (the D-14 import/decode cap) when heavy==true; otherwise no
        // lane = run immediately, bounded only by the global thread pool.
        const std::string ln = !lane.empty() ? lane
                             : (heavy ? std::string("heavy") : std::string());
        m_entries[id] = {id, name, tok, ln, key};
        if (!key.empty()) m_keyed[key] = id;

        // Type-erased launcher so lane-capped ops can wait in the queue until a
        // slot frees without the queue needing to know T.
        std::function<void()> start =
            [this, id, tok, key, ln,
             fn = std::move(fn), on_done = std::move(on_done), on_finally]() mutable {
                auto* w = new QFutureWatcher<T>(this);
                connect(w, &QFutureWatcher<T>::finished, this,
                    [this, w, id, tok, key, ln,
                     on_done = std::move(on_done), on_finally]() mutable {
                        w->deleteLater();
                        finishOp(id, key, ln);
                        if (tok.isCancelled()) {
                            emit operationCancelled(id);
                        } else {
                            try {
                                on_done(w->result());
                                emit operationCompleted(id);
                            } catch (const std::exception& ex) {
                                emit operationFailed(id, QString::fromStdString(ex.what()));
                            } catch (...) {
                                emit operationFailed(id, QStringLiteral("Background task failed"));
                            }
                        }
                        // on_finally fires on EVERY outcome, but AFTER on_done so a
                        // caller's success handler (e.g. installing map data) runs
                        // before finalizers (e.g. emitting loadingFinished).
                        if (on_finally) on_finally();
                    });
                w->setFuture(QtConcurrent::run(
                    [fn = std::move(fn), tok]() { return fn(tok); }));
            };

        // Honest queued/running: only emit operationStarted when the op actually
        // launches. A lane-capped op that has to wait emits operationQueued instead,
        // so the UI shows it as queued rather than (misleadingly) running.
        if (!ln.empty()) {
            Lane& L = laneFor(ln);
            if (L.running >= L.cap) {
                emit operationQueued(id, name);
                L.queue.push_back({id, key, tok, ln, name, std::move(start), on_finally});
                return {id, tok};
            }
            ++L.running;
        }
        emit operationStarted(id, name);
        start();
        return {id, tok};
    }

    void cancel(uint32_t op_id);
    // Cancel the op currently registered under a supersession key (no-op if none).
    void cancelByKey(const std::string& key);
    // Cancel every op whose key starts with `prefix` (e.g. "sss:load:" cancels all
    // per-layer sidescan loads on viewer teardown).
    void cancelByPrefix(const std::string& prefix);
    void cancelAll();
    int  activeCount() const { return static_cast<int>(m_entries.size()); }
    int  queuedCount() const {
        int n = 0;
        for (const auto& [name, L] : m_lanes) n += static_cast<int>(L.queue.size());
        return n;
    }

    // Set the concurrency cap for a named lane and pump it. "heavy" is the D-14
    // import/decode lane; "map" is the sidescan map-build lane.
    void setLaneCap(const std::string& lane, int cap) {
        laneFor(lane).cap = (cap < 1) ? 1 : cap;
        pumpLane(lane);
    }
    // Back-compat: the D-14 heavy lane.
    void setHeavyCap(int cap) { setLaneCap("heavy", cap); }

    // Register an externally-owned cancellation token (e.g. from a viewer window)
    // under a name so it is cancelled by cancelAll().  Replaces any prior entry
    // with the same name.  Call unregisterExternal when the operation completes.
    void registerExternal  (const std::string& name, CancellationToken token);
    void unregisterExternal(const std::string& name);

signals:
    // Fired when an op actually begins running (not when merely submitted).
    void operationStarted  (uint32_t op_id, const QString& name);
    // Fired when an op is accepted but parked behind its lane's concurrency cap.
    // A later operationStarted with the same op_id means it has begun running.
    void operationQueued   (uint32_t op_id, const QString& name);
    void operationCompleted(uint32_t op_id);
    void operationFailed   (uint32_t op_id, const QString& error);
    void operationCancelled(uint32_t op_id);

private:
    static constexpr int kDefaultLaneCap = 2;  // D-14 default for any lane

    struct Entry {
        uint32_t          op_id;
        QString           name;
        CancellationToken token;
        std::string       lane;     // concurrency lane ("" = uncapped, immediate)
        std::string       key;
    };
    struct Pending {
        uint32_t              op_id;
        std::string           key;
        CancellationToken     token;
        std::string           lane;
        QString               name;
        std::function<void()> start;
        std::function<void()> on_finally;
    };

    // A concurrency lane: at most `cap` ops run at once; the rest wait in `queue`.
    // Lanes are created on first use (default cap = kDefaultLaneCap).
    struct Lane {
        int                 cap     = kDefaultLaneCap;
        int                 running = 0;
        std::deque<Pending> queue;
    };
    Lane& laneFor(const std::string& name) {
        auto it = m_lanes.find(name);
        if (it == m_lanes.end()) it = m_lanes.emplace(name, Lane{}).first;
        return it->second;
    }

    // Erase op bookkeeping; for lane-capped ops free a slot and pump the lane.
    void finishOp(uint32_t op_id, const std::string& key, const std::string& lane);
    // Launch queued ops in one lane while a slot is free; drop cancelled en route.
    void pumpLane(const std::string& lane);

    std::unordered_map<uint32_t, Entry>                m_entries;
    std::unordered_map<std::string, uint32_t>          m_keyed;    // key -> current op_id
    std::unordered_map<std::string, Lane>              m_lanes;    // lane name -> cap/running/queue
    std::unordered_map<std::string, CancellationToken> m_external;
    uint32_t m_next_id       = 1;
};

} // namespace dolphin::app
