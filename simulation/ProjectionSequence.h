#pragma once

#include <functional>
#include <string>
#include <vector>

#include "simulation/CpuProjector.h"
#include "simulation/ScanParameters.h"

namespace precisioncbct::io {
struct VoxelGrid;
}

namespace precisioncbct::simulation {

struct SpectralProjectionOptions;
struct DetectorApplication;

/// 按起始角 → 终止角（含端点）展开的角度列表。
///
/// 支持 start > end 的倒序扫描；步长必须 > 0，否则返回空列表。
std::vector<double> sequenceAngles(const ScanParameters& parameters);

/// 单帧回调：返回 false 表示请求取消。
/// 参数依次为：帧序号（0 起）、总帧数、该帧角度、以及该帧投影。
using FrameVisitor = std::function<bool(int index, int count, double angleDeg,
                                        const DrProjection& projection)>;

struct SequenceResult
{
    int frameCount = 0;
    int completedFrames = 0;
    int width = 0;
    int height = 0;
    double firstAngleDeg = 0.0;
    double lastAngleDeg = 0.0;
    double maxLineIntegralMm = 0.0;
    double elapsedMs = 0.0;
    bool cancelled = false;
    bool ok = false;
};

/// 逐帧运行多角度锥束投影（CPU）。
///
/// 采用「逐帧回调」而不是把整段序列放进内存：360 帧 × 512² 的 float 投影约
/// 350 MB，直接留存会挤占体素与界面内存。需要预览的调用方自行决定保留哪些帧，
/// 需要导出的调用方在回调里直接写盘。
/// spectral 非空时改为「能谱 + 噪声」模型（Phase 3），否则用单一 μ。
/// detector 非空时在单一 μ 路径上再叠加探测器模型（谱路径由 spectral 自带该设置）。
/// visitor 抛异常时应由调用方捕获；本函数只负责角度遍历与取消判定。
SequenceResult runSequence(const io::VoxelGrid& grid,
                           const ScanParameters& scan,
                           const ProjectionParameters& projectionParams,
                           const FrameVisitor& visitor,
                           std::string* error,
                           const SpectralProjectionOptions* spectral = nullptr,
                           const DetectorApplication* detector = nullptr);

} // namespace precisioncbct::simulation
