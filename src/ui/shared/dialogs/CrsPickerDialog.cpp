// CrsPickerDialog.cpp — searchable EPSG CRS picker for survey projects.
//
// Performance strategy:
//   - buildTable() runs once in the constructor; all QTableWidgetItems are
//     allocated up front with setRowCount(N) so Qt does a single resize.
//   - applyFilter() only toggles setRowHidden() — zero allocation on search.
//   - A 100 ms debounce timer means fast typing never stalls the UI.
#include "ui/shared/dialogs/CrsPickerDialog.h"
#include "ui/shell/Theme.h"
#include "geo/EpsgDatabase.h"
#include "geo/GeoUtils.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

namespace dolphin::ui {

static constexpr int kColEpsg = 0;
static constexpr int kColName = 1;
static constexpr int kColType = 2;
static constexpr int kMinW    = 640;
static constexpr int kMinH    = 480;
static constexpr int kInitW   = 720;
static constexpr int kInitH   = 540;

// Row roles stored on column 0
static constexpr int kRoleCode      = Qt::UserRole;       // int  EPSG code
static constexpr int kRoleNameLower = Qt::UserRole + 1;   // QString lower-case name
static constexpr int kRoleCodeStr   = Qt::UserRole + 2;   // QString "epsg:NNNN"
static constexpr int kRoleIsSep     = Qt::UserRole + 3;   // bool separator row

CrsPickerDialog::CrsPickerDialog(const core::SpatialRef& current, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Select Coordinate Reference System"));
    setMinimumSize(kMinW, kMinH);
    resize(kInitW, kInitH);

    auto* vlay = new QVBoxLayout(this);
    vlay->setSpacing(Theme::kSpacing3);
    vlay->setContentsMargins(Theme::kSpacing4, Theme::kSpacing4, Theme::kSpacing4, Theme::kSpacing4);

    // -- Title -------------------------------------------------------------
    auto* title = new QLabel(
        tr("Choose the coordinate reference system used by this survey's navigation data."),
        this);
    title->setWordWrap(true);
    title->setObjectName("crsPickerTitle");
    vlay->addWidget(title);

    // -- Search bar --------------------------------------------------------
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search by name or EPSG code\u2026"));
    m_search->setClearButtonEnabled(true);
    vlay->addWidget(m_search);

    // -- Table -------------------------------------------------------------
    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("EPSG"), tr("Name"), tr("Type")});
    m_table->horizontalHeader()->setSectionResizeMode(kColEpsg, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kColType, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSortingEnabled(false);
    vlay->addWidget(m_table, 1);

    // -- Preview -----------------------------------------------------------
    m_preview = new QLabel(tr("No CRS selected"), this);
    m_preview->setObjectName("crsPreviewLabel");
    m_preview->setAlignment(Qt::AlignCenter);
    vlay->addWidget(m_preview);

    // -- Buttons -----------------------------------------------------------
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* ok_btn = buttons->button(QDialogButtonBox::Ok);
    ok_btn->setObjectName("dlgBtnOk");
    ok_btn->setText(tr("Apply CRS"));
    ok_btn->setEnabled(false);
    vlay->addWidget(buttons);

    // -- Debounce timer ----------------------------------------------------
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(100);
    connect(m_debounce, &QTimer::timeout, this, [this]() {
        applyFilter(m_pending_filter);
    });

    // -- Build table once --------------------------------------------------
    buildTable();

    // Pre-select the current CRS if it's in the database
    m_selected = current;
    if (!current.id.empty()) {
        if (const geo::EpsgEntry* e = geo::epsgByRef(current)) {
            const QString target = QString("EPSG:%1").arg(e->code);
            for (int row = 0; row < m_table->rowCount(); ++row) {
                auto* it = m_table->item(row, kColEpsg);
                if (it && it->text() == target) {
                    m_table->selectRow(row);
                    m_table->scrollToItem(it, QAbstractItemView::PositionAtCenter);
                    break;
                }
            }
        }
    }

    // -- Connections -------------------------------------------------------
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_pending_filter = text;
        m_debounce->start();          // restart; fires 100 ms after last keystroke
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this, ok_btn]() {
                onSelectionChanged();
                ok_btn->setEnabled(m_selected.kind != core::SpatialRefKind::Unknown);
            });
    connect(m_table, &QTableWidget::cellDoubleClicked,
            this, &CrsPickerDialog::onItemDoubleClicked);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void CrsPickerDialog::buildTable()
{
    const auto& db = geo::epsgDatabase();

    // Count non-separator rows to pre-size the table exactly once.
    // We add one separator row between common and non-common entries.
    const int total = static_cast<int>(db.size()) + 1;
    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(total);

    const QString typeGeo  = tr("Geographic");
    const QString typeProj = tr("Projected");
    const QString sepLabel = tr("-- All CRS -------------------------------");

    QFont boldFont   = m_table->font();  boldFont.setBold(true);
    QFont italicFont = m_table->font();  italicFont.setItalic(true);
    const QColor dimColor(120, 120, 120);

    bool separatorInserted = false;
    int row = 0;

    auto makeItem = [](const QString& text) {
        auto* it = new QTableWidgetItem(text);
        it->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        return it;
    };

    for (const auto& e : db) {
        // Insert separator once, between common and non-common entries
        if (!separatorInserted && !e.is_common) {
            auto* sep = new QTableWidgetItem(sepLabel);
            sep->setFlags(Qt::NoItemFlags);
            sep->setFont(italicFont);
            sep->setForeground(dimColor);
            sep->setData(kRoleIsSep, true);
            m_table->setItem(row, kColEpsg, new QTableWidgetItem());
            m_table->setItem(row, kColName, sep);
            m_table->setItem(row, kColType, new QTableWidgetItem());
            m_table->item(row, kColEpsg)->setFlags(Qt::NoItemFlags);
            m_table->item(row, kColType)->setFlags(Qt::NoItemFlags);
            m_table->setRowHeight(row, 20);
            ++row;
            separatorInserted = true;
        }

        const QString epsgStr  = QString("EPSG:%1").arg(e.code);
        const QString fullName = QString::fromStdString(e.full_name);
        const QString typeStr  = (e.kind == core::SpatialRefKind::Geographic) ? typeGeo : typeProj;

        auto* epsg_item = makeItem(epsgStr);
        auto* name_item = makeItem(fullName);
        auto* type_item = makeItem(typeStr);

        epsg_item->setTextAlignment(Qt::AlignCenter);
        type_item->setTextAlignment(Qt::AlignCenter);

        // Store lookup data on column 0 for fast filter/selection
        epsg_item->setData(kRoleCode,      e.code);
        epsg_item->setData(kRoleNameLower, fullName.toLower());
        epsg_item->setData(kRoleCodeStr,   epsgStr.toLower());
        epsg_item->setData(kRoleIsSep,     false);

        if (e.is_common)
            name_item->setFont(boldFont);

        m_table->setItem(row, kColEpsg, epsg_item);
        m_table->setItem(row, kColName, name_item);
        m_table->setItem(row, kColType, type_item);
        ++row;
    }

    // Trim any unused rows (in case separator was not inserted)
    m_table->setRowCount(row);
    m_table->setUpdatesEnabled(true);
}

