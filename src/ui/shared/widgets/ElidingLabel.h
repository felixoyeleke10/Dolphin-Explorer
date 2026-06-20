#pragma once
// ElidingLabel — a single-line QLabel that elides its text to the available width
// and exposes the full text as a tooltip. Crucially it reports a zero width hint,
// so a long unbreakable value (filename, CRS string) never forces its parent
// layout / scroll area wider than the viewport — the failure mode that pushed
// right-aligned inspector values off-screen.
//
// Store as ElidingLabel* (not QLabel*) at call sites: QLabel::setText is not
// virtual, so an upcast pointer would bypass the eliding override.
#include <QLabel>
#include <QResizeEvent>
#include <algorithm>

namespace dolphin::ui {

class ElidingLabel : public QLabel {
public:
    explicit ElidingLabel(QWidget* parent = nullptr) : QLabel(parent) {}
    explicit ElidingLabel(const QString& text, QWidget* parent = nullptr)
        : QLabel(parent) { setText(text); }

    void setElideMode(Qt::TextElideMode mode) { m_mode = mode; updateElision(); }

    // Hides QLabel::setText (non-virtual) — store the full text, tooltip it, elide.
    void setText(const QString& text) {
        m_full = text;
        setToolTip(text);
        updateElision();
    }
    QString fullText() const { return m_full; }

    // Never demand width from the layout — let it shrink to whatever is offered.
    QSize sizeHint()        const override { auto s = QLabel::sizeHint();        s.setWidth(0); return s; }
    QSize minimumSizeHint() const override { auto s = QLabel::minimumSizeHint(); s.setWidth(0); return s; }

protected:
    void resizeEvent(QResizeEvent* e) override {
        QLabel::resizeEvent(e);
        updateElision();
    }

private:
    void updateElision() {
        QLabel::setText(fontMetrics().elidedText(m_full, m_mode, std::max(0, width())));
    }

    QString           m_full;
    Qt::TextElideMode m_mode = Qt::ElideRight;
};

} // namespace dolphin::ui
