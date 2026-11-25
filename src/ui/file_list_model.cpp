#include "ui/file_list_model.h"

#include <QSet>

#include <utility>

#include "ui/thumbnail_provider.h"

namespace iv {

int FileListModel::find(const QString& name) const {
    for (int row = 0; row < rowCount(); ++row) {
        if (m_entries.at(row).name == name) {
            return row;
        }
    }
    return -1;
}

void FileListModel::reset(const QString& directory, const QList<FileEntry>& entries) {
    beginResetModel();
    m_directory = QDir(directory);
    m_entries = entries;
    m_selected.assign(static_cast<std::size_t>(entries.size()), 0);
    endResetModel();
}

bool FileListModel::update(const QList<FileEntry>& entries) {
    if (m_entries.isEmpty()) {
        if (entries.isEmpty()) {
            return false;
        }
        reset(m_directory.path(), entries);
        return true;
    }
    QSet<QString> incoming;
    for (const FileEntry& entry : entries) {
        incoming.insert(entry.name);
    }
    bool changed = false;
    for (int row = rowCount() - 1; row >= 0; --row) {
        if (!incoming.contains(m_entries.at(row).name)) {
            beginRemoveRows({}, row, row);
            m_entries.removeAt(row);
            m_selected.erase(m_selected.begin() + row);
            endRemoveRows();
            changed = true;
        }
    }

    QSet<QString> present;
    for (const FileEntry& entry : std::as_const(m_entries)) {
        present.insert(entry.name);
    }
    int kept = 0;
    bool sameOrder = true;
    for (const FileEntry& entry : entries) {
        if (present.contains(entry.name) && m_entries.at(kept++).name != entry.name) {
            sameOrder = false;
            break;
        }
    }
    if (!sameOrder) {
        replaceReordered(entries);
        return true;
    }

    for (int row = 0; row < entries.size(); ++row) {
        const FileEntry& entry = entries.at(row);
        if (row >= rowCount() || m_entries.at(row).name != entry.name) {
            beginInsertRows({}, row, row);
            m_entries.insert(row, entry);
            m_selected.insert(m_selected.begin() + row, 0);
            endInsertRows();
            changed = true;
        } else if (m_entries.at(row).modified != entry.modified || m_entries.at(row).size != entry.size) {
            m_entries[row] = entry;
            emit dataChanged(index(row), index(row), {ThumbnailRole});
            changed = true;
        }
    }
    return changed;
}

void FileListModel::thumbnailsChanged() {
    if (rowCount() > 0) {
        emit dataChanged(index(0), index(rowCount() - 1), {ThumbnailRole});
    }
}

bool FileListModel::setSelected(const std::vector<char>& selected) {
    int first = -1;
    int last = -1;
    for (int row = 0; row < rowCount(); ++row) {
        const auto i = static_cast<std::size_t>(row);
        if ((selected[i] != 0) != (m_selected[i] != 0)) {
            first = first < 0 ? row : first;
            last = row;
        }
    }
    if (first < 0) {
        return false;
    }
    m_selected = selected;
    emit dataChanged(index(first), index(last), {SelectedRole});
    return true;
}

int FileListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant FileListModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid)) {
        return {};
    }
    const FileEntry& entry = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return entry.name;
    case PathRole:
        return m_directory.filePath(entry.name);
    case ThumbnailRole:
        return ThumbnailProvider::urlForPath(m_directory.filePath(entry.name), entry.modified);
    case SelectedRole:
        return m_selected[static_cast<std::size_t>(index.row())] != 0;
    case MarkedRole:
        return m_marks.value(entry.name);
    default:
        return {};
    }
}

QHash<int, QByteArray> FileListModel::roleNames() const {
    return {{NameRole, "name"}, {PathRole, "path"}, {ThumbnailRole, "thumbnail"}, {SelectedRole, "selected"}, {MarkedRole, "marked"}};
}

void FileListModel::setMarks(const QHash<QString, QString>& marks) {
    if (marks != m_marks) {
        m_marks = marks;
        if (rowCount() > 0) {
            emit dataChanged(index(0), index(rowCount() - 1), {MarkedRole});
        }
    }
}

void FileListModel::replaceReordered(const QList<FileEntry>& entries) {
    QSet<QString> selectedNames;
    for (int row = 0; row < rowCount(); ++row) {
        if (m_selected[static_cast<std::size_t>(row)] != 0) {
            selectedNames.insert(m_entries.at(row).name);
        }
    }
    reset(m_directory.path(), entries);
    for (int row = 0; row < rowCount(); ++row) {
        m_selected[static_cast<std::size_t>(row)] = selectedNames.contains(m_entries.at(row).name) ? 1 : 0;
    }
}

}  // namespace iv