void CrsPickerDialog::applyFilter()
{
    applyFilter(m_pending_filter);
}

void CrsPickerDialog::applyFilter(const QString& text)
{
    const QString f = text.trimmed().toLower();
    const bool    searching = !f.isEmpty();

    m_table->setUpdatesEnabled(false);

    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto* epsg_item = m_table->item(row, kColEpsg);
        if (!epsg_item) continue;

        // Always hide separator rows while searching
        if (epsg_item->data(kRoleIsSep).toBool()) {
            m_table->setRowHidden(row, searching);
            continue;
        }

        if (!searching) {
            m_table->setRowHidden(row, false);
            continue;
        }

        const QString nameLower = epsg_item->data(kRoleNameLower).toString();
        const QString codeStr   = epsg_item->data(kRoleCodeStr).toString();
        const bool    matches   = nameLower.contains(f) || codeStr.contains(f);
        m_table->setRowHidden(row, !matches);
    }

    m_table->setUpdatesEnabled(true);
}

void CrsPickerDialog::onSelectionChanged()
{
    const auto rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        m_preview->setText(tr("No CRS selected"));
        m_selected = {};
        return;
    }

    const int row = rows.first().row();
    auto* epsg_item = m_table->item(row, kColEpsg);
    if (!epsg_item || epsg_item->data(kRoleIsSep).toBool()) {
        m_preview->setText(tr("No CRS selected"));
        m_selected = {};
        return;
    }

    const int code = epsg_item->data(kRoleCode).toInt();
    if (const geo::EpsgEntry* e = geo::epsgByCode(code)) {
        m_selected = geo::spatialRefFromId("EPSG:" + std::to_string(e->code));
        m_selected.exact = true;
        m_preview->setText(
            QString::fromStdString(e->full_name)
            + "   \u00b7   EPSG:" + QString::number(e->code));
    }
}

void CrsPickerDialog::onItemDoubleClicked(int /*row*/, int /*col*/)
{
    if (m_selected.kind != core::SpatialRefKind::Unknown)
        accept();
}

} // namespace dolphin::ui
