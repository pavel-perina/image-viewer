#include "ui/fuzzy_marks.h"

#include <string>

namespace iv {

QString markedText(const QString& text, const std::vector<FuzzyRun>& runs) {
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

FilteredNames filterNames(const QString& query, const QStringList& names) {
    FilteredNames out;
    const std::string typed = query.toStdString();
    const bool filtering = query.trimmed().size() > 0;
    int bestScore = 0;
    for (qsizetype row = 0; row < names.size(); ++row) {
        if (!filtering) {
            out.rows.push_back(static_cast<int>(row));
            out.marked.append(QString());
            continue;
        }
        const auto match = fuzzyMatch(typed, names[row].toStdString());
        if (!match) {
            continue;
        }
        // The first of equals: the list's own order breaks ties.
        if (out.best < 0 || match->score > bestScore) {
            out.best = static_cast<int>(out.rows.size());
            bestScore = match->score;
        }
        out.rows.push_back(static_cast<int>(row));
        out.marked.append(markedText(names[row], match->runs()));
    }
    return out;
}

}  // namespace iv
