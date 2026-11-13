#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace iv {

// A list of names narrowed and ranked by what is typed, fzf style (core/fuzzy_match.h), for a
// picker such as SearchCombo. Nothing in it is about fonts or files.
class FuzzyListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList items READ items WRITE setItems NOTIFY itemsChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        TextRole = Qt::UserRole + 1,
        // The text as rich text, escaped, with the letters matched bold and underlined.
        MarkedRole,
    };

    explicit FuzzyListModel(QObject* parent = nullptr);

    QStringList items() const { return m_items; }
    void setItems(const QStringList& items);
    QString filter() const { return m_filter; }
    void setFilter(const QString& filter);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // The text at a row of what is shown; empty past the end.
    Q_INVOKABLE QString textAt(int row) const;
    // The row a text is at in what is shown, or -1.
    Q_INVOKABLE int rowOf(const QString& text) const;

signals:
    void itemsChanged();
    void filterChanged();
    void countChanged();

private:
    void update();

    QStringList m_items;
    QString m_filter;
    QList<int> m_rows;        // indices into m_items, best first
    QStringList m_marked;     // per row
};

}  // namespace iv
