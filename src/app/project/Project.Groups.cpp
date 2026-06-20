// Project.Groups.cpp — ItemGroup CRUD for layers and contacts.
#include "app/project/Project.h"
#include <algorithm>

namespace dolphin::app {

// -- Layer groups --------------------------------------------------------------

ItemGroup* Project::addLayerGroup(const std::string& name)
{
    ItemGroup g;
    g.id   = generateId("lgrp");
    g.name = name;
    m_layer_groups.push_back(std::move(g));
    emit layerGroupsChanged();
    emit modified();
    return &m_layer_groups.back();
}

void Project::removeLayerGroup(const std::string& id)
{
    for (auto& l : m_layers)
        if (l && l->group_id == id) l->group_id.clear();

    const auto before = m_layer_groups.size();
    m_layer_groups.erase(
        std::remove_if(m_layer_groups.begin(), m_layer_groups.end(),
            [&](const ItemGroup& g) { return g.id == id; }),
        m_layer_groups.end());
    if (m_layer_groups.size() == before) return;

    emit layerGroupsChanged();
    emit modified();
}

void Project::renameLayerGroup(const std::string& id, const std::string& name)
{
    if (auto* g = findLayerGroup(id)) {
        g->name = name;
        emit layerGroupsChanged();
        emit modified();
    }
}

ItemGroup* Project::findLayerGroup(const std::string& id)
{
    for (auto& g : m_layer_groups)
        if (g.id == id) return &g;
    return nullptr;
}

void Project::setLayerGroup(const std::string& layer_id, const std::string& group_id)
{
    auto* layer = findLayer(layer_id);
    if (!layer || layer->group_id == group_id) return;
    layer->group_id = group_id;
    emit layerGroupsChanged();
    emit modified();
}

void Project::setLayerGroups(const std::vector<std::string>& layer_ids,
                             const std::string& group_id)
{
    bool changed = false;
    for (const auto& layer_id : layer_ids) {
        auto* layer = findLayer(layer_id);
        if (!layer || layer->group_id == group_id) continue;
        layer->group_id = group_id;
        changed = true;
    }
    if (!changed) return;

    emit layerGroupsChanged();
    emit modified();
}

void Project::setLayerTags(const std::string& layer_id, std::vector<std::string> tags)
{
    auto* layer = findLayer(layer_id);
    if (!layer) return;
    layer->tags = std::move(tags);
    emit modified();
}

// -- Contact groups ------------------------------------------------------------

ItemGroup* Project::addContactGroup(const std::string& name)
{
    ItemGroup g;
    g.id   = generateId("cgrp");
    g.name = name;
    m_contact_groups.push_back(std::move(g));
    emit contactGroupsChanged();
    emit modified();
    return &m_contact_groups.back();
}

void Project::removeContactGroup(const std::string& id)
{
    for (auto& c : m_contacts)
        if (c.group_id == id) c.group_id.clear();
    for (auto& c : m_recycled_contacts)   // also un-reference recycled contacts
        if (c.group_id == id) c.group_id.clear();

    const auto before = m_contact_groups.size();
    m_contact_groups.erase(
        std::remove_if(m_contact_groups.begin(), m_contact_groups.end(),
            [&](const ItemGroup& g) { return g.id == id; }),
        m_contact_groups.end());
    if (m_contact_groups.size() == before) return;

    emit contactGroupsChanged();
    emit modified();
}

void Project::renameContactGroup(const std::string& id, const std::string& name)
{
    if (auto* g = findContactGroup(id)) {
        g->name = name;
        emit contactGroupsChanged();
        emit modified();
    }
}

ItemGroup* Project::findContactGroup(const std::string& id)
{
    for (auto& g : m_contact_groups)
        if (g.id == id) return &g;
    return nullptr;
}

void Project::setContactGroup(uint64_t contact_id, const std::string& group_id)
{
    for (auto& c : m_contacts) {
        if (c.id != contact_id) continue;
        if (c.group_id == group_id) return;
        c.group_id = group_id;
        emit contactUpdated(contact_id);
        emit contactGroupsChanged();
        emit modified();
        return;
    }
}

void Project::setContactTags(uint64_t contact_id, std::vector<std::string> tags)
{
    for (auto& c : m_contacts) {
        if (c.id != contact_id) continue;
        c.tags = std::move(tags);
        emit modified();
        return;
    }
}

} // namespace dolphin::app
