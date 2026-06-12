#pragma once
#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  EmptyStateWidget — reusable "nothing here yet" placeholder.
//
//  Standard layout (all parts optional):
//    [icon]    — large centered text/emoji label
//    [title]   — semi-bold primary-color heading
//    [sub]     — muted word-wrapping subtitle
//    [actions] — one or more QPushButton rows (objectName: emptyStateActionBtn)
//
//  Usage:
//    auto* es = new EmptyStateWidget(parent);
//    es->setTitle(tr("No project open"));
//    es->setSubtitle(tr("Create a new project or import files to get started."));
//    auto* btn = es->addAction(tr("New Project…"));
//    connect(btn, &QPushButton::clicked, this, &MyWidget::onNewProject);
// -----------------------------------------------------------------------------

class EmptyStateWidget : public QWidget {
    Q_OBJECT
public:
    explicit EmptyStateWidget(QWidget* parent = nullptr);

    void setIcon    (const QString& glyph);   // emoji / single-char icon
    void setTitle   (const QString& title);
    void setSubtitle(const QString& subtitle);

    // Appends an action button.  Returns the button so caller can connect().
    QPushButton* addAction(const QString& label);

private:
    QLabel*      m_icon_lbl  = nullptr;
    QLabel*      m_title_lbl = nullptr;
    QLabel*      m_sub_lbl   = nullptr;
    QVBoxLayout* m_btn_area  = nullptr;
};

} // namespace dolphin::ui
