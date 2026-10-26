#pragma once

#include <string>

#include <vtkSmartPointer.h>

class vtkPolyData;

#include "io/MeshQuality.h"

namespace precisioncbct::io {

/// 网格自动修复结果。
struct MeshRepairResult
{
    vtkSmartPointer<vtkPolyData> polyData;
    MeshQuality before;
    MeshQuality after;
    long long weldedPoints = 0;      ///< 焊接掉的重复点数（vtkCleanPolyData）
    long long filledBoundaryEdges = 0; ///< 被补洞消除的边界边数
    long long addedTriangles = 0;    ///< 补洞新增的三角形数

    bool isValid() const { return polyData != nullptr; }

    /// 是否真的改动了网格
    bool changed() const
    {
        return weldedPoints > 0 || filledBoundaryEdges > 0 || addedTriangles > 0;
    }

    /// 修复后是否水密（可以安全判定内外）
    bool isWatertightAfter() const { return after.isWatertight(); }
};

struct MeshRepairOptions
{
    /// 焊接容差（mm）。STL 每个三角形都独立存顶点，必须焊接才能识别拓扑；
    /// 0 表示用包围盒对角线的 1e-7 倍自适应。
    double weldToleranceMm = 0.0;
    /// 是否补洞（vtkFillHolesFilter）。补洞会把开口「封上」，
    /// 因此调用方必须把 filledBoundaryEdges 报告给用户。
    bool fillHoles = true;
    /// 是否统一面朝向（vtkPolyDataNormals 的 Consistency + AutoOrient），
    /// 只影响显示与法向，不影响体素化，默认开启。
    bool orientNormals = true;
};

/// 轻量网格修复：焊接重复点 → 补洞 → 统一朝向，然后重新体检。
///
/// 只解决「常见的导出瑕疵」，不做重网格、不抗自相交、不保证流形：
/// 目标是在体素化前拿到一个水密可用的网格，并把改动如实报告出来。
/// 修复失败（例如仍然不水密）时 `isWatertightAfter()` 为 false，由调用方决定拒绝。
MeshRepairResult repairMesh(vtkPolyData* mesh, const MeshRepairOptions& options,
                            std::string* error);

} // namespace precisioncbct::io
