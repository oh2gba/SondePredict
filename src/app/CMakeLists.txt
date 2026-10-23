# The windows and dialogs live in a library so the tests can use them too.
qt_add_library(sp_app STATIC
    groundtexture.cpp groundtexture.h
    helpwindow.cpp helpwindow.h
    mainwindow.cpp mainwindow.h
    mapwidget.cpp mapwidget.h
    profilewidget.cpp profilewidget.h
    scene3d.cpp scene3d.h
    settingsdialog.cpp settingsdialog.h
)
qt_add_resources(sp_app "qml"
    PREFIX "/"
    FILES qml/TrackMap.qml qml/Track3D.qml
)
qt_add_resources(sp_app "icons"
    PREFIX "/icons"
    BASE ${PROJECT_SOURCE_DIR}/packaging/icons
    FILES
        ${PROJECT_SOURCE_DIR}/packaging/icons/sondepredict.svg
        ${PROJECT_SOURCE_DIR}/packaging/icons/sondepredict-256.png
        ${PROJECT_SOURCE_DIR}/packaging/icons/sondepredict-48.png
        ${PROJECT_SOURCE_DIR}/packaging/icons/sondepredict-32.png
        ${PROJECT_SOURCE_DIR}/packaging/icons/sondepredict-16.png
)
# Typical balloon values per launch site, from SondeHub (tools/sondehub_sites.py).
qt_add_resources(sp_app "data"
    PREFIX "/"
    FILES data/launch-sites.json
)
# The user manual, shown by Help → User manual.
file(GLOB SP_MANUAL_IMAGES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/docs/manual/images/*)
qt_add_resources(sp_app "manual"
    PREFIX "/manual"
    BASE ${PROJECT_SOURCE_DIR}/docs/manual
    FILES ${PROJECT_SOURCE_DIR}/docs/manual/user-manual.md ${SP_MANUAL_IMAGES}
)
target_include_directories(sp_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sp_app PUBLIC
    sp_core
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickWidgets
    Qt6::Location
    Qt6::Positioning
    Qt6::Quick3D
)

qt_add_executable(sondepredict WIN32 MACOSX_BUNDLE main.cpp)
target_link_libraries(sondepredict PRIVATE sp_app)
target_compile_definitions(sondepredict PRIVATE SP_VERSION="${PROJECT_VERSION}")

if(WIN32)
    set(SP_ICON_PATH "${PROJECT_SOURCE_DIR}/packaging/icons/sondepredict.ico")
    configure_file(${PROJECT_SOURCE_DIR}/packaging/windows/sondepredict.rc.in ${CMAKE_CURRENT_BINARY_DIR}/sondepredict.rc @ONLY)
    target_sources(sondepredict PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/sondepredict.rc)
endif()

include(GNUInstallDirs)
install(TARGETS sondepredict
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .)
if(UNIX AND NOT APPLE)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/sondepredict.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/io.github.sondepredict.SondePredict.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/icons/sondepredict.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    foreach(size 16 32 48 256)
        install(FILES ${PROJECT_SOURCE_DIR}/packaging/icons/sondepredict-${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
            RENAME sondepredict.png)
    endforeach()
endif()
