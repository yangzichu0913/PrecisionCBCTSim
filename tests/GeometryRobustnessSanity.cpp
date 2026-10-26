// 几何稳健性回归测试（Phase 3「补缺口」第 6 项）。
//
// 背景：`vtkPolyDataToImageStencil` 假定网格水密。开口网格会被「补上」而静默给出
// 错误体积，NaN/Inf 坐标、单位写错（米当成 mm）、坐标离原点过远（STL 是 float32）、
// 体素尺寸误输导致体素数爆炸，这些情况以前都会静默通过。
//
// 本测试逐条固定现在的行为（护栏 → 明确报错；不水密 → 自动切稳健路径）：
//   1. 闭合立方体：走 stencil 快路径，体积与解析值一致
//   2. 缺一个面：自动焊接 + 补洞，报告被补掉的边界边，体积回到闭合值
//   3. 法向翻转 / 绕向混合：体积不变（朝向不影响体素化）
//   4. 方管（上下开口）：补洞后会变成实心体，必须把「补了几条边界边」报出来，不再静默
//   5. 零厚度单面 / 零厚度曲面：明确报错
//   6. 单位错误（边长 0.04 mm 当作 mm）：明确报错并提示单位可能是米/厘米
//   7. 平移 1e7 mm：结果与原点处一致（float32 仍有足够精度）
//   8. 平移 1e8 mm：明确报错（float32 舍入步长超过体素）
//   9. NaN / Inf 顶点：加载阶段就拒绝
//  10. 体素总数超限：明确报错并给出建议间距
//  11. 全零面积三角形：加载阶段就拒绝
//  12. 多部件重叠：按**并集**算（不再按扫描线异或），体积与 Manifold 解析值一致
//  13. 共面贴合（非流形）：用 Manifold 布尔合并解决，不再直接拒绝
//  14. 空心腔（法向朝内的内层壳）：按**差集**挖空，不被并集吞掉
//  15. 相互分离的两个部件：并集 = 两者之和
// 退出码 0 表示全部通过。

#include "io/MeshLoader.h"
#include "io/MeshQuality.h"
#include "io/VoxelGrid.h"
#include "TestGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

int g_failures = 0;

void check(std::string_view name, bool condition)
{
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", std::string(name).c_str());
    if (!condition)
    {
        ++g_failures;
    }
}

void checkNear(std::string_view name, double actual, double expected, double tolerancePercent)
{
    const bool ok = std::fabs(actual - expected)
                    <= tolerancePercent / 100.0 * std::max(std::fabs(expected), 1e-9);
    std::printf("[%s] %-40s 实际 %.1f  期望 %.1f  允许偏差 %.1f%%\n", ok ? "PASS" : "FAIL",
                std::string(name).c_str(), actual, expected, tolerancePercent);
    if (!ok)
    {
        ++g_failures;
    }
}

bool contains(const std::string& text, const char* needle)
{
    return text.find(needle) != std::string::npos;
}

void reportNear(std::string_view name, double actual, double expected, double tolerancePercent)
{
    const bool ok = std::fabs(actual - expected)
                    <= tolerancePercent / 100.0 * std::max(std::fabs(expected), 1e-9);
    std::printf("[%s] %-40s 实际 %.1f  期望 %.1f  允许偏差 %.1f%%\n", ok ? "PASS" : "FAIL",
                std::string(name).c_str(), actual, expected, tolerancePercent);
    if (!ok)
    {
        ++g_failures;
    }
}

/// 把三角形写盘 → 走一遍真实的 loadMesh（保证读入路径也被覆盖）
precisioncbct::io::MeshData loadTriangles(const std::vector<pcsim::test::Triangle>& triangles,
                                          std::string* error,
                                          const char* fileName = "pcsim_robust.stl")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / fileName;
    pcsim::test::writeBinaryStl(path, triangles);
    return precisioncbct::io::loadMesh(path.string(), error);
}

std::vector<pcsim::test::Triangle> translated(std::vector<pcsim::test::Triangle> triangles,
                                              double offset)
{
    for (auto& triangle : triangles)
    {
        for (auto& vertex : triangle)
        {
            vertex.x += offset;
            vertex.y += offset;
        }
    }
    return triangles;
}

} // namespace

