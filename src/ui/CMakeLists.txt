qt_add_library(iv_ui STATIC
    browse_context.cpp
    browse_context.h
    color_settings.cpp
    color_settings.h
    directory_tree_model.cpp
    directory_tree_model.h
    file_operations.cpp
    file_operations.h
    histogram_handle.h
    histogram_view.cpp
    histogram_view.h
    image_view.cpp
    image_view.h
    thumbnail_provider.cpp
    thumbnail_provider.h
)

qt_add_qml_module(iv_ui
    URI ImageViewer
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/ImageViewer
    QML_FILES
        qml/BrowserPage.qml
        qml/FileActions.qml
        qml/InfoPanel.qml
        qml/Main.qml
        qml/ResampledView.qml
        qml/ViewerPage.qml
)

qt_add_shaders(iv_ui "iv_shaders"
    PREFIX "/"
    FILES
        shaders/mks_horizontal.frag
        shaders/mks_vertical.frag
)

target_include_directories(iv_ui
    PUBLIC ${CMAKE_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(iv_ui
    PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
    PRIVATE iv_core
)
