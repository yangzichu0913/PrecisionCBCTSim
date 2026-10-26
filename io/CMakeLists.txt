add_library(precisioncbct_io STATIC
    MaterialLabeling.h
    MaterialLabeling.cpp
    MeshBoolean.h
    MeshBoolean.cpp
    MeshLoader.h
    MeshLoader.cpp
    MeshQuality.h
    MeshQuality.cpp
    MeshRepair.h
    MeshRepair.cpp
    VoxelGrid.h
    VoxelGrid.cpp)

target_include_directories(precisioncbct_io
    PUBLIC ${PROJECT_SOURCE_DIR})

target_link_libraries(precisioncbct_io
    PUBLIC
        VTK::CommonCore
        VTK::CommonDataModel
        VTK::IOGeometry
        VTK::FiltersCore
        VTK::FiltersModeling
        VTK::ImagingCore
        VTK::ImagingStencil
    PRIVATE
        manifold::manifold)
