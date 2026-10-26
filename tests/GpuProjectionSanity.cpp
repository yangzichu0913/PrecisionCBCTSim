// CUDA 光线追踪后端的正确性与性能自检。
//
// 判据：
//   1. 设备可用时能拿到设备名（打印出来，便于确认跑在哪块卡上）
//   2. GPU 与 CPU 的路径长度逐像素一致（相对差 ≤ 0.5%，来自浮点累加顺序）
//   3. 多材料（标签）路径长度同样一致
//   4. 结构化 phantom（球）上的透射率一致
//   5. 打印单帧耗时对比（仅供参考，不做硬性断言——不同机器差异很大）
// 没有可用 GPU（或构建时未启用 CUDA）时，打印原因并以 0 退出，不让 CI 挂。

#include "io/MeshLoader.h"
#include "io/MaterialLabeling.h"
#include "io/VoxelGrid.h"
#include "simulation/ConeBeamView.h"
#include "simulation/CpuProjector.h"
#include "simulation/CudaProjector.h"
#include "TestGeometry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <vtkImageData.h>
#include <vtkSmartPointer.h>

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

/// 比较两个 float 场，返回最大相对偏差（以 max|a| 为分母）与平均绝对偏差
void compareFields(std::string_view name, const std::vector<float>& a, const std::vector<float>& b,
                   double tolerancePercent)
{
    if (a.size() != b.size() || a.empty())
    {
        check(name, false);
        return;
    }
    double maxAbs = 0.0;
    double maxDiff = 0.0;
    double sumDiff = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        maxAbs = std::max(maxAbs, std::fabs(static_cast<double>(a[i])));
        const double diff = std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
        maxDiff = std::max(maxDiff, diff);
        sumDiff += diff;
    }
    const double reference = std::max(maxAbs, 1e-12);
    const double relative = 100.0 * maxDiff / reference;
    const double meanRelative = 100.0 * (sumDiff / static_cast<double>(a.size())) / reference;
    const bool ok = relative <= tolerancePercent;
    std::printf("[%s] %-34s 最大相对差 %.4f%%（平均 %.5f%%）上限 %.2f%%\n", ok ? "PASS" : "FAIL",
                std::string(name).c_str(), relative, meanRelative, tolerancePercent);
    if (!ok)
    {
        ++g_failures;
    }
}

} // namespace

