#pragma once

#include <array>

#include <vtkSmartPointer.h>

#include "simulation/ScannerMode.h"

class vtkActor;
class vtkAssembly;
class vtkRenderer;
class vtkTransform;

namespace precisioncbct::viz {

/// 真实感机架模型：按机架类型生成不同形态，尺寸全部由投影几何推导。
///
/// - 医学 CBCT：环形外壳 + 转子（球管 / 准直器 / 平板）+ 检查床 + 立柱 + 地面；
/// - 工业 CT  ：门架 + 顶部微焦点球管 + 底部平板 + 样品转台（转台随扫描角自转）；
/// - C 型臂    ：C 形弧臂 + 两端球管/平板 + 手术床 + 立柱 + 地面。
///
/// 只负责外观，不参与任何物理计算。
class ScannerRig
{
public:
    ScannerRig();
    ~ScannerRig();

    /// 挂到渲染器上（构造后调用一次）
    void attachTo(vtkRenderer* renderer);

    /// 切换机架类型（会按上次的几何重建）
    void setMode(simulation::ScannerMode mode);
    simulation::ScannerMode mode() const { return m_mode; }

    /// 按当前投影几何摆位：球管在射线源方向、平板在探测器方向，机架转到对应角度
    void setBeamGeometry(const std::array<double, 3>& sourceMm,
                         const std::array<double, 3>& detectorCenterMm,
                         double detectorWidthMm,
                         double detectorHeightMm);

    /// 机架回到展示位（无投影几何时使用）
    void resetBeam();

    /// 工业 CT 下样品转台的旋转角（度）；其它机架忽略
    void setSampleRotationDeg(double angleDeg);
    /// 样品包围盒（mm，世界坐标）：决定转台高度、床面高度与取景
    void setSampleBounds(const double bounds[6]);

    void setVisible(bool visible);
    bool visible() const { return m_visible; }

    /// 整机包围盒（不含地面），供相机取景
    void bounds(double out[6]) const;

private:
    void rebuild(double sadMm, double detectorOffsetMm, double widthMm, double heightMm);
    void buildMedical(double sadMm, double detectorOffsetMm, double widthMm, double heightMm);
    void buildIndustrial(double sadMm, double detectorOffsetMm, double widthMm, double heightMm);
    void buildCArm(double sadMm, double detectorOffsetMm, double widthMm, double heightMm);
    void updateFloorAndCouch();
    void updateStageTransform();
    void applyVisibility();
    void clearParts();

    vtkRenderer* m_renderer = nullptr;
    simulation::ScannerMode m_mode = simulation::ScannerMode::Medical;

    vtkSmartPointer<vtkAssembly> m_stator;    ///< 外壳 / 门架 / 立柱 / 底座（不动）
    vtkSmartPointer<vtkAssembly> m_rotor;     ///< 旋转部分（球管 + 平板 + 弧臂）
    vtkSmartPointer<vtkAssembly> m_turntable; ///< 工业 CT 的样品转台（随样品自转）
    vtkSmartPointer<vtkTransform> m_rotorTransform;
    vtkSmartPointer<vtkTransform> m_turntableTransform;

    vtkSmartPointer<vtkActor> m_couchTop;
    vtkSmartPointer<vtkActor> m_couchColumn;
    vtkSmartPointer<vtkActor> m_couchBase;
    vtkSmartPointer<vtkActor> m_floor;

    double m_sadMm = 0.0;
    double m_detectorOffsetMm = 0.0;
    double m_detectorWidthMm = 0.0;
    double m_detectorHeightMm = 0.0;
    double m_ringOuterMm = 0.0;

    double m_couchTopY = -150.0;
    double m_couchZMin = -700.0;
    double m_couchZMax = 700.0;
    // 样品包围盒（工业 CT / C 臂是"z 轴朝上"的坐标系，转台要贴着样品底面）
    double m_sampleMinZ = -50.0;
    double m_sampleMinY = -50.0;
    double m_sampleSpanY = 100.0;
    double m_turntableAngleDeg = 0.0; ///< 转台当前角度
    double m_stageRadiusMm = 100.0;   ///< 转台盘半径
    double m_stageScale = 1.0;        ///< 按样品 footprint 放大/缩小转台（只缩放 x-y）

    bool m_built = false;
    bool m_visible = true;
};

} // namespace precisioncbct::viz
