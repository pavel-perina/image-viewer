// Finding names by a few typed letters, fzf style (core/fuzzy_match.h). The names are made up,
// shaped like the font families a system lists. The truth is the order of the results: which
// names fit, and which come first, as someone typing would expect.

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "core/fuzzy_match.h"

using namespace iv;

namespace {

// The names the query fits, best first.
std::vector<std::string> rank(std::string_view query, const std::vector<std::string_view>& names) {
    std::vector<std::string> out;
    for (std::size_t index : fuzzyRank(query, names)) {
        out.emplace_back(names[index]);
    }
    return out;
}

}  // namespace

// Why: the rule itself. The letters must all be there and in order, apart or together, and
// case does not matter.
TEST(FuzzyMatch, LettersMustAppearInOrderInAnyCase) {
    EXPECT_TRUE(fuzzyMatch("iosch", "Iosevka Charon"));
    EXPECT_TRUE(fuzzyMatch("PLSA", "IBM Plex Sans"));
    EXPECT_FALSE(fuzzyMatch("hcso", "Iosevka Charon")) << "out of order";
    EXPECT_FALSE(fuzzyMatch("iosx", "Iosevka Charon")) << "a letter missing";
    EXPECT_FALSE(fuzzyMatch("charonx", "Charon"));
}

// Why: what is shown highlighted. The letters are placed where they fit best, here at the
// starts of the words rather than the first letters of the same kind.
TEST(FuzzyMatch, TheLettersArePlacedAtTheStartsOfWords) {
    const auto match = fuzzyMatch("ps", "IBM Plex Sans");
    ASSERT_TRUE(match);
    EXPECT_EQ(match->positions, (std::vector<std::size_t>{4, 9}));
    EXPECT_EQ(match->runs(), (std::vector<FuzzyRun>{{4, 1}, {9, 1}}));
}

// Why: letters matched together are one run, for marking them as a piece.
TEST(FuzzyMatch, NeighboursMatchedAreOneRun) {
    const auto match = fuzzyMatch("libmo", "Liberation Mono");
    ASSERT_TRUE(match);
    EXPECT_EQ(match->runs(), (std::vector<FuzzyRun>{{0, 3}, {11, 2}}));
}

// Why: the ranking that makes it worth having. Letters together beat letters apart, and the
// start of a word beats the middle of one.
TEST(FuzzyMatch, LettersTogetherAndAtWordStartsRankFirst) {
    EXPECT_EQ(rank("sans", {"Sanskrit Unicode", "Open Sans", "Saint Andrews Script"}),
              (std::vector<std::string>{"Open Sans", "Sanskrit Unicode", "Saint Andrews Script"}))
            << "a whole word, then a word's start, then letters apart";
    EXPECT_EQ(rank("mono", {"Harmonious", "Liberation Mono"}), (std::vector<std::string>{"Liberation Mono", "Harmonious"}));
    EXPECT_EQ(rank("ps", {"Popsicle", "PT Sans"}), (std::vector<std::string>{"PT Sans", "Popsicle"}));
}

// Why: among equal fits the shorter name is likely the one meant, and after that the order
// given (alphabetical, as the list comes) stays.
TEST(FuzzyMatch, TiesGoToTheShorterNameThenTheOrderGiven) {
    EXPECT_EQ(rank("iosevka", {"Iosevka Term", "Iosevka", "Iosevka Fixed"}),
              (std::vector<std::string>{"Iosevka", "Iosevka Term", "Iosevka Fixed"}));
    EXPECT_EQ(rank("noto", {"Noto Sans B", "Noto Sans A"}), (std::vector<std::string>{"Noto Sans B", "Noto Sans A"}));
}

// Why: words of the query, as in fzf, each match anywhere, so "term ios" finds Iosevka Term.
TEST(FuzzyMatch, EachWordOfTheQueryMatchesAnywhere) {
    EXPECT_EQ(rank("term ios", {"Iosevka", "Iosevka Term", "Terminus"}), (std::vector<std::string>{"Iosevka Term"}));
    EXPECT_FALSE(fuzzyMatch("ios slab", "Iosevka Term"));
}

// Why: nothing typed yet shows the whole list as it was given.
TEST(FuzzyMatch, AnEmptyQueryKeepsEveryNameInOrder) {
    EXPECT_EQ(rank("", {"b", "a", "ccc"}), (std::vector<std::string>{"b", "a", "ccc"}));
    EXPECT_EQ(rank("  ", {"b", "a"}), (std::vector<std::string>{"b", "a"}));
}

// Why: names are not all ASCII. Letters beyond ASCII match themselves, and positions count
// characters, not bytes.
TEST(FuzzyMatch, NamesBeyondAsciiAreCountedByCharacter) {
    const auto match = fuzzyMatch("ž", "Písmo ž");
    ASSERT_TRUE(match);
    EXPECT_EQ(match->positions, (std::vector<std::size_t>{6}));
}

// Why: fzf's extended search, for lists of files: a quote asks for the letters together, ^ and
// $ for the start and end (the type of file), and ! leaves names out. Input: names shaped like
// photo files. The truth is which names fit; fzf's documentation gives the rules.
TEST(FuzzyMatch, MarkedWordsAskForTheLettersAsTheyAre) {
    const std::vector<std::string_view> files{"IMG_0012.jpg", "img_0013.png", "beach.jpg", "beach-raw.tif", "jpeg.png"};
    using Names = std::vector<std::string>;
    EXPECT_EQ(rank("'each", files).size(), 2u);
    EXPECT_EQ(rank("'ejg", files), Names{}) << "apart is not together";
    EXPECT_EQ(rank("^img", files).size(), 2u) << "in any case";
    EXPECT_EQ(rank("^jpeg", files), Names{"jpeg.png"});
    EXPECT_EQ(rank(".jpg$", files).size(), 2u);
    EXPECT_EQ(rank("^beach.jpg$", files), Names{"beach.jpg"});
    EXPECT_EQ(rank("beach !raw", files), Names{"beach.jpg"});
    EXPECT_EQ(rank("!^img !.png$", files), (Names{"beach.jpg", "beach-raw.tif"}));
}

// Why: the matched letters are marked, so a marked word marks where it was found, and a word
// left out marks nothing. Input: "^be" and "raw$ !x" against one name. The truth is the positions.
TEST(FuzzyMatch, MarkedWordsMarkWhereTheyWereFound) {
    const auto start = fuzzyMatch("^be", "beach-be.jpg");
    ASSERT_TRUE(start);
    EXPECT_EQ(start->positions, (std::vector<std::size_t>{0, 1}));
    const auto end = fuzzyMatch("raw$ !x", "beach-raw");
    ASSERT_TRUE(end);
    EXPECT_EQ(end->positions, (std::vector<std::size_t>{6, 7, 8}));
}

// Why: while a mark is being typed, before its letters, the list must not empty. Input: queries
// that are nothing but marks. The truth is that every name still fits.
TEST(FuzzyMatch, AMarkAloneAsksForNothingYet) {
    for (const char* query : {"'", "^", "!", "$", "!^", "^$"}) {
        EXPECT_EQ(rank(query, {"a", "b"}).size(), 2u) << query;
    }
}
