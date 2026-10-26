cmake_minimum_required(VERSION 3.21)

project(PrecisionCBCTSim
    VERSION 0.1.0
    DESCRIPTION "CBCT 成像仿真工具（Phase 1：Qt 6 + VTK 9 工程骨架）"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 单配置生成器（Ninja / Makefiles）下默认使用 Debug，多配置生成器（VS）由 --config 决定
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Qt 元对象编译
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 只保留 Debug/Release：VTK 与我们自己的依赖都只装这两个配置，
# 而 VS 生成器会为每个配置都执行一遍自定义构建步骤（例如 nvcc 编译 .cu）。
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Supported configurations" FORCE)
endif()

if(MSVC)
    # 源码使用 UTF-8 保存（含中文注释与界面字符串），必须显式告知 MSVC
    add_compile_options(/utf-8)
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets OpenGLWidgets Concurrent)

# GUISupportQt 只在使用 Qt 支持构建的 VTK 中可用；
# VTK 必须与 Qt 使用同一编译器/运行时（msvc2022_64 + x64-windows 之类）。
find_package(VTK 9.0 REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
    RenderingCore
    RenderingOpenGL2
    RenderingUI
    InteractionStyle
    GUISupportQt
    IOGeometry
    FiltersCore
    FiltersModeling
    FiltersSources
    ImagingCore
    ImagingStencil)

message(STATUS "Qt6         : ${Qt6_VERSION} (${Qt6_DIR})")
message(STATUS "VTK         : ${VTK_VERSION} (${VTK_DIR})")

# ------------------------------------------------------------------ CUDA
# GPU 加速只用于射线追踪（DDA）热点；找不到工具链时自动退化为纯 CPU 构建，
# 上层代码不需要任何 #ifdef（见 simulation/CudaProjectorStub.cpp）。
#
# 本机 VS 没有安装「CUDA 的 MSBuild 集成」（BuildCustomizations 里没有 CUDA*.props），
# 因此没法用 CMake 原生的 enable_language(CUDA)（它会报 "No CUDA toolset found"）。
# 这里改为直接用 nvcc 编译 .cu → .obj，再把 obj 加进静态库；
# nvcc 需要 MSVC 的 INCLUDE/LIB，所以通过 cmd 调用 vcvars64.bat 准备好环境。
option(PCSIM_ENABLE_CUDA "Build the CUDA projection backend when available" ON)
set(PCSIM_CUDA_ENABLED OFF)

if(PCSIM_ENABLE_CUDA)
    # nvcc：优先 CUDAToolkit_ROOT / CUDA_PATH
    find_program(PCSIM_NVCC nvcc
        HINTS "$ENV{CUDA_PATH}/bin" "$ENV{CUDAToolkit_ROOT}/bin")
    find_library(PCSIM_CUDART_LIBRARY cudart
        HINTS "$ENV{CUDA_PATH}/lib/x64" "$ENV{CUDAToolkit_ROOT}/lib/x64")

    # vcvars64.bat：用 vswhere 定位当前 VS 安装
    set(PCSIM_VCVARS "")
    find_program(PCSIM_VSWHERE vswhere
        HINTS "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer")
    if(PCSIM_VSWHERE)
        execute_process(
            COMMAND "${PCSIM_VSWHERE}" -latest -products * -property installationPath
            OUTPUT_VARIABLE PCSIM_VS_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(PCSIM_VS_PATH)
            set(PCSIM_VCVARS "${PCSIM_VS_PATH}/VC/Auxiliary/Build/vcvars64.bat")
        endif()
    endif()

    if(PCSIM_NVCC AND PCSIM_CUDART_LIBRARY AND EXISTS "${PCSIM_VCVARS}")
        # 按本机 GPU 的实际算力编译；跨机器分发时用 -DPCSIM_CUDA_ARCH=75;89
        set(PCSIM_CUDA_ARCH "native" CACHE STRING "nvcc -arch 参数")
        set(PCSIM_CUDA_ENABLED ON)
        message(STATUS "CUDA        : ${PCSIM_NVCC} (arch: ${PCSIM_CUDA_ARCH})")
    else()
        message(STATUS "CUDA        : 缺少 nvcc / cudart / vcvars64.bat，GPU 加速关闭（仅 CPU）")
        message(STATUS "              nvcc=${PCSIM_NVCC} cudart=${PCSIM_CUDART_LIBRARY} vcvars=${PCSIM_VCVARS}")
    endif()
else()
    message(STATUS "CUDA        : 已被 -DPCSIM_ENABLE_CUDA=OFF 关闭")
endif()

# Manifold（Apache-2.0）：多部件布尔并集 / 内腔差集，几何修复的最后一环。
# 用 scripts/build-manifold.ps1 一次性编译安装（静态库，无需额外 DLL）。
find_package(manifold 3.0 REQUIRED)
message(STATUS "Manifold    : ${manifold_VERSION} (${manifold_DIR})")

include(CTest)

add_subdirectory(visualization)
add_subdirectory(io)
add_subdirectory(simulation)
add_subdirectory(export)
add_subdirectory(settings)
add_subdirectory(app)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
