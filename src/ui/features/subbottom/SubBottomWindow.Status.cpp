// SubBottomWindow.Status.cpp — status bar and cursor update handlers.

#include "ui/features/subbottom/SubBottomWindow.h"
#include "ui/features/subbottom/SubBottomView.h"
#include "app/layers/DataLayer.h"
#include "ui/shared/CoordFormat.h"
#include "ui/shell/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyleFactory>
#include <QVBoxLayout>

#include <algorithm>

namespace dolphin::ui {

using namespace Theme;

void SubBottomWindow::buildStatusBar()
{
    auto* bar = new QFrame(this);
    bar->setObjectName("av_bottombar");
    bar->setFixedHeight(kAvBottomBarH);

    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(10, 0, 10, 0);
    bl->setSpacing(Theme::kSpacing3);

    m_progress_bar = new QProgressBar(bar);
    m_progress_bar->setRange(0, 100);
    m_progress_bar->setFixedWidth(kAvProgressW);
    m_progress_bar->setFixedHeight(kProgressBarH);
    m_progress_bar->setTextVisible(false);
    m_progress_bar->setVisible(false);
    m_progress_bar->setStyle(QStyleFactory::create("Fusion"));
    bl->addWidget(m_progress_bar);

    m_status_left = new QLabel(bar);
    m_status_left->setObjectName("statusChrome");
    bl->addWidget(m_status_left, 1);

    m_status_right = new QLabel(bar);
    m_status_right->setObjectName("statusData");
    bl->addWidget(m_status_right);

    qobject_cast<QVBoxLayout*>(layout())->addWidget(bar);
}

void SubBottomWindow::startProgress()
{
    if (!m_progress_bar) return;
    m_progress_bar->setValue(0);
    m_progress_bar->setRange(0, 0);  // indeterminate
    m_progress_bar->setVisible(true);
}

void SubBottomWindow::finishProgress()
{
    if (!m_progress_bar) return;
    m_progress_bar->setRange(0, 100);
    m_progress_bar->setValue(100);
    m_progress_bar->setVisible(false);
}

void SubBottomWindow::onScrollbarMoved(int first_trace)
{
    m_view->scrollToTrace(first_trace);
}

void SubBottomWindow::onViewScrollChanged(int first_trace, int total, int visible)
{
    QSignalBlocker sb(m_hscroll);
    const int max_val = std::max(0, total - visible);
    m_hscroll->setRange(0, max_val);
    m_hscroll->setValue(first_trace);
    m_hscroll->setPageStep(std::max(1, visible));

    if (m_status_left && m_layer) {
        m_status_left->setText(tr("Traces %1–%2 of %3  |  %4")
            .arg(first_trace + 1)
            .arg(std::min(first_trace + visible, total))
            .arg(total)
            .arg(QString::fromStdString(m_layer->label)));
    }
}

void SubBottomWindow::onCursorMoved(int /*trace_idx*/, float depth_s,
                                    double lat, double lon, bool nav_projected)
{
    if (!m_status_right) return;

    QString pos;
    if (lat != 0.0 || lon != 0.0)
        pos = QStringLiteral("  ") + formatPosition(lat, lon, nav_projected);

    float depth_m = -1.f;
    QString depth;
    if (depth_s >= 0.f) {
        depth_m = depth_s * m_sound_half_speed;
        depth = QString("~%1 m  (%2 ms)")
                    .arg(static_cast<double>(depth_m), 0, 'f', 0)
                    .arg(static_cast<double>(depth_s * 1000.f), 0, 'f', 1);
    }

    m_status_right->setText(depth + pos);
    emit cursorUpdated(depth_m, lat, lon, nav_projected);
}

void SubBottomWindow::onCursorLeft()
{
    if (m_status_right) m_status_right->clear();
    emit cursorUpdated(-1.f, 0.0, 0.0, false);
}

} // namespace dolphin::ui
