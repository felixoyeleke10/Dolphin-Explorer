// NodeInspectorPanel.cpp — auto-generated parameter editor.
#include "ui/features/nodegraph/NodeInspectorPanel.h"
#include "ui/shell/Theme.h"
#include "pipeline/NodeGraph.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <variant>

namespace dolphin::ui {

NodeInspectorPanel::NodeInspectorPanel(QWidget* parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);

    m_content = new QWidget(this);
    m_content->setObjectName("nodeInspectorContent");
    auto* root = new QVBoxLayout(m_content);
    root->addStretch();
    root->setContentsMargins(0, 0, 0, 0);
    setWidget(m_content);

    clearNode();
}

void NodeInspectorPanel::clearNode()
{
    m_graph = nullptr;
    m_node_id.clear();
    m_status_title.clear();
    m_status_detail.clear();
    rebuild();
}

void NodeInspectorPanel::showNode(pipeline::NodeGraph* graph, const std::string& node_id)
{
    m_graph   = graph;
    m_node_id = node_id;
    m_status_title.clear();
    m_status_detail.clear();
    rebuild();
}

void NodeInspectorPanel::showStatus(const QString& title, const QString& detail)
{
    m_graph = nullptr;
    m_node_id.clear();
    m_status_title = title;
    m_status_detail = detail;
    rebuild();
}

