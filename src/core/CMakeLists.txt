qt_add_library(sp_core STATIC
    atmosphere.cpp atmosphere.h
    batch.cpp batch.h
    flightprofile.cpp flightprofile.h
    geo.cpp geo.h
    landingarea.cpp landingarea.h
    meps.cpp meps.h
    network.cpp network.h
    openmeteo.cpp openmeteo.h
    predictionstore.cpp predictionstore.h
    predictor.cpp predictor.h
    settings.cpp settings.h
    simulator.cpp simulator.h
    sitestats.cpp sitestats.h
    sondehub.cpp sondehub.h
    trackerlog.cpp trackerlog.h
    trackexport.cpp trackexport.h
    updatecheck.cpp updatecheck.h
    v3import.cpp v3import.h
    windfield.cpp windfield.h
    windsource.h
)

target_include_directories(sp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sp_core PUBLIC Qt6::Core Qt6::Network Qt6::Sql)
