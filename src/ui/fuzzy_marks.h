#pragma once

#include <QString>
#include <QStringList>

#include <vector>

#include "core/fuzzy_match.h"

namespace iv {

// The text as styled text, with the stretches a fuzzy query matched bold and underlined, and
// the rest escaped.
QString markedText(const QString& text, const std::vector<FuzzyRun>& runs);

// What a typed filter keeps of a list of names, for lists that keep their own order, such as
// files sorted by date: the rows kept, in order, each name with what matched marked, and which
// of them fits best, to be made current. Without a filter every row is kept, unmarked.
struct FilteredNames {
    std::vector<int> rows;
    QStringList marked;  // one per kept row; empty strings without a filter
    int best = -1;       // an index into rows, or -1 without a filter or with nothing kept
};
FilteredNames filterNames(const QString& query, const QStringList& names);

}  // namespace iv
