// ImportProgressDialog.cpp — construction, layout, card/chip building, and window
// chrome (elapsed tick, run-in-background, show/close). The job/state model and
// progress logic live in ImportProgressDialog.Jobs.cpp.
#include "ui/features/import/ImportProgressDialog.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QColor>
#include <QDateTime>
#include <QCloseEvent>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace dolphin::ui {

static constexpr int kDialogW  = 520;  // fixed dialog width
static constexpr int kListMinH = 140;  // task list min height (comfortable with few tasks)
static constexpr int kListMaxH = 440;  // task list max height before it scrolls (~9 rows)
// Format-badge size is a design token (Theme::kFormatBadgeSize / @badgeSize in QSS)
// so the C++ setFixedSize and the stylesheet min/max stay in sync from one source.

// -- Constructor ----------------------------------------------------------------

ExecutionProgressDialog::ExecutionProgressDialog(QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::CustomizeWindowHint
                     | Qt::WindowTitleHint | Qt::WindowCloseButtonHint)
{
    setWindowTitle(tr("Background Tasks"));
    setModal(false);
    setFixedWidth(kDialogW);
    // Show without stealing activation: the main window is frameless, and a popup that
    // grabs the foreground makes it drop/redraw (a visible "blink"). The user can still
    // click this dialog's buttons — clicking activates it then.
    setAttribute(Qt::WA_ShowWithoutActivating);


    // -- Root layout -----------------------------------------------------------
    auto* root = makeCompactLayout<QVBoxLayout>(this);

    // -- Header ----------------------------------------------------------------
    auto* header = new QWidget(this);
    header->setObjectName("epdHeader");
    auto* hdr_lay = new QVBoxLayout(header);
    hdr_lay->setContentsMargins(Theme::kSpacing5, 12, Theme::kSpacing5, Theme::kSpacing3);
    hdr_lay->setSpacing(Theme::kSpacing1);

    m_title_lbl = new QLabel(tr("Background Tasks"), header);
    m_title_lbl->setObjectName("titleLabel");
    hdr_lay->addWidget(m_title_lbl);

    // Stage pipeline (e.g. Reading → Building map). Chips are created on the first
    // job once the operation kind is known, and refreshed by updateStages().
    m_stage_box = new QWidget(header);
    m_stage_box->setObjectName("epdStages");
    m_stage_lay = new QHBoxLayout(m_stage_box);
    m_stage_lay->setContentsMargins(0, 2, 0, 2);
    m_stage_lay->setSpacing(Theme::kSpacing2);
    m_stage_lay->addStretch(1);
    hdr_lay->addWidget(m_stage_box);

    hdr_lay->addSpacing(4);

    m_overall_bar = new QProgressBar(header);
    m_overall_bar->setObjectName("overallBar");
    m_overall_bar->setRange(0, 100);
    m_overall_bar->setValue(0);
    m_overall_bar->setTextVisible(false);
    hdr_lay->addWidget(m_overall_bar);

    // Current-activity line ("Reading …" / "Building map — X of Y" / "… complete").
    m_sub_lbl = new QLabel(tr("Starting…"), header);
    m_sub_lbl->setObjectName("subtitleLabel");
    hdr_lay->addWidget(m_sub_lbl);

    root->addWidget(header);

    // -- Scrollable file list --------------------------------------------------
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setMinimumHeight(kListMinH);
    m_scroll->setMaximumHeight(kListMaxH);

    m_list_body = new QWidget();
    m_list_body->setObjectName("epdListBody");
    m_list_lay = new QVBoxLayout(m_list_body);
    m_list_lay->setContentsMargins(Theme::kSpacing3, Theme::kSpacing2, Theme::kSpacing3, Theme::kSpacing2);
    m_list_lay->setSpacing(0);   // rows are flush; separated by each row's bottom border
    m_list_lay->addStretch(1);

    m_scroll->setWidget(m_list_body);
    root->addWidget(m_scroll, 1);

    // -- Footer ----------------------------------------------------------------
    auto* footer = new QWidget(this);
    footer->setObjectName("epdFooter");
    auto* foot_lay = new QHBoxLayout(footer);
    foot_lay->setContentsMargins(Theme::kSpacing4, 8, Theme::kSpacing4, 8);
    foot_lay->setSpacing(Theme::kSpacing3);

    m_elapsed_lbl = new QLabel(tr("Elapsed: 0:00"), footer);
    m_elapsed_lbl->setObjectName("elapsedLabel");
    foot_lay->addWidget(m_elapsed_lbl, 1);

    m_bg_btn = new QPushButton(tr("Run in Background"), footer);
    m_bg_btn->setObjectName("bgBtn");
    connect(m_bg_btn, &QPushButton::clicked, this, &ExecutionProgressDialog::runInBackground);
    foot_lay->addWidget(m_bg_btn);

    m_close_btn = new QPushButton(tr("Close"), footer);
    m_close_btn->setObjectName("closeBtn");
    m_close_btn->setEnabled(false);
    connect(m_close_btn, &QPushButton::clicked, this, &QDialog::accept);
    foot_lay->addWidget(m_close_btn);

    root->addWidget(footer);

    // -- Elapsed timer ---------------------------------------------------------
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &ExecutionProgressDialog::onTick);
}

// -- Card / chip construction ---------------------------------------------------

