#pragma once

#include <QMetaType>

#include <memory>

#include "core/image_source.h"

// An edited page as QML passes it from the view to saving, without looking inside.
Q_DECLARE_METATYPE(std::shared_ptr<iv::ImageSource>)
