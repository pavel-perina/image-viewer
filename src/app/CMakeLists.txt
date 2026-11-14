# WIN32: a windowed program on Windows, without a console beside it. Messages still go to the
# debugger's output, or to DebugView outside one.
qt_add_executable(imageviewer WIN32 main.cpp)

# Inter 4.1 (SIL Open Font License, assets/fonts/Inter-LICENSE.txt), every weight in the one
# variable file. BASE keeps it at :/fonts/.
qt_add_resources(imageviewer "fonts"
    PREFIX "/"
    BASE ${CMAKE_SOURCE_DIR}/assets
    FILES ${CMAKE_SOURCE_DIR}/assets/fonts/InterVariable.ttf
)

# Which build this is, for the About dialog: the commit, "-dirty" if changed, and the time.
# Made at every build, not at configure time, so it never goes stale.
set(IV_BUILD_INFO ${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.h)
add_custom_target(iv_build_info
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${IV_BUILD_INFO}
            -P ${CMAKE_SOURCE_DIR}/cmake/build_info.cmake
    BYPRODUCTS ${IV_BUILD_INFO}
    COMMENT "Writing build_info.h"
    VERBATIM)
add_dependencies(imageviewer iv_build_info)
target_include_directories(imageviewer PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_link_libraries(imageviewer PRIVATE
    iv_ui
    iv_uiplugin
    iv_style
    iv_styleplugin
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
