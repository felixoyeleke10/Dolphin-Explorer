#include "ui/mainwindow/rightpanel/RightPanel.Display.h"
#include "ui/shell/Theme.h"
#include "render/sonar/SSSPalette.h"
#include "render/sonar/SonarDisplayParams.h"
#include "ui/shared/dialogs/SettingsDialog.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace dolphin::ui {

DisplayModule::DisplayModule(QWidget* parent)
    : QWidget(parent)
{
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 2, 0, Theme::kSpacing2);
    vl->setSpacing(0);

    auto* row = new QWidget(this);
    auto* rl  = new QHBoxLayout(row);
    rl->setContentsMargins(Theme::kSpacing4, 3, Theme::kSpacing4, 5);
    rl->setSpacing(Theme::kSpacing3);

    auto* k = new QLabel(tr("Palette"), this);
    k->setObjectName("inspMetaKey");
    k->setFixedWidth(Theme::kKeyLabelW);
    k->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_palette = new QComboBox(this);
    m_palette->setObjectName("avPaletteCombo");
    m_palette->setToolTip(
        tr("Colour palette used to render sidescan backscatter intensity.\n"
           "Greyscale: darker = stronger return (conventional SSS display).\n"
           "Thermal / Copper / Viridis: false-colour, useful for spotting subtle texture differences.\n"
           "The default palette can be changed in application settings."));
    for (int i = 0; i < PaletteIndex::Count; ++i)
        m_palette->addItem(SSSPalette::name(i));

    {
        QSettings qs;
        const QVariant sss_idx = qs.value(QStringLiteral("sss/paletteIdx"));
        if (sss_idx.isValid()) {
            m_palette->setCurrentIndex(sss_idx.toInt());
        } else {
            m_palette->setCurrentIndex(SSSPalette::indexFromName(
                qs.value(SettingsDialog::kKeyDefaultPalette,
                         QStringLiteral("Gray")).toString()));
        }
    }

    connect(m_palette, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) { emit paletteChanged(idx); });

    rl->addWidget(k);
    rl->addWidget(m_palette, 1);
    vl->addWidget(row);

    // Channel row — Port / Both / Starboard
    auto* ch_row = new QWidget(this);
    auto* crl    = new QHBoxLayout(ch_row);
    crl->setContentsMargins(Theme::kSpacing4, 3, Theme::kSpacing4, 5);
    crl->setSpacing(Theme::kSpacing3);

    auto* ck = new QLabel(tr("Channel"), this);
    ck->setObjectName("inspMetaKey");
    ck->setFixedWidth(Theme::kKeyLabelW);
    ck->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_channel = new QComboBox(this);
    m_channel->setObjectName("avChannelCombo");
    m_channel->setToolTip(
        tr("Choose which transducer channels to display.\n"
           "Both: port and starboard side by side (default).\n"
           "Port: left channel only.\n"
           "Starboard: right channel only."));
    m_channel->addItem(tr("Both"),      static_cast<int>(DisplayChannel::Both));
    m_channel->addItem(tr("Port"),      static_cast<int>(DisplayChannel::Port));
    m_channel->addItem(tr("Starboard"), static_cast<int>(DisplayChannel::Starboard));

    connect(m_channel, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int /*idx*/) {
                emit channelChanged(static_cast<DisplayChannel>(
                    m_channel->currentData().toInt()));
            });

    crl->addWidget(ck);
    crl->addWidget(m_channel, 1);
    vl->addWidget(ch_row);
    vl->addStretch(1);
}

void DisplayModule::setLayer(app::DataLayer* layer)
{
    if (!layer) return;

    if (m_palette) {
        int pal = layer->sss_palette;
        if (pal < 0) {
            QSettings qs;
            const QVariant v = qs.value(QStringLiteral("sss/paletteIdx"));
            pal = v.isValid() ? v.toInt()
                              : SSSPalette::indexFromName(
                                    qs.value(SettingsDialog::kKeyDefaultPalette,
                                             QStringLiteral("Gray")).toString());
        }
        QSignalBlocker sb(m_palette);
        m_palette->setCurrentIndex(pal);
    }

    if (m_channel) {
        const int ch  = static_cast<int>(layer->sss_display_state.params.display_channel);
        const int idx = m_channel->findData(ch);
        QSignalBlocker sb(m_channel);
        m_channel->setCurrentIndex(idx >= 0 ? idx : 0);
    }
}

int DisplayModule::currentPaletteIndex() const
{
    return m_palette ? m_palette->currentIndex() : 0;
}

void DisplayModule::setPalette(int idx)
{
    if (!m_palette) return;
    QSignalBlocker sb(m_palette);
    m_palette->setCurrentIndex(idx);
}

void DisplayModule::setChannel(DisplayChannel ch)
{
    if (!m_channel) return;
    const int idx = m_channel->findData(static_cast<int>(ch));
    QSignalBlocker sb(m_channel);
    if (idx >= 0) m_channel->setCurrentIndex(idx);
}

} // namespace dolphin::ui
