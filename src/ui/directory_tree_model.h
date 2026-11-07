#pragma once

#include <QFileSystemModel>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace iv {

// The directories of the file system as one column, for a TreeView. Qt reads them on its
// own thread, so a slow disk does not block the UI.
class DirectoryTreeModel : public QFileSystemModel {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit DirectoryTreeModel(QObject* parent = nullptr);

    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    Q_INVOKABLE QModelIndex indexForPath(const QString& path) const;
    Q_INVOKABLE QString pathForIndex(const QModelIndex& index) const;
    // Going straight to a folder gives its parents only the one child that leads there, so this
    // reads them in full. The rows arrive in the background, with directoryLoaded.
    Q_INVOKABLE void readParentsOf(const QString& path);
};

}  // namespace iv
