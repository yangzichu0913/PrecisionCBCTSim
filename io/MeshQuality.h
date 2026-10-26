#pragma once

#include <string>

class vtkPolyData;

namespace precisioncbct::io {

/// 网格质量检查结果（几何稳健性的第一道防线）。
///
/// 这里只做「体检」不做「治疗」：调用方据此决定是拒绝、告警还是切换到稳健路径。
struct MeshQuality
{
    long long triangles = 0;
    long long nonFinitePoints = 0;    ///< NaN / Inf 坐标的顶点数（>0 必须拒绝）
    long long boundaryEdges = 0;      ///< 只被 1 个面共享的边：网格有洞
    long long nonManifoldEdges = 0;   ///< 被 3 个以上面共享的边
    long long degenerateTriangles = 0;///< 面积小于 tol 的三角形
    double bounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    double maxAbsCoordinate = 0.0;    ///< 顶点坐标绝对值的最大值（判断是否远离原点）

    /// 水密（无洞、无非流形边）才可以用快速的 stencil 体素化
    bool isWatertight() const { return boundaryEdges == 0 && nonManifoldEdges == 0; }

    bool hasNonFinite() const { return nonFinitePoints > 0; }

    /// 包围盒最大边长；退化（零厚度）时为 0
    double maxExtentMm() const;
};

/// 三角形面积小于该值（mm²）即视为退化，默认 1e-12
MeshQuality analyzeMesh(vtkPolyData* mesh, double degenerateAreaToleranceMm2 = 1e-12);

/// 一句话描述质量（用于状态栏/日志）；水密且无异常时返回空字符串
std::string describeQuality(const MeshQuality& quality);

} // namespace precisioncbct::io
