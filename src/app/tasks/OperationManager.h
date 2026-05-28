#pragma once
#include "app/tasks/CancellationToken.h"
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QtConcurrent>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace dolphin::app {

// Central owner for background operations.
//
// Wraps QtConcurrent::run with automatic lifecycle signals, cancellation
// tracking, and stale-result guards.  All methods must be called from the
// main thread.
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
    // fn:      runs on a background thread; return value forwarded to on_done.
    // on_done: called on the main thread with fn's result; skipped if cancelled.
    // Returns a Handle with the op_id and a copy of the cancellation token.
    template<typename T>
    Handle run(const QString& name,
               std::function<T()> fn,
               std::function<void(T)> on_done)
    {
        const uint32_t id = m_next_id++;
        CancellationToken tok;
        m_entries[id] = {id, name, tok};

        auto* w = new QFutureWatcher<T>(this);
        connect(w, &QFutureWatcher<T>::finished, this,
                [this, w, id, tok, on_done]() mutable {
                    w->deleteLater();
                    m_entries.erase(id);
                    if (tok.isCancelled()) {
                        emit operationCancelled(id);
                        return;
                    }
                    try {
                        on_done(w->result());
                        emit operationCompleted(id);
                    } catch (const std::exception& ex) {
                        emit operationFailed(id, QString::fromStdString(ex.what()));
                    } catch (...) {
                        emit operationFailed(id, QStringLiteral("Background task failed"));
                    }
                });
        w->setFuture(QtConcurrent::run(std::move(fn)));

        emit operationStarted(id, name);
        return {id, tok};
    }

    void cancel(uint32_t op_id);
    void cancelAll();
    int  activeCount() const { return static_cast<int>(m_entries.size()); }

    // Register an externally-owned cancellation token (e.g. from a viewer window)
    // under a name so it is cancelled by cancelAll().  Replaces any prior entry
    // with the same name.  Call unregisterExternal when the operation completes.
    void registerExternal  (const std::string& name, CancellationToken token);
    void unregisterExternal(const std::string& name);

signals:
    void operationStarted  (uint32_t op_id, const QString& name);
    void operationCompleted(uint32_t op_id);
    void operationFailed   (uint32_t op_id, const QString& error);
    void operationCancelled(uint32_t op_id);

private:
    struct Entry {
        uint32_t          op_id;
        QString           name;
        CancellationToken token;
    };

    std::unordered_map<uint32_t, Entry>     m_entries;
    std::unordered_map<std::string, CancellationToken> m_external;
    uint32_t m_next_id = 1;
};

} // namespace dolphin::app
