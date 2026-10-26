#pragma once

#include <string>

#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

#include "io/MeshQuality.h"

namespace precisioncbct::io {

/// 读入的三角网格模型。
///
/// 坐标单位约定为 mm（STL/OBJ 均不携带单位，由用户自行保证）。
struct MeshData
{
    vtkSmartPointer<vtkPolyData> polyData;
    std::string sourcePath;
    long long triangleCount = 0;   ///< 三角面片数（读取后已三角化）
    double bounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0}; ///< xmin,xmax,ymin,ymax,zmin,zmax
    MeshQuality quality;           ///< 网格体检结果（是否水密、有无 NaN 等）

    bool isValid() const { return polyData != nullptr && triangleCount > 0; }

    /// 包围盒在各轴的尺寸（mm）
    void boundsSize(double out[3]) const
    {
        out[0] = bounds[1] - bounds[0];
        out[1] = bounds[3] - bounds[2];
        out[2] = bounds[5] - bounds[4];
    }
};

/// 是否为本模块支持的模型格式（按扩展名判断：.stl / .obj）
bool isSupportedMeshFile(const std::string& path);

/// 读取 STL/OBJ 并三角化。
///
/// 失败时返回 isValid()==false 的结果，并向 error 写入原因。
/// 坐标含 NaN/Inf 时直接判定失败（几何稳健性护栏），不会返回半个可用的模型。
MeshData loadMesh(const std::string& path, std::string* error);

} // namespace precisioncbct::io
