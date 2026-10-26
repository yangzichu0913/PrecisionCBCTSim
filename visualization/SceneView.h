#pragma once

#include <QVTKOpenGLNativeWidget.h>

#include <array>
#include <memory>

#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNew.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>

#include "visualization/ScannerRig.h"
#include "simulation/ScannerMode.h"

class vtkActor;
class vtkImageActor;
class vtkImageData;
class vtkImageMapToColors;
class vtkPolyData;
class vtkPolyDataMapper;
class vtkScalarsToColors;
class vtkTextActor;

namespace precisioncbct::viz {

/// 三维视图容器：封装 QVTKOpenGLNativeWidget 与唯一的场景渲染器。
///
/// 负责管理两类可视化对象：
/// - 三角网格模型（表面渲染）
/// - 体素网格（半透明体渲染，用于检查体素化结果）
/// 射线源 / 探测器 / 旋转轨迹等仿真元素在 Phase 1 第 3 步加入。
class SceneView : public QVTKOpenGLNativeWidget
{
    Q_OBJECT

public:
    explicit SceneView(QWidget* parent = nullptr);
    // 显式声明析构函数：成员含 std::array<vtkSmartPointer<...>>，
    // 只在 SceneView.cpp（含完整 VTK 头）中实例化，避免不完整类型报错
    ~SceneView() override;

    /// 场景渲染器。调用方负责添加/移除 Actor，并在修改后触发渲染。
    vtkRenderer* renderer() const { return m_renderer; }

    /// 显示/替换模型网格；传入 nullptr 等同于 clearMesh()
    void setMesh(vtkPolyData* mesh);
    void clearMesh();

    /// 机架类型：医学 CBCT / 工业 CT / C 型臂（会切换三维机架外观）
    void setScannerMode(simulation::ScannerMode mode);
    simulation::ScannerMode scannerMode() const { return m_scannerMode; }

    /// 显示/替换体素网格：在网格中心取轴位/冠状/矢状三个切面，
    /// 用二值掩膜（0=外部、1=内部）着色。传入 nullptr 等同于 clearVoxelGrid()
    void setVoxelGrid(vtkImageData* grid);
    void clearVoxelGrid();

    /// 显示重建出来的断层体数据（浮点 μ）：同样取三个中心正交切面，
    /// 但用灰度窗 [windowMin, windowMax] 着色（而不是二进制的透明/浅蓝）。
    /// 切面位置可用 setSliceIndices() 调整。
    void setReconstructedVolume(vtkImageData* volume, double windowMin, double windowMax);

    /// 调整三个切面的位置（索引超出范围会被夹到范围内）
    void setSliceIndices(int axialIndex, int coronalIndex, int sagittalIndex);

    /// 当前体数据的维度（未设置时返回 false）
    bool volumeDimensions(int out[3]) const;

    /// 显示当前投影几何：射线源（小球）+ 探测器平面；用于直观核对 SAD/SDD/角度。
    void setProjectionGeometry(const std::array<double, 3>& sourceMm,
                               const std::array<double, 3>& detectorCenterMm,
                               const std::array<double, 3>& uAxis,
                               const std::array<double, 3>& vAxis,
                               double detectorWidthMm,
                               double detectorHeightMm);
    void clearProjectionGeometry();

    /// 扫描轨迹：射线源与探测器中心绕 z 轴扫过的圆弧，用于直观展示机架运动范围。
    void setScanTrajectory(double sadMm, double detectorOffsetMm, double startDeg, double endDeg);
    void clearScanTrajectory();

    /// 移除模型与体素，恢复空白场景
    void clearScene();

    bool hasMesh() const { return m_meshActor != nullptr; }

public slots:
    /// 让相机适配当前场景内容；场景为空时回到默认视角。
    void resetView();

private:
    void applyMeshAppearance();
    void render();

    vtkNew<vtkGenericOpenGLRenderWindow> m_vtkRenderWindow;
    vtkSmartPointer<vtkRenderer> m_renderer;

    vtkSmartPointer<vtkPolyDataMapper> m_meshMapper;
    vtkSmartPointer<vtkActor> m_meshActor;
    double m_meshCenter[3] = {0.0, 0.0, 0.0}; ///< 模型包围盒中心（工业 CT 绕它旋转）

    simulation::ScannerMode m_scannerMode = simulation::ScannerMode::Medical;
    double m_sampleAngleDeg = 0.0; ///< 工业 CT：样品转台当前角度

    vtkSmartPointer<vtkImageData> m_voxelGrid;
    std::array<vtkSmartPointer<vtkImageActor>, 3> m_sliceActors;
    std::array<vtkSmartPointer<vtkImageMapToColors>, 3> m_sliceColors;
    std::array<vtkSmartPointer<vtkScalarsToColors>, 3> m_sliceLuts;
    int m_sliceIndices[3] = {0, 0, 0};
    bool m_volumeIsScalar = false; ///< true 表示当前显示的是浮点 μ 重建体

    vtkSmartPointer<vtkActor> m_trajectoryActor;
    vtkSmartPointer<vtkPolyDataMapper> m_trajectoryMapper;

    /// 真实感机架（外壳 + 转子 + 检查床 + 地面）；只做外观，不参与物理计算
    std::unique_ptr<ScannerRig> m_rig;

    vtkSmartPointer<vtkTextActor> m_emptyHint; ///< 空场景提示文字（有模型时隐藏）
};

} // namespace precisioncbct::viz
