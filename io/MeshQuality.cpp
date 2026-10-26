#include "io/MeshQuality.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <vtkCellArray.h>
#include <vtkFeatureEdges.h>
#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>

namespace precisioncbct::io {
namespace {

double triangleArea(const double a[3], const double b[3], const double c[3])
{
    const double ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const double ac[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const double cross[3] = {
        ab[1] * ac[2] - ab[2] * ac[1],
        ab[2] * ac[0] - ab[0] * ac[2],
        ab[0] * ac[1] - ab[1] * ac[0],
    };
    return 0.5 * std::sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
}

} // namespace

double MeshQuality::maxExtentMm() const
{
    return std::max({bounds[1] - bounds[0], bounds[3] - bounds[2], bounds[5] - bounds[4]});
}

MeshQuality analyzeMesh(vtkPolyData* mesh, double degenerateAreaToleranceMm2)
{
    MeshQuality quality;
    if (mesh == nullptr)
    {
        return quality;
    }

    vtkPoints* points = mesh->GetPoints();
    quality.triangles = mesh->GetNumberOfPolys();
    if (points == nullptr)
    {
        return quality;
    }

    // 1. 非有限坐标：NaN/Inf 会让包围盒与光栅化全部失效，必须先发现
    const vtkIdType pointCount = points->GetNumberOfPoints();
    for (vtkIdType i = 0; i < pointCount; ++i)
    {
        double xyz[3];
        points->GetPoint(i, xyz);
        bool finite = true;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(xyz[axis]))
            {
                finite = false;
                break;
            }
            quality.maxAbsCoordinate = std::max(quality.maxAbsCoordinate, std::fabs(xyz[axis]));
        }
        if (!finite)
        {
            ++quality.nonFinitePoints;
        }
    }

    // 2. 边界边 / 非流形边：判断网格是否水密
    //    vtkFeatureEdges 把两类边输出到同一个 polyData，靠开关区分，因此跑两次分别计数。
    {
        vtkNew<vtkFeatureEdges> boundaryOnly;
        boundaryOnly->SetInputData(mesh);
        boundaryOnly->BoundaryEdgesOn();
        boundaryOnly->NonManifoldEdgesOff();
        boundaryOnly->ManifoldEdgesOff();
        boundaryOnly->FeatureEdgesOff();
        boundaryOnly->Update();
        quality.boundaryEdges =
            static_cast<long long>(boundaryOnly->GetOutput()->GetNumberOfCells());

        vtkNew<vtkFeatureEdges> nonManifoldOnly;
        nonManifoldOnly->SetInputData(mesh);
        nonManifoldOnly->BoundaryEdgesOff();
        nonManifoldOnly->NonManifoldEdgesOn();
        nonManifoldOnly->ManifoldEdgesOff();
        nonManifoldOnly->FeatureEdgesOff();
        nonManifoldOnly->Update();
        quality.nonManifoldEdges =
            static_cast<long long>(nonManifoldOnly->GetOutput()->GetNumberOfCells());
    }

    // 3. 零面积三角形：会让法向/绕向判定失效
    vtkCellArray* polys = mesh->GetPolys();
    if (polys != nullptr)
    {
        vtkIdType cellSize = 0;
        const vtkIdType* cellPoints = nullptr;
        polys->InitTraversal();
        while (polys->GetNextCell(cellSize, cellPoints) != 0)
        {
            if (cellSize != 3)
            {
                continue;
            }
            double a[3], b[3], c[3];
            points->GetPoint(cellPoints[0], a);
            points->GetPoint(cellPoints[1], b);
            points->GetPoint(cellPoints[2], c);
            if (!std::isfinite(a[0]) || !std::isfinite(b[0]) || !std::isfinite(c[0]))
            {
                continue; // 已在非有限统计里算过
            }
            if (triangleArea(a, b, c) <= degenerateAreaToleranceMm2)
            {
                ++quality.degenerateTriangles;
            }
        }
    }

    if (quality.triangles > 0 && quality.nonFinitePoints == 0)
    {
        mesh->GetBounds(quality.bounds);
    }
    return quality;
}

std::string describeQuality(const MeshQuality& quality)
{
    std::string text;
    if (quality.hasNonFinite())
    {
        text += "含 " + std::to_string(quality.nonFinitePoints) + " 个 NaN/Inf 顶点；";
    }
    if (quality.boundaryEdges > 0)
    {
        text += "有 " + std::to_string(quality.boundaryEdges) + " 条边界边（网格不封闭）；";
    }
    if (quality.nonManifoldEdges > 0)
    {
        text += "有 " + std::to_string(quality.nonManifoldEdges) + " 条非流形边；";
    }
    if (quality.degenerateTriangles > 0)
    {
        text += "有 " + std::to_string(quality.degenerateTriangles) + " 个零面积三角形；";
    }
    return text;
}

} // namespace precisioncbct::io
