#include "ui/directory_tree_model.h"

#include <QDir>

namespace iv {

DirectoryTreeModel::DirectoryTreeModel(QObject* parent) : QFileSystemModel(parent) {
    setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);
    setRootPath(QString());
}

int DirectoryTreeModel::columnCount(const QModelIndex&) const { return 1; }

QModelIndex DirectoryTreeModel::indexForPath(const QString& path) const { return index(path); }

QString DirectoryTreeModel::pathForIndex(const QModelIndex& index) const { return filePath(index); }

}  // namespace iv
