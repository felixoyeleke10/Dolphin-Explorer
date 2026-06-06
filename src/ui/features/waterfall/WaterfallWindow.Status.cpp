// WaterfallWindow.Status.cpp — bottom status bar and progress bar helpers.
#include "ui/features/waterfall/WaterfallWindow.h"
#include "ui/shell/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QStyleFactory>
#include <QTimer>
#include <QVBoxLayout>

static constexpr int kProgressDelayMs = 180;  // show bar only when op takes longer than this

namespace dolphin::ui {

using namespace Theme;

void WaterfallWindow::buildBottomBar()
{
    auto* bar = new QFrame(this);
    bar->setObjectName("av_bottombar");
    bar->setFixedHeight(kAvBottomBarH);

    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(Theme::kSpacing4, 0, Theme::kSpacing4, 0);
    bl->setSpacing(Theme::kSpacing3);

    m_status_left   = new QLabel(bar);
    m_status_centre = new QLabel(bar);
    m_status_right  = new QLabel(bar);
    m_status_left  ->setObjectName("wfStatus");
    m_status_centre->setObjectName("wfStatusLine");
    m_status_right ->setObjectName("wfStatusCoord");

    m_progress_bar = new QProgressBar(bar);
    m_progress_bar->setObjectName("wfProgressBar");
    m_progress_bar->setFixedHeight(kProgressBarH);
    m_progress_bar->setFixedWidth(kAvProgressW);
    m_progress_bar->setRange(0, 100);
    m_progress_bar->setValue(0);
    m_progress_bar->setTextVisible(false);
    m_progress_bar->setVisible(false);
    // Force Fusion style so QSS applies to the indeterminate animation;
    // the Windows native style ignores stylesheets for setRange(0,0).
    m_progress_bar->setStyle(QStyleFactory::create("Fusion"));

    bl->addWidget(m_status_left);
    bl->addWidget(m_progress_bar);
    bl->addStretch();
    bl->addWidget(m_status_centre);
    bl->addStretch();
    bl->addWidget(m_status_right);

    qobject_cast<QVBoxLayout*>(layout())->addWidget(bar);

    m_progress_delay = new QTimer(this);
    m_progress_delay->setSingleShot(true);
    m_progress_delay->setInterval(kProgressDelayMs);
    connect(m_progress_delay, &QTimer::timeout, this, [this] {
        if (m_progress_bar) {
            m_progress_bar->setRange(0, 0);
            m_progress_bar->setVisible(true);
        }
    });
}

void WaterfallWindow::startProgress()
{
    if (!m_progress_delay) return;
    // Arm the delay: only show the spinner after kProgressDelayMs.
    // If finishProgress() fires first the timer is cancelled, avoiding the flash.
    m_progress_delay->start();
}

void WaterfallWindow::finishProgress()
{
    if (m_progress_delay) m_progress_delay->stop();
    if (!m_progress_bar) return;
    m_progress_bar->setRange(0, 100);
    m_progress_bar->setVisible(false);
}

void WaterfallWindow::flashProgress()
{
    if (!m_progress_bar) return;
    // Cancel any pending startProgress delay so the bar doesn't reappear as
    // indeterminate after the 100% flash — the two can be armed in the same
    // call path when applyExternalParams triggers invalidateProcessedCache().
    if (m_progress_delay) m_progress_delay->stop();
    m_progress_bar->setRange(0, 100);
    m_progress_bar->setValue(100);
    m_progress_bar->setVisible(true);
    QTimer::singleShot(450, this, [this] {
        if (m_progress_bar) m_progress_bar->setVisible(false);
    });
}

} // namespace dolphin::ui