QFrame* ExecutionProgressDialog::buildCard(FileRow& row, QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName("fileCard");

    // One compact line: [status icon] [name (elided, stretch)] [status/result (right)].
    // No nested vertical layout and no per-row bar, so a row is always a single line.
    auto* h = new QHBoxLayout(card);
    h->setContentsMargins(Theme::kSpacing3, Theme::kSpacing2, Theme::kSpacing3, Theme::kSpacing2);
    h->setSpacing(Theme::kSpacing2);

    // Status icon (replaces the old DLP badge): ● reading / ✓ done / ✕ failed.
    row.badge = new QLabel(QStringLiteral("●"), card);
    row.badge->setObjectName("rowStatusIcon");
    row.badge->setAlignment(Qt::AlignCenter);
    row.badge->setFixedWidth(16);
    row.badge->setStyleSheet(QString("color:%1; font-weight:700;")
        .arg(QColor(Theme::kAccent).name()));
    h->addWidget(row.badge);

    row.name_lbl = new QLabel(row.display_name, card);
    row.name_lbl->setObjectName("fileName");
    h->addWidget(row.name_lbl, 1);

    // Live status while active ("Reading X%"), then the result / error. Right-aligned
    // and kept short (name pre-elided) so the row never overflows — no h-scroll.
    row.result_lbl = new QLabel(tr("Reading…"), card);
    row.result_lbl->setObjectName("fileResult");
    row.result_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    h->addWidget(row.result_lbl);

    row.bar        = nullptr;   // overall bar + per-row "Reading X%" instead
    row.meta_lbl   = nullptr;
    row.status_lbl = nullptr;
    return card;
}

void ExecutionProgressDialog::applyCardState(FileRow& row, FileRow::State s)
{
    row.state = s;
    if (!row.badge) return;
    const QString glyph = (s == FileRow::State::Done)   ? QStringLiteral("✓")
                        : (s == FileRow::State::Failed) ? QStringLiteral("✕")
                                                        : QStringLiteral("●");
    const QColor c = (s == FileRow::State::Done)   ? QColor(Theme::kSuccess)
                   : (s == FileRow::State::Failed) ? QColor(Theme::kDanger)
                                                   : QColor(Theme::kAccent);
    row.badge->setText(glyph);
    row.badge->setStyleSheet(QString("color:%1; font-weight:600;").arg(c.name()));
}

void ExecutionProgressDialog::buildStageChips(int n)
{
    if (!m_stage_lay) return;
    // Wipe any existing chips / arrows / stretch, then build n fresh chips.
    QLayoutItem* it;
    while ((it = m_stage_lay->takeAt(0)) != nullptr) {
        if (auto* w = it->widget()) w->deleteLater();
        delete it;
    }
    m_stage_lbls.clear();
    for (int i = 0; i < n; ++i) {
        if (i > 0) {
            auto* arrow = new QLabel(QStringLiteral("→"), m_stage_box);
            arrow->setStyleSheet(QString("color:%1;")
                .arg(Theme::textMutedColor().name()));
            m_stage_lay->addWidget(arrow);
        }
        auto* chip = new QLabel(m_stage_box);
        chip->setObjectName("stageChip");
        m_stage_lbls.push_back(chip);
        m_stage_lay->addWidget(chip);
    }
    m_stage_lay->addStretch(1);
}

// -- Window chrome --------------------------------------------------------------

void ExecutionProgressDialog::onTick()
{
    const qint64 elapsed = (QDateTime::currentMSecsSinceEpoch() - m_start_ms) / 1000;
    const int mins = static_cast<int>(elapsed / 60);
    const int secs = static_cast<int>(elapsed % 60);
    m_elapsed_lbl->setText(
        QString("Elapsed: %1:%2").arg(mins).arg(secs, 2, 10, QChar('0')));
}

void ExecutionProgressDialog::runInBackground()
{
    m_backgrounded = true;
    hide();
}

void ExecutionProgressDialog::embedIn(QWidget* host)
{
    if (!host) return;
    m_embedded = true;
    m_host     = host;
    setParent(host);
    // Render as an in-window child overlay, not a top-level window. A popup over the
    // frameless main window blinks it when shown during open; a child cannot.
    setWindowFlags(Qt::Widget);
    host->installEventFilter(this);
    hide();
}

void ExecutionProgressDialog::positionInParent()
{
    if (!m_host) return;
    adjustSize();   // size to current content (cards/stage list)
    const int margin = 18;
    const int x = (m_host->width()  - width())  / 2;
    const int y =  m_host->height() - height() - margin;
    move(std::max(margin, x), std::max(margin, y));
}

bool ExecutionProgressDialog::eventFilter(QObject* obj, QEvent* ev)
{
    if (m_embedded && obj == m_host && ev->type() == QEvent::Resize && isVisible())
        positionInParent();
    return QDialog::eventFilter(obj, ev);
}

void ExecutionProgressDialog::showForActiveBatch()
{
    if (m_backgrounded || isVisible()) return;

    if (m_embedded) {
        positionInParent();
        show();
        raise();   // above the map/siblings within the host
    } else {
        if (parentWidget()) {
            const QRect pr = parentWidget()->geometry();
            move(pr.center().x() - width() / 2,
                 pr.center().y() - height() / 2);
        }
        show();
    }
    m_start_ms = QDateTime::currentMSecsSinceEpoch();
    m_timer->start();
}

void ExecutionProgressDialog::closeEvent(QCloseEvent* event)
{
    if (!m_all_done) {
        runInBackground();
        event->ignore();
        return;
    }

    QDialog::closeEvent(event);
}

} // namespace dolphin::ui
