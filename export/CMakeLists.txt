add_library(precisioncbct_export STATIC
    DrExporter.h
    DrExporter.cpp
    VolumeExporter.h
    VolumeExporter.cpp)

target_include_directories(precisioncbct_export
    PUBLIC ${PROJECT_SOURCE_DIR})

# 只用 Qt Gui（QImage/PNG 写入）+ Qt Core（JSON 头），不引入新的第三方依赖
target_link_libraries(precisioncbct_export
    PUBLIC
        precisioncbct_simulation
        Qt6::Gui
        Qt6::Core)
