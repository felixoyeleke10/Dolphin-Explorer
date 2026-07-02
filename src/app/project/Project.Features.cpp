// Project.Features.cpp — Feature CRUD methods.
//
// Features are SHAPE annotations (polylines/polygons) — distinct from contacts
// (point picks). The CRUD shape mirrors Project.Contacts.cpp so both annotation
// kinds behave identically (monotonic ids, default labels, timestamps, signals).
#include "app/project/Project.h"
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace dolphin::app {

void Project::addFeature(const core::Feature& f)
{
    core::Feature nf = f;
    const auto now = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    nf.id = m_next_feature_id++;
    // Default label from the monotonic id — never reused, never shifts when other
    // features are removed. Callers may pass an explicit label (rename, paste).
    if (nf.label.empty()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "F%03llu",
                      static_cast<unsigned long long>(nf.id));
        nf.label = buf;
    }
    if (nf.spatial_ref.empty()) nf.spatial_ref = m_display_spatial_ref;
    if (nf.created_at == 0.0)   nf.created_at = now;
    nf.modified_at = now;
    m_features.push_back(nf);
    emit featureAdded(nf);
    emit modified();
}

void Project::updateFeature(const core::Feature& f)
{
    for (auto& e : m_features) {
        if (e.id != f.id) continue;
        e = f;
        if (e.spatial_ref.empty()) e.spatial_ref = m_display_spatial_ref;
        e.modified_at = std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        emit featureUpdated(e.id);
        emit modified();
        return;
    }
}

void Project::removeFeature(uint64_t id)
{
    const auto before = m_features.size();
    m_features.erase(std::remove_if(m_features.begin(), m_features.end(),
        [id](const core::Feature& f){ return f.id == id; }), m_features.end());
    if (m_features.size() == before) return;
    emit featureRemoved(id);
    emit modified();
}

} // namespace dolphin::app
