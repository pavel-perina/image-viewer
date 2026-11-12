qt_add_library(iv_ui STATIC
    app_info.cpp
    app_info.h
    browse_context.cpp
    browse_context.h
    browse_context_files.cpp
    browse_context_p.h
    color_lut.cpp
    color_lut.h
    color_settings.cpp
    color_settings.h
    crop_overlay.cpp
    crop_overlay.h
    straighten_overlay.cpp
    straighten_overlay.h
    directory_tree_model.cpp
    directory_tree_model.h
    edited_image.h
    file_clipboard.cpp
    file_clipboard.h
    file_job_runner.cpp
    file_job_runner.h
    file_list_model.cpp
    file_list_model.h
    file_operations.cpp
    file_operations.h
    folder_listing.cpp
    folder_listing.h
    folder_lists.cpp
    folder_lists.h
    key_hold.cpp
    key_hold.h
    key_names.cpp
    key_names.h
    histogram_handle.h
    histogram_view.cpp
    histogram_view.h
    image_overlay.cpp
    image_overlay.h
    overlay_style.h
    probe_overlay.cpp
    probe_overlay.h
    image_view.cpp
    image_view.h
    subfolder_list.cpp
    subfolder_list.h
    thumbnail_provider.cpp
    thumbnail_provider.h
    thumbnail_store.cpp
    thumbnail_store.h
)

set_source_files_properties(qml/Theme.qml qml/Screens.qml qml/ViewSettings.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(iv_ui
    URI ImageViewer
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/ImageViewer
    QML_FILES
        qml/AboutDialog.qml
        qml/AdjustPanel.qml
        qml/AppDialog.qml
        qml/AppMenu.qml
        qml/AppMenuItem.qml
        qml/AppScrollBar.qml
        qml/BarButton.qml
        qml/BrowserPage.qml
        qml/Caret.qml
        qml/ColorPreviewEffect.qml
        qml/CompareSplitter.qml
        qml/CropPanel.qml
        qml/FileActions.qml
        qml/FolderButton.qml
        qml/FileChooser.qml
        qml/FileListView.qml
        qml/FolderTree.qml
        qml/ImageScrollBars.qml
        qml/ImageSurface.qml
        qml/JpegMenu.qml
        qml/LevelsControl.qml
        qml/LockButton.qml
        qml/InfoPanel.qml
        qml/Main.qml
        qml/MessageBox.qml
        qml/PathBar.qml
        qml/PreviewPanel.qml
        qml/ProbePanel.qml
        qml/ResizeDialog.qml
        qml/StraightenPanel.qml
        qml/ResampledView.qml
        qml/SaveOptionsDialog.qml
        qml/Theme.qml
        qml/Screens.qml
        qml/ViewSettings.qml
        qml/ViewerPage.qml
)

qt_add_shaders(iv_ui "iv_shaders"
    PREFIX "/"
    FILES
        shaders/mks_horizontal.frag
        shaders/mks_vertical.frag
)

# texelFetch needs GLSL ES 3 or GLSL 1.30.
qt_add_shaders(iv_ui "iv_color_shaders"
    PREFIX "/"
    GLSL "300es,330"
    FILES
        shaders/color_preview.frag
)

target_include_directories(iv_ui
    PUBLIC ${CMAKE_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(iv_ui
    PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
    PRIVATE iv_core
)
