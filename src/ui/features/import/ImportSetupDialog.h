#pragma once
#include "core/Artifact.h"
#include <QDialog>
#include <vector>

class QCheckBox;

namespace dolphin::ui {

// Step-1 dialog of the import flow: the user selects which sensor types
// they are importing before the file picker opens.
// Text-based toggle rows — no icons.
class ImportSetupDialog : public QDialog {
public:
    explicit ImportSetupDialog(QWidget* parent = nullptr);

    // Empty vector = all types selected (no restriction passed to ImportDialog).
    std::vector<core::ArtifactType> moduleFilter() const;

private:
    static constexpr int kModuleCount = 4;
    QCheckBox* m_checkboxes[kModuleCount] = {};
};

} // namespace dolphin::ui
