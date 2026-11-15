#include "ui/retired_settings.h"

#include <QSettings>

namespace iv {

void forgetRetiredSettings(QSettings& settings) {
    static constexpr const char* retired[] = {
        "InfoPanel/sharpResampling",  // sharp resampling is always on
    };
    for (const char* key : retired) {
        settings.remove(QLatin1StringView(key));
    }
}

}  // namespace iv
