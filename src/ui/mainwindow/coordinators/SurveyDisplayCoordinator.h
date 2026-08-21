#pragma once

#include <QObject>
#include <string>

namespace dolphin::app { class Project; }

namespace dolphin::ui {

class MapView;
class SidescanViewController;

// Owns the survey-level display policy for newly imported side-scan lines.
// Every line is materialized in the shared 2D map model (which automatically
// feeds 3D), while worker completion order is never allowed to steal selection.
class SurveyDisplayCoordinator : public QObject {
    Q_OBJECT
public:
    enum class Presentation { Select, Background };

    SurveyDisplayCoordinator(SidescanViewController* sidescan,
                             MapView* map,
                             QObject* parent = nullptr);

    static Presentation presentationFor(const std::string& active_layer_id,
                                        const std::string& imported_layer_id);

    bool materializeImportedSidescan(app::Project* project,
                                     const std::string& layer_id,
                                     const std::string& active_layer_id);
    void completeImportBatch(int materialized_line_count);

signals:
    void selectionRequested(const std::string& layer_id);

private:
    SidescanViewController* m_sidescan = nullptr;
    MapView*                m_map      = nullptr;
};

} // namespace dolphin::ui
