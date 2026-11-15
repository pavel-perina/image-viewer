#include "ui/directory_tree_model.h"

#include <QDir>

namespace iv {

DirectoryTreeModel::DirectoryTreeModel(QObject* parent) : QFileSystemModel(parent) {
    setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);
    setRootPath(QString());
}

int DirectoryTreeModel::columnCount(const QModelIndex&) const { return 1; }

bool DirectoryTreeModel::showHidden() const { return filter().testFlag(QDir::Hidden); }

void DirectoryTreeModel::setShowHidden(bool show) {
    if (show != showHidden()) {
        setFilter(filter() ^ QDir::Hidden);
        emit showHiddenChanged();
    }
}

QModelIndex DirectoryTreeModel::indexForPath(const QString& path) const { return index(path); }

QString DirectoryTreeModel::pathForIndex(const QModelIndex& index) const { return filePath(index); }

void DirectoryTreeModel::readParentsOf(const QString& path) {
    for (QModelIndex parent = index(path).parent(); parent.isValid(); parent = parent.parent()) {
        if (canFetchMore(parent)) {
            fetchMore(parent);
        }
    }
}

}  // namespace iv
