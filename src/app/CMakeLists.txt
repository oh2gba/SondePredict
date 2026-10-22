# The windows and dialogs live in a library so the tests can use them too.
qt_add_library(sp_app STATIC
    mainwindow.cpp mainwindow.h
    mapwidget.cpp mapwidget.h
    settingsdialog.cpp settingsdialog.h
)
qt_add_resources(sp_app "qml"
    PREFIX "/"
    FILES qml/TrackMap.qml
)
target_include_directories(sp_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sp_app PUBLIC
    sp_core
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickWidgets
    Qt6::Location
    Qt6::Positioning
)

qt_add_executable(sondepredict WIN32 MACOSX_BUNDLE main.cpp)
target_link_libraries(sondepredict PRIVATE sp_app)
target_compile_definitions(sondepredict PRIVATE SP_VERSION="${PROJECT_VERSION}")
