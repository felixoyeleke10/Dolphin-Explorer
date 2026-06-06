#pragma once
#include <Qt>
#include <QTreeWidgetItem>

// Shared role offsets and item-type enum used by all LineListPanel translation units.
namespace dolphin::ui::detail {

// -- Tag color palette (8 named colors, stored as tag id strings) -------------
struct TagPaletteEntry {
    const char* id;
    const char* label;
    quint8 r, g, b;
};

inline constexpr TagPaletteEntry kTagPalette[] = {
    { "red",    "Red",    220,  53,  69 },
    { "orange", "Orange", 253, 126,  20 },
    { "yellow", "Yellow", 255, 193,   7 },
    { "green",  "Green",   40, 167,  69 },
    { "teal",   "Teal",    32, 201, 151 },
    { "blue",   "Blue",    13, 110, 253 },
    { "purple", "Purple", 111,  66, 193 },
    { "gray",   "Gray",   108, 117, 125 },
};
inline constexpr int kTagPaletteSize = static_cast<int>(sizeof(kTagPalette) / sizeof(kTagPalette[0]));

    static constexpr int kRoleId       = Qt::UserRole;
    static constexpr int kRoleType     = Qt::UserRole + 1;
    static constexpr int kRoleModality = Qt::UserRole + 2;
    static constexpr int kRoleGroupId  = Qt::UserRole + 3;  // stored on LayerGroup/ContactGroup items

    // Stored as int in kRoleType. Replaces the previous stringly-typed approach.
    enum class ItemType : int {
        Unknown = 0,
        Project,
        Modality,
        Layer,
        Source,
        ContactsSection,
        Contact,
        FeaturesSection,
        LayerGroup,      // user-defined layer group
        ContactGroup,    // user-defined contact group
    };

    inline ItemType itemTypeOf(const QTreeWidgetItem* item)
    {
        if (!item) return ItemType::Unknown;
        return static_cast<ItemType>(item->data(0, kRoleType).toInt());
    }

    inline void setItemType(QTreeWidgetItem* item, ItemType type)
    {
        item->setData(0, kRoleType, static_cast<int>(type));
    }

} // namespace dolphin::ui::detail