int main()
{
    using precisioncbct::simulation::ComputeBackend;
    using precisioncbct::simulation::ConeBeamView;

    std::printf("CUDA 设备：%s\n",
                precisioncbct::simulation::cudaAvailable()
                    ? precisioncbct::simulation::cudaDeviceDescription().c_str()
                    : "(不可用)");
    if (!precisioncbct::simulation::cudaAvailable())
    {
        std::printf("跳过：%s\n",
                    precisioncbct::simulation::lastCudaError().empty()
                        ? "本构建未启用 CUDA 或没有可用设备"
                        : precisioncbct::simulation::lastCudaError().c_str());
        return 0;
    }

    // ---------- phantom：r=50mm 球 @2mm 体素（与主程序默认量级接近） ----------
    const std::filesystem::path spherePath =
        std::filesystem::temp_directory_path() / "pcsim_gpu_sphere.stl";
    pcsim::test::writeBinaryStl(spherePath, pcsim::test::makeIcosphere(50.0, 3));

    std::string error;
    const auto mesh = precisioncbct::io::loadMesh(spherePath.string(), &error);
    if (!mesh.isValid())
    {
        std::printf("FAIL 球模型读取失败: %s\n", error.c_str());
        return 1;
    }

    precisioncbct::io::VoxelizationOptions voxelOptions;
    voxelOptions.spacingMm = 2.0;
    const auto grid = precisioncbct::io::voxelize(mesh.polyData, voxelOptions, &error);
    if (!grid.isValid())
    {
        std::printf("FAIL 体素化失败: %s\n", error.c_str());
        return 1;
    }
    std::printf("体素网格：%d×%d×%d @ %.1f mm\n", grid.dimensions[0], grid.dimensions[1],
                grid.dimensions[2], grid.spacingMm[0]);

    ConeBeamView view;
    view.sourceAxisDistanceMm = 500.0;
    view.sourceDetectorDistanceMm = 1000.0;
    view.pixelSizeMm = 0.4;
    view.columns = 512;
    view.rows = 512;
    view.angleDeg = 30.0;

    // ---------- 1. 单材料路径长度对拍 ----------
    precisioncbct::simulation::setPreferredBackend(ComputeBackend::Cpu);
    const auto cpuField = precisioncbct::simulation::computePathLengths(grid, view, &error);
    check("CPU 路径长度有效", cpuField.isValid());

    precisioncbct::simulation::setPreferredBackend(ComputeBackend::Gpu);
    const auto gpuField = precisioncbct::simulation::computePathLengths(grid, view, &error);
    check("GPU 路径长度有效", gpuField.isValid());
    // 关键：GPU 失败会自动回退 CPU，结果看起来一样但根本没加速，所以必须显式检查没有回退
    check("GPU 后端没有被静默回退",
          precisioncbct::simulation::lastCudaError().empty());

    if (cpuField.isValid() && gpuField.isValid())
    {
        compareFields("单材料路径长度 CPU vs GPU", cpuField.lengthsMm, gpuField.lengthsMm, 0.5);
    }

    // ---------- 2. 多材料（标签）路径长度对拍 ----------
    precisioncbct::io::LabelingOptions labelingOptions;
    labelingOptions.shellThicknessMm = 6.0;
    labelingOptions.shellMaterial = precisioncbct::simulation::Material::CorticalBone;
    labelingOptions.coreMaterial = precisioncbct::simulation::Material::Water;
    precisioncbct::io::MaterialLabeling labeling;
    const bool labeled =
        precisioncbct::io::labelMaterials(grid, labelingOptions, &labeling, &error);
    check("材料标签生成成功", labeled && labeling.isValid());

    if (labeled && labeling.isValid())
    {
        precisioncbct::io::VoxelGrid labelGrid;
        labelGrid.image = vtkSmartPointer<vtkImageData>::New();
        labelGrid.image->DeepCopy(labeling.labels);
        std::copy(std::begin(grid.dimensions), std::end(grid.dimensions), labelGrid.dimensions);
        std::copy(std::begin(grid.spacingMm), std::end(grid.spacingMm), labelGrid.spacingMm);
        labelGrid.totalVoxels = grid.totalVoxels;
        labelGrid.insideVoxels = grid.insideVoxels;

        precisioncbct::simulation::setPreferredBackend(ComputeBackend::Cpu);
        const auto cpuMaterials = precisioncbct::simulation::computeMaterialPathLengths(
            labelGrid, view, static_cast<int>(labeling.materials.size()), &error);

        precisioncbct::simulation::setPreferredBackend(ComputeBackend::Gpu);
        const auto gpuMaterials = precisioncbct::simulation::computeMaterialPathLengths(
            labelGrid, view, static_cast<int>(labeling.materials.size()), &error);

        check("多材料 CPU/GPU 都有效", cpuMaterials.isValid() && gpuMaterials.isValid());
        if (cpuMaterials.isValid() && gpuMaterials.isValid())
        {
            // 把 4 条 lane 摊平后再比
            std::vector<float> cpuFlat;
            std::vector<float> gpuFlat;
            cpuFlat.reserve(cpuMaterials.lengthsMm.size() * 2);
            gpuFlat.reserve(gpuMaterials.lengthsMm.size() * 2);
            for (size_t i = 0; i < cpuMaterials.lengthsMm.size(); ++i)
            {
                for (size_t lane = 0; lane < 2; ++lane)
                {
                    cpuFlat.push_back(cpuMaterials.lengthsMm[i][lane]);
                    gpuFlat.push_back(gpuMaterials.lengthsMm[i][lane]);
                }
            }
            compareFields("双材料路径长度 CPU vs GPU", cpuFlat, gpuFlat, 0.5);
        }
    }

    // ---------- 3. 投影（透射率）对拍 ----------
    {
        precisioncbct::simulation::ProjectionParameters params;

        precisioncbct::simulation::setPreferredBackend(ComputeBackend::Cpu);
        const auto cpuProjection =
            precisioncbct::simulation::projectConeBeam(grid, view, params, &error);

        precisioncbct::simulation::setPreferredBackend(ComputeBackend::Gpu);
        const auto gpuProjection =
            precisioncbct::simulation::projectConeBeam(grid, view, params, &error);

        check("CPU/GPU 投影都有效", cpuProjection.isValid() && gpuProjection.isValid());
        if (cpuProjection.isValid() && gpuProjection.isValid())
        {
            compareFields("透射率 CPU vs GPU", cpuProjection.transmission,
                          gpuProjection.transmission, 0.5);
        }
    }

    // ---------- 4. 直接计时 CudaPathLengthSolver（排除回退干扰） ----------
    {
        precisioncbct::simulation::CudaGridInput input;
        input.labels = static_cast<const unsigned char*>(grid.image->GetScalarPointer());
        std::copy(std::begin(grid.dimensions), std::end(grid.dimensions), input.dimensions);
        double gridOrigin[3];
        grid.image->GetOrigin(gridOrigin);
        std::copy(std::begin(gridOrigin), std::end(gridOrigin), input.origin);
        std::copy(std::begin(grid.spacingMm), std::end(grid.spacingMm), input.spacingMm);

        precisioncbct::simulation::CudaPathLengthSolver solver;
        std::string solverError;
        const bool initialized = solver.initializeGrid(input, &solverError);
        check("GPU 求解器初始化成功" + std::string(initialized ? "" : "：" + solverError), initialized);

        if (initialized)
        {
            std::vector<float> buffer;
            ConeBeamView frame = view;
            (void)solver.compute(frame, 1, &buffer, &solverError); // 热身

            constexpr int kRepeats = 10;
            const auto started = std::chrono::steady_clock::now();
            for (int i = 0; i < kRepeats; ++i)
            {
                frame.angleDeg = 360.0 * i / kRepeats;
                if (!solver.compute(frame, 1, &buffer, &solverError))
                {
                    std::printf("FAIL GPU 求解失败：%s\n", solverError.c_str());
                    ++g_failures;
                    break;
                }
            }
            const auto finished = std::chrono::steady_clock::now();
            const double perFrameMs =
                std::chrono::duration<double, std::milli>(finished - started).count() / kRepeats;
            const size_t nonZero = static_cast<size_t>(
                std::count_if(buffer.begin(), buffer.end(), [](float v) { return v > 0.0f; }));
            std::printf("       GPU 求解器单帧 %.2f ms（%zu/%zu 像素有路径）\n", perFrameMs, nonZero,
                        buffer.size());
            check("GPU 单帧耗时低于 CPU 的 1/2", perFrameMs * 2.0 < 63.1 * 0.5);
        }
    }

    // ---------- 5. 单帧耗时对比（信息性，不做断言） ----------
    {
        constexpr int kFrames = 6;

        auto timeBackend = [&](ComputeBackend backend) {
            precisioncbct::simulation::setPreferredBackend(backend);
            ConeBeamView frame = view;
            // 第一帧热身（GPU 还要建上下文/传掩膜）
            (void)precisioncbct::simulation::computePathLengths(grid, frame, &error);

            const auto started = std::chrono::steady_clock::now();
            for (int i = 0; i < kFrames; ++i)
            {
                frame.angleDeg = 360.0 * i / kFrames;
                (void)precisioncbct::simulation::computePathLengths(grid, frame, &error);
            }
            const auto finished = std::chrono::steady_clock::now();
            return std::chrono::duration<double, std::milli>(finished - started).count() / kFrames;
        };

        const double cpuMs = timeBackend(ComputeBackend::Cpu);
        const double gpuMs = timeBackend(ComputeBackend::Gpu);
        std::printf("       单帧耗时：CPU %.1f ms，GPU %.1f ms（%dx 加速，512² 探测器）\n", cpuMs,
                    gpuMs, static_cast<int>(std::lround(cpuMs / std::max(gpuMs, 1e-6))));
    }

    precisioncbct::simulation::setPreferredBackend(ComputeBackend::Auto);
    std::printf("\n%s：失败 %d 项\n", g_failures == 0 ? "全部通过" : "存在失败", g_failures);
    return g_failures == 0 ? 0 : 1;
}
