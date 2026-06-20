// Project.Contacts.cpp — Contact CRUD methods.
#include "app/project/Project.h"
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace dolphin::app {

void Project::addContact(const core::Contact& c)
{
    core::Contact nc = c;
    const auto now = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    nc.id = m_next_contact_id++;
    // Default label from the monotonic id — never reused, never shifts when other
    // contacts are removed. (Deriving it from contacts().size() let a later pick
    // reuse a surviving contact's number after a deletion.) Callers may pass an
    // explicit label (rename, paste) which is kept as-is.
    if (nc.label.empty()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "C%03llu",
                      static_cast<unsigned long long>(nc.id));
        nc.label = buf;
    }
    if (nc.spatial_ref.empty()) nc.spatial_ref = m_display_spatial_ref;
    if (nc.created_at == 0.0)   nc.created_at = now;
    nc.modified_at = now;
    m_contacts.push_back(nc);
    emit contactAdded(nc);
    emit modified();
}

void Project::updateContact(const core::Contact& c)
{
    for (auto& e : m_contacts) {
        if (e.id != c.id) continue;
        e = c;
        if (e.spatial_ref.empty()) e.spatial_ref = m_display_spatial_ref;
        e.modified_at = std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        emit contactUpdated(e.id);
        emit modified();
        return;
    }
}

void Project::removeContact(uint64_t id)
{
    const auto before = m_contacts.size();
    m_contacts.erase(std::remove_if(m_contacts.begin(), m_contacts.end(),
        [id](const core::Contact& c){ return c.id == id; }), m_contacts.end());
    if (m_contacts.size() == before) return;
    emit contactRemoved(id);
    emit modified();
}

// -- Recycle bin --------------------------------------------------------------

void Project::recycleContact(uint64_t id)
{
    auto it = std::find_if(m_contacts.begin(), m_contacts.end(),
        [id](const core::Contact& c){ return c.id == id; });
    if (it == m_contacts.end()) return;
    m_recycled_contacts.push_back(std::move(*it));   // keep id stable for restore
    m_contacts.erase(it);
    emit contactRemoved(id);
    emit recycleBinChanged();
    emit modified();
}

void Project::restoreContact(uint64_t id)
{
    auto it = std::find_if(m_recycled_contacts.begin(), m_recycled_contacts.end(),
        [id](const core::Contact& c){ return c.id == id; });
    if (it == m_recycled_contacts.end()) return;
    core::Contact c = std::move(*it);
    m_recycled_contacts.erase(it);
    m_contacts.push_back(c);
    emit contactAdded(c);
    emit recycleBinChanged();
    emit modified();
}

void Project::purgeContact(uint64_t id)
{
    const auto before = m_recycled_contacts.size();
    m_recycled_contacts.erase(std::remove_if(m_recycled_contacts.begin(), m_recycled_contacts.end(),
        [id](const core::Contact& c){ return c.id == id; }), m_recycled_contacts.end());
    if (m_recycled_contacts.size() == before) return;
    emit recycleBinChanged();
    emit modified();
}

void Project::emptyRecycleBin()
{
    if (m_recycled_contacts.empty()) return;
    m_recycled_contacts.clear();
    emit recycleBinChanged();
    emit modified();
}

} // namespace dolphin::app
