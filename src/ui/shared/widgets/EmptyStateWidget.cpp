// EmptyStateWidget.cpp — reusable empty / placeholder panel.
#include "ui/shared/widgets/EmptyStateWidget.h"
#include "ui/shared/UiUtils.h"
#include "ui/shell/Theme.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace dolphin::ui {

EmptyStateWidget::EmptyStateWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* root = makeCompactLayout<QVBoxLayout>(this);
    root->addStretch(1);

    m_icon_lbl = new QLabel(this);
    m_icon_lbl->setObjectName("emptyStateIcon");
    m_icon_lbl->setAlignment(Qt::AlignHCenter);
    m_icon_lbl->hide();
    root->addWidget(m_icon_lbl);

    m_title_lbl = new QLabel(this);
    m_title_lbl->setObjectName("emptyStateTitle");
    m_title_lbl->setAlignment(Qt::AlignHCenter);
    m_title_lbl->hide();
    root->addWidget(m_title_lbl);

    m_sub_lbl = new QLabel(this);
    m_sub_lbl->setObjectName("emptyStateSub");
    m_sub_lbl->setAlignment(Qt::AlignHCenter);
    m_sub_lbl->setWordWrap(true);
    m_sub_lbl->hide();
    root->addWidget(m_sub_lbl);

    // Spacing between labels and buttons.
    root->addSpacing(Theme::kSpacing3);

    // Button area — actions are appended here.
    auto* btn_holder = new QWidget(this);
    m_btn_area = makeCompactLayout<QVBoxLayout>(btn_holder);
    m_btn_area->setSpacing(Theme::kSpacing1);
    root->addWidget(btn_holder, 0, Qt::AlignHCenter);

    root->addStretch(1);
}

void EmptyStateWidget::setIcon(const QString& glyph)
{
    m_icon_lbl->setText(glyph);
    m_icon_lbl->setVisible(!glyph.isEmpty());
}

void EmptyStateWidget::setTitle(const QString& title)
{
    m_title_lbl->setText(title);
    m_title_lbl->setVisible(!title.isEmpty());
}

void EmptyStateWidget::setSubtitle(const QString& subtitle)
{
    m_sub_lbl->setText(subtitle);
    m_sub_lbl->setVisible(!subtitle.isEmpty());
}

QPushButton* EmptyStateWidget::addAction(const QString& label)
{
    auto* btn = new QPushButton(label, this);
    btn->setObjectName("emptyStateActionBtn");
    m_btn_area->addWidget(btn);
    return btn;
}

} // namespace dolphin::ui
