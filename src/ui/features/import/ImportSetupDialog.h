#pragma once
#include "core/Artifact.h"
#include <QDialog>
#include <vector>

class QRadioButton;

namespace dolphin::ui {

// Step-1 dialog of the import flow: the user selects which sensor type
// they are importing before the file picker opens.
// Sensor types are mutually exclusive — one per import session.
class ImportSetupDialog : public QDialog {
public:
    explicit ImportSetupDialog(QWidget* parent = nullptr);

    // Returns a single-element filter for the selected sensor type.
    std::vector<core::ArtifactType> moduleFilter() const;

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    static constexpr int kModuleCount = 4;
    QRadioButton* m_radios[kModuleCount] = {};
};

} // namespace dolphin::ui
