qt_add_executable(imageviewer main.cpp)

target_link_libraries(imageviewer PRIVATE
    iv_ui
    iv_uiplugin
    Qt6::QuickControls2
)

# Windows finds DLLs next to the executable, so put Qt there to run from the build folder.
if(WIN32)
    add_custom_command(TARGET imageviewer POST_BUILD
        COMMAND Qt6::windeployqt
            --qmldir ${CMAKE_SOURCE_DIR}/src/ui/qml
            $<IF:$<CONFIG:Debug>,--debug,--release>
            --no-compiler-runtime
            --no-translations
            # The UI tests run from this folder too, and Qt looks for plugins here first.
            --include-plugins qoffscreen
            $<TARGET_FILE:imageviewer>
        VERBATIM
    )

    # What the installer ships: see installer/build-installer.ps1.
    install(TARGETS imageviewer RUNTIME DESTINATION .)

    # The Visual C++ runtime goes next to the executable, so installing needs no admin rights.
    # The Universal CRT is part of Windows 10 and later.
    set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    include(InstallRequiredSystemLibraries)

    install(CODE "
        execute_process(
            COMMAND \"$<TARGET_FILE:Qt6::windeployqt>\"
                --qmldir \"${CMAKE_SOURCE_DIR}/src/ui/qml\"
                --release
                --no-compiler-runtime
                --no-translations
                --no-opengl-sw
                --skip-plugin-types qmltooling,networkinformation,tls,generic
                \"\${CMAKE_INSTALL_PREFIX}/imageviewer.exe\"
            COMMAND_ERROR_IS_FATAL ANY
        )
    ")
endif()
