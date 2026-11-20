import QtQuick
import QtQuick.Controls

// A spin box for numbers with decimals, which Qt's SpinBox, counting in whole numbers, does not
// take: it counts in hundredths (or whatever `decimals` says) and shows the point. Not named
// DoubleSpinBox, which newer Qt has and would clash with.
SpinBox {
    id: control

    property int decimals: 2
    property real realValue: 0
    property real realFrom: 0
    property real realTo: 100
    property real realStepSize: 0.1
    readonly property real factor: Math.pow(10, decimals)
    readonly property bool pendingEdit: contentItem && contentItem.activeFocus && contentItem.acceptableInput
                                       && valueFromText(contentItem.text, locale) !== value

    // The value changed by the person, not by a binding. The owner sets realValue from it; the
    // box does not set its own, which would cut realValue off from what it is bound to.
    signal realValueModified(real value)

    from: Math.round(realFrom * factor)
    to: Math.round(realTo * factor)
    stepSize: Math.max(1, Math.round(realStepSize * factor))
    value: Math.round(realValue * factor)
    editable: true

    validator: DoubleValidator {
        bottom: Math.min(control.realFrom, control.realTo)
        top: Math.max(control.realFrom, control.realTo)
        decimals: control.decimals
        notation: DoubleValidator.StandardNotation
        locale: control.locale.name
    }

    textFromValue: (value, locale) => Number(value / factor).toLocaleString(locale, "f", decimals)
    valueFromText: (text, locale) => Math.round(Number.fromLocaleString(locale, text) * factor)

    onValueModified: realValueModified(value / factor)
}
