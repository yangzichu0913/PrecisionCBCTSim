#pragma once

#include <string>

#include <vtkSmartPointer.h>

class vtkPolyData;

namespace precisioncbct::io {

/// 布尔合并结果。
struct MeshBooleanResult
{
    vtkSmartPointer<vtkPolyData> polyData;
    long long components = 0;      ///< 输入的连通壳体数
    long long positiveShells = 0;  ///< 法向朝外（实体）的壳体数
    long long cavityShells = 0;    ///< 法向朝内（内腔）的壳体数
    double volumeMm3 = 0.0;        ///< Manifold 算出的精确体积（可用来校验体素化）

    bool isValid() const { return polyData != nullptr; }

    /// 是否真的做了合并（单壳体且流形时不做，直接复用原网格）
    bool changed() const { return components > 1 || cavityShells > 0; }
};

/// 统计连通壳体数（O(n) 一次遍历）。用于判断是否需要做布尔合并。
long long countShells(vtkPolyData* mesh);

/// 把网格按「连通壳体 + 布尔运算」整理成一个流形实体。
///
/// 为什么需要它（实测数据）：
/// - 一个 STL 里放多个相互重叠的闭合部件时，`vtkPolyDataToImageStencil` 按扫描线奇偶
///   判定，会把重叠区算成「异或」：40mm 与 30mm 立方体偏移 15mm，应有并集 73000 mm³，
///   实际得到 60010 mm³，且静默无告警；
/// - 两个部件共面贴合时会出现非流形边，stencil 完全失效。
///
/// 处理方式（与物理语义一致）：
///   1. 用连通分量把网格拆成若干闭合壳体；
///   2. 每个壳体交给 Manifold（Apache-2.0）做 CSG；
///   3. 法向朝外（体积 > 0）的壳体求**并集**，法向朝内（体积 < 0，即内腔）的壳体求**差集**。
/// 因此「多部件 phantom」按并集算，「带内腔的骨」按差集算，互不干扰。
///
/// 单壳体且已是流形时直接返回原网格（`changed()==false`），不引入任何行为变化。
/// 任一分量不是流形（Manifold 返回 NotManifold）时返回 isValid()==false 并写明原因。
MeshBooleanResult unifyComponents(vtkPolyData* mesh, std::string* error);

} // namespace precisioncbct::io
