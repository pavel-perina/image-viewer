#include "ui/folder_lists.h"

#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QVariantMap>

#include <algorithm>
#include <utility>

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

QVariantList FolderLists::places() const {
    QVariantList places;
    QStringList seen;
    const auto add = [&](const QString& name, const QString& path) {
        if (!path.isEmpty() && !seen.contains(path) && QFileInfo(path).isDir()) {
            seen << path;
            places << QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("path"), path}};
        }
    };
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    add(QStringLiteral("Home"), home);
    for (const auto& [name, location] : {std::pair{"Desktop", QStandardPaths::DesktopLocation},
                                         std::pair{"Documents", QStandardPaths::DocumentsLocation},
                                         std::pair{"Pictures", QStandardPaths::PicturesLocation},
                                         std::pair{"Downloads", QStandardPaths::DownloadLocation}}) {
        const QString path = QStandardPaths::writableLocation(location);
        if (path != home) {  // where a folder is missing, Qt gives the home folder instead
            add(QString::fromLatin1(name), path);
        }
    }
    // Disks, leaving out what the system mounts for itself.
#ifndef Q_OS_WIN
    const QByteArray systemDevice = QStorageInfo::root().device();
#endif
    for (const QStorageInfo& volume : QStorageInfo::mountedVolumes()) {
        const QString root = volume.rootPath();
        if (!volume.isValid() || !volume.isReady()) {
            continue;
        }
#ifndef Q_OS_WIN
        static const QStringList kSystem{QStringLiteral("/proc"), QStringLiteral("/sys"), QStringLiteral("/dev"),
                                         QStringLiteral("/run"), QStringLiteral("/snap"), QStringLiteral("/boot"),
                                         QStringLiteral("/tmp"), QStringLiteral("/var"), QStringLiteral("/efi")};
        const bool removable = root.startsWith(QStringLiteral("/run/media/")) || root.startsWith(QStringLiteral("/media/")) ||
                               root.startsWith(QStringLiteral("/mnt/"));
        const bool system = std::ranges::any_of(kSystem, [&](const QString& prefix) {
            return root == prefix || root.startsWith(prefix + QLatin1Char('/'));
        });
        if (system && !removable) {
            continue;
        }
        if (root == home || (root != QStringLiteral("/") && home.startsWith(root + QLatin1Char('/')) && !removable)) {
            continue;  // the disk holding the home folder, which is already there
        }
        // Parts of the system disk mounted on their own, such as Btrfs subvolumes.
        if (root != QStringLiteral("/") && (volume.device() == systemDevice || QFileInfo(root).fileName().startsWith(QLatin1Char('.')))) {
            continue;
        }
#endif
        QString name = volume.displayName();
        if (root == QStringLiteral("/")) {
            name = QStringLiteral("File System");
        } else if (name.isEmpty() || name == root) {
            name = QFileInfo(root).fileName().isEmpty() ? root : QFileInfo(root).fileName();
        }
#ifdef Q_OS_WIN
        name = QStringLiteral("%1 (%2)").arg(name, root.left(2));
#endif
        add(name, root);
    }
    return places;
}

}  // namespace iv
