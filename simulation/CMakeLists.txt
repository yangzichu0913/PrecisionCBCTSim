add_library(precisioncbct_simulation STATIC
    AttenuationTable.h
    AttenuationTable.cpp
    ConeBeamView.h
    ConeBeamView.cpp
    CpuProjector.h
    CpuProjector.cpp
    CudaProjector.h
    DetectorModel.h
    DetectorModel.cpp
    DetectorModelParams.h
    FdkReconstructor.h
    FdkReconstructor.cpp
    ProjectionSequence.h
    ProjectionSequence.cpp
    MaterialTypes.h
    ScannerMode.h
    ScannerMode.cpp
    SpectralProjector.h
    SpectralProjector.cpp
    XRaySpectrum.h
    XRaySpectrum.cpp)

if(PCSIM_CUDA_ENABLED)
    # 用 nvcc 单独把 .cu 编译成 .obj（本机 VS 没有 CUDA 的 MSBuild 集成，
    # 所以不走 CMake 的 CUDA 语言支持）。nvcc 需要 MSVC 的 INCLUDE/LIB，
    # 因此通过 cmd 先 call vcvars64.bat 准备好环境。
    #
    # 注意：VS 生成器不会展开自定义命令 OUTPUT 里的 $<CONFIG>（实测会生成
    # "CudaProjector_(CONFIG).obj" 这种废名字），所以在配置期按配置各生成一条命令，
    # 再用 $<CONFIG:...> 选择对应的 obj。
    set(_pcsim_cu "${CMAKE_CURRENT_SOURCE_DIR}/CudaProjector.cu")
    # 注意必须是普通字符串：CMake 的 list 展开成 "a;b"，nvcc 会当成一个参数
    set(_pcsim_compat "-allow-unsupported-compiler -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH")

    if(NOT CMAKE_CONFIGURATION_TYPES)
        set(_pcsim_configs "${CMAKE_BUILD_TYPE}")
    else()
        set(_pcsim_configs ${CMAKE_CONFIGURATION_TYPES})
    endif()

    foreach(_cfg ${_pcsim_configs})
        set(_obj "${CMAKE_CURRENT_BINARY_DIR}/CudaProjector_${_cfg}.obj")
        if(_cfg STREQUAL "Debug")
            set(_runtime_flag "/MDd")
        else()
            set(_runtime_flag "/MD")
        endif()

        # 用 file(GENERATE) 生成每配置的 .cmd：把「call vcvars + nvcc」写成脚本文件，
        # 避免 add_custom_command 里层层转义引号（实测会被 CMake 转义坏）。
        set(_script "${CMAKE_CURRENT_BINARY_DIR}/compile_cuda_${_cfg}.cmd")
        file(GENERATE OUTPUT "${_script}" CONTENT
"@echo off
call \"${PCSIM_VCVARS}\" >nul
if errorlevel 1 exit /b 1
\"${PCSIM_NVCC}\" -c \"${_pcsim_cu}\" -o \"${_obj}\" -I\"${PROJECT_SOURCE_DIR}\" -O3 --std=c++17 -arch=${PCSIM_CUDA_ARCH} ${_pcsim_compat} -Xcompiler /utf-8,/EHsc,${_runtime_flag}
exit /b %ERRORLEVEL%
")

        add_custom_command(
            OUTPUT "${_obj}"
            COMMAND cmd /c "${_script}"
            DEPENDS "${_pcsim_cu}" "${CMAKE_CURRENT_SOURCE_DIR}/CudaProjector.h"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ConeBeamView.h" "${_script}"
            COMMENT "Compiling CUDA projection kernel (${_cfg}, arch ${PCSIM_CUDA_ARCH})"
            VERBATIM)
        target_sources(precisioncbct_simulation PRIVATE $<$<CONFIG:${_cfg}>:${_obj}>)
    endforeach()

    target_compile_definitions(precisioncbct_simulation PUBLIC PCSIM_HAS_CUDA=1)
    target_link_libraries(precisioncbct_simulation PRIVATE ${PCSIM_CUDART_LIBRARY})
else()
    target_sources(precisioncbct_simulation PRIVATE CudaProjectorStub.cpp)
endif()

target_include_directories(precisioncbct_simulation
    PUBLIC ${PROJECT_SOURCE_DIR})

target_link_libraries(precisioncbct_simulation
    PUBLIC
        precisioncbct_io
        VTK::CommonCore
        VTK::CommonDataModel)
