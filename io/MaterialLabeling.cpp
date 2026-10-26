#include "io/MaterialLabeling.h"

#include "io/VoxelGrid.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <vtkImageData.h>
#include <vtkType.h>

namespace precisioncbct::io {
namespace {

constexpr double kInfinity = 1e30;

/// 一维精确平方距离变换（Felzenszwalb & Huttenlocher 抛物线下包络法）。
///
/// 输入 f[i] 为「该位置作为源点的代价」（0 或 ∞），positions[i] 为物理坐标（mm）。
/// 输出 d[i] = min_j ( f[j] + (positions[i] − positions[j])² )，即平方距离（mm²）。
/// 算法精确，并且对非均匀间距（各向异性体素）同样成立。
void distanceTransform1D(const std::vector<double>& f,
                         const std::vector<double>& positions,
                         std::vector<double>* d)
{
    const int n = static_cast<int>(f.size());
    d->assign(static_cast<size_t>(n), kInfinity);
    if (n == 0)
    {
        return;
    }

    std::vector<int> parabolaVertex(static_cast<size_t>(n), 0);
    std::vector<double> boundary(static_cast<size_t>(n) + 1, 0.0);
    int k = 0;
    parabolaVertex[0] = 0;
    boundary[0] = -kInfinity;
    boundary[1] = kInfinity;

    for (int q = 1; q < n; ++q)
    {
        const double fq = f[static_cast<size_t>(q)];
        if (fq >= kInfinity)
        {
            continue; // 无穷代价的位置不能成为源点
        }

        const double xq = positions[static_cast<size_t>(q)];
        while (k >= 0)
        {
            const int p = parabolaVertex[static_cast<size_t>(k)];
            const double fp = f[static_cast<size_t>(p)];
            if (fp >= kInfinity)
            {
                --k; // 该抛物线无效，弹出后重试
                continue;
            }

            const double xp = positions[static_cast<size_t>(p)];
            const double s = ((fq + xq * xq) - (fp + xp * xp)) / (2.0 * (xq - xp));
            if (s <= boundary[static_cast<size_t>(k)])
            {
                --k;
                continue;
            }

            ++k;
            parabolaVertex[static_cast<size_t>(k)] = q;
            boundary[static_cast<size_t>(k)] = s;
            boundary[static_cast<size_t>(k + 1)] = kInfinity;
            break;
        }

        if (k < 0)
        {
            k = 0;
            parabolaVertex[0] = q;
            boundary[0] = -kInfinity;
            boundary[1] = kInfinity;
        }
    }

    if (f[static_cast<size_t>(parabolaVertex[0])] >= kInfinity)
    {
        return; // 没有任何源点
    }

    k = 0;
    for (int q = 0; q < n; ++q)
    {
        const double xq = positions[static_cast<size_t>(q)];
        while (boundary[static_cast<size_t>(k + 1)] < xq)
        {
            ++k;
        }
        const int p = parabolaVertex[static_cast<size_t>(k)];
        const double dx = xq - positions[static_cast<size_t>(p)];
        (*d)[static_cast<size_t>(q)] = f[static_cast<size_t>(p)] + dx * dx;
    }
}

} // namespace

bool labelMaterials(const VoxelGrid& mask,
                    const LabelingOptions& options,
                    MaterialLabeling* labeling,
                    std::string* error)
{
    if (labeling == nullptr)
    {
        if (error) { *error = "内部错误：结果指针为空"; }
        return false;
    }
    if (!mask.isValid())
    {
        if (error) { *error = "体素网格无效，请先完成体素化"; }
        return false;
    }
    if (options.shellThicknessMm < 0.0)
    {
        if (error) { *error = "皮质骨厚度不能为负"; }
        return false;
    }

    MaterialLabeling result;
    const int dims[3] = {mask.dimensions[0], mask.dimensions[1], mask.dimensions[2]};
    const size_t voxelCount = static_cast<size_t>(dims[0]) * dims[1] * dims[2];
    const auto* maskScalars = static_cast<const unsigned char*>(mask.image->GetScalarPointer());

    result.labels = vtkSmartPointer<vtkImageData>::New();
    result.labels->DeepCopy(mask.image);
    auto* labels = static_cast<unsigned char*>(result.labels->GetScalarPointer());

    // 单材料模式：内部为标签 1，外部为空气
    if (!(options.shellThicknessMm > 0.0))
    {
        result.materials = {options.coreMaterial};
        for (size_t i = 0; i < voxelCount; ++i)
        {
            labels[i] = (maskScalars[i] != 0) ? 1u : kLabelAir;
            ++result.voxelCounts[labels[i] == 0 ? 0 : 1];
        }
        *labeling = result;
        return true;
    }

    // 双材料模式：先用三维精确距离变换求每个内部体素到空气的距离（mm²）
    std::vector<double> cost(voxelCount, kInfinity);
    for (size_t i = 0; i < voxelCount; ++i)
    {
        if (maskScalars[i] == 0)
        {
            cost[i] = 0.0; // 空气是源点
        }
    }

    std::vector<double> lineIn;
    std::vector<double> lineOut;
    std::vector<double> positions;

    const size_t strides[3] = {1,
                               static_cast<size_t>(dims[0]),
                               static_cast<size_t>(dims[0]) * static_cast<size_t>(dims[1])};

    for (int axis = 0; axis < 3; ++axis)
    {
        const int length = dims[axis];
        const double spacing = mask.spacingMm[axis];
        positions.resize(static_cast<size_t>(length));
        for (int i = 0; i < length; ++i)
        {
            positions[static_cast<size_t>(i)] = i * spacing;
        }

        const size_t stride = strides[axis];
        const int otherA = (axis + 1) % 3;
        const int otherB = (axis + 2) % 3;

        for (int ib = 0; ib < dims[otherB]; ++ib)
        {
            for (int ia = 0; ia < dims[otherA]; ++ia)
            {
                const size_t base = static_cast<size_t>(ib) * strides[otherB]
                                    + static_cast<size_t>(ia) * strides[otherA];

                lineIn.resize(static_cast<size_t>(length));
                for (int i = 0; i < length; ++i)
                {
                    lineIn[static_cast<size_t>(i)] = cost[base + static_cast<size_t>(i) * stride];
                }

                distanceTransform1D(lineIn, positions, &lineOut);
                for (int i = 0; i < length; ++i)
                {
                    cost[base + static_cast<size_t>(i) * stride] = lineOut[static_cast<size_t>(i)];
                }
            }
        }
    }

    result.materials = {options.shellMaterial, options.coreMaterial};
    const double thresholdSquared = options.shellThicknessMm * options.shellThicknessMm;
    for (size_t i = 0; i < voxelCount; ++i)
    {
        if (maskScalars[i] == 0)
        {
            labels[i] = kLabelAir;
            ++result.voxelCounts[0];
            continue;
        }

        // distance² ≥ t² ⇔ distance ≥ t
        if (cost[i] >= thresholdSquared)
        {
            labels[i] = 2u;
            ++result.voxelCounts[2];
        }
        else
        {
            labels[i] = 1u;
            ++result.voxelCounts[1];
        }
    }

    if (result.voxelCounts[1] == 0 && result.voxelCounts[2] == 0)
    {
        if (error) { *error = "标签结果为空：网格可能不封闭"; }
        return false;
    }

    *labeling = result;
    return true;
}

} // namespace precisioncbct::io
