# The look of the controls: a Qt Quick Controls style of our own, which takes what it does not
# redo from Fusion. Pages keep writing Button, CheckBox and the like; main() and the tests choose
# this style by its name, ImageViewerStyle.
qt_add_library(iv_style STATIC)

qt_add_qml_module(iv_style
    URI ImageViewerStyle
    VERSION 6.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/ImageViewerStyle
    IMPORTS
        QtQuick.Controls.Fusion/auto
    DEPENDENCIES
        ImageViewer
    QML_FILES
        Button.qml
        CheckBox.qml
        ComboBox.qml
        Menu.qml
        MenuItem.qml
        MenuSeparator.qml
        RadioButton.qml
        Slider.qml
        SpinBox.qml
        SplitView.qml
        TextField.qml
)

target_link_libraries(iv_style PUBLIC Qt6::Quick Qt6::QuickControls2)
