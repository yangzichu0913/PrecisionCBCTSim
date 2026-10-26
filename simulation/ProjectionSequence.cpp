#include "simulation/ProjectionSequence.h"

#include "io/VoxelGrid.h"
#include "simulation/DetectorModel.h"
#include "simulation/SpectralProjector.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace precisioncbct::simulation {
namespace {

constexpr double kAngleEpsilon = 1e-9;

} // namespace

std::vector<double> sequenceAngles(const ScanParameters& parameters)
{
    std::vector<double> angles;
    if (!(parameters.angleStepDeg > 0.0))
    {
        return angles;
    }

    const bool ascending = parameters.endAngleDeg >= parameters.startAngleDeg;
    const double span = std::fabs(parameters.endAngleDeg - parameters.startAngleDeg);
    const int count = static_cast<int>(std::floor(span / parameters.angleStepDeg + kAngleEpsilon)) + 1;
    angles.reserve(static_cast<size_t>(count));

    for (int i = 0; i < count; ++i)
    {
        const double offset = i * parameters.angleStepDeg;
        angles.push_back(ascending ? parameters.startAngleDeg + offset
                                   : parameters.startAngleDeg - offset);
    }
    return angles;
}

SequenceResult runSequence(const io::VoxelGrid& grid,
                           const ScanParameters& scan,
                           const ProjectionParameters& projectionParams,
                           const FrameVisitor& visitor,
                           std::string* error,
                           const SpectralProjectionOptions* spectral,
                           const DetectorApplication* detector)
{
    SequenceResult result;

    if (!grid.isValid())
    {
        if (error) { *error = "体素网格无效，请先完成体素化"; }
        return result;
    }

    const std::vector<double> angles = sequenceAngles(scan);
    if (angles.empty())
    {
        if (error) { *error = "角度序列为空：请确认角度步长 > 0"; }
        return result;
    }
    if (!visitor)
    {
        if (error) { *error = "内部错误：缺少帧回调"; }
        return result;
    }

    ConeBeamView view;
    view.sourceAxisDistanceMm = scan.sourceAxisDistanceMm;
    view.sourceDetectorDistanceMm = scan.sourceDetectorDistanceMm;
    view.pixelSizeMm = scan.detectorPixelSizeMm;
    view.columns = scan.detectorColumns;
    view.rows = scan.detectorRows;
    if (!view.update())
    {
        if (error) { *error = "投影几何参数非法（需满足 0 < SAD < SDD、像素尺寸 > 0、行列数 > 0）"; }
        return result;
    }

    result.frameCount = static_cast<int>(angles.size());
    result.width = scan.detectorColumns;
    result.height = scan.detectorRows;
    result.firstAngleDeg = angles.front();
    result.lastAngleDeg = angles.back();

    const auto started = std::chrono::steady_clock::now();

    for (int index = 0; index < result.frameCount; ++index)
    {
        view.angleDeg = angles[static_cast<size_t>(index)];
        if (!view.update())
        {
            if (error) { *error = "投影几何参数非法"; }
            return result;
        }

        DrProjection projection =
            spectral != nullptr ? toDrProjection(projectConeBeamSpectral(grid, view, *spectral, error))
                                : projectConeBeam(grid, view, projectionParams, error);
        // 单一 μ 路径：探测器模型在这里叠加（谱路径已在投影内部完成）
        if (!projection.isValid())
        {
            return result; // error 已由投影函数填写
        }
        if (spectral == nullptr && detector != nullptr && detector->options.hasAnyEffect())
        {
            if (!applyDetectorModelToDr(&projection, detector->options,
                                        detector->incidentPhotonsPerPixel,
                                        detector->quantumNoise, detector->seed, error))
            {
                return result;
            }
        }

        result.maxLineIntegralMm = std::max<double>(result.maxLineIntegralMm,
                                                    projection.maxLineIntegralMm);
        ++result.completedFrames;

        if (!visitor(index, result.frameCount, view.angleDeg, projection))
        {
            result.cancelled = true;
            break;
        }
    }

    const auto finished = std::chrono::steady_clock::now();
    result.elapsedMs =
        std::chrono::duration<double, std::milli>(finished - started).count();
    result.ok = !result.cancelled && result.completedFrames == result.frameCount;
    return result;
}

} // namespace precisioncbct::simulation
