qt_add_executable(PrecisionCBCTSim WIN32
    main.cpp
    MainWindow.h
    MainWindow.cpp
    Theme.h
    Theme.cpp
    ScanParameterPanel.h
    ScanParameterPanel.cpp
    DRPreviewWidget.h
    DRPreviewWidget.cpp
    resources.qrc)

# Windows 可执行文件图标与版本信息（资源编译器需要 app 目录作为包含路径）
if(WIN32)
    enable_language(RC)
    target_sources(PrecisionCBCTSim PRIVATE app.rc)
    set_source_files_properties(app.rc PROPERTIES INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR})
endif()

target_include_directories(PrecisionCBCTSim
    PRIVATE ${PROJECT_SOURCE_DIR})

target_link_libraries(PrecisionCBCTSim
    PRIVATE
        precisioncbct_visualization
        precisioncbct_io
        precisioncbct_simulation
        precisioncbct_export
        precisioncbct_settings
        Qt6::Widgets
        Qt6::Concurrent)

target_compile_definitions(PrecisionCBCTSim
    PRIVATE PCSIM_VERSION="${PROJECT_VERSION}")

# VTK 9 的渲染后端（对象工厂）通过编译期注入注册：
# 必须在最终可执行目标上调用，否则运行期会报 "no override found for vtkRenderWindow"。
vtk_module_autoinit(TARGETS PrecisionCBCTSim MODULES ${VTK_LIBRARIES})

install(TARGETS PrecisionCBCTSim RUNTIME DESTINATION bin)
