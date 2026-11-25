#pragma once

#include <QAbstractListModel>
#include <QDir>
#include <QHash>
#include <QList>
#include <QString>

#include <vector>

namespace iv {

// An image file as a scan of its directory found it.
struct FileEntry {
    QString name;
    qint64 size = 0;
    qint64 modified = 0;
};

// The images of one directory for views, in the order given, and which of them are selected.
class FileListModel final : public QAbstractListModel {
public:
    // MarkedRole: the name as styled text with what a filter matched marked; empty without one.
    enum Role { NameRole = Qt::UserRole + 1, PathRole, ThumbnailRole, SelectedRole, MarkedRole };

    using QAbstractListModel::QAbstractListModel;

    const QString& name(int row) const { return m_entries.at(row).name; }
    qint64 size(int row) const { return m_entries.at(row).size; }

    int find(const QString& name) const;  // the row, or -1
    void reset(const QString& directory, const QList<FileEntry>& entries);  // clears the selection

    // Takes a new scan of the same directory, keeping the selection by name. Only the rows
    // that differ change, so that a view stays where it was scrolled to. Returns whether any did.
    bool update(const QList<FileEntry>& entries);

    const std::vector<char>& selected() const { return m_selected; }

    void thumbnailsChanged();

    // The names with what a filter matched marked, by name; empty without a filter.
    void setMarks(const QHash<QString, QString>& marks);  // they are to be made again, as when the colour settings change

    // Replaces the selection, telling views only about the rows that differ.
    bool setSelected(const std::vector<char>& selected);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    void replaceReordered(const QList<FileEntry>& entries);

    QDir m_directory;
    QList<FileEntry> m_entries;
    std::vector<char> m_selected;
    QHash<QString, QString> m_marks;
};


}  // namespace iv
