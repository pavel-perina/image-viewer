#include "core/fuzzy_match.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <string>

namespace iv {

namespace {

// The numbers fzf uses, so that the ranking feels the same.
constexpr int matchScore = 16;
constexpr int gapStart = -3;
constexpr int gapExtension = -1;
constexpr int boundaryBonus = matchScore / 2;
constexpr int camelBonus = boundaryBonus - 1;
constexpr int runBonus = -(gapStart + gapExtension);
constexpr int firstLetterFactor = 2;
constexpr int none = std::numeric_limits<int>::min() / 2;

// UTF-8 to code points; a broken byte stands for itself.
std::u32string decode(std::string_view text) {
    std::u32string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const int extra = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
        if (extra == 0 || i + extra >= text.size()) {
            out.push_back(lead);
            ++i;
            continue;
        }
        char32_t code = lead & (0x3F >> extra);
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            ok = ok && (next & 0xC0) == 0x80;
            code = (code << 6) | (next & 0x3F);
        }
        if (!ok) {
            out.push_back(lead);
            ++i;
            continue;
        }
        out.push_back(code);
        i += extra + 1;
    }
    return out;
}

char32_t fold(char32_t c) {
    return c >= U'A' && c <= U'Z' ? c - U'A' + U'a' : c;
}

bool isUpper(char32_t c) { return c >= U'A' && c <= U'Z'; }
bool isLower(char32_t c) { return c >= U'a' && c <= U'z'; }
bool isDigit(char32_t c) { return c >= U'0' && c <= U'9'; }
bool isDelimiter(char32_t c) {
    return c == U' ' || c == U'\t' || c == U'-' || c == U'_' || c == U'.' || c == U'/' || c == U'\\' || c == U',' ||
           c == U'(' || c == U')';
}

// What a letter of the name earns for starting a word.
int bonusAt(const std::u32string& name, std::size_t at) {
    if (at == 0) {
        return boundaryBonus;
    }
    const char32_t before = name[at - 1];
    const char32_t here = name[at];
    if (isDelimiter(before)) {
        return boundaryBonus;
    }
    if ((isLower(before) && isUpper(here)) || (!isDigit(before) && isDigit(here))) {
        return camelBonus;
    }
    return 0;
}

// One word of the query against the whole name: the best placing, by dynamic programming over
// (letter of the word, character of the name it is matched to).
std::optional<FuzzyMatch> matchWord(const std::u32string& word, const std::u32string& name) {
    const std::size_t m = word.size();
    const std::size_t n = name.size();
    if (m == 0) {
        return FuzzyMatch{};
    }
    if (m > n) {
        return std::nullopt;
    }
    std::vector<int> bonus(n);
    for (std::size_t j = 0; j < n; ++j) {
        bonus[j] = bonusAt(name, j);
    }
    // score[i][j]: the best with letter i matched at j; from[i][j]: where letter i-1 was.
    // run[i][j]: the bonus of the letter that began the run ending at j, which the run keeps.
    std::vector<int> score(m * n, none);
    std::vector<std::size_t> from(m * n, 0);
    std::vector<int> run(m * n, 0);
    for (std::size_t i = 0; i < m; ++i) {
        for (std::size_t j = i; j < n; ++j) {
            if (fold(name[j]) != fold(word[i])) {
                continue;
            }
            if (i == 0) {
                score[j] = matchScore + bonus[j] * firstLetterFactor;
                run[j] = bonus[j];
                continue;
            }
            int best = none;
            for (std::size_t k = i - 1; k < j; ++k) {
                const int before = score[(i - 1) * n + k];
                if (before == none) {
                    continue;
                }
                int value;
                int keep;
                if (k + 1 == j) {
                    keep = std::max({run[(i - 1) * n + k], bonus[j], runBonus});
                    value = before + matchScore + keep;
                } else {
                    const int gap = static_cast<int>(j - k - 1);
                    keep = bonus[j];
                    value = before + gapStart + gapExtension * (gap - 1) + matchScore + bonus[j];
                }
                if (value > best) {
                    best = value;
                    from[i * n + j] = k;
                    run[i * n + j] = keep;
                }
            }
            score[i * n + j] = best;
        }
    }
    std::size_t end = n;
    int best = none;
    for (std::size_t j = m - 1; j < n; ++j) {
        if (score[(m - 1) * n + j] > best) {
            best = score[(m - 1) * n + j];
            end = j;
        }
    }
    if (best == none) {
        return std::nullopt;
    }
    FuzzyMatch match;
    match.score = best;
    match.positions.resize(m);
    for (std::size_t i = m; i-- > 0;) {
        match.positions[i] = end;
        end = from[i * n + end];
    }
    return match;
}

