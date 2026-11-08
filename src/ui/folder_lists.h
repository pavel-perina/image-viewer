#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace iv {

// The favourite folders, in the order the user gave them, and those visited lately, the latest
// first. Shared by the whole application and kept between runs.
class FolderLists : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QStringList favorites READ favorites NOTIFY favoritesChanged)
    Q_PROPERTY(QStringList recent READ recent NOTIFY recentChanged)

public:
    explicit FolderLists(QObject* parent = nullptr);

    QStringList favorites() const { return m_favorites; }
    QStringList recent() const { return m_recent; }

    Q_INVOKABLE void addFavorite(const QString& path);
    Q_INVOKABLE void removeFavorite(const QString& path);
    Q_INVOKABLE void moveFavorite(int from, int to);
    Q_INVOKABLE void noteVisited(const QString& path);
    // A folder on a disk that is not attached stays listed, but cannot be opened for now.
    Q_INVOKABLE bool exists(const QString& path) const;
    // The home folder and the usual ones in it that exist, then the disks and drives attached,
    // each as {name, path}. Read anew on each call, as disks come and go.
    Q_INVOKABLE QVariantList places() const;

signals:
    void favoritesChanged();
    void recentChanged();

private:
    QStringList m_favorites;
    QStringList m_recent;
};

}  // namespace iv
