#include "ui/folder_lists.h"

#include <QFileInfo>
#include <QSettings>

namespace iv {

namespace {

constexpr qsizetype kMaxRecent = 12;
const auto kFavoritesKey = QStringLiteral("Folders/favorites");
const auto kRecentKey = QStringLiteral("Folders/recent");

}  // namespace

FolderLists::FolderLists(QObject* parent)
    : QObject(parent),
      m_favorites(QSettings().value(kFavoritesKey).toStringList()),
      m_recent(QSettings().value(kRecentKey).toStringList()) {}

void FolderLists::addFavorite(const QString& path) {
    if (!path.isEmpty() && !m_favorites.contains(path)) {
        m_favorites.append(path);
        QSettings().setValue(kFavoritesKey, m_favorites);
        emit favoritesChanged();
    }
}

void FolderLists::removeFavorite(const QString& path) {
    if (m_favorites.removeAll(path) > 0) {
        QSettings().setValue(kFavoritesKey, m_favorites);
        emit favoritesChanged();
    }
}

void FolderLists::moveFavorite(int from, int to) {
    const qsizetype count = m_favorites.size();
    if (from != to && from >= 0 && from < count && to >= 0 && to < count) {
        m_favorites.move(from, to);
        QSettings().setValue(kFavoritesKey, m_favorites);
        emit favoritesChanged();
    }
}

void FolderLists::noteVisited(const QString& path) {
    if (path.isEmpty() || (!m_recent.isEmpty() && m_recent.front() == path)) {
        return;
    }
    m_recent.removeAll(path);
    m_recent.prepend(path);
    if (m_recent.size() > kMaxRecent) {
        m_recent.resize(kMaxRecent);
    }
    QSettings().setValue(kRecentKey, m_recent);
    emit recentChanged();
}

bool FolderLists::exists(const QString& path) const { return QFileInfo(path).isDir(); }

}  // namespace iv
