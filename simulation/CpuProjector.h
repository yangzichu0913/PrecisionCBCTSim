#pragma once

#include <array>
#include <string>
#include <vector>

#include "simulation/ConeBeamView.h"

namespace precisioncbct::io {
struct VoxelGrid;
}

namespace precisioncbct::simulation {

struct ProjectionParameters
{
    /// 均匀线衰减系数（1/mm）。Phase 1 只区分“有物体/无物体”，
    /// Phase 3 会换成能谱相关的 μ(E) 与多材料赋值。
    double attenuationPerMm = 0.02;
    double incidentIntensity = 1.0;
};

/// 单张 DR 投影结果（线积分与透射率，均为行优先 row-major）。
struct DrProjection
{
    int width = 0;
    int height = 0;
    std::vector<float> lineIntegralMm; ///< 每条射线在物体内的路径长度
    std::vector<float> transmission;   ///< I/I0 = exp(-μ·L)，范围 (0,1]
    float maxLineIntegralMm = 0.0f;
    float minTransmission = 1.0f;

    bool isValid() const
    {
        return width > 0 && height > 0
            && lineIntegralMm.size() == static_cast<size_t>(width) * static_cast<size_t>(height);
    }
};

/// 每条射线的「物体内路径长度」场，供谱加权投影复用（避免重复做 DDA）。
struct PathLengthField
{
    int width = 0;
    int height = 0;
    std::vector<float> lengthsMm;
    ConeBeamView resolvedView; ///< 已 update() 的几何副本（含源/探测器位姿）

    bool isValid() const
    {
        return width > 0 && height > 0
            && lengthsMm.size() == static_cast<size_t>(width) * static_cast<size_t>(height);
    }
};

/// 计算所有探测器像素的路径长度（多线程 DDA）。失败时返回 isValid()==false。
PathLengthField computePathLengths(const io::VoxelGrid& grid,
                                   const ConeBeamView& view,
                                   std::string* error);

/// 谱加权投影最多跟踪的材料层数（外壳 + 内核 + 预留）
inline constexpr int kMaxTrackedMaterials = 4;

/// 每条射线在各材料层内的路径长度（按标签 1..labelCount 分别累计，单位 mm）。
struct MaterialPathLengthField
{
    int width = 0;
    int height = 0;
    int labelCount = 0;
    /// 每个像素对应各标签的路径长度；下标 0 对应标签 1
    std::vector<std::array<float, kMaxTrackedMaterials>> lengthsMm;
    ConeBeamView resolvedView;

    bool isValid() const
    {
        return width > 0 && height > 0 && labelCount > 0
            && lengthsMm.size() == static_cast<size_t>(width) * static_cast<size_t>(height);
    }
};

/// 多材料版本的路径长度计算（labeledGrid.image 的体素值为标签：0=空气，1..n=材料）。
MaterialPathLengthField computeMaterialPathLengths(const io::VoxelGrid& labeledGrid,
                                                   const ConeBeamView& view,
                                                   int maxLabels,
                                                   std::string* error);

/// CPU 版正向投影（Phase 1）。
///
/// 算法：对每个探测器像素构造一条从焦点到像素中心的射线，在体素索引空间做
/// DDA 遍历（Amanatides & Woo），精确累加射线穿过“内部体素”的长度（mm），
/// 再按 Beer-Lambert 定律 I = I0·exp(-μ·L) 得到 DR。
/// 复杂度 O(像素数 × 射线穿越的体素数)；Phase 2 加多线程，Phase 3 换成 CUDA 内核。
DrProjection projectConeBeam(const io::VoxelGrid& grid,
                             const ConeBeamView& view,
                             const ProjectionParameters& parameters,
                             std::string* error);

} // namespace precisioncbct::simulation
