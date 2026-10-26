add_executable(pcsim_voxel_sanity VoxelSanity.cpp TestGeometry.h)

target_link_libraries(pcsim_voxel_sanity
    PRIVATE
        precisioncbct_io)

add_executable(pcsim_projection_sanity ProjectionSanity.cpp TestGeometry.h)

target_link_libraries(pcsim_projection_sanity
    PRIVATE
        precisioncbct_simulation)

add_executable(pcsim_sequence_sanity SequenceSanity.cpp TestGeometry.h)

target_link_libraries(pcsim_sequence_sanity
    PRIVATE
        precisioncbct_simulation)

add_executable(pcsim_spectral_sanity SpectralSanity.cpp TestGeometry.h)

target_link_libraries(pcsim_spectral_sanity
    PRIVATE
        precisioncbct_simulation)

add_executable(pcsim_labeling_sanity MaterialLabelingSanity.cpp TestGeometry.h)

target_link_libraries(pcsim_labeling_sanity
    PRIVATE
        precisioncbct_io
        precisioncbct_simulation)

add_executable(pcsim_detector_sanity DetectorModelSanity.cpp)

target_link_libraries(pcsim_detector_sanity
    PRIVATE
        precisioncbct_simulation)

add_executable(pcsim_settings_sanity SettingsPresetSanity.cpp)

target_link_libraries(pcsim_settings_sanity
    PRIVATE
        precisioncbct_settings)

add_executable(pcsim_image_quality_sanity ImageQualitySanity.cpp TestGeometry.h)

target_link_libraries(pcsim_image_quality_sanity
    PRIVATE
        precisioncbct_io
        precisioncbct_simulation)

add_executable(pcsim_robustness_sanity GeometryRobustnessSanity.cpp TestGeometry.h)

target_link_libraries(pcsim_robustness_sanity
    PRIVATE
        precisioncbct_io)

add_executable(pcsim_fdk_sanity FdkReconstructionSanity.cpp TestGeometry.h)

target_link_libraries(pcsim_fdk_sanity
    PRIVATE
        precisioncbct_io
        precisioncbct_simulation)

add_executable(pcsim_gpu_sanity GpuProjectionSanity.cpp TestGeometry.h)

target_link_libraries(pcsim_gpu_sanity
    PRIVATE
        precisioncbct_io
        precisioncbct_simulation)

# VTK 的对象工厂注册需要在最终可执行目标上做一次
vtk_module_autoinit(TARGETS pcsim_voxel_sanity pcsim_projection_sanity pcsim_sequence_sanity
    pcsim_spectral_sanity pcsim_labeling_sanity pcsim_detector_sanity
    pcsim_settings_sanity pcsim_image_quality_sanity pcsim_robustness_sanity
    pcsim_fdk_sanity pcsim_gpu_sanity MODULES ${VTK_LIBRARIES})

add_test(NAME voxel_sanity COMMAND pcsim_voxel_sanity)
add_test(NAME projection_sanity COMMAND pcsim_projection_sanity)
add_test(NAME sequence_sanity COMMAND pcsim_sequence_sanity)
add_test(NAME spectral_sanity COMMAND pcsim_spectral_sanity)
add_test(NAME labeling_sanity COMMAND pcsim_labeling_sanity)
add_test(NAME detector_sanity COMMAND pcsim_detector_sanity)
add_test(NAME settings_sanity COMMAND pcsim_settings_sanity)
add_test(NAME image_quality_sanity COMMAND pcsim_image_quality_sanity)
add_test(NAME robustness_sanity COMMAND pcsim_robustness_sanity)
add_test(NAME fdk_sanity COMMAND pcsim_fdk_sanity)
add_test(NAME gpu_sanity COMMAND pcsim_gpu_sanity)

# pcsim_settings_sanity 用到 Qt Core，运行时需要 Qt 的 bin 目录在 PATH 里，
# 否则 ctest 会以 0xc0000135（找不到 DLL）失败。
set_tests_properties(settings_sanity PROPERTIES ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
