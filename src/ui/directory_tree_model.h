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
    // Hidden folders are listed too (dot names on Linux, the hidden attribute on Windows).
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)

public:
    explicit DirectoryTreeModel(QObject* parent = nullptr);

    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    bool showHidden() const;
    void setShowHidden(bool show);

    Q_INVOKABLE QModelIndex indexForPath(const QString& path) const;
    Q_INVOKABLE QString pathForIndex(const QModelIndex& index) const;
    // Going straight to a folder gives its parents only the one child that leads there, so this
    // reads them in full. The rows arrive in the background, with directoryLoaded.
    Q_INVOKABLE void readParentsOf(const QString& path);

signals:
    void showHiddenChanged();
};

}  // namespace iv
