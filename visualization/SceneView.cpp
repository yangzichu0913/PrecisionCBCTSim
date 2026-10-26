#include "visualization/SceneView.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkImageActor.h>
#include <vtkImageData.h>
#include <vtkImageMapToColors.h>
#include <vtkImageMapper3D.h>
#include <vtkImageProperty.h>
#include <vtkLookupTable.h>
#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyLine.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>

#include "visualization/ScannerRig.h"

namespace precisioncbct::viz {
namespace {

// 有切面显示时把表面调暗，否则不透明外壳会完全遮住内部结构
constexpr double kMeshOpacityWithSlices = 0.18;
constexpr double kMeshOpacityAlone = 1.0;

} // namespace

SceneView::SceneView(QWidget* parent)
    : QVTKOpenGLNativeWidget(parent)
{
    // QVTKOpenGLNativeWidget 只接受 vtkGenericOpenGLRenderWindow：
    // 它把 VTK 的渲染结果通过 Qt 的 OpenGL 上下文合成到 QWidget 上。
    setRenderWindow(m_vtkRenderWindow.Get());

    m_renderer = vtkSmartPointer<vtkRenderer>::New();

    // 深色渐变背景：即使场景为空也能确认 OpenGL 渲染管线工作正常
    // 与深色主题一致：冷灰蓝渐变，和面板的 #22272e 同一族，不偏黄
    m_renderer->GradientBackgroundOn();
    m_renderer->SetBackground(0.075, 0.085, 0.105);
    m_renderer->SetBackground2(0.165, 0.195, 0.235);

    m_vtkRenderWindow->AddRenderer(m_renderer);

    // 真实感机架：外壳环 + 转子 + 检查床 + 地面。默认就显示，开门见"设备"
    m_rig = std::make_unique<ScannerRig>();
    m_rig->attachTo(m_renderer);
    m_rig->resetBeam();

    // 空场景提示：否则用户看到的就是一块渐变，不知道下一步该做什么。
    // 注意 VTK 内置字体不含中文字形，叠加文字只能用 ASCII
    m_emptyHint = vtkSmartPointer<vtkTextActor>::New();
    m_emptyHint->SetInput("No model loaded - press Ctrl+O to open a model");
    vtkTextProperty* hintProperty = m_emptyHint->GetTextProperty();
    hintProperty->SetColor(0.62, 0.68, 0.75);
    hintProperty->SetFontFamilyToArial();
    hintProperty->SetFontSize(16);
    hintProperty->SetJustificationToCentered();
    hintProperty->SetVerticalJustificationToCentered();
    m_emptyHint->GetPositionCoordinate()->SetCoordinateSystemToNormalizedViewport();
    m_emptyHint->SetPosition(0.5, 0.10); // 放在画面下方，别挡住机架
    m_emptyHint->SetVisibility(true);
    m_renderer->AddActor2D(m_emptyHint);

    // 一启动就按"整机"取景，否则默认相机把设备拍成一个小点
    resetView();
}

SceneView::~SceneView() = default;

void SceneView::setMesh(vtkPolyData* mesh)
{
    if (mesh == nullptr)
    {
        clearMesh();
        return;
    }

    if (!m_meshActor)
    {
        m_meshMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        m_meshActor = vtkSmartPointer<vtkActor>::New();
        m_meshActor->SetMapper(m_meshMapper);
        m_meshActor->GetProperty()->SetColor(0.78, 0.80, 0.84);
        m_meshActor->GetProperty()->SetSpecular(0.25);
        m_meshActor->GetProperty()->SetSpecularPower(20.0);
        m_renderer->AddActor(m_meshActor);
    }

    m_meshMapper->SetInputData(mesh);
    m_meshMapper->ScalarVisibilityOff();
    m_meshMapper->Update();

    double bounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    mesh->GetBounds(bounds);
    // 工业 CT 里样品要随转台自转，绕包围盒中心转才不会甩出去
    m_meshCenter[0] = 0.5 * (bounds[0] + bounds[1]);
    m_meshCenter[1] = 0.5 * (bounds[2] + bounds[3]);
    m_meshCenter[2] = 0.5 * (bounds[4] + bounds[5]);
    m_meshActor->SetOrigin(m_meshCenter);
    m_meshActor->SetOrientation(0.0, 0.0, m_sampleAngleDeg);

    // 让检查床托住模型：床面贴着模型最低点，长度覆盖整个模型
    if (m_rig)
    {
        m_rig->setSampleBounds(bounds);
    }

    if (m_emptyHint != nullptr)
    {
        m_emptyHint->SetVisibility(false);
    }
    applyMeshAppearance();
    render();
}

void SceneView::clearMesh()
{
    if (m_meshActor)
    {
        m_renderer->RemoveActor(m_meshActor);
        m_meshActor = nullptr;
        m_meshMapper = nullptr;
        m_sampleAngleDeg = 0.0;
        render();
    }
    if (m_emptyHint != nullptr)
    {
        m_emptyHint->SetVisibility(true);
    }
}

void SceneView::setVoxelGrid(vtkImageData* grid)
{
    if (grid == nullptr)
    {
        clearVoxelGrid();
        return;
    }

    int extent[6] = {0, -1, 0, -1, 0, -1};
    grid->GetExtent(extent);
    if (extent[1] < extent[0])
    {
        clearVoxelGrid();
        return;
    }

    m_voxelGrid = grid;
    m_volumeIsScalar = false;

    const int centre[3] = {
        (extent[0] + extent[1]) / 2,
        (extent[2] + extent[3]) / 2,
        (extent[4] + extent[5]) / 2,
    };

    // 三个正交切面：轴位(z)、冠状(y)、矢状(x)。
    // 选择切面而非体渲染，是因为二值掩膜用切面观察更直观，
    // 也避免了 CPU ray-cast 后端在部分显卡/远程桌面环境下的兼容问题。
    const int sliceExtents[3][6] = {
        {extent[0], extent[1], extent[2], extent[3], centre[2], centre[2]},
        {extent[0], extent[1], centre[1], centre[1], extent[4], extent[5]},
        {centre[0], centre[0], extent[2], extent[3], extent[4], extent[5]},
    };

    for (int i = 0; i < 3; ++i)
    {
        if (!m_sliceActors[i])
        {
            // 套一层查色表把 0 映射成全透明，避免切面外的空白体素显示成黑方块
            m_sliceColors[i] = vtkSmartPointer<vtkImageMapToColors>::New();
            m_sliceColors[i]->SetOutputFormatToRGBA();

            vtkNew<vtkLookupTable> lut;
            lut->SetNumberOfTableValues(2);
            lut->SetTableValue(0, 0.0, 0.0, 0.0, 0.0);   // 外部：透明
            lut->SetTableValue(1, 0.85, 0.90, 1.00, 1.0); // 内部：浅蓝白
            m_sliceLuts[i] = lut;
            m_sliceColors[i]->SetLookupTable(lut);

            m_sliceActors[i] = vtkSmartPointer<vtkImageActor>::New();
            m_sliceActors[i]->InterpolateOff();
            m_sliceActors[i]->GetProperty()->SetInterpolationTypeToNearest();
            m_sliceActors[i]->GetProperty()->SetOpacity(0.95);
            m_renderer->AddActor(m_sliceActors[i]);
        }

        m_sliceColors[i]->SetInputData(grid);
        m_sliceActors[i]->GetMapper()->SetInputConnection(m_sliceColors[i]->GetOutputPort());
        m_sliceActors[i]->SetDisplayExtent(sliceExtents[i]);
    }

    applyMeshAppearance();
    render();
}

void SceneView::clearVoxelGrid()
{
    bool removed = false;
    for (auto& actor : m_sliceActors)
    {
        if (actor)
        {
            m_renderer->RemoveActor(actor);
            actor = nullptr;
            removed = true;
        }
    }
    for (auto& lut : m_sliceLuts)
    {
        lut = nullptr;
    }
    m_voxelGrid = nullptr;
    m_volumeIsScalar = false;
    if (removed)
    {
        applyMeshAppearance();
        render();
    }
}

void SceneView::setReconstructedVolume(vtkImageData* volume, double windowMin, double windowMax)
{
    if (volume == nullptr || volume->GetNumberOfPoints() <= 0)
    {
        clearVoxelGrid();
        return;
    }
    if (!(windowMax > windowMin))
    {
        windowMax = windowMin + 1e-6;
    }

    int extent[6] = {0, -1, 0, -1, 0, -1};
    volume->GetExtent(extent);
    if (extent[1] < extent[0])
    {
        clearVoxelGrid();
        return;
    }

    m_voxelGrid = volume;
    m_volumeIsScalar = true;

    // 灰度窗：把 [windowMin, windowMax] 映射到黑→白。
    // 与二值掩膜不同，这里不能把 0 变透明，否则空气与低衰减组织看起来一样。
    for (int i = 0; i < 3; ++i)
    {
        if (!m_sliceActors[i])
        {
            m_sliceColors[i] = vtkSmartPointer<vtkImageMapToColors>::New();
            m_sliceColors[i]->SetOutputFormatToRGBA();

            m_sliceActors[i] = vtkSmartPointer<vtkImageActor>::New();
            m_sliceActors[i]->InterpolateOff();
            m_sliceActors[i]->GetProperty()->SetInterpolationTypeToNearest();
            m_sliceActors[i]->GetProperty()->SetOpacity(1.0);
            m_renderer->AddActor(m_sliceActors[i]);
        }

        vtkNew<vtkLookupTable> lut;
        lut->SetNumberOfTableValues(256);
        lut->SetRange(windowMin, windowMax);
        // 默认 vtkLookupTable 是彩虹色（HueRange 0~0.67），灰度必须显式设置
        lut->SetHueRange(0.0, 0.0);
        lut->SetSaturationRange(0.0, 0.0);
        lut->SetValueRange(0.0, 1.0);
        lut->SetAlphaRange(1.0, 1.0);
        lut->Build();
        m_sliceLuts[i] = lut;
        m_sliceColors[i]->SetLookupTable(lut);
        m_sliceColors[i]->SetInputData(volume);
        m_sliceActors[i]->GetMapper()->SetInputConnection(m_sliceColors[i]->GetOutputPort());
    }

    setSliceIndices((extent[4] + extent[5]) / 2, (extent[2] + extent[3]) / 2,
                    (extent[0] + extent[1]) / 2);
    applyMeshAppearance();
    render();
}

void SceneView::setSliceIndices(int axialIndex, int coronalIndex, int sagittalIndex)
{
    if (!m_voxelGrid)
    {
        return;
    }
    int extent[6] = {0, -1, 0, -1, 0, -1};
    m_voxelGrid->GetExtent(extent);
    if (extent[1] < extent[0])
    {
        return;
    }

    m_sliceIndices[0] = std::clamp(axialIndex, extent[4], extent[5]);
    m_sliceIndices[1] = std::clamp(coronalIndex, extent[2], extent[3]);
    m_sliceIndices[2] = std::clamp(sagittalIndex, extent[0], extent[1]);

    const int sliceExtents[3][6] = {
        {extent[0], extent[1], extent[2], extent[3], m_sliceIndices[0], m_sliceIndices[0]},
        {extent[0], extent[1], m_sliceIndices[1], m_sliceIndices[1], extent[4], extent[5]},
        {m_sliceIndices[2], m_sliceIndices[2], extent[2], extent[3], extent[4], extent[5]},
    };
    for (int i = 0; i < 3; ++i)
    {
        if (m_sliceActors[i])
        {
            m_sliceActors[i]->SetDisplayExtent(sliceExtents[i]);
        }
    }
    render();
}

bool SceneView::volumeDimensions(int out[3]) const
{
    if (!m_voxelGrid)
    {
        return false;
    }
    m_voxelGrid->GetDimensions(out);
    return out[0] > 0 && out[1] > 0 && out[2] > 0;
}

void SceneView::setProjectionGeometry(const std::array<double, 3>& sourceMm,
                                      const std::array<double, 3>& detectorCenterMm,
                                      const std::array<double, 3>& uAxis,
                                      const std::array<double, 3>& vAxis,
                                      double detectorWidthMm,
                                      double detectorHeightMm)
{
    const double uLength = std::sqrt(uAxis[0] * uAxis[0] + uAxis[1] * uAxis[1] + uAxis[2] * uAxis[2]);
    const double vLength = std::sqrt(vAxis[0] * vAxis[0] + vAxis[1] * vAxis[1] + vAxis[2] * vAxis[2]);
    if (detectorWidthMm <= 0.0 || detectorHeightMm <= 0.0 || uLength < 0.5 || vLength < 0.5)
    {
        // 位姿未初始化（零向量）时不做任何绘制
        clearProjectionGeometry();
        return;
    }

    // 机架直接按真实几何摆位：球管在射线源方向、平板在探测器方向、转子转到当前角度，
    // 界面看到的就是设备真实的旋转状态，而不是几个抽象记号
    const double angleDeg = std::atan2(sourceMm[0], -sourceMm[1]) * 180.0 / 3.14159265358979323846;
    if (m_rig)
    {
        m_rig->setBeamGeometry(sourceMm, detectorCenterMm, detectorWidthMm, detectorHeightMm);
    }

    if (simulation::scannerModeRotatesSample(m_scannerMode))
    {
        // 工业 CT：源与探测器固定，样品随转台反向旋转（与机架正转在投影上等价）
        m_sampleAngleDeg = -angleDeg;
        if (m_meshActor != nullptr)
        {
            m_meshActor->SetOrientation(0.0, 0.0, m_sampleAngleDeg);
            m_meshActor->SetOrigin(m_meshCenter);
        }
    }
    else
    {
        m_sampleAngleDeg = 0.0;
        if (m_meshActor != nullptr)
        {
            m_meshActor->SetOrientation(0.0, 0.0, 0.0);
        }
    }
    render();
}

void SceneView::setScannerMode(simulation::ScannerMode mode)
{
    if (m_rig)
    {
        m_rig->setMode(mode);
        m_rig->resetBeam();
    }
    const bool modeChanged = m_scannerMode != mode;
    m_scannerMode = mode;
    if (modeChanged && m_meshActor != nullptr && !simulation::scannerModeRotatesSample(mode))
    {
        m_sampleAngleDeg = 0.0;
        m_meshActor->SetOrientation(0.0, 0.0, 0.0);
    }
    resetView();
}

void SceneView::setScanTrajectory(double sadMm, double detectorOffsetMm, double startDeg, double endDeg)
{
    if (!(sadMm > 0.0) || !(detectorOffsetMm >= 0.0))
    {
        clearScanTrajectory();
        return;
    }

    constexpr double kPi = 3.14159265358979323846;
    const int samples = 128;

    vtkNew<vtkPoints> points;
    vtkNew<vtkCellArray> lines;

    // 两条圆弧：源（半径 SAD）与探测器中心（半径 SDD-SAD），角度都绕 z 轴
    const int arcs = (detectorOffsetMm > 0.0) ? 2 : 1;
    for (int arc = 0; arc < arcs; ++arc)
    {
        const double radius = (arc == 0) ? sadMm : detectorOffsetMm;
        const vtkIdType firstId = points->GetNumberOfPoints();
        for (int i = 0; i <= samples; ++i)
        {
            const double t = static_cast<double>(i) / samples;
            const double angle = (startDeg + (endDeg - startDeg) * t) * kPi / 180.0;
            // 与 ConeBeamView 的约定一致：angle=0 时源位于 -y
            const double x = radius * std::sin(angle);
            const double y = -radius * std::cos(angle);
            if (arc == 1)
            {
                points->InsertNextPoint(-x, -y, 0.0);
            }
            else
            {
                points->InsertNextPoint(x, y, 0.0);
            }
        }

        vtkNew<vtkPolyLine> polyline;
        polyline->GetPointIds()->SetNumberOfIds(samples + 1);
        for (int i = 0; i <= samples; ++i)
        {
            polyline->GetPointIds()->SetId(i, firstId + i);
        }
        lines->InsertNextCell(polyline);
    }

    vtkNew<vtkPolyData> polyData;
    polyData->SetPoints(points);
    polyData->SetLines(lines);

    if (!m_trajectoryActor)
    {
        m_trajectoryMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        m_trajectoryActor = vtkSmartPointer<vtkActor>::New();
        m_trajectoryActor->SetMapper(m_trajectoryMapper);
        m_trajectoryActor->GetProperty()->SetColor(0.45, 0.75, 0.95);
        m_trajectoryActor->GetProperty()->SetLineWidth(1.5);
        // 有机架模型时轨迹只是辅助信息，压暗一点别把人造设备的轮廓搅乱
        m_trajectoryActor->GetProperty()->SetOpacity(0.4);
        m_renderer->AddActor(m_trajectoryActor);
    }

    m_trajectoryMapper->SetInputData(polyData);
    m_trajectoryMapper->Update();
    render();
}

void SceneView::clearScanTrajectory()
{
    if (m_trajectoryActor)
    {
        m_renderer->RemoveActor(m_trajectoryActor);
        m_trajectoryActor = nullptr;
        m_trajectoryMapper = nullptr;
        render();
    }
}

void SceneView::clearProjectionGeometry()
{
    // 机架本身常驻场景，这里只把转子回正，避免"清空"时整台设备消失
    if (m_rig)
    {
        m_rig->resetBeam();
    }
    render();
}

void SceneView::clearScene()
{
    clearVoxelGrid();
    clearMesh();
    render();
}

void SceneView::applyMeshAppearance()
{
    if (!m_meshActor)
    {
        return;
    }
    m_meshActor->GetProperty()->SetOpacity(m_sliceActors[0] ? kMeshOpacityWithSlices : kMeshOpacityAlone);
}

void SceneView::render()
{
    m_vtkRenderWindow->Render();
}

void SceneView::resetView()
{
    if (!m_renderer)
    {
        return;
    }

    // 相机与焦点由场景包围盒推导；空场景时保持默认相机参数
    double sceneBounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    bool hasBounds = false;
    if (m_rig)
    {
        m_rig->bounds(sceneBounds);
        hasBounds = true;
    }
    if (m_meshActor != nullptr)
    {
        double meshBounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        m_meshActor->GetBounds(meshBounds);
        if (!hasBounds)
        {
            std::copy(meshBounds, meshBounds + 6, sceneBounds);
            hasBounds = true;
        }
        else
        {
            for (int i = 0; i < 3; ++i)
            {
                sceneBounds[2 * i] = std::min(sceneBounds[2 * i], meshBounds[2 * i]);
                sceneBounds[2 * i + 1] = std::max(sceneBounds[2 * i + 1], meshBounds[2 * i + 1]);
            }
        }
    }

    if (hasBounds)
    {
        // 固定机位；工业 CT / C 臂是"z 轴朝上"的坐标系，视角也要把 z 当成屏幕上的"上"
        double dir[3] = {0.62, 0.26, 0.74};
        double up[3] = {0.0, 1.0, 0.0};
        double zoom = 1.08;
        if (m_scannerMode == simulation::ScannerMode::Industrial)
        {
            dir[0] = -0.24;
            dir[1] = -0.90;
            dir[2] = 0.36;
            up[1] = 0.0;
            up[2] = 1.0;
            zoom = 1.28;
        }
        else if (m_scannerMode == simulation::ScannerMode::CArm)
        {
            // 略偏斜的正视角：既能看清 C 形弧臂的倾斜姿态，也能看清平板正对射线的感光面
            dir[0] = -0.50;
            dir[1] = -0.86;
            dir[2] = 0.26;
            up[1] = 0.0;
            up[2] = 1.0;
            zoom = 1.10;
        }
        const double centreX = 0.5 * (sceneBounds[0] + sceneBounds[1]);
        double centreY = 0.5 * (sceneBounds[2] + sceneBounds[3]);
        const double centreZ = 0.5 * (sceneBounds[4] + sceneBounds[5]);
        const double norm = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        dir[0] /= norm;
        dir[1] /= norm;
        dir[2] /= norm;

        const double reach = std::max({sceneBounds[1] - sceneBounds[0],
                                       sceneBounds[3] - sceneBounds[2],
                                       sceneBounds[5] - sceneBounds[4]});

        vtkCamera* camera = m_renderer->GetActiveCamera();
        camera->SetFocalPoint(centreX, centreY, centreZ);
        camera->SetPosition(centreX + dir[0] * reach * 2.0, centreY + dir[1] * reach * 2.0,
                            centreZ + dir[2] * reach * 2.0);
        camera->SetViewUp(up[0], up[1], up[2]);
        // 再按包围盒算一次合适的距离（ResetCamera 沿当前方向求距离，不会改变朝向）
        m_renderer->ResetCamera(sceneBounds);
        camera->Zoom(zoom);
    }
    else
    {
        m_renderer->ResetCamera();
    }
    m_renderer->ResetCameraClippingRange();
    m_vtkRenderWindow->Render();
}

} // namespace precisioncbct::viz