// The word as it is, letters together, at one place of the name: scored as a run of matched
// letters that starts there.
FuzzyMatch scoreRun(const std::u32string& name, std::size_t at, std::size_t length) {
    FuzzyMatch match;
    int keep = 0;
    for (std::size_t k = 0; k < length; ++k) {
        const int bonus = bonusAt(name, at + k);
        if (k == 0) {
            match.score = matchScore + bonus * firstLetterFactor;
            keep = bonus;
        } else {
            keep = std::max({keep, bonus, runBonus});
            match.score += matchScore + keep;
        }
        match.positions.push_back(at + k);
    }
    return match;
}

bool sameAt(const std::u32string& name, std::size_t at, const std::u32string& word) {
    if (at + word.size() > name.size()) {
        return false;
    }
    for (std::size_t k = 0; k < word.size(); ++k) {
        if (fold(name[at + k]) != fold(word[k])) {
            return false;
        }
    }
    return true;
}

// The word as it is, where the marks allow: at the start, at the end, or the best place anywhere.
std::optional<FuzzyMatch> matchExact(const std::u32string& word, const std::u32string& name, bool prefix, bool suffix) {
    if (word.size() > name.size()) {
        return std::nullopt;
    }
    const std::size_t last = name.size() - word.size();
    std::optional<FuzzyMatch> best;
    for (std::size_t at = prefix ? 0 : (suffix ? last : 0); at <= (prefix ? 0 : last); ++at) {
        if ((suffix && at != last) || !sameAt(name, at, word)) {
            continue;
        }
        FuzzyMatch here = scoreRun(name, at, word.size());
        if (!best || here.score > best->score) {
            best = std::move(here);
        }
    }
    return best;
}

// One word of the query with its marks; none if the name does not fit it.
std::optional<FuzzyMatch> matchTerm(std::u32string word, const std::u32string& name) {
    const bool negated = !word.empty() && word.front() == U'!';
    if (negated) {
        word.erase(0, 1);
    }
    bool exact = negated;
    bool prefix = false;
    bool suffix = false;
    if (!word.empty() && word.front() == U'\'') {
        exact = true;
        word.erase(0, 1);
    } else if (!word.empty() && word.front() == U'^') {
        exact = prefix = true;
        word.erase(0, 1);
    }
    if (!word.empty() && word.back() == U'$') {
        exact = suffix = true;
        word.pop_back();
    }
    if (word.empty()) {
        return FuzzyMatch{};
    }
    std::optional<FuzzyMatch> found = exact ? matchExact(word, name, prefix, suffix) : matchWord(word, name);
    if (negated) {
        return found ? std::nullopt : std::optional<FuzzyMatch>(FuzzyMatch{});
    }
    return found;
}

}  // namespace

std::vector<FuzzyRun> FuzzyMatch::runs() const {
    std::vector<FuzzyRun> out;
    for (const std::size_t at : positions) {
        if (!out.empty() && out.back().start + out.back().length == at) {
            ++out.back().length;
        } else {
            out.push_back({at, 1});
        }
    }
    return out;
}

std::optional<FuzzyMatch> fuzzyMatch(std::string_view query, std::string_view name) {
    const std::u32string text = decode(name);
    const std::u32string all = decode(query);
    FuzzyMatch total;
    std::size_t start = 0;
    while (start < all.size()) {
        if (all[start] == U' ') {
            ++start;
            continue;
        }
        std::size_t stop = all.find(U' ', start);
        if (stop == std::u32string::npos) {
            stop = all.size();
        }
        const auto word = matchTerm(all.substr(start, stop - start), text);
        if (!word) {
            return std::nullopt;
        }
        total.score += word->score;
        total.positions.insert(total.positions.end(), word->positions.begin(), word->positions.end());
        start = stop;
    }
    std::sort(total.positions.begin(), total.positions.end());
    total.positions.erase(std::unique(total.positions.begin(), total.positions.end()), total.positions.end());
    return total;
}

std::vector<std::size_t> fuzzyRank(std::string_view query, const std::vector<std::string_view>& names) {
    struct Ranked {
        std::size_t index;
        int score;
        std::size_t length;
    };
    if (query.find_first_not_of(' ') == std::string_view::npos) {
        std::vector<std::size_t> all(names.size());
        std::iota(all.begin(), all.end(), std::size_t{0});
        return all;
    }
    std::vector<Ranked> ranked;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (const auto match = fuzzyMatch(query, names[i])) {
            ranked.push_back({i, match->score, decode(names[i]).size()});
        }
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.length < b.length;
    });
    std::vector<std::size_t> out;
    out.reserve(ranked.size());
    for (const Ranked& r : ranked) {
        out.push_back(r.index);
    }
    return out;
}

}  // namespace iv
