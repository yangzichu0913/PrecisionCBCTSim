#include "io/MeshRepair.h"

#include <algorithm>
#include <cmath>

#include <vtkCleanPolyData.h>
#include <vtkFillHolesFilter.h>
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkTriangleFilter.h>

namespace precisioncbct::io {

MeshRepairResult repairMesh(vtkPolyData* mesh, const MeshRepairOptions& options,
                            std::string* error)
{
    MeshRepairResult result;
    if (mesh == nullptr || mesh->GetNumberOfPolys() <= 0)
    {
        if (error) { *error = "网格为空或没有面片，无法修复"; }
        return result;
    }

    MeshQuality before = analyzeMesh(mesh);
    result.before = before;
    if (before.hasNonFinite())
    {
        if (error) { *error = "网格含 NaN/Inf 顶点，无法自动修复"; }
        return result;
    }

    // 1. 焊接重复点：STL 每个三角形独立存顶点，不焊接就识别不出「边」
    double tolerance = options.weldToleranceMm;
    if (!(tolerance > 0.0))
    {
        double bounds[6];
        mesh->GetBounds(bounds);
        const double diagonal = std::sqrt(std::pow(bounds[1] - bounds[0], 2.0)
                                          + std::pow(bounds[3] - bounds[2], 2.0)
                                          + std::pow(bounds[5] - bounds[4], 2.0));
        tolerance = std::max(diagonal * 1e-7, 1e-9);
    }

    vtkNew<vtkCleanPolyData> clean;
    clean->SetInputData(mesh);
    clean->SetToleranceIsAbsolute(true);
    clean->SetAbsoluteTolerance(tolerance);
    clean->PointMergingOn();
    clean->ConvertPolysToLinesOff();
    clean->Update();

    vtkPolyData* cleaned = clean->GetOutput();
    result.weldedPoints = static_cast<long long>(mesh->GetNumberOfPoints())
                          - static_cast<long long>(cleaned->GetNumberOfPoints());

    vtkSmartPointer<vtkPolyData> current = vtkSmartPointer<vtkPolyData>::New();
    current->DeepCopy(cleaned);

    // 2. 补洞：只有在确实存在边界边时才做（水密网格补洞是空操作且更慢）
    const MeshQuality cleanedQuality = analyzeMesh(current);
    if (options.fillHoles && cleanedQuality.boundaryEdges > 0)
    {
        const long long trianglesBefore = static_cast<long long>(current->GetNumberOfPolys());

        vtkNew<vtkFillHolesFilter> fill;
        fill->SetInputData(current);
        // 不限制洞的周长：小洞多半是导出瑕疵，大洞若补错会被 filledBoundaryEdges 暴露出来
        fill->SetHoleSize(1.0e9);
        fill->Update();

        vtkNew<vtkTriangleFilter> triangulateFilled;
        triangulateFilled->SetInputData(fill->GetOutput());
        triangulateFilled->Update();

        vtkSmartPointer<vtkPolyData> filled = vtkSmartPointer<vtkPolyData>::New();
        filled->DeepCopy(triangulateFilled->GetOutput());

        // vtkFillHolesFilter 有时会重建所有三角形；用「修复前后边界边数」衡量效果，
        // 而不是用三角形数量差（否则会给出误导性的数字）
        const MeshQuality filledQuality = analyzeMesh(filled);
        if (filledQuality.boundaryEdges < cleanedQuality.boundaryEdges)
        {
            result.filledBoundaryEdges = cleanedQuality.boundaryEdges - filledQuality.boundaryEdges;
            result.addedTriangles =
                std::max<long long>(0, static_cast<long long>(filled->GetNumberOfPolys())
                                           - trianglesBefore);
            current = filled;
        }
    }

    // 3. 统一朝向（只影响显示/法向，不影响体素化）；失败也不影响修复结果
    if (options.orientNormals)
    {
        vtkNew<vtkPolyDataNormals> normals;
        normals->SetInputData(current);
        normals->ConsistencyOn();
        normals->AutoOrientNormalsOn();
        normals->SplittingOff();
        normals->ComputePointNormalsOff();
        normals->ComputeCellNormalsOn();
        normals->Update();
        if (normals->GetOutput() != nullptr && normals->GetOutput()->GetNumberOfPolys() > 0)
        {
            vtkSmartPointer<vtkPolyData> oriented = vtkSmartPointer<vtkPolyData>::New();
            oriented->DeepCopy(normals->GetOutput());
            current = oriented;
        }
    }

    result.polyData = current;
    result.after = analyzeMesh(current);
    return result;
}

} // namespace precisioncbct::io
