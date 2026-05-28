// Project.Sources.cpp — ProjectSource CRUD methods.
#include "app/project/Project.h"
#include "app/project/Project_p.h"
#include <QFileInfo>

namespace dolphin::app {

static void updateSourceMetadata(ProjectSource& src, const std::string& path)
{
    const QString np = detail::normalisePath(path);
    QFileInfo info(np);
    src.path = np.toStdString();
    src.size_bytes = info.exists() ? static_cast<uint64_t>(info.size()) : 0;
    src.modified_utc_ms = info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0;
}

ProjectSource* Project::addSource(const std::string& path, const std::string& format)
{
    ProjectSource src;
    src.id     = generateId("src");
    src.format = format;
    updateSourceMetadata(src, path);
    m_sources.push_back(std::move(src));
    emit modified();
    return &m_sources.back();
}

ProjectSource* Project::findSource(const std::string& id)
{
    for (auto& s : m_sources) if (s.id == id) return &s;
    return nullptr;
}

ProjectSource* Project::findSourceByPath(const std::string& path)
{
    const QString wanted = detail::normalisePath(path);
    for (auto& s : m_sources) {
        if (detail::normalisePath(s.path) == wanted) return &s;
    }
    return nullptr;
}

} // namespace dolphin::app
