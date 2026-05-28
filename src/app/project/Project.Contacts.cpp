// Project.Contacts.cpp — Contact CRUD methods.
#include "app/project/Project.h"
#include <algorithm>
#include <chrono>

namespace dolphin::app {

void Project::addContact(const core::Contact& c)
{
    core::Contact nc = c;
    const auto now = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    nc.id = m_next_contact_id++;
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

} // namespace dolphin::app
