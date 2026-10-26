#include "io/MeshLoader.h"

#include "io/MeshQuality.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include <vtkAbstractPolyDataReader.h>
#include <vtkNew.h>
#include <vtkOBJReader.h>
#include <vtkPolyData.h>
#include <vtkSTLReader.h>
#include <vtkTriangleFilter.h>

namespace precisioncbct::io {
namespace {

std::string lowerExtension(const std::string& path)
{
    const std::string ext = std::filesystem::path(path).extension().string();
    std::string lowered(ext);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

} // namespace

bool isSupportedMeshFile(const std::string& path)
{
    const std::string ext = lowerExtension(path);
    return ext == ".stl" || ext == ".obj";
}

MeshData loadMesh(const std::string& path, std::string* error)
{
    MeshData mesh;
    mesh.sourcePath = path;

    if (path.empty())
    {
        if (error) { *error = "文件路径为空"; }
        return mesh;
    }
    if (!std::filesystem::exists(path))
    {
        if (error) { *error = "文件不存在：" + path; }
        return mesh;
    }

    const std::string ext = lowerExtension(path);
    vtkNew<vtkSTLReader> stlReader;
    vtkNew<vtkOBJReader> objReader;
    vtkAbstractPolyDataReader* reader = nullptr;

    if (ext == ".stl")
    {
        stlReader->SetFileName(path.c_str());
        reader = stlReader;
    }
    else if (ext == ".obj")
    {
        objReader->SetFileName(path.c_str());
        reader = objReader;
    }
    else
    {
        if (error) { *error = "不支持的格式 " + (ext.empty() ? std::string("(无扩展名)") : ext) + "，仅支持 .stl / .obj"; }
        return mesh;
    }

    // 统一三角化：体素化与面片计数都依赖三角面片
    vtkNew<vtkTriangleFilter> triangulate;
    triangulate->SetInputConnection(reader->GetOutputPort());
    triangulate->Update();

    vtkPolyData* output = triangulate->GetOutput();
    if (output == nullptr || output->GetNumberOfPoints() <= 0 || output->GetNumberOfPolys() <= 0)
    {
        if (error) { *error = "无法从文件中解析出三角网格（文件可能为空或格式不正确）"; }
        return mesh;
    }

    // 复制出管线结果，避免依赖 reader 的生命周期
    mesh.polyData = vtkSmartPointer<vtkPolyData>::New();
    mesh.polyData->DeepCopy(output);
    mesh.triangleCount = static_cast<long long>(mesh.polyData->GetNumberOfPolys());

    // 几何稳健性第一道防线：NaN/Inf 坐标会让包围盒、体素化、投影全部失效，
    // 必须在入口拒绝，而不是等到算出一堆无意义数字后才由用户发现。
    mesh.quality = analyzeMesh(mesh.polyData);
    if (mesh.quality.hasNonFinite())
    {
        if (error)
        {
            *error = "模型含 " + std::to_string(mesh.quality.nonFinitePoints)
                     + " 个 NaN/Inf 顶点坐标，无法用于仿真；请在建模软件里修复后重新导出";
        }
        return MeshData{};
    }

    mesh.polyData->GetBounds(mesh.bounds);
    return mesh;
}

} // namespace precisioncbct::io
