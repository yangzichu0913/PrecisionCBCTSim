#include "io/VoxelGrid.h"

#include "io/MeshBoolean.h"
#include "io/MeshQuality.h"
#include "io/MeshRepair.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

#include <vtkImageData.h>
#include <vtkImageStencil.h>
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkPolyDataToImageStencil.h>
#include <vtkType.h>

namespace precisioncbct::io {
namespace {

constexpr unsigned char kOutside = 0;
constexpr unsigned char kInside = 1;

/// 由包围盒、间距与 padding 推出网格维度/原点
void computeGridGeometry(const VoxelizationOptions& options, const double bounds[6],
                         int dimensions[3], double origin[3])
{
    const double sizes[3] = {
        bounds[1] - bounds[0],
        bounds[3] - bounds[2],
        bounds[5] - bounds[4],
    };
    const double minCorner[3] = {bounds[0], bounds[2], bounds[4]};
    const int padding = std::max(0, options.paddingVoxels);

    for (int axis = 0; axis < 3; ++axis)
    {
        // +1 让包围盒两端都落在体素节点上，再各向外扩 padding 层
        dimensions[axis] =
            static_cast<int>(std::ceil(sizes[axis] / options.spacingMm)) + 1 + 2 * padding;
        dimensions[axis] = std::max(dimensions[axis], 3);
        origin[axis] = minCorner[axis] - padding * options.spacingMm;
    }
}

/// 把网格灌进 VoxelGrid（两条体素化路径共用）
VoxelGrid finalizeGrid(vtkImageData* image, const VoxelizationOptions& options, bool robust,
                       std::string* error)
{
    VoxelGrid grid;
    if (image == nullptr || image->GetNumberOfPoints() <= 0)
    {
        if (error) { *error = "体素化失败：输出为空"; }
        return grid;
    }

    const double spacingXyz[3] = {options.spacingMm, options.spacingMm, options.spacingMm};
    int dimensions[3];
    image->GetDimensions(dimensions);

    grid.image = image;
    grid.image->SetSpacing(spacingXyz);
    std::copy(std::begin(spacingXyz), std::end(spacingXyz), grid.spacingMm);
    std::copy(std::begin(dimensions), std::end(dimensions), grid.dimensions);
    grid.usedRobustVoxelization = robust;

    const auto* scalars = static_cast<const unsigned char*>(image->GetScalarPointer());
    grid.totalVoxels = static_cast<long long>(image->GetNumberOfPoints());
    grid.insideVoxels = std::count_if(scalars, scalars + grid.totalVoxels,
                                      [](unsigned char value) { return value == kInside; });

    if (grid.insideVoxels == 0)
    {
        if (error)
        {
            *error = robust
                         ? "体素化结果为空：稳健路径也没找到内部体素（几何可能只有曲面、没有体积）"
                         : "体素化结果为空：网格可能不封闭，或体素尺寸过大";
        }
        return VoxelGrid{};
    }
    return grid;
}

} // namespace

VoxelGrid voxelize(vtkPolyData* mesh, const VoxelizationOptions& options, std::string* error)
{
    VoxelGrid grid;

    if (mesh == nullptr || mesh->GetNumberOfPolys() <= 0)
    {
        if (error) { *error = "网格为空或没有面片，无法体素化"; }
        return grid;
    }
    if (!(options.spacingMm > 0.0))
    {
        if (error) { *error = "体素尺寸必须大于 0"; }
        return grid;
    }

    const MeshQuality quality = analyzeMesh(mesh);
    if (quality.hasNonFinite())
    {
        if (error)
        {
            *error = "模型中存在 NaN/Inf 顶点坐标（" + std::to_string(quality.nonFinitePoints)
                     + " 个），无法体素化；请在建模软件里修复后重试";
        }
        return grid;
    }

    const double maxExtent = quality.maxExtentMm();
    if (!(maxExtent > 0.0))
    {
        if (error) { *error = "模型包围盒退化（三个方向尺寸都为 0），无法体素化"; }
        return grid;
    }

    // 单位护栏：比 2 个体素还小的模型几乎必然是单位错误（米/厘米被当成 mm）
    if (maxExtent < 2.0 * options.spacingMm)
    {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer),
                      "模型包围盒最大边长只有 %.4g mm，小于 2 个体素（%.4g mm）。"
                      "单位可能是米或厘米：请确认模型以 mm 为单位，或把体素尺寸改到 %.4g mm 以下",
                      maxExtent, 2.0 * options.spacingMm, maxExtent / 2.0);
        if (error) { *error = buffer; }
        return grid;
    }

    // 精度护栏：STL 顶点是 float32（相对精度约 1.2e-7）。模型离原点越远，
    // 顶点坐标被舍入得越厉害；当舍入步长超过 2 个体素时，几何细节已经被吃掉，
    // 体素化结果会静默偏小（实测平移 1e8 mm 的 40mm 立方体少了 35% 体积）。
    if (quality.maxAbsCoordinate > 0.0)
    {
        constexpr double kFloat32Eps = 1.2e-7;
        const double quantisationMm = quality.maxAbsCoordinate * kFloat32Eps;
        if (quantisationMm > 2.0 * options.spacingMm)
        {
            char buffer[320];
            std::snprintf(buffer, sizeof(buffer),
                          "模型坐标远离原点（最大 |坐标| = %.4g mm），单精度 STL 的舍入步长已达 %.3g mm，"
                          "超过 2 个体素（%.3g mm），几何细节已丢失。请先在建模软件里把模型移到原点附近"
                          "再导出，或减小体素尺寸到 %.3g mm 以下",
                          quality.maxAbsCoordinate, quantisationMm, 2.0 * options.spacingMm,
                          quantisationMm / 2.0);
            if (error) { *error = buffer; }
            return grid;
        }
    }

    // 单轴零厚度（纯曲面）无论 stencil 还是射线奇偶都判不出体积，直接拒绝
    for (int axis = 0; axis < 3; ++axis)
    {
        const double size = quality.bounds[2 * axis + 1] - quality.bounds[2 * axis];
        if (size <= 0.0)
        {
            if (error)
            {
                *error = "模型在某一轴上零厚度（纯平面/纯曲面），无法生成有体积的体素网格";
            }
            return grid;
        }
    }

    int dimensions[3] = {1, 1, 1};
    double origin[3] = {0.0, 0.0, 0.0};
    const double spacingXyz[3] = {options.spacingMm, options.spacingMm, options.spacingMm};
    computeGridGeometry(options, quality.bounds, dimensions, origin);

    // 内存护栏：先算体素数，超限就给出建议间距而不是硬着头皮分配
    const long long voxelCount =
        static_cast<long long>(dimensions[0]) * dimensions[1] * dimensions[2];
    if (options.maxVoxels > 0 && voxelCount > options.maxVoxels)
    {
        const double suggested = options.spacingMm
                                 * std::cbrt(static_cast<double>(voxelCount)
                                             / static_cast<double>(options.maxVoxels));
        char buffer[320];
        std::snprintf(buffer, sizeof(buffer),
                      "网格将达到 %d×%d×%d = %lld 个体素，超过上限 %lld。"
                      "请把体素尺寸放大到 %.3g mm 以上",
                      dimensions[0], dimensions[1], dimensions[2], voxelCount, options.maxVoxels,
                      suggested);
        if (error) { *error = buffer; }
        return grid;
    }

    // 几何整理：目标是拿到「水密 + 已合并」的实体，再交给 stencil 光栅化。
    // 三步逐级升级，任何一步失败都不会静默放过：
    //   ① 不水密 → 焊接重复点 + 补洞（轻量、无损，改的是导出瑕疵）
    //   ② 多壳体 / 仍不水密 / 非流形 → Manifold 布尔合并
    //      （实体求并集、法向朝内的壳体求差集）
    //   ③ 最后再严格校验一次水密，仍不行就明确拒绝
    vtkSmartPointer<vtkPolyData> meshToRasterize = mesh;
    long long repairedBoundaryEdges = 0;
    MeshQuality currentQuality = quality;

    if (!currentQuality.isWatertight() && options.autoRobustFallback)
    {
        const MeshRepairResult repaired = repairMesh(mesh, MeshRepairOptions{}, error);
        if (repaired.isValid() && repaired.isWatertightAfter())
        {
            meshToRasterize = repaired.polyData;
            repairedBoundaryEdges = repaired.filledBoundaryEdges;
            currentQuality = repaired.after;
        }
        else if (!options.autoRobustFallback)
        {
            if (error)
            {
                *error = "网格不水密（" + describeQuality(quality) + "），无法可靠判定内外";
            }
            return grid;
        }
    }
    else if (!currentQuality.isWatertight() && !options.autoRobustFallback)
    {
        if (error)
        {
            *error = "网格不水密（" + describeQuality(quality)
                     + "），无法可靠判定内外；请先封闭网格，或允许自动修复";
        }
        return grid;
    }

    // ② 布尔合并：多壳体（重叠/分离/内腔）或非流形时必须做，
    //    否则扫描线奇偶会把重叠区算成异或（40mm+30mm 偏移 15mm：73000 → 60010，且无告警）。
    long long mergedComponents = 0;
    double exactVolumeMm3 = 0.0;
    bool usedBooleanMerge = false;
    const long long shells = countShells(meshToRasterize);
    const bool needBoolean =
        options.booleanUnify || shells > 1 || !currentQuality.isWatertight();
    if (needBoolean)
    {
        std::string booleanError;
        const MeshBooleanResult unified = unifyComponents(meshToRasterize, &booleanError);
        if (!unified.isValid())
        {
            if (error)
            {
                *error = "几何合并失败：" + booleanError
                         + "（建议在建模软件里把各部件导出为独立的闭合实体）";
            }
            return grid;
        }
        mergedComponents = unified.components;
        exactVolumeMm3 = unified.volumeMm3;
        // 布尔路径的产出是 Manifold 重新三角化的流形实体，一律采用：
        // 它把重叠区并起来、把内腔挖掉，也顺手修掉了焊接后共享面的非流形接缝。
        meshToRasterize = unified.polyData;
        currentQuality = analyzeMesh(meshToRasterize);
        usedBooleanMerge = true;
    }

    if (!currentQuality.isWatertight())
    {
        if (error)
        {
            *error = "网格不水密（" + describeQuality(currentQuality)
                     + "），自动修复与布尔合并都没能消除这些缺陷，无法判定内外；"
                       "请先在建模软件里把网格封闭（开放曲面无法用于体素化）";
        }
        return grid;
    }

    vtkNew<vtkImageData> canvas;
    canvas->SetDimensions(dimensions);
    canvas->SetSpacing(spacingXyz);
    canvas->SetOrigin(origin);
    canvas->AllocateScalars(VTK_UNSIGNED_CHAR, 1);
    // 先整块填 1：vtkImageStencil 默认只把 stencil 之外置为背景值，
    // 因此“内部保持 1、外部变 0”正好得到内外掩膜。
    std::fill_n(static_cast<unsigned char*>(canvas->GetScalarPointer()),
                static_cast<size_t>(dimensions[0]) * dimensions[1] * dimensions[2], kInside);

    // 把闭合网格光栅化成 stencil，再用它裁剪空白体数据
    vtkNew<vtkPolyDataToImageStencil> rasterizer;
    rasterizer->SetInputData(meshToRasterize);
    rasterizer->SetOutputOrigin(origin);
    rasterizer->SetOutputSpacing(spacingXyz);
    rasterizer->SetOutputWholeExtent(0, dimensions[0] - 1, 0, dimensions[1] - 1, 0,
                                     dimensions[2] - 1);
    rasterizer->Update();

    vtkNew<vtkImageStencil> stencil;
    stencil->SetInputData(canvas);
    stencil->SetStencilConnection(rasterizer->GetOutputPort());
    stencil->SetBackgroundValue(kOutside);
    stencil->Update();

    vtkImageData* result = stencil->GetOutput();
    if (result == nullptr || result->GetNumberOfPoints() <= 0)
    {
        if (error) { *error = "体素化失败：stencil 输出为空"; }
        return grid;
    }
    result->SetSpacing(spacingXyz);
    result->SetOrigin(origin);

    VoxelGrid finalGrid = finalizeGrid(result, options, false, error);
    if (finalGrid.isValid())
    {
        finalGrid.usedBooleanMerge = usedBooleanMerge;
        if (repairedBoundaryEdges > 0 || usedBooleanMerge)
        {
            finalGrid.usedRobustVoxelization = true;
        }
        finalGrid.repairedBoundaryEdges = repairedBoundaryEdges;
        finalGrid.mergedComponents = mergedComponents;
        finalGrid.exactVolumeMm3 = exactVolumeMm3;
    }
    return finalGrid;
}
} // namespace precisioncbct::io
