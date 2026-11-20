#include "ui/slider_scale.h"

#include <algorithm>
#include <cmath>

namespace iv {

SliderScale::SliderScale(QObject* parent) : QObject(parent) {}

void SliderScale::setKind(Kind kind) {
    if (kind != m_kind) {
        m_kind = kind;
        emit changed();
    }
}

void SliderScale::setFrom(double from) {
    if (from != m_from) {
        m_from = from;
        emit changed();
    }
}

void SliderScale::setTo(double to) {
    if (to != m_to) {
        m_to = to;
        emit changed();
    }
}

void SliderScale::setStep(double step) {
    if (step != m_step) {
        m_step = step;
        emit changed();
    }
}

void SliderScale::setValue(double value) {
    if (value != m_value) {
        m_value = value;
        emit changed();
    }
}

double SliderScale::warp(double value) const {
    switch (m_kind) {
    case Kind::Linear: return value;
    case Kind::Reciprocal: return 1 / value;
    case Kind::Logarithmic: return std::log(value);
    }
    return value;
}

double SliderScale::unwarp(double warped) const {
    switch (m_kind) {
    case Kind::Linear: return warped;
    case Kind::Reciprocal: return 1 / warped;
    case Kind::Logarithmic: return std::exp(warped);
    }
    return warped;
}

double SliderScale::positionOf(double value) const {
    const double low = warp(m_from);
    const double high = warp(m_to);
    if (high == low || !std::isfinite(low) || !std::isfinite(high)) {
        return 0;
    }
    const double clamped = std::clamp(value, std::min(m_from, m_to), std::max(m_from, m_to));
    return std::clamp((warp(clamped) - low) / (high - low), 0.0, 1.0);
}

double SliderScale::valueAt(double position) const {
    const double low = warp(m_from);
    const double high = warp(m_to);
    double value = unwarp(low + std::clamp(position, 0.0, 1.0) * (high - low));
    if (m_step > 0) {
        value = std::round(value / m_step) * m_step;
    }
    return std::clamp(value, std::min(m_from, m_to), std::max(m_from, m_to));
}

}  // namespace iv
