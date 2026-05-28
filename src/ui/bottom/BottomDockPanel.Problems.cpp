// BottomDockPanel.Problems.cpp — Problems tab: build, slots, empty-state helpers.
#include "ui/bottom/BottomDockPanel.h"
#include "ui/shell/Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QShortcut>
#include <QStackedWidget>

namespace dolphin::ui {

using namespace Theme;

void BottomDockPanel::buildProblemsTab(QWidget* parent)
{
    auto* s = qobject_cast<QStackedWidget*>(parent);
    Q_ASSERT(s);

    m_prob_list = new QListWidget;
    m_prob_list->setObjectName("panelProbList");
    m_prob_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_prob_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_prob_list->setWordWrap(true);
    s->addWidget(m_prob_list);
    updateProblemsEmptyState();

    connect(m_prob_list, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem* item) {
                const QString lid = item->data(Qt::UserRole).toString();
                if (!lid.isEmpty())
                    emit layerFocusRequested(lid);
            });

    auto* copy_sc = new QShortcut(QKeySequence::Copy, m_prob_list);
    copy_sc->setContext(Qt::WidgetShortcut);
    connect(copy_sc, &QShortcut::activated, this, [this]() {
        if (auto* item = m_prob_list->currentItem())
            QApplication::clipboard()->setText(item->text());
    });

    m_prob_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_prob_list, &QListWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
                auto* item = m_prob_list->itemAt(pos);
                QMenu menu(m_prob_list);
                auto* act_copy     = menu.addAction(tr("Copy"));
                auto* act_copy_all = menu.addAction(tr("Copy All"));
                act_copy->setEnabled(item != nullptr);
                connect(act_copy, &QAction::triggered, this, [item]() {
                    if (item) QApplication::clipboard()->setText(item->text());
                });
                connect(act_copy_all, &QAction::triggered, this, [this]() {
                    QStringList lines;
                    for (int i = 0; i < m_prob_list->count(); ++i)
                        lines << m_prob_list->item(i)->text();
                    QApplication::clipboard()->setText(lines.join('\n'));
                });
                menu.exec(m_prob_list->viewport()->mapToGlobal(pos));
            });
}

void BottomDockPanel::removeProblemsEmptyState()
{
    if (!m_prob_list) return;
    for (int i = m_prob_list->count() - 1; i >= 0; --i) {
        auto* item = m_prob_list->item(i);
        if (item && item->data(Qt::UserRole).toString() == QStringLiteral("__empty__"))
            delete m_prob_list->takeItem(i);
    }
}

void BottomDockPanel::updateProblemsEmptyState()
{
    if (!m_prob_list) return;

    removeProblemsEmptyState();
    if (m_hub && m_hub->totalProblemCount() > 0) return;

    auto* item = new QListWidgetItem(tr("No problems detected"), m_prob_list);
    item->setForeground(QColor(kTextMuted));
    item->setFlags(Qt::NoItemFlags);
    item->setData(Qt::UserRole, QStringLiteral("__empty__"));
}

void BottomDockPanel::onProblemPosted(const DiagnosticsHub::Problem& p)
{
    removeProblemsEmptyState();

    const char* label = (p.severity == DiagnosticsHub::Severity::Error)   ? "[ERROR] "
                      : (p.severity == DiagnosticsHub::Severity::Warning) ? "[WARN] "
                      :                                                      "[INFO] ";
    const QColor col  = (p.severity == DiagnosticsHub::Severity::Error)   ? QColor(kDanger)
                      : (p.severity == DiagnosticsHub::Severity::Warning) ? QColor(kWarning)
                      :                                                      QColor(kAccent);

    QString text = QString::fromLatin1(label) + p.message;
    if (!p.layer_id.isEmpty())
        text += "  -  " + p.layer_id;

    auto* item = new QListWidgetItem(text, m_prob_list);
    item->setForeground(col);
    item->setData(Qt::UserRole, p.layer_id);
    item->setToolTip(p.timestamp.toString("hh:mm:ss") +
                     (p.layer_id.isEmpty() ? QString{} : "\nLayer: " + p.layer_id));

    m_prob_list->scrollToBottom();

    const int n_err = m_hub->errorCount();
    const int n_all = m_hub->totalProblemCount();
    updateBadge(0, n_all, n_err > 0);
}

void BottomDockPanel::onProblemsCleared(const QString& layer_id)
{
    if (layer_id.isEmpty()) {
        m_prob_list->clear();
    } else {
        for (int i = m_prob_list->count() - 1; i >= 0; --i) {
            auto* item = m_prob_list->item(i);
            if (item && item->data(Qt::UserRole).toString() == layer_id)
                delete m_prob_list->takeItem(i);
        }
    }
    updateProblemsEmptyState();
    updateBadge(0, m_hub->totalProblemCount(), m_hub->errorCount() > 0);
}

} // namespace dolphin::ui
