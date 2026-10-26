#pragma once

namespace precisioncbct::simulation {

/// 材料种类。
///
/// 单独放在只含枚举的头文件里：`io` 需要它做体素标签，
/// 而 `io` 不能依赖 `simulation` 库（否则两个静态库会循环依赖）。
enum class Material
{
    Water = 0,
    CorticalBone,
    PMMA,
    Air,
    Aluminum,
};

inline constexpr int kMaterialCount = 5;

} // namespace precisioncbct::simulation
