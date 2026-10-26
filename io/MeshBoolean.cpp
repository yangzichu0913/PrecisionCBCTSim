#include "io/MeshBoolean.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <manifold/manifold.h>

#include <vtkCellArray.h>
#include <vtkDataArray.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataConnectivityFilter.h>
#include <vtkType.h>

namespace precisioncbct::io {
namespace {

/// vtkPolyData（三角网格）→ Manifold 的 MeshGL
manifold::MeshGL toMeshGL(vtkPolyData* poly)
{
    manifold::MeshGL mesh;
    mesh.numProp = 3;

    vtkPoints* points = poly->GetPoints();
    const vtkIdType pointCount = points != nullptr ? points->GetNumberOfPoints() : 0;
    mesh.vertProperties.reserve(static_cast<size_t>(pointCount) * 3);
    for (vtkIdType i = 0; i < pointCount; ++i)
    {
        double xyz[3];
        points->GetPoint(i, xyz);
        mesh.vertProperties.push_back(static_cast<float>(xyz[0]));
        mesh.vertProperties.push_back(static_cast<float>(xyz[1]));
        mesh.vertProperties.push_back(static_cast<float>(xyz[2]));
    }

    vtkCellArray* polys = poly->GetPolys();
    if (polys != nullptr)
    {
        mesh.triVerts.reserve(static_cast<size_t>(poly->GetNumberOfPolys()) * 3);
        vtkIdType cellSize = 0;
        const vtkIdType* cellPoints = nullptr;
        polys->InitTraversal();
        while (polys->GetNextCell(cellSize, cellPoints) != 0)
        {
            if (cellSize != 3)
            {
                continue; // 调用方保证已三角化
            }
            for (int k = 0; k < 3; ++k)
            {
                mesh.triVerts.push_back(static_cast<uint32_t>(cellPoints[k]));
            }
        }
    }
    return mesh;
}

/// MeshGL → vtkPolyData
vtkSmartPointer<vtkPolyData> toPolyData(const manifold::MeshGL& mesh)
{
    if (mesh.triVerts.empty() || mesh.vertProperties.empty())
    {
        return nullptr;
    }

    vtkNew<vtkPoints> points;
    const size_t vertexCount = mesh.vertProperties.size() / mesh.numProp;
    points->SetNumberOfPoints(static_cast<vtkIdType>(vertexCount));
    for (size_t i = 0; i < vertexCount; ++i)
    {
        const double xyz[3] = {
            static_cast<double>(mesh.vertProperties[i * mesh.numProp + 0]),
            static_cast<double>(mesh.vertProperties[i * mesh.numProp + 1]),
            static_cast<double>(mesh.vertProperties[i * mesh.numProp + 2]),
        };
        points->SetPoint(static_cast<vtkIdType>(i), xyz);
    }

    vtkNew<vtkCellArray> polys;
    const size_t triangleCount = mesh.triVerts.size() / 3;
    polys->Allocate(static_cast<vtkIdType>(triangleCount));
    for (size_t t = 0; t < triangleCount; ++t)
    {
        vtkIdType ids[3] = {
            static_cast<vtkIdType>(mesh.triVerts[t * 3 + 0]),
            static_cast<vtkIdType>(mesh.triVerts[t * 3 + 1]),
            static_cast<vtkIdType>(mesh.triVerts[t * 3 + 2]),
        };
        polys->InsertNextCell(3, ids);
    }

    vtkSmartPointer<vtkPolyData> poly = vtkSmartPointer<vtkPolyData>::New();
    poly->SetPoints(points);
    poly->SetPolys(polys);
    return poly;
}

/// 反转三角形绕向（用于把「内腔壳」翻成实体再做差集）
void flipWinding(manifold::MeshGL* mesh)
{
    for (size_t i = 0; i + 2 < mesh->triVerts.size(); i += 3)
    {
        std::swap(mesh->triVerts[i + 1], mesh->triVerts[i + 2]);
    }
}

const char* statusName(manifold::Manifold::Error error)
{
    switch (error)
    {
    case manifold::Manifold::Error::NoError: return "NoError";
    case manifold::Manifold::Error::NonFiniteVertex: return "NonFiniteVertex";
    case manifold::Manifold::Error::NotManifold: return "NotManifold";
    case manifold::Manifold::Error::VertexOutOfBounds: return "VertexOutOfBounds";
    case manifold::Manifold::Error::PropertiesWrongLength: return "PropertiesWrongLength";
    case manifold::Manifold::Error::MissingPositionProperties: return "MissingPositionProperties";
    case manifold::Manifold::Error::MergeVectorsDifferentLengths: return "MergeVectorsDifferentLengths";
    case manifold::Manifold::Error::MergeIndexOutOfBounds: return "MergeIndexOutOfBounds";
    case manifold::Manifold::Error::TransformWrongLength: return "TransformWrongLength";
    case manifold::Manifold::Error::RunIndexWrongLength: return "RunIndexWrongLength";
    case manifold::Manifold::Error::FaceIDWrongLength: return "FaceIDWrongLength";
    case manifold::Manifold::Error::InvalidConstruction: return "InvalidConstruction";
    case manifold::Manifold::Error::ResultTooLarge: return "ResultTooLarge";
    case manifold::Manifold::Error::InvalidTangents: return "InvalidTangents";
    case manifold::Manifold::Error::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

} // namespace

long long countShells(vtkPolyData* mesh)
{
    if (mesh == nullptr || mesh->GetNumberOfPolys() <= 0)
    {
        return 0;
    }
    vtkNew<vtkPolyDataConnectivityFilter> connectivity;
    connectivity->SetInputData(mesh);
    connectivity->SetExtractionModeToAllRegions();
    connectivity->Update();
    return static_cast<long long>(connectivity->GetNumberOfExtractedRegions());
}

MeshBooleanResult unifyComponents(vtkPolyData* mesh, std::string* error)
{
    MeshBooleanResult result;
    if (mesh == nullptr || mesh->GetNumberOfPolys() <= 0)
    {
        if (error) { *error = "网格为空或没有面片"; }
        return result;
    }

    // 1. 按连通性拆成壳体
    vtkNew<vtkPolyDataConnectivityFilter> connectivity;
    connectivity->SetInputData(mesh);
    connectivity->SetExtractionModeToAllRegions();
    connectivity->ColorRegionsOn();
    connectivity->Update();
    const int regionCount = connectivity->GetNumberOfExtractedRegions();
    result.components = regionCount;
    if (regionCount <= 0)
    {
        if (error) { *error = "网格没有可用的连通壳体"; }
        return result;
    }

    // 2. 逐壳体转成 Manifold，按体积符号分成「实体」与「内腔」
    std::vector<manifold::Manifold> solids;
    std::vector<manifold::Manifold> cavities;
    solids.reserve(static_cast<size_t>(regionCount));
    cavities.reserve(static_cast<size_t>(regionCount));

    for (int region = 0; region < regionCount; ++region)
    {
        vtkNew<vtkPolyDataConnectivityFilter> extractor;
        extractor->SetInputData(mesh);
        extractor->SetExtractionModeToSpecifiedRegions();
        extractor->AddSpecifiedRegion(region);
        extractor->Update();

        manifold::MeshGL gl = toMeshGL(extractor->GetOutput());
        if (gl.triVerts.empty())
        {
            continue;
        }
        // STL 每个三角形独立存顶点，必须先焊接，否则 Manifold 一律判 NotManifold
        gl.Merge();

        manifold::Manifold component(gl);
        if (component.Status() != manifold::Manifold::Error::NoError)
        {
            if (error)
            {
                *error = "第 " + std::to_string(region + 1) + " 个壳体不是流形实体（"
                         + statusName(component.Status()) + "），无法做布尔合并";
            }
            return result;
        }

        if (component.Volume() < 0.0)
        {
            // 法向朝内 = 内腔：翻正后作为「被挖掉的实体」
            flipWinding(&gl);
            manifold::Manifold solid(gl);
            if (solid.Status() != manifold::Manifold::Error::NoError)
            {
                if (error) { *error = "内腔壳体翻转后无效"; }
                return result;
            }
            cavities.push_back(solid);
            ++result.cavityShells;
        }
        else
        {
            solids.push_back(component);
            ++result.positiveShells;
        }
    }

    if (solids.empty())
    {
        if (error) { *error = "网格里没有法向朝外的实体壳体（只有内腔？）"; }
        return result;
    }

    // 3. 单个实体、无内腔：不需要布尔运算，但仍走一遍 Manifold，
    //    这样「单个非流形连通体」（例如两个立方体焊接后共享一个面）也能被修成流形。
    if (result.cavityShells == 0 && solids.size() == 1)
    {
        result.volumeMm3 = solids.front().Volume();
        result.polyData = toPolyData(solids.front().GetMeshGL());
        if (result.polyData == nullptr)
        {
            if (error) { *error = "无法把实体转回三角网格"; }
        }
        return result;
    }

    // 4. 实体求并集，内腔求差集
    manifold::Manifold combined = solids.front();
    for (size_t i = 1; i < solids.size(); ++i)
    {
        combined = combined + solids[i];
        if (combined.Status() != manifold::Manifold::Error::NoError)
        {
            if (error)
            {
                *error = std::string("并集失败：") + statusName(combined.Status());
            }
            return result;
        }
    }
    for (const manifold::Manifold& cavity : cavities)
    {
        combined = combined - cavity;
        if (combined.Status() != manifold::Manifold::Error::NoError)
        {
            if (error)
            {
                *error = std::string("内腔差集失败：") + statusName(combined.Status());
            }
            return result;
        }
    }

    if (combined.IsEmpty())
    {
        if (error) { *error = "布尔合并结果为空（实体被内腔完全吞掉？）"; }
        return result;
    }

    result.volumeMm3 = combined.Volume();
    result.polyData = toPolyData(combined.GetMeshGL());
    if (result.polyData == nullptr)
    {
        if (error) { *error = "布尔合并结果无法转回三角网格"; }
        return result;
    }
    return result;
}

} // namespace precisioncbct::io