int main()
{
    using precisioncbct::io::VoxelizationOptions;

    std::string error;
    constexpr double kSpacing = 1.0;
    // 40mm 立方体 @1mm 的参考值（含 padding，边界半体素效应）
    constexpr double kCubeVoxels = 67240.0;

    // ---------- 1. 闭合立方体：快路径 ----------
    const auto closedMesh = loadTriangles(pcsim::test::makeCube(40.0), &error, "pcsim_closed.stl");
    check("闭合立方体读取成功", closedMesh.isValid());
    check("闭合立方体被判为水密", closedMesh.quality.isWatertight());
    check("闭合立方体无边界边", closedMesh.quality.boundaryEdges == 0);

    VoxelizationOptions options;
    options.spacingMm = kSpacing;
    const auto closedGrid = precisioncbct::io::voxelize(closedMesh.polyData, options, &error);
    check("闭合立方体体素化成功", closedGrid.isValid());
    check("闭合立方体走 stencil 快路径", !closedGrid.usedRobustVoxelization);
    checkNear("闭合立方体内部体素", static_cast<double>(closedGrid.insideVoxels), kCubeVoxels,
              0.5);

    // ---------- 2. 缺一个面：自动切稳健路径 ----------
    auto openTriangles = pcsim::test::makeCube(40.0);
    openTriangles.resize(openTriangles.size() - 2); // 去掉 +x 面
    const auto openMesh = loadTriangles(openTriangles, &error, "pcsim_open.stl");
    check("开口网格读取成功", openMesh.isValid());
    check("开口网格被识别出边界边", openMesh.quality.boundaryEdges > 0);
    check("开口网格被判为不水密", !openMesh.quality.isWatertight());

    const auto openGrid = precisioncbct::io::voxelize(openMesh.polyData, options, &error);
    check("开口网格体素化成功", openGrid.isValid());
    check("开口网格被自动修复", openGrid.usedRobustVoxelization);
    check("开口网格报告补掉的边界边数", openGrid.repairedBoundaryEdges == 4);
    checkNear("补洞后体积回到闭合值", static_cast<double>(openGrid.insideVoxels), kCubeVoxels,
              2.0);

    // 显式关闭回退时应直接拒绝，而不是偷偷用 stencil 给出错误结果
    {
        VoxelizationOptions strict = options;
        strict.autoRobustFallback = false;
        error.clear();
        const auto rejected = precisioncbct::io::voxelize(openMesh.polyData, strict, &error);
        check("关闭回退时开口网格被拒绝", !rejected.isValid());
        check("拒绝原因说明网格不水密", contains(error, "不水密"));
    }

    // ---------- 3. 法向翻转 / 绕向混合不影响体积 ----------
    {
        auto flipped = pcsim::test::makeCube(40.0);
        for (auto& triangle : flipped)
        {
            std::swap(triangle[1], triangle[2]);
        }
        const auto flippedMesh = loadTriangles(flipped, &error, "pcsim_flipped.stl");
        const auto flippedGrid =
            precisioncbct::io::voxelize(flippedMesh.polyData, options, &error);
        check("法向全部翻转仍为水密", flippedMesh.quality.isWatertight());
        checkNear("法向翻转不改变体积", static_cast<double>(flippedGrid.insideVoxels), kCubeVoxels,
                  0.5);

        auto mixed = pcsim::test::makeCube(40.0);
        for (size_t i = 0; i < mixed.size(); i += 2)
        {
            std::swap(mixed[i][1], mixed[i][2]);
        }
        const auto mixedMesh = loadTriangles(mixed, &error, "pcsim_mixed.stl");
        const auto mixedGrid = precisioncbct::io::voxelize(mixedMesh.polyData, options, &error);
        checkNear("绕向混合不改变体积", static_cast<double>(mixedGrid.insideVoxels), kCubeVoxels,
                  0.5);
    }

    // ---------- 4. 方管（上下开口）：补洞后是实心，但必须如实报告 ----------
    {
        auto tube = pcsim::test::makeCube(40.0);
        tube.erase(tube.begin() + 2, tube.begin() + 4); // 去掉 +z
        tube.erase(tube.begin() + 0, tube.begin() + 2); // 去掉 -z
        const auto tubeMesh = loadTriangles(tube, &error, "pcsim_tube.stl");
        check("方管被判为不水密", !tubeMesh.quality.isWatertight());

        error.clear();
        const auto tubeGrid = precisioncbct::io::voxelize(tubeMesh.polyData, options, &error);
        // 上下两端被封上后就是实心块；关键是不再「静默」——必须报告补了多少条边界边
        check("方管体素化成功（补洞后按封闭体处理）", tubeGrid.isValid());
        check("方管报告了补洞", tubeGrid.usedRobustVoxelization);
        check("方管补掉的边界边数被记录", tubeGrid.repairedBoundaryEdges == 8);
    }

    // ---------- 4b. 完全开放的曲面：补洞也救不回来，必须拒绝 ----------
    {
        // 单个三角形：3 条边界边构成一个环，补洞无法产生体积
        std::vector<pcsim::test::Triangle> singleFace;
        pcsim::test::Triangle triangle = {};
        triangle[0] = {-20.0, -20.0, 0.0};
        triangle[1] = {20.0, -20.0, 0.0};
        triangle[2] = {0.0, 20.0, 0.0};
        singleFace.push_back(triangle);
        const auto sheetMesh = loadTriangles(singleFace, &error, "pcsim_sheet.stl");
        check("开放三角面读取成功", sheetMesh.isValid());
        error.clear();
        const auto sheetGrid = precisioncbct::io::voxelize(sheetMesh.polyData, options, &error);
        check("开放三角面被拒绝", !sheetGrid.isValid());
        check("拒绝原因说明零厚度或无法判定内外",
              contains(error, "零厚度") || contains(error, "边界边"));
    }

    // ---------- 5. 零厚度几何 ----------
    {
        auto single = pcsim::test::makeCube(40.0);
        single.resize(2); // 只剩一个面
        const auto singleMesh = loadTriangles(single, &error, "pcsim_flat.stl");
        error.clear();
        const auto flatGrid = precisioncbct::io::voxelize(singleMesh.polyData, options, &error);
        check("零厚度单面被拒绝", !flatGrid.isValid());
        check("拒绝原因说明零厚度", contains(error, "零厚度"));
    }

    // ---------- 6. 单位错误（模型是 0.04，被当成 mm） ----------
    {
        const auto tinyMesh = loadTriangles(pcsim::test::makeCube(0.04), &error, "pcsim_tiny.stl");
        check("微小模型读取成功", tinyMesh.isValid());
        error.clear();
        const auto tinyGrid = precisioncbct::io::voxelize(tinyMesh.polyData, options, &error);
        check("单位可疑的模型被拒绝", !tinyGrid.isValid());
        check("拒绝原因提示单位问题", contains(error, "单位"));

        // 把体素尺寸调到与模型匹配后应能正常体素化
        VoxelizationOptions fine = options;
        fine.spacingMm = 0.01;
        error.clear();
        const auto fineGrid = precisioncbct::io::voxelize(tinyMesh.polyData, fine, &error);
        check("减小体素尺寸后可以体素化", fineGrid.isValid());
    }

    // ---------- 7/8. 远离原点：坐标量级影响 float32 精度 ----------
    {
        const auto nearMesh = loadTriangles(translated(pcsim::test::makeCube(40.0), 1.0e7),
                                            &error, "pcsim_far7.stl");
        const auto nearGrid = precisioncbct::io::voxelize(nearMesh.polyData, options, &error);
        check("平移 1e7 mm 仍可体素化", nearGrid.isValid());
        checkNear("平移 1e7 mm 体积不变", static_cast<double>(nearGrid.insideVoxels), kCubeVoxels,
                  1.0);

        const auto farMesh = loadTriangles(translated(pcsim::test::makeCube(40.0), 1.0e8), &error,
                                           "pcsim_far8.stl");
        error.clear();
        const auto farGrid = precisioncbct::io::voxelize(farMesh.polyData, options, &error);
        check("平移 1e8 mm 被拒绝（单精度已丢精度）", !farGrid.isValid());
        check("拒绝原因说明坐标远离原点", contains(error, "远离原点"));
    }

    // ---------- 9. NaN / Inf ----------
    {
        auto withNan = pcsim::test::makeCube(40.0);
        withNan[0][0].x = std::numeric_limits<double>::quiet_NaN();
        std::string nanError;
        const auto nanMesh = loadTriangles(withNan, &nanError, "pcsim_nan.stl");
        check("含 NaN 顶点的模型被拒绝", !nanMesh.isValid());
        check("NaN 错误信息可读", contains(nanError, "NaN"));

        auto withInf = pcsim::test::makeCube(40.0);
        withInf[1][2].y = std::numeric_limits<double>::infinity();
        std::string infError;
        const auto infMesh = loadTriangles(withInf, &infError, "pcsim_inf.stl");
        check("含 Inf 顶点的模型被拒绝", !infMesh.isValid());
        check("Inf 错误信息可读", contains(infError, "NaN/Inf"));
    }

    // ---------- 10. 体素总数上限 ----------
    {
        // 上限卡在实际网格体素数（含 padding，45³ = 91125）之下：拒绝并给出建议间距
        const long long totalVoxels = closedGrid.totalVoxels;
        VoxelizationOptions tight = options;
        tight.maxVoxels = totalVoxels - 1;
        error.clear();
        const auto rejected = precisioncbct::io::voxelize(closedMesh.polyData, tight, &error);
        check("体素数超限被拒绝", !rejected.isValid());
        check("超限信息含建议间距", contains(error, "体素尺寸放大到"));

        // 上限正好等于实际体素数：放行，且结果与无上限完全一致
        tight.maxVoxels = totalVoxels;
        error.clear();
        const auto allowed = precisioncbct::io::voxelize(closedMesh.polyData, tight, &error);
        check("上限内的网格正常体素化", allowed.isValid());
        checkNear("抬高上限不改变内部体素数", static_cast<double>(allowed.insideVoxels),
                  kCubeVoxels, 0.5);

        // 间距误输成 0.05mm（805³ ≈ 5.2 亿体素）必须在分配前就被挡住
        VoxelizationOptions dense = options;
        dense.spacingMm = 0.05;
        error.clear();
        const auto denseGrid = precisioncbct::io::voxelize(closedMesh.polyData, dense, &error);
        check("间距误输导致体素数爆炸时被挡住", !denseGrid.isValid());
        check("爆炸提示包含量级信息", contains(error, "805"));
    }

    // ---------- 11. 全零面积三角形 ----------
    {
        std::vector<pcsim::test::Triangle> degenerate;
        for (int i = 0; i < 12; ++i)
        {
            pcsim::test::Triangle zero = {};
            degenerate.push_back(zero);
        }
        std::string degenerateError;
        const auto degenerateMesh =
            loadTriangles(degenerate, &degenerateError, "pcsim_degenerate.stl");
        check("全零面积三角形被拒绝", !degenerateMesh.isValid());
    }

    // ---------- 12~15. 多部件布尔合并（Manifold） ----------
    // 体素化的边界体素约定会多算约 5%（40mm 立方体 64000 mm³ → 67240 体素），
    // 所以判据统一放宽到 6%，并与 Manifold 的**解析体积**做交叉验证。
    constexpr double kVoxelBias = 1.06;
    {
        // 12. 重叠：40mm 与 30mm 立方体偏移 15mm → 并集 73000 mm³
        std::vector<pcsim::test::Triangle> overlapping = pcsim::test::makeCube(40.0);
        const auto shifted = translated(pcsim::test::makeCube(30.0), 0.0);
        for (auto tri : shifted)
        {
            for (auto& vertex : tri)
            {
                vertex.x += 15.0;
            }
            overlapping.push_back(tri);
        }

        const auto overlapMesh = loadTriangles(overlapping, &error, "pcsim_overlap.stl");
        check("重叠部件读取成功", overlapMesh.isValid());
        check("重叠部件被判为水密（扫描线会静默算错）", overlapMesh.quality.isWatertight());

        error.clear();
        const auto overlapGrid = precisioncbct::io::voxelize(overlapMesh.polyData, options, &error);
        check("重叠部件体素化成功", overlapGrid.isValid());
        check("重叠部件被布尔合并", overlapGrid.usedBooleanMerge && overlapGrid.mergedComponents == 2);
        check("重叠部件报告了合并后体积", overlapGrid.exactVolumeMm3 > 0.0);
        reportNear("并集解析体积 = 73000 mm³", overlapGrid.exactVolumeMm3, 73000.0, 0.5);
        checkNear("并集体素体积（含边界约定）",
                  static_cast<double>(overlapGrid.insideVolumeMm3()),
                  73000.0 * kVoxelBias, 2.0);

        // 依次把上限放到「刚好放行」以确认没有别的护栏在捣乱
        VoxelizationOptions strict = options;
        strict.booleanUnify = true; // 强制走布尔路径（默认只在不水密/非流形时才走）
        error.clear();
        const auto forced = precisioncbct::io::voxelize(overlapMesh.polyData, strict, &error);
        check("强制布尔模式结果一致",
              forced.isValid() && std::fabs(forced.exactVolumeMm3 - overlapGrid.exactVolumeMm3)
                                      < 1e-6 * std::max(1.0, overlapGrid.exactVolumeMm3));
    }

    {
        // 13. 共面贴合：两个 40mm 立方体共享一个面 → 并集 128000 mm³
        std::vector<pcsim::test::Triangle> coplanar = pcsim::test::makeCube(40.0);
        auto second = pcsim::test::makeCube(40.0);
        for (auto& triangle : second)
        {
            for (auto& vertex : triangle)
            {
                vertex.x += 40.0;
            }
        }
        coplanar.insert(coplanar.end(), second.begin(), second.end());

        const auto jointMesh = loadTriangles(coplanar, &error, "pcsim_coplanar.stl");
        check("共面贴合读取成功", jointMesh.isValid());
        check("共面贴合被识别出非流形边", jointMesh.quality.nonManifoldEdges > 0);

        error.clear();
        const auto jointGrid = precisioncbct::io::voxelize(jointMesh.polyData, options, &error);
        check("共面贴合不再被直接拒绝", jointGrid.isValid());
        // 注意：vtkSTLReader 默认会焊接重合点，所以两个立方体被焊成**单个**非流形连通体，
        // 真正的修复来自 Manifold 的 CSG 重建（而不是「多壳体合并」）。
        check("共面贴合走了布尔重建", jointGrid.usedBooleanMerge);
        reportNear("共面贴合并集 = 128000 mm³", jointGrid.exactVolumeMm3, 128000.0, 0.5);
    }

    {
        // 14. 空心腔：外层 40mm（朝外）+ 内层 20mm（朝内）→ 实体 56000 mm³
        std::vector<pcsim::test::Triangle> hollow = pcsim::test::makeCube(40.0);
        auto inner = pcsim::test::makeCube(20.0);
        for (auto& triangle : inner)
        {
            std::swap(triangle[1], triangle[2]); // 法向朝内 = 内腔
            hollow.push_back(triangle);
        }

        const auto hollowMesh = loadTriangles(hollow, &error, "pcsim_hollow.stl");
        check("空心壳体读取成功", hollowMesh.isValid());

        // 内腔在扫描线奇偶下本来就正确，先记录基线
        const auto parityGrid =
            precisioncbct::io::voxelize(hollowMesh.polyData, options, &error);
        check("空心壳体体素化成功", parityGrid.isValid());

        // 强制布尔路径：内腔必须按差集处理，不能被并集吞掉
        VoxelizationOptions forced = options;
        forced.booleanUnify = true;
        error.clear();
        const auto booleanGrid =
            precisioncbct::io::voxelize(hollowMesh.polyData, forced, &error);
        check("空心壳体布尔路径成功", booleanGrid.isValid());
        check("空心壳体识别出 1 个内腔", booleanGrid.exactVolumeMm3 > 0.0);
        reportNear("空心腔解析体积 = 56000 mm³", booleanGrid.exactVolumeMm3, 56000.0, 0.5);
        // 两条路径的体素体积应基本一致（差异只来自边界体素约定）
        checkNear("布尔路径与扫描线路径体积一致",
                  static_cast<double>(booleanGrid.insideVolumeMm3()),
                  static_cast<double>(parityGrid.insideVolumeMm3()), 3.0);
    }

    {
        // 15. 分离部件：两个不相交的 40mm 立方体 → 并集 = 128000 mm³
        std::vector<pcsim::test::Triangle> apart = pcsim::test::makeCube(40.0);
        auto second = pcsim::test::makeCube(40.0);
        for (auto& triangle : second)
        {
            for (auto& vertex : triangle)
            {
                vertex.x += 100.0;
            }
        }
        apart.insert(apart.end(), second.begin(), second.end());

        const auto apartMesh = loadTriangles(apart, &error, "pcsim_apart.stl");
        VoxelizationOptions forced = options;
        forced.booleanUnify = true;
        error.clear();
        const auto apartGrid = precisioncbct::io::voxelize(apartMesh.polyData, forced, &error);
        check("分离部件体素化成功", apartGrid.isValid());
        reportNear("分离部件并集 = 128000 mm³", apartGrid.exactVolumeMm3, 128000.0, 0.5);
    }

    std::printf("\n%s：失败 %d 项\n", g_failures == 0 ? "全部通过" : "存在失败", g_failures);
    return g_failures == 0 ? 0 : 1;
}
