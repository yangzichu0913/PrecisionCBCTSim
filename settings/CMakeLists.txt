add_library(precisioncbct_settings STATIC
    SettingsPreset.h
    SettingsPreset.cpp)

target_include_directories(precisioncbct_settings
    PUBLIC ${PROJECT_SOURCE_DIR})

target_link_libraries(precisioncbct_settings
    PUBLIC
        precisioncbct_simulation
        Qt6::Core)
