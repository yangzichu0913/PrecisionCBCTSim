add_library(precisioncbct_visualization STATIC
    SceneView.h
    SceneView.cpp
    ScannerRig.h
    ScannerRig.cpp)

# 项目根目录作为公共包含目录：下游统一使用 "visualization/SceneView.h" 之类的前缀路径
target_include_directories(precisioncbct_visualization
    PUBLIC ${PROJECT_SOURCE_DIR})

target_link_libraries(precisioncbct_visualization
    PUBLIC
        Qt6::Widgets
        # Qt 6 起 QOpenGLWidget（QVTKOpenGLNativeWidget 的基类）位于 OpenGLWidgets 模块
        Qt6::OpenGLWidgets
        ${VTK_LIBRARIES})
