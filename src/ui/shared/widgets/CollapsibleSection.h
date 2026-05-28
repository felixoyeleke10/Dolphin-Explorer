#pragma once
#include <QWidget>

class QLabel;
class QPropertyAnimation;
class QVBoxLayout;

namespace dolphin::ui {

// Collapsible panel section with a clickable header bar.
// The header (chevron + icon + uppercase title) toggles the content widget
// between visible (expanded) and hidden (collapsed) with a smooth height animation.
class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    explicit CollapsibleSection(const QString& title, QWidget* parent = nullptr);

    void setContent(QWidget* content);
    void setExpanded(bool expanded);
    bool isExpanded() const { return m_expanded; }

    // Optional SVG icon shown in the header beside the chevron.
    void setIcon(const QString& icon_path);

    // Optional modality badge shown at the right of the header (e.g. "SBP", "SSS").
    void setBadge(const QString& text);

    // Show the section always but lock it when it doesn't apply to the active layer.
    // When not applicable the header is dimmed, the chevron replaced with a lock,
    // clicks are ignored, and a one-line hint replaces the content area.
    // hint: e.g. "Select a Sidescan layer to use this section."
    void setApplicable(bool applicable, const QString& hint = {});

signals:
    void expandedChanged(bool expanded);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    void toggle();
    void animateToExpanded(bool expanding);

    QWidget*            m_header      = nullptr;
    QLabel*             m_arrow       = nullptr;
    QLabel*             m_icon        = nullptr;
    QLabel*             m_badge       = nullptr;
    QLabel*             m_hint        = nullptr;  // shown in place of content when not applicable
    QWidget*            m_content     = nullptr;
    QVBoxLayout*        m_layout      = nullptr;
    QPropertyAnimation* m_anim        = nullptr;
    bool                m_expanded    = true;
    bool                m_applicable  = true;
};

} // namespace dolphin::ui
