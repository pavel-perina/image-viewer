#pragma once

#include <QMetaType>

#include <memory>

#include "core/histogram.h"

// How a histogram travels through QML: as an opaque QVariant from a producer to HistogramView.
Q_DECLARE_METATYPE(std::shared_ptr<const iv::Histogram>)
