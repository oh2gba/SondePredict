qt_add_library(sp_core STATIC
    atmosphere.cpp atmosphere.h
    flightprofile.cpp flightprofile.h
    geo.cpp geo.h
    openmeteo.cpp openmeteo.h
    predictor.cpp predictor.h
    simulator.cpp simulator.h
    trackerlog.cpp trackerlog.h
    windfield.cpp windfield.h
    windsource.h
)

target_include_directories(sp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sp_core PUBLIC Qt6::Core Qt6::Network)
