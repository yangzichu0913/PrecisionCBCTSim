#pragma once

#include <string>

#include <vtkSmartPointer.h>

class vtkImageData;
class vtkPolyData;

namespace precisioncbct::io {

/// 体素化结果：与物理坐标对齐的规则网格。
///
/// 标量数据为 unsigned char：1 表示位于模型内部，0 表示外部。
/// 网格原点会比模型包围盒外扩 paddingVoxels 个体素，
/// 便于后续射线追踪从模型外部入射。
struct VoxelGrid
{
    vtkSmartPointer<vtkImageData> image;
    double spacingMm[3] = {1.0, 1.0, 1.0};
    int dimensions[3] = {0, 0, 0};
    long long insideVoxels = 0;
    long long totalVoxels = 0;
    /// true 表示体素化前对网格做过自动修复（焊接重复点 / 补洞）或布尔合并，
    /// 调用方应把这件事告诉用户：体积是按「补洞/合并后的封闭体」算出来的
    bool usedRobustVoxelization = false;
    /// 自动补洞消除的边界边数（>0 说明几何被改过）
    long long repairedBoundaryEdges = 0;
    /// 布尔合并出的连通壳体数（>1 说明是按并集/差集算的）
    long long mergedComponents = 0;
    /// 是否真的走了 Manifold 布尔重建（重叠并集、内腔差集、非流形接缝修复）
    bool usedBooleanMerge = false;
    /// Manifold 给出的解析体积（mm³）；0 表示没有计算
    double exactVolumeMm3 = 0.0;

    bool isValid() const { return image != nullptr && totalVoxels > 0; }

    /// 内部体素对应的体积（mm³），用于与解析体积比对
    double insideVolumeMm3() const
    {
        return static_cast<double>(insideVoxels) * spacingMm[0] * spacingMm[1] * spacingMm[2];
    }
};

struct VoxelizationOptions
{
    double spacingMm = 1.0;  ///< 各向同性体素边长
    int paddingVoxels = 2;   ///< 包围盒外侧留空体素数
    /// 体素总数上限（防止误输间距把内存打爆）。8e7 ≈ 430³ ≈ 80 MB
    long long maxVoxels = 80000000LL;
    /// 网格不水密时是否自动修复（焊接重复点 + 补洞）；false 时直接报错
    bool autoRobustFallback = true;
    /// 是否总是做布尔合并（多部件按并集、内腔按差集）。
    /// 默认 false：只在「不水密 / 非流形」时才做，水密单壳体走最快路径。
    bool booleanUnify = false;
};

/// CPU 体素化。
///
/// 处理流程（按需逐级升级，越靠下越慢）：
/// 1. 水密单壳体 → 直接 `vtkPolyDataToImageStencil`（最快，行为与早期版本一致）；
/// 2. 不水密 → 轻量修复（焊接重复点 + 补洞 + 统一朝向）；
/// 3. 修复后仍不水密 / 出现非流形边 → 用 Manifold 做布尔合并
///    （实体求并集、法向朝内的壳体求差集），得到流形实体后再走 stencil。
/// 升级过流程时会置 `usedRobustVoxelization`、`repairedBoundaryEdges`、
/// `mergedComponents` 与 `exactVolumeMm3`，调用方应把这些告诉用户。
/// 失败时返回 isValid()==false 的结果，并向 error 写入原因。
///
/// 会拒绝以下输入（几何稳健性护栏）：
/// - 顶点含 NaN/Inf；
/// - 包围盒最大边长小于 2 个体素：几乎一定是单位错误（米/厘米被当成 mm）；
/// - 坐标离原点过远，单精度 STL 的舍入步长已超过 2 个体素；
/// - 某一轴零厚度（纯平面/纯曲面）；
/// - 体素总数超过 `maxVoxels`：提示当前几何下可用的间距；
/// - 修复与布尔合并都无法得到水密实体。
VoxelGrid voxelize(vtkPolyData* mesh, const VoxelizationOptions& options, std::string* error);

} // namespace precisioncbct::io