void NodeInspectorPanel::rebuild()
{
    // Destroy all existing children of m_content
    if (auto* old = m_content->layout()) {
        QLayoutItem* item;
        while ((item = old->takeAt(0)) != nullptr) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
        delete old;
    }

    auto* root = new QVBoxLayout(m_content);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    if (!m_graph || m_node_id.empty()) {
        auto* wrap = new QWidget(m_content);
        auto* wrap_layout = new QVBoxLayout(wrap);
        wrap_layout->setContentsMargins(18, 18, 18, 18);
        wrap_layout->setSpacing(Theme::kSpacing3);

        auto* lbl = new QLabel(
            m_status_title.isEmpty() ? tr("No node selected") : m_status_title,
            wrap);
        lbl->setObjectName("panelPlaceholder");
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setWordWrap(true);

        wrap_layout->addWidget(lbl);

        if (!m_status_detail.isEmpty()) {
            auto* detail = new QLabel(m_status_detail, wrap);
            detail->setObjectName("nodeStatusDetail");
            detail->setAlignment(Qt::AlignCenter);
            detail->setWordWrap(true);
            wrap_layout->addWidget(detail);
        }

        root->addStretch();
        root->addWidget(wrap);
        root->addStretch();
        return;
    }

    auto node_ptr = m_graph->findNode(m_node_id);
    if (!node_ptr) { clearNode(); return; }
    auto* node = node_ptr.get();
    const pipeline::NodeSchema schema = node->schema();

    // -- Header ----------------------------------------------------------------
    auto* hdr = new QFrame(m_content);
    hdr->setObjectName("panelHdr");
    auto* hdr_l = new QVBoxLayout(hdr);
    hdr_l->setContentsMargins(Theme::kSpacing4, 10, Theme::kSpacing4, Theme::kSpacing3);
    hdr_l->setSpacing(2);

    auto* type_lbl = new QLabel(QString::fromStdString(schema.label), hdr);
    type_lbl->setObjectName("panelTitle");
    hdr_l->addWidget(type_lbl);

    auto* id_lbl = new QLabel(QString::fromStdString(node->instance_id), hdr);
    id_lbl->setObjectName("nodeInstanceId");
    hdr_l->addWidget(id_lbl);
    root->addWidget(hdr);

    if (node->typeId() == "sss_input") {
        auto* source_wrap = new QWidget(m_content);
        auto* source_layout = new QVBoxLayout(source_wrap);
        source_layout->setContentsMargins(Theme::kSpacing4, Theme::kSpacing4, Theme::kSpacing4, 0);
        source_layout->setSpacing(Theme::kSpacing3);

        auto* helper = new QLabel(
            tr("Import sidescan data here to create project layers that can run through this pipeline."),
            source_wrap);
        helper->setObjectName("nodeHelperText");
        helper->setWordWrap(true);
        source_layout->addWidget(helper);

        auto* import_btn = new QPushButton(tr("Import Data..."), source_wrap);
        connect(import_btn, &QPushButton::clicked, this, &NodeInspectorPanel::importRequested);
        source_layout->addWidget(import_btn);

        root->addWidget(source_wrap);
    }

    if (schema.params.empty()) {
        auto* lbl = new QLabel(tr("This node has no parameters."), m_content);
        lbl->setObjectName("nodeNoParams");
        lbl->setWordWrap(true);
        lbl->setContentsMargins(Theme::kSpacing4, Theme::kSpacing4, Theme::kSpacing4, 0);
        root->addWidget(lbl);
        root->addStretch();
        return;
    }

    // -- Parameter form --------------------------------------------------------
    auto* form_w = new QWidget(m_content);
    auto* form   = new QFormLayout(form_w);
    form->setContentsMargins(Theme::kSpacing4, Theme::kSpacing4, Theme::kSpacing4, Theme::kSpacing4);
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    for (const auto& [key, param] : schema.params) {
        // Get current value (instance override or schema default)
        pipeline::Value cur = param.default_value;
        if (node->params.count(key)) cur = node->params.at(key);

        QWidget* widget = nullptr;
        const std::string key_copy = key;   // capture for lambda

        std::visit([&](const auto& def) {
            using T = std::decay_t<decltype(def)>;

            if constexpr (std::is_same_v<T, bool>) {
                auto* cb = new QCheckBox(form_w);
                cb->setChecked(std::get<bool>(cur));
                connect(cb, &QCheckBox::toggled, [this, node_ptr, key_copy](bool v) {
                    node_ptr->params[key_copy] = v;
                    m_graph->markDirty(node_ptr->instance_id);
                    emit paramChanged();
                });
                widget = cb;

            } else if constexpr (std::is_same_v<T, int>) {
                auto* sb = new QSpinBox(form_w);
                sb->setRange(
                    std::holds_alternative<int>(param.min_value) ? std::get<int>(param.min_value) : -100000,
                    std::holds_alternative<int>(param.max_value) ? std::get<int>(param.max_value) :  100000);
                sb->setValue(std::get<int>(cur));
                connect(sb, &QSpinBox::valueChanged, [this, node_ptr, key_copy](int v) {
                    node_ptr->params[key_copy] = v;
                    m_graph->markDirty(node_ptr->instance_id);
                    emit paramChanged();
                });
                widget = sb;

            } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
                auto* sb = new QDoubleSpinBox(form_w);
                sb->setDecimals(4);
                sb->setSingleStep(0.01);

                auto toDouble = [](const pipeline::Value& v) -> double {
                    if (std::holds_alternative<float>(v))  return std::get<float>(v);
                    if (std::holds_alternative<double>(v)) return std::get<double>(v);
                    return 0.0;
                };
                const double mn = std::visit([&](const auto& x) -> double {
                    using U = std::decay_t<decltype(x)>;
                    if constexpr (std::is_arithmetic_v<U>) return static_cast<double>(x);
                    return -1e9;
                }, param.min_value);
                const double mx = std::visit([&](const auto& x) -> double {
                    using U = std::decay_t<decltype(x)>;
                    if constexpr (std::is_arithmetic_v<U>) return static_cast<double>(x);
                    return  1e9;
                }, param.max_value);

                sb->setRange(mn, mx);
                sb->setValue(toDouble(cur));

                const bool store_as_float = std::is_same_v<T, float>;
                connect(sb, &QDoubleSpinBox::valueChanged, [this, node_ptr, key_copy, store_as_float](double v) {
                    if (store_as_float)
                        node_ptr->params[key_copy] = static_cast<float>(v);
                    else
                        node_ptr->params[key_copy] = v;
                    m_graph->markDirty(node_ptr->instance_id);
                    emit paramChanged();
                });
                widget = sb;

            } else if constexpr (std::is_same_v<T, std::string>) {
                if (!param.options.empty()) {
                    auto* combo = new QComboBox(form_w);
                    for (const auto& option : param.options)
                        combo->addItem(QString::fromStdString(option));

                    const QString current = QString::fromStdString(std::get<std::string>(cur));
                    const int index = combo->findText(current);
                    if (index >= 0)
                        combo->setCurrentIndex(index);

                    connect(combo, &QComboBox::currentTextChanged,
                            [this, node_ptr, key_copy](const QString& text) {
                                node_ptr->params[key_copy] = text.toStdString();
                                m_graph->markDirty(node_ptr->instance_id);
                                emit paramChanged();
                            });
                    widget = combo;
                } else {
                    auto* le = new QLineEdit(form_w);
                    le->setText(QString::fromStdString(std::get<std::string>(cur)));
                    connect(le, &QLineEdit::editingFinished, [this, le, node_ptr, key_copy]() {
                        node_ptr->params[key_copy] = le->text().toStdString();
                        m_graph->markDirty(node_ptr->instance_id);
                        emit paramChanged();
                    });
                    widget = le;
                }
            }
        }, param.default_value);

        if (widget) {
            auto* lbl = new QLabel(QString::fromStdString(param.label), form_w);
            lbl->setObjectName("nodeParamLabel");
            form->addRow(lbl, widget);
        }
    }

    root->addWidget(form_w);
    root->addStretch();
}

} // namespace dolphin::ui
