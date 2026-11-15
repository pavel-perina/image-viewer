qt_add_library(iv_ui STATIC
    app_info.cpp
    app_info.h
    annotate_overlay.cpp
    annotate_overlay.h
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
    fuzzy_list_model.cpp
    fuzzy_list_model.h
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
    program_font.cpp
    program_font.h
    shape_painter.cpp
    shape_painter.h
    image_view.cpp
    image_view.h
    subfolder_list.cpp
    subfolder_list.h
    theme_palette.cpp
    theme_palette.h
    themed_icon.cpp
    themed_icon.h
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
        qml/AnnotatePanel.qml
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
        qml/DecimalSpinBox.qml
        qml/DialogWindow.qml
        qml/FileActions.qml
        qml/FolderButton.qml
        qml/FileChooser.qml
        qml/FileListView.qml
        qml/FolderTree.qml
        qml/IconButton.qml
        qml/ImageScrollBars.qml
        qml/ImageSurface.qml
        qml/JpegMenu.qml
        qml/LevelsControl.qml
        qml/ListFrame.qml
        qml/LockButton.qml
        qml/InfoPanel.qml
        qml/Main.qml
        qml/MessageBox.qml
        qml/NumberRow.qml
        qml/PaneHeader.qml
        qml/PathBar.qml
        qml/PreviewPanel.qml
        qml/ProbePanel.qml
        qml/ResizeDialog.qml
        qml/SearchCombo.qml
        qml/StraightenPanel.qml
        qml/ResampledView.qml
        qml/SaveOptionsDialog.qml
        qml/Theme.qml
        qml/Screens.qml
        qml/ToolPanel.qml
        qml/ViewSettings.qml
        qml/ViewerPage.qml
)

# Inter 4.1 (SIL Open Font License, assets/fonts/Inter-LICENSE.txt), every weight in the one
# variable file, for useProgramFont. BASE keeps it at :/fonts/.
qt_add_resources(iv_ui "fonts"
    PREFIX "/"
    BASE ${CMAKE_SOURCE_DIR}/assets
    FILES ${CMAKE_SOURCE_DIR}/assets/fonts/InterVariable.ttf
)

# The icons, at qrc:/icons/<name>.svg for ThemedIcon. They are named by meaning, following the
# freedesktop icon naming spec where it has a name, while the files keep the names they are
# exported under.
set(iv_icons
    "24x24_zoom_in.svg=zoom-in"
    "24x24_zoom_out.svg=zoom-out"
    "24x24_zoom_fit.svg=zoom-fit-best"
    "24x24_zoom_reset.svg=zoom-original"
    "24x24_info.svg=dialog-information"
    "24x24_fullscreen.svg=view-fullscreen"
    "24x24_refresh.svg=view-refresh"
    "24x24_directory_back.svg=go-previous"
    "24x24_directory_forward.svg=go-next"
    "24x24_directory_up.svg=go-up"
    "24x24_favorites.svg=non-starred"
    "24x24_favorites_alt.svg=starred"
    "24x24_search.svg=edit-find"
    "24x24_sort.svg=view-sort"
    "24x24_position.svg=position"
)
set(iv_icon_files)
foreach(entry IN LISTS iv_icons)
    string(REPLACE "=" ";" parts "${entry}")
    list(GET parts 0 file)
    list(GET parts 1 name)
    set(path ${CMAKE_SOURCE_DIR}/assets/icons/${file})
    set_source_files_properties(${path} PROPERTIES QT_RESOURCE_ALIAS ${name}.svg)
    list(APPEND iv_icon_files ${path})
endforeach()
qt_add_resources(iv_ui "icons"
    PREFIX "/icons"
    FILES ${iv_icon_files}
)

# The shaders live in assets/shaders; BASE keeps them at qrc:/shaders/.
qt_add_shaders(iv_ui "iv_shaders"
    PREFIX "/"
    BASE ${CMAKE_SOURCE_DIR}/assets
    FILES
        ${CMAKE_SOURCE_DIR}/assets/shaders/mks_horizontal.frag
        ${CMAKE_SOURCE_DIR}/assets/shaders/mks_vertical.frag
)

# texelFetch needs GLSL ES 3 or GLSL 1.30.
qt_add_shaders(iv_ui "iv_color_shaders"
    PREFIX "/"
    GLSL "300es,330"
    BASE ${CMAKE_SOURCE_DIR}/assets
    FILES
        ${CMAKE_SOURCE_DIR}/assets/shaders/color_preview.frag
)

target_include_directories(iv_ui
    PUBLIC ${CMAKE_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(iv_ui
    PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
    PRIVATE iv_core Qt6::Svg
)
