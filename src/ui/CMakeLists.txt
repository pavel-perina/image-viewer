qt_add_library(iv_ui STATIC
    app_info.cpp
    app_info.h
    appearance_draft.cpp
    appearance_draft.h
    annotate_overlay.cpp
    annotate_overlay.h
    browse_context.cpp
    browse_context.h
    browse_context_files.cpp
    browse_context_p.h
    color_lut.cpp
    color_lut.h
    color_table_list.cpp
    color_table_list.h
    color_settings.cpp
    color_settings.h
    crop_overlay.cpp
    crop_overlay.h
    straighten_overlay.cpp
    straighten_overlay.h
    directory_tree_model.cpp
    directory_tree_model.h
    display_output.cpp
    display_output.h
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
    color_choice_model.cpp
    color_choice_model.h
    color_ramp.cpp
    color_ramp.h
    histogram_view.cpp
    histogram_view.h
    image_overlay.cpp
    image_overlay.h
    overlay_style.h
    probe_overlay.cpp
    slider_scale.cpp
    slider_scale.h
    probe_overlay.h
    gray_pick_overlay.cpp
    gray_pick_overlay.h
    hidden_folders.h
    program_font.cpp
    program_font.h
    preferences.cpp
    preferences.h
    retired_settings.cpp
    retired_settings.h
    settings_draft.cpp
    settings_draft.h
    shape_painter.cpp
    shape_painter.h
    strip_builder.cpp
    strip_builder.h
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
        qml/AppScrollArea.qml
        qml/ColorChooser.qml
        qml/ColorChooserDialog.qml
        qml/ColorField.qml
        qml/ColorSwatches.qml
        qml/BarButton.qml
        qml/BrowserPage.qml
        qml/Caret.qml
        qml/ColorPreviewEffect.qml
        qml/CompareSplitter.qml
        qml/CropPanel.qml
        qml/DecimalSpinBox.qml
        qml/DialogWindow.qml
        qml/FileActions.qml
        qml/FolderMenu.qml
        qml/NameDialog.qml
        qml/FolderButton.qml
        qml/FileChooser.qml
        qml/FileListView.qml
        qml/FolderTree.qml
        qml/FrameDialog.qml
        qml/GradientSlider.qml
        qml/IconButton.qml
        qml/ImageScrollBars.qml
        qml/ImageSurface.qml
        qml/JpegMenu.qml
        qml/LevelsControl.qml
        qml/ListFrame.qml
        qml/SelectionBackground.qml
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
        qml/SettingsDialog.qml
        qml/SettingsPage.qml
        qml/SettingsAppearancePage.qml
        qml/SettingsViewingPage.qml
        qml/SettingsThumbnailsPage.qml
        qml/ThumbnailCacheDialogs.qml
        qml/StraightenPanel.qml
        qml/StripBuilderDialog.qml
        qml/ResampledView.qml
        qml/SaveOptionsDialog.qml
        qml/Theme.qml
        qml/ThumbnailCell.qml
        qml/Screens.qml
        qml/ToolPanel.qml
        qml/ToolSection.qml
        qml/SliderRow.qml
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
# freedesktop icon naming spec where it has a name, while the files keep the names of the set
# they come from (assets/icons/<set>/, each with its licence).
set(iv_icons
    "tabler/brush.svg=preferences-desktop-theme"
    "tabler/zoom-in.svg=zoom-in"
    "tabler/zoom-out.svg=zoom-out"
    "tabler/zoom-scan.svg=zoom-fit-best"
    "tabler/zoom-reset.svg=zoom-original"
    "tabler/info-circle.svg=dialog-information"
    "tabler/arrows-maximize.svg=view-fullscreen"
    "tabler/refresh.svg=view-refresh"
    "tabler/arrow-left.svg=go-previous"
    "tabler/arrow-right.svg=go-next"
    "tabler/arrow-up.svg=go-up"
    "tabler/star.svg=non-starred"
    "tabler/star-filled.svg=starred"
    "tabler/search.svg=edit-find"
    "tabler/sort-descending.svg=view-sort"
    "tabler/current-location.svg=position"
    "tabler/photo.svg=view-image"
    "tabler/layout-grid.svg=view-browser"
    "tabler/chevron-left.svg=image-previous"
    "tabler/chevron-right.svg=image-next"
    "tabler/file-arrow-left.svg=page-previous"
    "tabler/file-arrow-right.svg=page-next"
    "tabler/contrast.svg=auto-contrast"
    "tabler/crop.svg=transform-crop"
    "tabler/perspective.svg=straighten"
    "tabler/rotate.svg=object-rotate-left"
    "tabler/rotate-clockwise.svg=object-rotate-right"
    "tabler/arrow-back-up.svg=edit-reset"
    "tabler/pencil.svg=annotate"
    "tabler/dimensions.svg=image-resize"
    "tabler/adjustments-horizontal.svg=adjust-colours"
    "tabler/square-half.svg=before-after"
    "tabler/eye.svg=show-original"
    "tabler/color-picker.svg=color-picker"
    "tabler/folder-plus.svg=folder-new"
    "tabler/list.svg=view-list-brief"
    "tabler/list-details.svg=view-list-details"
    "tabler/info-circle-filled.svg=message-info"
    "tabler/help-circle-filled.svg=message-question"
    "tabler/alert-triangle-filled.svg=message-warning"
    "tabler/circle-x-filled.svg=message-error"
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
# The program's own icon, the one the installers use: in colour, so apart from the themed ones.
set(iv_program_icon ${CMAKE_SOURCE_DIR}/installer/linux/imageviewer.svg)
set_source_files_properties(${iv_program_icon} PROPERTIES QT_RESOURCE_ALIAS icon.svg)
qt_add_resources(iv_ui "program_icon"
    PREFIX "/program"
    FILES ${iv_program_icon}
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
