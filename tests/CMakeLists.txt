function(sp_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE sp_core Qt6::Test)
    target_compile_definitions(${name} PRIVATE
        SP_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data"
        SP_RECOVERED_DATA="${PROJECT_SOURCE_DIR}/recovered/test_data")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

sp_add_test(tst_geo)
sp_add_test(tst_atmosphere)
sp_add_test(tst_flightprofile)
sp_add_test(tst_windfield)
sp_add_test(tst_openmeteo)
sp_add_test(tst_simulator)
sp_add_test(tst_predictor)
sp_add_test(tst_trackerlog)
