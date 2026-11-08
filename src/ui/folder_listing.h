#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace iv {

// The folders and files of one folder, folders first, read in the background and read again when
// the folder changes. For views that list files by name, size, type and date rather than as
// thumbnails, such as the file chooser. Hidden entries are left out.
class FolderListing : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    // Extensions of the files to list, such as "jpg", in any case; empty lists every file.
    Q_PROPERTY(QStringList extensions READ extensions WRITE setExtensions NOTIFY extensionsChanged)
    Q_PROPERTY(bool showFiles READ showFiles WRITE setShowFiles NOTIFY showFilesChanged)
    Q_PROPERTY(SortKey sortKey READ sortKey WRITE setSortKey NOTIFY sortChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY sortChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum class SortKey { Name, Size, Type, Modified };
    Q_ENUM(SortKey)

    enum Role {
        NameRole = Qt::UserRole + 1,
        PathRole,
        IsFolderRole,
        SizeRole,      // bytes, -1 for a folder
        SizeTextRole,  // "1.5 MB", empty for a folder
        TypeRole,      // "JPEG image", "Folder"
        ModifiedRole,
        ModifiedTextRole,
    };

    explicit FolderListing(QObject* parent = nullptr);
    ~FolderListing() override;

    QString path() const;
    void setPath(const QString& path);
    QStringList extensions() const;
    void setExtensions(const QStringList& extensions);
    bool showFiles() const;
    void setShowFiles(bool show);
    SortKey sortKey() const;
    void setSortKey(SortKey key);
    bool sortDescending() const;
    void setSortDescending(bool descending);
    bool loading() const;
    int count() const;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // The row of an entry by name, or -1.
    Q_INVOKABLE int indexOf(const QString& name) const;
    Q_INVOKABLE QVariantMap get(int row) const;
    // "folder", "file" or empty when nothing is there, for any path.
    Q_INVOKABLE QString kindOf(const QString& path) const;
    Q_INVOKABLE void refresh();

signals:
    void pathChanged();
    void extensionsChanged();
    void showFilesChanged();
    void sortChanged();
    void loadingChanged();
    void countChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace iv
