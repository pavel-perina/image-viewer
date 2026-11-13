#pragma once

// Finding names by a few typed letters, the way fzf does: the letters must appear in the name
// in order, not together, and the names are ranked by how well they fit. Standalone on
// purpose, with nothing of the rest of the program, so that it can be lifted out.
//
// How a fit is scored, per name and word of the query:
//  * every letter matched scores; one that starts a word (the name's first letter, or after a
//    space, '-', '_', '.' or '/', or a capital or digit inside a word) scores more, and the
//    query's first letter counts that bonus twice;
//  * a letter right after the one matched before scores at least a run bonus;
//  * letters skipped in between cost, the first more than the rest.
// The best placing of the letters is the one taken. Case is ignored for ASCII letters; other
// characters must be the same.

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace iv {

// A stretch of characters matched together, for marking them.
struct FuzzyRun {
    std::size_t start = 0;
    std::size_t length = 0;
    bool operator==(const FuzzyRun&) const = default;
};

struct FuzzyMatch {
    int score = 0;
    // The characters matched, as indices of code points of the name, in order.
    std::vector<std::size_t> positions;
    // The same, as runs of neighbours: "ps" in "IBM Plex Sans" is {4, 1} and {9, 1}.
    std::vector<FuzzyRun> runs() const;
};

// How a query fits a name, both UTF-8; none if it does not. A query of several words,
// separated by spaces, fits when every word does, anywhere; their scores add. An empty query
// fits every name with no score.
std::optional<FuzzyMatch> fuzzyMatch(std::string_view query, std::string_view name);

// The indices of the names the query fits, best first: by score, then the shorter name, then
// the order given. An empty query gives them all, in the order given.
std::vector<std::size_t> fuzzyRank(std::string_view query, const std::vector<std::string_view>& names);

}  // namespace iv
