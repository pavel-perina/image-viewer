qt_add_executable(imageviewer main.cpp)

target_link_libraries(imageviewer PRIVATE
    iv_ui
    iv_uiplugin
    Qt6::QuickControls2
)
