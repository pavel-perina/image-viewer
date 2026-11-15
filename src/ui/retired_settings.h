#pragma once

class QSettings;

namespace iv {

// Removes the settings the program no longer has, so that a value saved by an older version
// does not linger in the file, nor come back if a setting of that name returns one day.
// Call once at start, after the settings have their home.
void forgetRetiredSettings(QSettings& settings);

}  // namespace iv
