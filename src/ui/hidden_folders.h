#pragma once

#include <QDir>

namespace iv {

// What QDir is asked for to list the subfolders of a folder: hidden ones too only when they are
// to be shown (names starting with a dot on Linux, the hidden attribute on Windows).
inline QDir::Filters subfolderFilters(bool showHidden) {
    QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot;
    if (showHidden) {
        filters |= QDir::Hidden;
    }
    return filters;
}

}  // namespace iv
