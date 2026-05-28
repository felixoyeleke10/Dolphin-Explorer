#pragma once
// Private helpers shared across Project*.cpp compilation units.
// Never included from headers.
#include <string>
#include <algorithm>
#include <QDir>
#include <QFileInfo>

namespace dolphin::app::detail {

inline QString normalisePath(const QString& path)
{
    QFileInfo info(path);
    QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath())
                               : QDir::cleanPath(canonical);
}

inline QString normalisePath(const std::string& path)
{
    return normalisePath(QString::fromStdString(path));
}

inline std::string normaliseFormat(std::string fmt)
{
    std::transform(fmt.begin(), fmt.end(), fmt.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return fmt;
}

inline QString manifestDirectory(const std::string& manifest_path)
{
    if (manifest_path.empty()) return {};
    return QDir::cleanPath(
        QFileInfo(QString::fromStdString(manifest_path)).absolutePath());
}

inline QString cacheRootForManifest(const std::string& manifest_path)
{
    const QString base = manifestDirectory(manifest_path);
    return base.isEmpty() ? QString{}
                          : QDir::cleanPath(QDir(base).absoluteFilePath("data"));
}

inline bool pathHasPrefix(const QString& path, const QString& prefix)
{
    if (path.isEmpty() || prefix.isEmpty()) return false;
#ifdef _WIN32
    constexpr Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    const QString cp = QDir::cleanPath(path);
    QString pp = QDir::cleanPath(prefix);
    if (cp.compare(pp, cs) == 0) return true;
    if (!pp.endsWith('/')) pp += '/';
    return cp.startsWith(pp, cs);
}

} // namespace dolphin::app::detail
