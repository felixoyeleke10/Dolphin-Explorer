#pragma once
#include <QObject>
#include <string>
#include <vector>

namespace dolphin::app { class Project; }
namespace dolphin::ui  { class ProjectSessionController; }

namespace dolphin::ui {

// Owns layer-selection state and the back/forward navigation stack.
//
// MainWindow creates this alongside ProjectSessionController and wires:
//   layerActivationRequested → MainWindow::onLayerSelected
//   navigationChanged        → enable/disable nav buttons
//
// onLayerSelected() itself stays in MainWindow (it drives 12+ widgets),
// but it calls into this coordinator to record history and set active state.
class LayerDisplayCoordinator : public QObject {
    Q_OBJECT
public:
    explicit LayerDisplayCoordinator(ProjectSessionController* psc,
                                     QObject* parent = nullptr);

    // -- Active layer --------------------------------------------------------
    const std::string& activeLayerId() const { return m_active_layer_id; }
    void setActiveLayer(const std::string& id);
    void clearActiveLayer();

    // -- Navigation replay guard ---------------------------------------------
    // True while navigateBack/navigateForward is replaying a history entry.
    // MainWindow::onLayerSelected checks this to skip re-recording.
    bool isReplaying() const { return m_replaying; }

    // -- History manipulation ------------------------------------------------
    // Record a user-initiated selection (no-op when isReplaying()).
    void recordSelection(const std::string& layer_id);
    // Remove entries for layers that no longer exist in the project.
    void pruneHistory(const app::Project* project);
    // Drop all history (called on project close / clear).
    void clearHistory();

public slots:
    void navigateBack();
    void navigateForward();

signals:
    // Fired by navigateBack/Forward — MainWindow calls onLayerSelected(id).
    void layerActivationRequested(const std::string& layer_id);
    // Fired whenever the history changes; MainWindow uses it to enable/disable
    // the ← → toolbar buttons.
    void navigationChanged(bool back_enabled, bool forward_enabled);

private:
    void emitNavigationChanged();
    app::Project* project() const;

    ProjectSessionController* m_psc;

    std::string              m_active_layer_id;
    std::vector<std::string> m_navigation_history;
    int                      m_navigation_index = -1;
    bool                     m_replaying        = false;

    static constexpr int kNavHistoryLimit = 100;
};

} // namespace dolphin::ui
