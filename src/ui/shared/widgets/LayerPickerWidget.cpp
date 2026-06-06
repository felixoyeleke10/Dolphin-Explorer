#include "ui/shared/widgets/LayerPickerWidget.h"
#include "ui/shared/panels/LineListPanel.h"
#include "ui/shell/Theme.h"

#include <QBitmap>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QSvgRenderer>
#include <QVBoxLayout>

namespace dolphin::ui {

namespace {

QColor panelShadow()
{
    QColor c(Qt::black);
    c.setAlpha(12);
    return c;
}

} // namespace

static constexpr int kChevronSz = 16;  // chevron expand/collapse indicator size

// -- Small painted chevron widget ----------------------------------------------
class ChevronWidget : public QWidget {
public:
    bool expanded = false;
    explicit ChevronWidget(QWidget* p) : QWidget(p) { setFixedSize(kChevronSz, kChevronSz); }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(Theme::kTextMuted), 1.5f, Qt::SolidLine,
                      Qt::RoundCap, Qt::RoundJoin));
        if (expanded) {
            // Up chevron
            p.drawLine(QPointF(3, 10), QPointF(8,  5));
            p.drawLine(QPointF(8,  5), QPointF(13, 10));
        } else {
            // Down chevron
            p.drawLine(QPointF(3,  5), QPointF(8, 10));
            p.drawLine(QPointF(8, 10), QPointF(13, 5));
        }
    }
};

// -----------------------------------------------------------------------------

LayerPickerWidget::LayerPickerWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("layerPicker");

    // Transparent background so our paintEvent controls every pixel.
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);

    // Load the layers SVG icon
    m_icon_svg = new QSvgRenderer(QString(":/icons/layers.svg"), this);

    // -- Layout ----------------------------------------------------------------
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);   // 1px inset for border
    root->setSpacing(0);

    // Header
    m_header = new QWidget(this);
    m_header->setObjectName("layerPickerHeader");
    m_header->setFixedHeight(kCollapsedH - 2);
    m_header->setCursor(Qt::PointingHandCursor);
    m_header->setAttribute(Qt::WA_TranslucentBackground);

    auto* hl = new QHBoxLayout(m_header);
    hl->setContentsMargins(10, 0, Theme::kSpacing3, 0);
    hl->setSpacing(7);

    auto* title = new QLabel("LAYERS", m_header);
    title->setObjectName("layerPickerTitle");

    auto* chevron = new ChevronWidget(m_header);
    m_chevron = chevron;

    hl->addSpacing(18);   // room for the icon we paint manually
    hl->addWidget(title);
    hl->addStretch();
    hl->addWidget(chevron);

    m_header->installEventFilter(this);
    root->addWidget(m_header);

    // List
    m_list = new LineListPanel(this, LineListPanel::ContentMode::LayersOnly);
    m_list->setObjectName("layerPickerBody");
    m_list->setVisible(false);
    root->addWidget(m_list, 1);

    connect(m_list, &LineListPanel::layerSelected,
            this,   &LayerPickerWidget::layerSelected);
    connect(m_list, &LineListPanel::sourceSelected,
            this,   &LayerPickerWidget::sourceSelected);
    connect(m_list, &LineListPanel::contactSelected,
            this,   &LayerPickerWidget::contactSelected);

    // Start collapsed
    resize(kWidth, kCollapsedH);
}

void LayerPickerWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // Clip to our own rect so the shadow loop below never pushes dirty regions
    // outside the window bounds (which causes UpdateLayeredWindowIndirect to fail
    // on Windows when WA_TranslucentBackground is set).
    p.setClipRect(rect());

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    // -- Drop shadow -----------------------------------------------------------
    for (int i = 6; i >= 1; --i) {
        QPainterPath shadow;
        shadow.addRoundedRect(r.adjusted(i, i, i, i), kRadius, kRadius);
        p.fillPath(shadow, panelShadow());
    }

    // -- Panel background ------------------------------------------------------
    QPainterPath panel;
    panel.addRoundedRect(r, kRadius, kRadius);
    QColor panel_bg(Theme::kBgElevated);
    panel_bg.setAlpha(230);
    p.fillPath(panel, panel_bg);

    // -- Border ----------------------------------------------------------------
    QColor border(Theme::kBorderMenu);
    border.setAlpha(180);
    p.setPen(QPen(border, 1.0));
    p.drawPath(panel);

    // -- Header divider (only when expanded) -----------------------------------
    if (m_expanded) {
        p.setPen(QPen(QColor(Theme::kBorder), 1));
        double y = kCollapsedH - 0.5;
        p.drawLine(QPointF(kRadius, y), QPointF(width() - kRadius, y));
    }

    // -- Layers icon (top-left of header) -------------------------------------
    if (m_icon_svg && m_icon_svg->isValid()) {
        QRectF ico_rect(11, (kCollapsedH - 14) / 2.0, 14, 14);
        m_icon_svg->render(&p, ico_rect);
    }
}

bool LayerPickerWidget::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_header && ev->type() == QEvent::MouseButtonPress)
        toggle();
    return QWidget::eventFilter(obj, ev);
}

void LayerPickerWidget::toggle()
{
    m_expanded = !m_expanded;

    // Flip chevron
    if (auto* ch = static_cast<ChevronWidget*>(m_chevron))
        ch->expanded = m_expanded, ch->update();

    // This is a floating widget (not in a parent layout), so maximumHeight has
    // no effect on actual geometry.  Animate the size property directly so the
    // widget actually grows and shrinks on screen.
    if (!m_size_anim) {
        m_size_anim = new QPropertyAnimation(this, "size", this);
        m_size_anim->setDuration(160);
        m_size_anim->setEasingCurve(QEasingCurve::OutCubic);
    }

    m_size_anim->stop();

    if (m_expanded) {
        m_list->setVisible(true);
        m_size_anim->setStartValue(QSize(kWidth, height()));
        m_size_anim->setEndValue(QSize(kWidth, kExpandedH));
    } else {
        m_size_anim->setStartValue(QSize(kWidth, height()));
        m_size_anim->setEndValue(QSize(kWidth, kCollapsedH));
        connect(m_size_anim, &QPropertyAnimation::finished, this, [this]() {
            if (!m_expanded) { m_list->setVisible(false); resize(kWidth, kCollapsedH); }
        }, Qt::SingleShotConnection);
    }
    m_size_anim->start();
}

void LayerPickerWidget::expand()
{
    if (!m_expanded)
        toggle();
}

void LayerPickerWidget::setProject(app::Project* project)
{
    m_list->setProject(project);
}

void LayerPickerWidget::refresh()
{
    m_list->refresh();
}

void LayerPickerWidget::setLayerVisibility(const std::string& id, bool visible)
{
    m_list->setLayerVisibility(id, visible);
}

void LayerPickerWidget::updateLayerLabel(const std::string& id, const std::string& label)
{
    m_list->updateLayerLabel(id, label);
}

void LayerPickerWidget::refreshContacts()
{
    m_list->refreshContacts();
}

void LayerPickerWidget::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    // Apply rounded-rect mask so children are clipped to the panel shape.
    QBitmap mask(size());
    mask.clear();
    QPainter mp(&mask);
    mp.setRenderHint(QPainter::Antialiasing);
    mp.setBrush(Qt::color1);
    mp.setPen(Qt::NoPen);
    mp.drawRoundedRect(rect(), kRadius, kRadius);
    setMask(mask);
}

void LayerPickerWidget::reposition(const QSize& /*parent_size*/, int left_x)
{
    int target_x = left_x + kMargin;
    int target_y = kMargin;

    if (!m_pos_anim) {
        m_pos_anim = new QPropertyAnimation(this, "pos", this);
        m_pos_anim->setDuration(160);
        m_pos_anim->setEasingCurve(QEasingCurve::OutCubic);
    }
    m_pos_anim->stop();
    m_pos_anim->setStartValue(pos());
    m_pos_anim->setEndValue(QPoint(target_x, target_y));
    m_pos_anim->start();
}

} // namespace dolphin::ui
