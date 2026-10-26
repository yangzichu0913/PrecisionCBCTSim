#pragma once

#include <array>
#include <string>
#include <vector>

#include <vtkSmartPointer.h>

#include "simulation/MaterialTypes.h"

class vtkImageData;

namespace precisioncbct::io {

struct VoxelGrid;

/// 体素标签约定：0 = 空气（网格外部），1..k = 材料索引 + 1
constexpr unsigned char kLabelAir = 0u;

/// 多材料标签结果。
///
/// labels 是与体素网格同尺寸/同 spacing/origin 的 unsigned char 体数据；
/// materials[i] 对应标签 i+1 的物理材料（最多 2 层：外壳 + 内核）。
struct MaterialLabeling
{
    vtkSmartPointer<vtkImageData> labels;
    std::vector<simulation::Material> materials;
    std::array<long long, 3> voxelCounts = {0, 0, 0}; ///< 空气 / 标签1 / 标签2 的体素数

    int labelCount() const { return static_cast<int>(materials.size()); }
    bool isValid() const { return labels != nullptr && labelCount() > 0; }
};

struct LabelingOptions
{
    /// 外壳（皮质骨）厚度；0 表示整个物体都用 coreMaterial（等价于单材料）
    double shellThicknessMm = 0.0;
    simulation::Material shellMaterial = simulation::Material::CorticalBone;
    simulation::Material coreMaterial = simulation::Material::Water;
};

/// 在体素掩膜（1 = 物体内部）基础上生成材料标签：
/// - 距表面 ≥ shellThicknessMm 的内部体素 → coreMaterial（标签 2）
/// - 其余内部体素 → shellMaterial（标签 1）
/// - 外部 → 空气（标签 0）
///
/// 表面距离用欧氏距离变换（Saito，考虑非各向同性体素）计算。
/// shellThicknessMm = 0 时只产生标签 1（全部 coreMaterial）。
bool labelMaterials(const VoxelGrid& mask,
                    const LabelingOptions& options,
                    MaterialLabeling* labeling,
                    std::string* error);

} // namespace precisioncbct::io
