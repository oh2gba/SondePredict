qt_add_executable(sp-cli main.cpp)
target_link_libraries(sp-cli PRIVATE sp_core)
target_compile_definitions(sp-cli PRIVATE SP_VERSION="${PROJECT_VERSION}")
