#include "ui/fuzzy_list_model.h"

#include <string>
#include <string_view>
#include <vector>

#include "core/fuzzy_match.h"

namespace iv {

namespace {

QString marked(const QString& text, const std::vector<FuzzyRun>& runs) {
    const QList<uint> codes = text.toUcs4();
    const auto piece = [&](std::size_t from, std::size_t to) {
        QString part;
        for (std::size_t i = from; i < to && i < static_cast<std::size_t>(codes.size()); ++i) {
            const auto code = static_cast<char32_t>(codes[static_cast<qsizetype>(i)]);
            part += QString::fromUcs4(&code, 1);
        }
        return part.toHtmlEscaped();
    };
    QString out;
    std::size_t at = 0;
    for (const FuzzyRun& run : runs) {
        out += piece(at, run.start);
        out += QStringLiteral("<b><u>") + piece(run.start, run.start + run.length) + QStringLiteral("</u></b>");
        at = run.start + run.length;
    }
    return out + piece(at, static_cast<std::size_t>(codes.size()));
}

}  // namespace

FuzzyListModel::FuzzyListModel(QObject* parent) : QAbstractListModel(parent) {}

void FuzzyListModel::setItems(const QStringList& items) {
    if (items != m_items) {
        m_items = items;
        update();
        emit itemsChanged();
    }
}

void FuzzyListModel::setFilter(const QString& filter) {
    if (filter != m_filter) {
        m_filter = filter;
        update();
        emit filterChanged();
    }
}

void FuzzyListModel::update() {
    const int before = static_cast<int>(m_rows.size());
    beginResetModel();
    const std::string query = m_filter.toStdString();
    std::vector<std::string> utf8;
    utf8.reserve(static_cast<std::size_t>(m_items.size()));
    for (const QString& item : m_items) {
        utf8.push_back(item.toStdString());
    }
    const std::vector<std::string_view> views(utf8.begin(), utf8.end());
    m_rows.clear();
    m_marked.clear();
    for (const std::size_t index : fuzzyRank(query, views)) {
        m_rows.append(static_cast<int>(index));
        const auto match = fuzzyMatch(query, views[index]);
        m_marked.append(marked(m_items[static_cast<qsizetype>(index)], match ? match->runs() : std::vector<FuzzyRun>{}));
    }
    endResetModel();
    if (m_rows.size() != before) {
        emit countChanged();
    }
}

int FuzzyListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant FuzzyListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }
    switch (role) {
    case TextRole:
    case Qt::DisplayRole:
        return m_items[m_rows[index.row()]];
    case MarkedRole:
        return m_marked[index.row()];
    default:
        return {};
    }
}

QHash<int, QByteArray> FuzzyListModel::roleNames() const {
    return {{TextRole, "text"}, {MarkedRole, "marked"}};
}

QString FuzzyListModel::textAt(int row) const {
    return row >= 0 && row < m_rows.size() ? m_items[m_rows[row]] : QString();
}

int FuzzyListModel::rowOf(const QString& text) const {
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        if (m_items[m_rows[row]] == text) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

}  // namespace iv
