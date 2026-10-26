#include "visualization/ScannerRig.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <vtkActor.h>
#include <vtkAssembly.h>
#include <vtkCellArray.h>
#include <vtkConeSource.h>
#include <vtkCubeSource.h>
#include <vtkCylinderSource.h>
#include <vtkLight.h>
#include <vtkLinearExtrusionFilter.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyLine.h>
#include <vtkProp3D.h>
#include <vtkProp3DCollection.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkSphereSource.h>
#include <vtkTransform.h>
#include <vtkTubeFilter.h>

namespace precisioncbct::viz {
namespace {

constexpr double kPi = 3.14159265358979323846;

/// 配色：制造"产品照片"的层次感——深色花岗岩底座、冷灰机身、亮铝装饰、深色阳极氧化球管
constexpr std::array<double, 3> kGranite{0.075, 0.085, 0.10};     ///< 花岗岩底座（最深）
constexpr std::array<double, 3> kGraniteTop{0.19, 0.205, 0.225};    ///< 机加工台面
constexpr std::array<double, 3> kSteel{0.58, 0.63, 0.70};          ///< 亮铝/机加工件
constexpr std::array<double, 3> kSteelDim{0.40, 0.45, 0.51};       ///< 一般钣金
constexpr std::array<double, 3> kSteelDark{0.17, 0.195, 0.225};    ///< 结构件暗面
constexpr std::array<double, 3> kAnodized{0.095, 0.105, 0.12};     ///< 球管外壳（黑阳极氧化）
constexpr std::array<double, 3> kGlass{0.035, 0.045, 0.06};        ///< 探测器感光面
constexpr std::array<double, 3> kAccentLight{0.18, 0.62, 0.95};    ///< 指示灯（偏青蓝）
constexpr std::array<double, 3> kFocalSpot{1.0, 0.80, 0.30};
constexpr std::array<double, 3> kBeam{0.80, 0.90, 1.0};

// 兼容旧名字（医学/C 臂外观沿用）
constexpr std::array<double, 3> kHousing = kSteelDim;
constexpr std::array<double, 3> kHousingSeam = kSteel;
constexpr std::array<double, 3> kPedestal = kSteelDark;
constexpr std::array<double, 3> kFrame = kSteelDim;
constexpr std::array<double, 3> kTubeHousing = kSteel;
constexpr std::array<double, 3> kCollimator = kAnodized;
constexpr std::array<double, 3> kPanelBody = kSteelDim;
constexpr std::array<double, 3> kPanelBezel = kSteel;
constexpr std::array<double, 3> kPanelFace = kGlass;
constexpr std::array<double, 3> kCouch = kSteelDim;
constexpr std::array<double, 3> kCouchFrame = kSteelDark;
constexpr std::array<double, 3> kFloor{0.085, 0.095, 0.115};

/// x-z 平面上的圆弧（C 形臂用）：从 (x0,0,0) 绕过顶部到 (x1,0,0)
vtkSmartPointer<vtkActor> makeActor(const std::array<double, 3>& rgb, double specular,
                                    double ambient);
vtkSmartPointer<vtkActor> archActor(double x0, double x1, double tubeRadius,
                                    const std::array<double, 3>& rgb, double specular)
{
    const double centreX = 0.5 * (x0 + x1);
    const double radius = 0.5 * (x1 - x0);
    constexpr int kSegments = 140;

    vtkNew<vtkPoints> points;
    vtkNew<vtkPolyLine> line;
    line->GetPointIds()->SetNumberOfIds(kSegments + 1);
    for (int i = 0; i <= kSegments; ++i)
    {
        const double a = kPi * (1.0 - static_cast<double>(i) / kSegments); // 180° → 0°
        points->InsertNextPoint(centreX + radius * std::cos(a), 0.0, radius * std::sin(a));
        line->GetPointIds()->SetId(i, i);
    }
    vtkNew<vtkCellArray> cells;
    cells->InsertNextCell(line);
    vtkNew<vtkPolyData> polyline;
    polyline->SetPoints(points);
    polyline->SetLines(cells);

    vtkNew<vtkTubeFilter> tube;
    tube->SetInputData(polyline);
    tube->SetRadius(tubeRadius);
    tube->SetNumberOfSides(32);
    tube->CappingOn();
    tube->Update();

    auto data = vtkSmartPointer<vtkPolyData>::New();
    data->DeepCopy(tube->GetOutput());
    auto actor = makeActor(rgb, specular, 0.30);
    auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
    mapper->SetInputData(data);
    mapper->ScalarVisibilityOff();
    return actor;
}

/// 圆环实体：把"圆"当成闭合折线再套一层管状滤镜，等价于圆环（VTK 没有现成的 torus）
vtkSmartPointer<vtkPolyData> arcTorusData(double radius, double tubeRadius, double zMm,
                                          double startDeg, double endDeg, int segments)
{
    vtkNew<vtkPoints> points;
    vtkNew<vtkPolyLine> ring;
    ring->GetPointIds()->SetNumberOfIds(segments + 1);
    for (int i = 0; i <= segments; ++i)
    {
        const double t = static_cast<double>(i) / segments;
        const double angle = (startDeg + (endDeg - startDeg) * t) * kPi / 180.0;
        points->InsertNextPoint(radius * std::cos(angle), radius * std::sin(angle), zMm);
        ring->GetPointIds()->SetId(i, i);
    }
    vtkNew<vtkCellArray> cells;
    cells->InsertNextCell(ring);

    vtkNew<vtkPolyData> line;
    line->SetPoints(points);
    line->SetLines(cells);

    vtkNew<vtkTubeFilter> tube;
    tube->SetInputData(line);
    tube->SetRadius(tubeRadius);
    tube->SetNumberOfSides(40);
    tube->SetCapping(false);
    tube->Update();

    auto result = vtkSmartPointer<vtkPolyData>::New();
    result->DeepCopy(tube->GetOutput());
    return result;
}

vtkSmartPointer<vtkPolyData> torusData(double radius, double tubeRadius, double zMm)
{
    return arcTorusData(radius, tubeRadius, zMm, 0.0, 360.0, 160);
}

vtkSmartPointer<vtkActor> makeActor(const std::array<double, 3>& rgb, double specular,
                                    double ambient = 0.30)
{
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    vtkProperty* property = actor->GetProperty();
    property->SetColor(rgb[0], rgb[1], rgb[2]);
    property->SetAmbient(ambient);
    property->SetDiffuse(0.85);
    property->SetSpecular(specular);
    property->SetSpecularPower(28.0);
    return actor;
}

vtkSmartPointer<vtkActor> torusActor(double radius, double tubeRadius, double zMm,
                                     const std::array<double, 3>& rgb, double specular)
{
    auto actor = makeActor(rgb, specular);
    auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
    mapper->SetInputData(torusData(radius, tubeRadius, zMm));
    mapper->ScalarVisibilityOff();
    return actor;
}

vtkSmartPointer<vtkActor> arcTorusActor(double radius, double tubeRadius, double zMm,
                                        double startDeg, double endDeg,
                                        const std::array<double, 3>& rgb, double specular)
{
    auto actor = makeActor(rgb, specular);
    auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
    mapper->SetInputData(arcTorusData(radius, tubeRadius, zMm, startDeg, endDeg,
                                      static_cast<int>(std::max(24.0, std::abs(endDeg - startDeg)))));
    mapper->ScalarVisibilityOff();
    return actor;
}

vtkSmartPointer<vtkActor> cubeActor(double sizeX, double sizeY, double sizeZ,
                                    const std::array<double, 3>& center,
                                    const std::array<double, 3>& rgb, double specular,
                                    bool withEdges = true)
{
    vtkNew<vtkCubeSource> cube;
    cube->SetXLength(sizeX);
    cube->SetYLength(sizeY);
    cube->SetZLength(sizeZ);
    cube->SetCenter(center[0], center[1], center[2]);
    auto actor = makeActor(rgb, specular);
    auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
    mapper->SetInputConnection(cube->GetOutputPort());
    mapper->ScalarVisibilityOff();
    if (withEdges)
    {
        // 结构件描一圈浅色棱线：深色背景下轮廓清楚，观感更接近 CAD 渲染而不是糊成一团灰
        vtkProperty* property = actor->GetProperty();
        property->EdgeVisibilityOn();
        property->SetEdgeColor(0.55, 0.63, 0.72);
        property->SetLineWidth(1.0);
    }
    return actor;
}

/// 圆柱（VTK 的圆柱轴线沿 y，正好与本项目的"射线方向"一致）
vtkSmartPointer<vtkActor> cylinderActor(double radius, double height,
                                        const std::array<double, 3>& center,
                                        const std::array<double, 3>& rgb, double specular)
{
    vtkNew<vtkCylinderSource> cylinder;
    cylinder->SetRadius(radius);
    cylinder->SetHeight(height);
    cylinder->SetCenter(center[0], center[1], center[2]);
    cylinder->SetResolution(48);
    auto actor = makeActor(rgb, specular);
    auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
    mapper->SetInputConnection(cylinder->GetOutputPort());
    mapper->ScalarVisibilityOff();
    return actor;
}

/// 圆角矩形轮廓（x-y 平面，z=0）：机加工件用圆角比尖角像真东西得多
vtkSmartPointer<vtkPolyData> roundedRectPolygon(double w, double h, double r, int segPerCorner = 5)
{
    const double hw = w * 0.5;
    const double hh = h * 0.5;
    const double radius = std::clamp(r, 0.0, std::min(hw, hh) - 1.0);

    vtkNew<vtkPoints> points;
    const double cornerX[4] = {hw - radius, -(hw - radius), -(hw - radius), hw - radius};
    const double cornerY[4] = {hh - radius, hh - radius, -(hh - radius), -(hh - radius)};
    const double startDeg[4] = {0.0, 90.0, 180.0, 270.0};
    for (int corner = 0; corner < 4; ++corner)
    {
        for (int i = 0; i <= segPerCorner; ++i)
        {
            const double a = (startDeg[corner] + 90.0 * i / segPerCorner) * kPi / 180.0;
            points->InsertNextPoint(cornerX[corner] + radius * std::cos(a),
                                    cornerY[corner] + radius * std::sin(a), 0.0);
        }
    }

    const vtkIdType count = points->GetNumberOfPoints();
    std::vector<vtkIdType> ids(static_cast<std::size_t>(count));
    for (vtkIdType i = 0; i < count; ++i)
    {
        ids[static_cast<std::size_t>(i)] = i;
    }
    vtkNew<vtkCellArray> polys;
    polys->InsertNextCell(count, ids.data());

    auto data = vtkSmartPointer<vtkPolyData>::New();
    data->SetPoints(points);
    data->SetPolys(polys);
    return data;
}

/// 带圆角、有厚度的板：轮廓在 x-y 平面，沿 +z 挤出
vtkSmartPointer<vtkActor> plateActor(double w, double h, double thickness, double cornerRadius,
                                     const std::array<double, 3>& rgb, double specular,
                                     double ambient = 0.32)
{
    vtkNew<vtkLinearExtrusionFilter> extrude;
    extrude->SetInputData(roundedRectPolygon(w, h, cornerRadius));
    extrude->SetExtrusionTypeToVectorExtrusion();
    extrude->SetVector(0.0, 0.0, thickness);
    extrude->CappingOn();
    extrude->Update();

    auto data = vtkSmartPointer<vtkPolyData>::New();
    data->DeepCopy(extrude->GetOutput());
    auto actor = makeActor(rgb, specular, ambient);
    auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
    mapper->SetInputData(data);
    mapper->ScalarVisibilityOff();
    vtkProperty* property = actor->GetProperty();
    property->EdgeVisibilityOn();
    property->SetEdgeColor(0.55, 0.63, 0.72);
    property->SetLineWidth(1.0);
    return actor;
}

/// 螺栓头（朝 +z 的小圆柱），给结构件加一点真实细节
void addBolts(vtkAssembly* assembly, int countX, int countY, double spanX, double spanY,
              const std::array<double, 3>& center, double radiusMm)
{
    for (int iy = 0; iy < countY; ++iy)
    {
        for (int ix = 0; ix < countX; ++ix)
        {
            const double fx = countX > 1 ? (static_cast<double>(ix) / (countX - 1) - 0.5) : 0.0;
            const double fy = countY > 1 ? (static_cast<double>(iy) / (countY - 1) - 0.5) : 0.0;
            // 几何建在原点再 SetOrientation/SetPosition：直接给中心点再旋转会绕世界原点转飞
            auto bolt = cylinderActor(radiusMm, 12.0, {0.0, 0.0, 0.0}, kSteel, 0.6);
            bolt->SetOrientation(90.0, 0.0, 0.0);
            bolt->SetPosition(center[0] + fx * spanX, center[1] + fy * spanY, center[2] + 6.0);
            assembly->AddPart(bolt);
        }
    }
}

/// 线缆：沿折线套一层细管，让设备不显得像"悬浮的积木"
vtkSmartPointer<vtkActor> cableActor(const std::vector<std::array<double, 3>>& path,
                                     double radiusMm, const std::array<double, 3>& rgb)
{
    vtkNew<vtkPoints> points;
    vtkNew<vtkPolyLine> line;
    line->GetPointIds()->SetNumberOfIds(static_cast<vtkIdType>(path.size()));
    for (std::size_t i = 0; i < path.size(); ++i)
    {
        points->InsertNextPoint(path[i][0], path[i][1], path[i][2]);
        line->GetPointIds()->SetId(static_cast<vtkIdType>(i), static_cast<vtkIdType>(i));
    }
    vtkNew<vtkCellArray> cells;
    cells->InsertNextCell(line);
    vtkNew<vtkPolyData> polyline;
    polyline->SetPoints(points);
    polyline->SetLines(cells);

    vtkNew<vtkTubeFilter> tube;
    tube->SetInputData(polyline);
    tube->SetRadius(radiusMm);
    tube->SetNumberOfSides(10);
    tube->CappingOn();
    tube->Update();

    auto data = vtkSmartPointer<vtkPolyData>::New();
    data->DeepCopy(tube->GetOutput());
    auto actor = makeActor(rgb, 0.35, 0.5);
    auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
    mapper->SetInputData(data);
    mapper->ScalarVisibilityOff();
    return actor;
}

vtkSmartPointer<vtkActor> sphereActor(double radius, const std::array<double, 3>& center,
                                      const std::array<double, 3>& rgb, bool emissive)
{
    vtkNew<vtkSphereSource> sphere;
    sphere->SetRadius(radius);
    sphere->SetCenter(center[0], center[1], center[2]);
    sphere->SetThetaResolution(24);
    sphere->SetPhiResolution(16);
    auto actor = makeActor(rgb, 0.0, emissive ? 1.0 : 0.3);
    if (emissive)
    {
        actor->GetProperty()->SetDiffuse(0.0);
        actor->GetProperty()->SetAmbient(1.0);
    }
    auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
    mapper->SetInputConnection(sphere->GetOutputPort());
    mapper->ScalarVisibilityOff();
    return actor;
}

/// 球管总成：外壳 + 后盖 + 颈部 + 准直器 + 暗色出束口 + 焦点亮点。
///
/// sourceBelow=true 时源在 -y（医学/C 臂），false 时源在 +y（工业 CT，源在上方）。
void addTubeHead(vtkAssembly* assembly, double sadMm, bool sourceBelow)
{
    const int s = sourceBelow ? 1 : -1; // 源一侧的符号
    const double headRadius = std::clamp(sadMm * 0.17, 60.0, 115.0);
    const double headLength = headRadius * 2.2;

    // "dist" 表示离焦点再往外（远离被照物）的距离
    const auto yAt = [&](double dist) { return -s * (sadMm + dist); };

    // 外壳 + 后端盖（后端盖略粗，看起来像真实的 X 射线管头）
    assembly->AddPart(cylinderActor(headRadius, headLength,
                                    {0.0, yAt(headLength * 0.5 + headRadius * 0.15), 0.0},
                                    kSteelDim, 0.45));
    assembly->AddPart(cylinderActor(headRadius * 1.08, headRadius * 0.22,
                                    {0.0, yAt(headLength + headRadius * 0.18), 0.0},
                                    kSteel, 0.65));

    // 颈部：从外壳前脸伸向准直器
    assembly->AddPart(cylinderActor(headRadius * 0.55, headRadius * 0.5,
                                    {0.0, yAt(headRadius * 0.1), 0.0}, kSteel, 0.55));

    // 准直器：朝被照物方向突出的方形盒 + 出束口的亮铝环 + 暗色窗口
    const double collimatorHeight = headRadius * 1.0;
    assembly->AddPart(cubeActor(headRadius * 1.9, collimatorHeight, headRadius * 1.9,
                                {0.0, yAt(-collimatorHeight * 0.55), 0.0}, kAnodized, 0.35));
    assembly->AddPart(cylinderActor(headRadius * 0.62, headRadius * 0.18,
                                    {0.0, yAt(-collimatorHeight * 1.05), 0.0}, kSteel, 0.7));
    assembly->AddPart(cubeActor(headRadius * 0.95, headRadius * 0.16, headRadius * 0.95,
                                {0.0, yAt(-collimatorHeight * 1.02), 0.0}, kPanelFace, 0.6));

    // 焦点：真正的射线源位置，给一个小亮点
    assembly->AddPart(sphereActor(std::clamp(headRadius * 0.1, 6.0, 12.0), {0.0, yAt(0.0), 0.0},
                                  kFocalSpot, true));
}

/// 平板探测器总成：后壳 + 边框 + 深色感光面 + 线缆盒 + 安装支架。
void addDetectorPanel(vtkAssembly* assembly, double offsetMm, double widthMm, double heightMm,
                      bool sourceBelow)
{
    const int s = sourceBelow ? 1 : -1;
    const double depth = std::clamp(widthMm * 0.22, 55.0, 110.0);
    const double frame = std::clamp(widthMm * 0.06, 22.0, 60.0);
    const auto yAt = [&](double dist) { return s * (offsetMm + dist); };

    // 后壳 + 边框（边框比感光面大一圈，形成"面板"的层次）
    assembly->AddPart(cubeActor(widthMm + frame * 2.0, depth, heightMm + frame * 2.0,
                                {0.0, yAt(depth * 0.5), 0.0}, kPanelBody, 0.3));
    assembly->AddPart(cubeActor(widthMm + frame, depth * 0.35, heightMm + frame,
                                {0.0, yAt(depth * 0.16), 0.0}, kPanelBezel, 0.5));
    // 感光面：比边框更靠被照物一侧，颜色接近黑玻璃
    assembly->AddPart(cubeActor(widthMm, depth * 0.12, heightMm, {0.0, yAt(depth * 0.02), 0.0},
                                kPanelFace, 0.75));
    // 线缆盒 + 安装支架（真实平板上都有）
    assembly->AddPart(cubeActor(widthMm * 0.28, depth * 0.5, depth * 0.6,
                                {widthMm * 0.3, yAt(depth * 1.05), 0.0}, kCollimator, 0.3));
    assembly->AddPart(cubeActor(widthMm * 0.55, depth * 1.1, depth * 0.9,
                                {0.0, yAt(depth * 1.7), 0.0}, kFrame, 0.35));
}

/// 锥束（半透明）：从源一直张到平板，直观显示"照射范围"。
/// 用局部"源在 -y、平板在 +y"的约定，与球管/平板总成一致（工业 CT 与 C 臂整体再转 -90°）。
void addBeamCone(vtkAssembly* assembly, double sadMm, double detectorOffsetMm, double widthMm,
                 double heightMm, double opacity)
{
    vtkNew<vtkConeSource> cone;
    cone->SetDirection(0.0, -1.0, 0.0);
    cone->SetCenter(0.0, 0.5 * (detectorOffsetMm - sadMm), 0.0);
    cone->SetHeight(sadMm + detectorOffsetMm);
    cone->SetRadius(std::max(widthMm, heightMm) * 0.5);
    cone->SetResolution(48);
    cone->CappingOff();

    auto actor = makeActor(kBeam, 0.0, 1.0);
    actor->GetProperty()->SetOpacity(opacity);
    actor->GetProperty()->SetDiffuse(0.0);
    actor->GetProperty()->SetAmbient(1.0);
    auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
    mapper->SetInputConnection(cone->GetOutputPort());
    mapper->ScalarVisibilityOff();
    assembly->AddPart(actor);
}

} // namespace

ScannerRig::ScannerRig() = default;
ScannerRig::~ScannerRig() = default;

void ScannerRig::attachTo(vtkRenderer* renderer)
{
    if (renderer == nullptr || m_renderer == renderer)
    {
        return;
    }
    m_renderer = renderer;

    m_stator = vtkSmartPointer<vtkAssembly>::New();
    m_rotor = vtkSmartPointer<vtkAssembly>::New();
    m_turntable = vtkSmartPointer<vtkAssembly>::New();
    m_rotorTransform = vtkSmartPointer<vtkTransform>::New();
    m_turntableTransform = vtkSmartPointer<vtkTransform>::New();
    m_rotor->SetUserTransform(m_rotorTransform);
    m_turntable->SetUserTransform(m_turntableTransform);
    m_renderer->AddActor(m_stator);
    m_renderer->AddActor(m_rotor);
    m_renderer->AddActor(m_turntable);

    m_couchTop = cubeActor(470.0, 40.0, 1400.0, {0.0, m_couchTopY - 20.0, 0.0}, kCouch, 0.2, false);
    m_couchColumn = cubeActor(200.0, 400.0, 200.0, {0.0, -800.0, -560.0}, kCouchFrame, 0.1, false);
    m_couchBase = cubeActor(520.0, 40.0, 460.0, {0.0, -1000.0, -560.0}, kCouchFrame, 0.1, false);
    m_floor = cubeActor(7000.0, 40.0, 7000.0, {0.0, -1040.0, 0.0}, kFloor, 0.05, false);
    m_floor->GetProperty()->SetDiffuse(0.6);
    for (const auto& actor : {m_couchTop, m_couchColumn, m_couchBase, m_floor})
    {
        m_renderer->AddActor(actor);
    }

    // 三点布光：主光偏左上，补光偏右下，再给一盏轮廓光把机架从背景里拉出来。
    // 用平行光（不设 positional），位置只决定方向，避免光源落进机架内部导致整台设备发黑
    const double lightScale = 2700.0;
    vtkNew<vtkLight> key;
    key->SetPosition(lightScale, lightScale * 0.85, lightScale * 0.9);
    key->SetFocalPoint(0.0, 0.0, 0.0);
    key->SetIntensity(0.95);
    key->SetColor(1.0, 0.98, 0.94);
    m_renderer->AddLight(key);

    vtkNew<vtkLight> fill;
    fill->SetPosition(-lightScale, lightScale * 0.35, -lightScale * 0.6);
    fill->SetFocalPoint(0.0, 0.0, 0.0);
    fill->SetIntensity(0.55);
    fill->SetColor(0.80, 0.88, 1.0);
    m_renderer->AddLight(fill);

    vtkNew<vtkLight> rim;
    rim->SetPosition(lightScale * 0.2, -lightScale, lightScale * 0.4);
    rim->SetFocalPoint(0.0, 0.0, 0.0);
    rim->SetIntensity(0.45);
    rim->SetColor(0.75, 0.85, 1.0);
    m_renderer->AddLight(rim);

    vtkNew<vtkLight> front;
    front->SetPosition(0.15 * lightScale, 0.25 * lightScale, lightScale);
    front->SetFocalPoint(0.0, 0.0, 0.0);
    front->SetIntensity(0.5);
    front->SetColor(0.9, 0.94, 1.0);
    m_renderer->AddLight(front);

    // 默认就摆好一台完整设备：启动时先看到机架，而不是一堆零件等参数
    rebuild(500.0, 500.0, 204.8, 204.8);
    applyVisibility();
}

void ScannerRig::clearParts()
{
    // vtkAssembly 没有 RemoveAllParts()，只能先收集再逐个摘掉
    const auto detachAll = [](vtkAssembly* assembly) {
        if (assembly == nullptr || assembly->GetParts() == nullptr)
        {
            return;
        }
        std::vector<vtkProp3D*> parts;
        const int count = assembly->GetParts()->GetNumberOfItems();
        parts.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            if (auto* part = vtkProp3D::SafeDownCast(assembly->GetParts()->GetItemAsObject(i)))
            {
                parts.push_back(part);
            }
        }
        for (vtkProp3D* part : parts)
        {
            assembly->RemovePart(part);
        }
    };
    detachAll(m_stator);
    detachAll(m_rotor);
    detachAll(m_turntable);
    m_built = false;
}

void ScannerRig::setMode(simulation::ScannerMode mode)
{
    if (m_mode == mode)
    {
        return;
    }
    m_mode = mode;
    if (m_built)
    {
        rebuild(m_sadMm, m_detectorOffsetMm, m_detectorWidthMm, m_detectorHeightMm);
    }
    applyVisibility();
}

void ScannerRig::rebuild(double sadMm, double detectorOffsetMm, double widthMm, double heightMm)
{
    if (m_renderer == nullptr)
    {
        return;
    }

    clearParts();
    m_sadMm = sadMm;
    m_detectorOffsetMm = detectorOffsetMm;
    m_detectorWidthMm = widthMm;
    m_detectorHeightMm = heightMm;
    m_rotorTransform->Identity();
    m_turntableTransform->Identity();

    switch (m_mode)
    {
    case simulation::ScannerMode::Industrial:
        buildIndustrial(sadMm, detectorOffsetMm, widthMm, heightMm);
        break;
    case simulation::ScannerMode::CArm:
        buildCArm(sadMm, detectorOffsetMm, widthMm, heightMm);
        break;
    case simulation::ScannerMode::Medical:
    default:
        buildMedical(sadMm, detectorOffsetMm, widthMm, heightMm);
        break;
    }

    m_built = true;
    updateFloorAndCouch();
    applyVisibility();
}

void ScannerRig::buildMedical(double sadMm, double detectorOffsetMm, double widthMm,
                              double heightMm)
{
    // 外壳内径要容得下球管与平板；再按比例给一圈厚实的机架外壳
    const double boreRadius =
        std::max({detectorOffsetMm + heightMm * 0.5 + 100.0, sadMm + 150.0, 380.0});
    const double housingTube = std::clamp(boreRadius * 0.16, 70.0, 140.0);
    const double housingRadius = boreRadius + housingTube;
    m_ringOuterMm = housingRadius + housingTube;

    // ---------------- 定子：外壳 + 分模线 + 灯带 + 立柱 + 底座
    m_stator->AddPart(torusActor(housingRadius, housingTube, 0.0, kHousing, 0.25));
    m_stator->AddPart(torusActor(housingRadius + housingTube * 0.55, 9.0, 0.0, kHousingSeam, 0.4));
    m_stator->AddPart(torusActor(housingRadius + housingTube * 0.62, 4.5, 0.0, kAccentLight, 0.6));

    const double floorY = -std::max(1000.0, m_ringOuterMm + 220.0);
    const double pedestalTop = -(housingRadius + housingTube);
    if (pedestalTop > floorY)
    {
        const double height = pedestalTop - floorY;
        m_stator->AddPart(
            cubeActor(330.0, height, 330.0, {0.0, floorY + height * 0.5, 0.0}, kPedestal, 0.15));
        m_stator->AddPart(cubeActor(980.0, 60.0, 760.0, {0.0, floorY + 30.0, 0.0}, kPedestal, 0.1));
    }

    // ---------------- 转子：环形转台 + 两个安装滑块 + 球管 + 平板
    const double rotorRadius = std::max(sadMm, detectorOffsetMm) + 60.0;
    m_rotor->AddPart(torusActor(rotorRadius, 26.0, 0.0, kFrame, 0.45));
    m_rotor->AddPart(torusActor(rotorRadius, 10.0, 40.0, kHousingSeam, 0.5));
    m_rotor->AddPart(torusActor(rotorRadius, 10.0, -40.0, kHousingSeam, 0.5));

    const double headRadius = std::clamp(sadMm * 0.17, 60.0, 115.0);
    const double headSpan = sadMm + headRadius * 2.4;
    m_rotor->AddPart(cubeActor(rotorRadius - headSpan + 60.0, 70.0, 150.0,
                               {0.0, -(headSpan + rotorRadius) * 0.5, 0.0}, kFrame, 0.35));
    const double panelSpan = detectorOffsetMm + 200.0;
    m_rotor->AddPart(cubeActor(rotorRadius - panelSpan + 60.0, 70.0, 150.0,
                               {0.0, (panelSpan + rotorRadius) * 0.5, 0.0}, kFrame, 0.35));

    addTubeHead(m_rotor, sadMm, true);
    addDetectorPanel(m_rotor, detectorOffsetMm, widthMm, heightMm, true);
    // 锥束（半透明）：医学 CBCT 也有"源 → 平板"的照射范围，顺手把光锥画上
    addBeamCone(m_rotor, sadMm, detectorOffsetMm, widthMm, heightMm, 0.20);
}

namespace {

/// 水平转台：圆盘 + 定位环 + 三爪 + 中心卡盘，整体绕 z 轴自转（z 轴朝上的坐标系）
void addSampleStage(vtkAssembly* turntable, double radiusMm)
{
    // 盘体（圆柱轴线 y，转 90° 后指向 z，即盘面水平）
    auto disc = cylinderActor(radiusMm, 26.0, {0.0, 0.0, 0.0}, kSteelDim, 0.45);
    disc->SetOrientation(90.0, 0.0, 0.0);
    turntable->AddPart(disc);

    auto face = cylinderActor(radiusMm * 0.93, 8.0, {0.0, 0.0, 0.0}, kSteel, 0.55);
    face->SetOrientation(90.0, 0.0, 0.0);
    face->SetPosition(0.0, 0.0, 15.0);
    turntable->AddPart(face);

    turntable->AddPart(torusActor(radiusMm * 0.97, 5.0, 19.0, kAccentLight, 0.7));

    // 三爪：立在盘面上，沿径向夹住样品
    for (int i = 0; i < 3; ++i)
    {
        const double angle = 2.0 * kPi * i / 3.0 + kPi / 6.0;
        auto jaw = cubeActor(radiusMm * 0.30, radiusMm * 0.18, 34.0, {0.0, 0.0, 0.0}, kSteel, 0.55);
        jaw->SetOrientation(0.0, 0.0, angle * 180.0 / kPi);
        jaw->SetPosition(radiusMm * 0.55 * std::cos(angle), radiusMm * 0.55 * std::sin(angle),
                         32.0);
        turntable->AddPart(jaw);
    }

    // 中心卡盘
    auto chuck = cylinderActor(radiusMm * 0.34, 44.0, {0.0, 0.0, 0.0}, kAnodized, 0.35);
    chuck->SetOrientation(90.0, 0.0, 0.0);
    chuck->SetPosition(0.0, 0.0, 30.0);
    turntable->AddPart(chuck);
}

/// 转台的固定支撑：立柱 + 底板（不随样品旋转，也不挡射线束）
void addStageSupport(vtkAssembly* stator, double radiusMm, double chuckZ, double floorY)
{
    const double columnZ = -chuckZ - radiusMm * 0.55;
    auto column = plateActor(220.0, -floorY * 0.62, 220.0, 18.0, kSteelDim, 0.4);
    column->SetPosition(0.0, floorY * 0.6, columnZ);
    stator->AddPart(column);
    auto arm = plateActor(radiusMm * 0.9, 150.0, 190.0, 16.0, kSteelDim, 0.45);
    arm->SetPosition(0.0, 0.0, columnZ + radiusMm * 0.4);
    stator->AddPart(arm);
    stator->AddPart(cubeActor(440.0, 30.0, 440.0, {0.0, floorY + 180.0, columnZ}, kSteelDark, 0.3));
    addBolts(stator, 2, 2, 300.0, 120.0, {0.0, 0.0, columnZ + radiusMm * 0.4 + 95.0}, 9.0);
}

} // namespace

void ScannerRig::buildIndustrial(double sadMm, double detectorOffsetMm, double widthMm,
                                 double heightMm)
{
    // 工业 CT（z 轴朝上：转台就是水平转盘，绕 z 自转）
    // 花岗岩底座 + 两根立柱，左柱装射线源、右柱装平板探测器，射线沿 x 水平穿过，
    // 样品立在中下方的水平转台上
    const double zFloor = -(std::max({sadMm, detectorOffsetMm}) + 430.0);
    const double postX = std::max(sadMm, detectorOffsetMm) + 300.0;
    const double towerWidth = 320.0;
    const double towerDepth = 380.0;
    const double zBottom = zFloor + 176.0;
    const double zTop = 620.0;
    const double towerHeight = zTop - zBottom;

    m_ringOuterMm = std::max(postX + 160.0, -zFloor * 0.9);

    // ---------------- 底座：花岗岩块 + 机加工台面
    m_stator->AddPart(cubeActor(postX * 2.0 + 560.0, 780.0, 150.0,
                                {0.0, 0.0, zFloor + 75.0}, kGranite, 0.10));
    m_stator->AddPart(cubeActor(postX * 2.0 + 440.0, 660.0, 26.0,
                                {0.0, 0.0, zFloor + 163.0}, kGraniteTop, 0.35));
    m_stator->AddPart(cubeActor(postX * 2.0 + 300.0, 520.0, 14.0,
                                {0.0, 0.0, zFloor + 183.0}, kSteelDark, 0.4));

    // ---------------- 两根立柱（沿 z 立起来）
    for (const double sign : {-1.0, 1.0})
    {
        const double x = sign * postX;
        m_stator->AddPart(cubeActor(towerWidth, towerDepth, towerHeight,
                                    {x, 0.0, zBottom + towerHeight * 0.5}, kSteelDark, 0.3));

        // 朝观察者一侧（-y）的圆角面板
        auto panel = plateActor(towerWidth - 34.0, towerHeight - 46.0, 28.0, 28.0, kSteelDim, 0.5);
        panel->SetOrientation(90.0, 0.0, 0.0);
        panel->SetPosition(x, -towerDepth * 0.5, zBottom + towerHeight * 0.5);
        m_stator->AddPart(panel);

        // 顶部压盖 + 立式指示灯带
        m_stator->AddPart(cubeActor(towerWidth + 30.0, towerDepth + 30.0, 44.0,
                                    {x, 0.0, zTop + 22.0}, kSteel, 0.55));
        m_stator->AddPart(cubeActor(9.0, 8.0, towerHeight * 0.5,
                                    {x + sign * (towerWidth * 0.5 - 12.0), -towerDepth * 0.5 - 6.0,
                                     zBottom + towerHeight * 0.35},
                                    kAccentLight, 0.7));

        // 安装滑座（球管/平板固定在上面，扫描时不移动）
        m_stator->AddPart(cubeActor(340.0, 320.0, 250.0, {x, 0.0, 0.0}, kSteelDim, 0.45));
    }

    // 射线方向沿 x：把"沿 y 出束"的球管/平板总成整体转 -90°，复用同一套模型
    auto beam = vtkSmartPointer<vtkAssembly>::New();
    addTubeHead(beam, sadMm, true);  // 源在 -y → 转后位于 -x（左柱）
    addDetectorPanel(beam, detectorOffsetMm, widthMm, heightMm, true); // 转后位于 +x（右柱）
    {
        vtkNew<vtkTransform> rotateBeam;
        rotateBeam->RotateZ(-90.0);
        beam->SetUserTransform(rotateBeam);
    }

    // 锥束（半透明），直观显示"源 → 平板"的照射范围
    addBeamCone(beam, sadMm, detectorOffsetMm, widthMm, heightMm, 0.30);
    m_rotor->AddPart(beam);

    // ---------------- 样品转台（水平圆盘，绕 z 轴自转）
    m_stageRadiusMm = std::clamp(std::max(widthMm, heightMm) * 0.55, 110.0, 240.0);
    addSampleStage(m_turntable, m_stageRadiusMm);
    // 转台的固定立柱（绕 z 旋转但自身是圆柱，看起来不动）
    {
        auto pedestal = cylinderActor(m_stageRadiusMm * 0.42, std::max(80.0, -zFloor * 0.55),
                                      {0.0, 0.0, 0.0}, kSteelDim, 0.35);
        pedestal->SetOrientation(90.0, 0.0, 0.0);
        pedestal->SetPosition(0.0, 0.0, -std::max(80.0, -zFloor * 0.55) * 0.5);
        m_turntable->AddPart(pedestal);
    }
}

void ScannerRig::buildCArm(double sadMm, double detectorOffsetMm, double widthMm, double heightMm)
{
    // C 型臂（z 轴朝上）：弧臂跨过病床，一端球管沉到床下、一端平板升到病人上方
    // （临床最常见的 AP 位）。床面放水平转台，样品在转台上自转；球管与平板全程不动。
    const double tiltDeg = -30.0; // 初始姿态：整条弧臂绕 y 轴倾斜 30°
    const double headReach = 140.0;
    const double archX0 = -sadMm - headReach;
    const double archX1 = detectorOffsetMm + headReach;
    const double archRadius = 0.5 * (archX1 - archX0);
    const double archCentreX = 0.5 * (archX0 + archX1);
    m_ringOuterMm = archRadius + 220.0;

    auto arm = vtkSmartPointer<vtkAssembly>::New();
    {
        vtkNew<vtkTransform> tilt;
        tilt->RotateY(tiltDeg);
        arm->SetUserTransform(tilt);
    }

    // 弧臂本体 + 内侧加强筋（x-z 平面的半圆，端点正好落在球管/平板处）
    arm->AddPart(archActor(archX0, archX1, 56.0, kSteelDim, 0.45));
    arm->AddPart(archActor(archX0 * 0.94, archX1 * 0.94, 18.0, kSteelDark, 0.30));

    // 高压电缆：从球管沿弧臂外侧绕到平板，细节上更像真设备
    {
        std::vector<std::array<double, 3>> path;
        const double cableRadius = archRadius + 78.0;
        for (int i = 0; i <= 16; ++i)
        {
            const double a = kPi * (1.0 - static_cast<double>(i) / 16.0);
            path.push_back({archCentreX + cableRadius * std::cos(a), 74.0, cableRadius * std::sin(a)});
        }
        arm->AddPart(cableActor(path, 13.0, kAnodized));
    }

    // 弧臂两端的安装法兰（细一些，别把球管/平板整个吞掉）
    arm->AddPart(cubeActor(96.0, 180.0, 180.0, {-sadMm - 120.0, 0.0, 26.0}, kSteelDim, 0.45));
    arm->AddPart(cubeActor(96.0, 310.0, 310.0, {detectorOffsetMm + 118.0, 0.0, 0.0}, kSteelDim,
                           0.45));
    // 平板探测器的固定臂：从法兰伸到平板背面（让平板端正地朝向球管）
    arm->AddPart(cubeActor(150.0, 120.0, 120.0, {detectorOffsetMm + 56.0, 0.0, -18.0}, kSteelDark,
                           0.35));

    // 射线方向沿 x：把"沿 y 出束"的球管/平板总成整体转 -90°，复用同一套模型
    auto beam = vtkSmartPointer<vtkAssembly>::New();
    addTubeHead(beam, sadMm, true);
    addDetectorPanel(beam, detectorOffsetMm, widthMm, heightMm, true);
    {
        // 锥束（半透明）：直观显示"球管 → 平板"的照射范围
        addBeamCone(beam, sadMm, detectorOffsetMm, widthMm, heightMm, 0.22);

        vtkNew<vtkTransform> rotateBeam;
        rotateBeam->RotateZ(-90.0);
        beam->SetUserTransform(rotateBeam);
    }
    arm->AddPart(beam);
    m_rotor->AddPart(arm);

    // ---------------- 落地立柱：立在病床另一侧（+y）托住弧臂顶点，不挡视线也不碰射线束
    const double tiltRad = tiltDeg * kPi / 180.0;
    const double apexX = archCentreX * std::cos(tiltRad) + archRadius * std::sin(tiltRad);
    const double apexZ = -archCentreX * std::sin(tiltRad) + archRadius * std::cos(tiltRad);
    const double zFloor = -(apexZ * 0.55 + 620.0);
    const double columnY = 560.0;

    // 底座（地面上）+ 地脚螺栓
    m_stator->AddPart(cubeActor(780.0, 780.0, 70.0, {apexX, columnY, zFloor + 35.0}, kPedestal,
                                0.15));
    addBolts(m_stator, 2, 2, 540.0, 540.0, {apexX, columnY, zFloor + 76.0}, 16.0);

    // 立柱：方形型材 + 正面装饰面板 + 竖灯带
    const double columnBottom = zFloor + 70.0;
    const double columnTop = apexZ + 60.0;
    m_stator->AddPart(cubeActor(250.0, 250.0, columnTop - columnBottom,
                                {apexX, columnY, 0.5 * (columnBottom + columnTop)}, kSteelDim,
                                0.35));
    {
        auto columnPanel =
            plateActor(200.0, columnTop - columnBottom - 120.0, 20.0, 20.0, kSteelDark, 0.5);
        columnPanel->SetOrientation(-90.0, 0.0, 0.0);
        columnPanel->SetPosition(apexX, columnY - 125.0, 0.5 * (columnBottom + columnTop));
        m_stator->AddPart(columnPanel);
        m_stator->AddPart(cubeActor(9.0, 8.0, (columnTop - columnBottom) * 0.4,
                                    {apexX + 96.0, columnY - 133.0,
                                     columnBottom + (columnTop - columnBottom) * 0.35},
                                    kAccentLight, 0.7));
    }

    // 托臂：从立柱向前伸，尾端夹住弧臂顶点的背面（弧臂倾斜角固定，不会互相穿插）
    m_stator->AddPart(cubeActor(210.0, 470.0, 190.0, {apexX, columnY - 235.0, apexZ - 20.0},
                                kSteelDim, 0.45));
    m_stator->AddPart(cubeActor(200.0, 150.0, 210.0, {apexX, 85.0, apexZ - 10.0}, kSteelDark, 0.4));

    // 桌面上再放一个水平转台
    m_stageRadiusMm = std::clamp(std::max(widthMm, heightMm) * 0.45, 90.0, 180.0);
    addSampleStage(m_turntable, m_stageRadiusMm);
}

void ScannerRig::updateFloorAndCouch()
{
    if (m_renderer == nullptr)
    {
        return;
    }
    auto* top = vtkCubeSource::SafeDownCast(
        vtkPolyDataMapper::SafeDownCast(m_couchTop->GetMapper())->GetInputAlgorithm());
    auto* column = vtkCubeSource::SafeDownCast(
        vtkPolyDataMapper::SafeDownCast(m_couchColumn->GetMapper())->GetInputAlgorithm());
    auto* base = vtkCubeSource::SafeDownCast(
        vtkPolyDataMapper::SafeDownCast(m_couchBase->GetMapper())->GetInputAlgorithm());
    auto* floor = vtkCubeSource::SafeDownCast(
        vtkPolyDataMapper::SafeDownCast(m_floor->GetMapper())->GetInputAlgorithm());

    const double tableLength = std::max(600.0, m_couchZMax - m_couchZMin);
    const double tableCentreZ = 0.5 * (m_couchZMin + m_couchZMax);

    if (m_mode == simulation::ScannerMode::Medical)
    {
        // 医学：y 轴朝上，检查床沿 z 铺开
        const double floorY = -std::max(1000.0, m_ringOuterMm * 0.85 + 260.0);
        top->SetXLength(470.0);
        top->SetYLength(40.0);
        top->SetZLength(tableLength);
        top->SetCenter(0.0, m_couchTopY - 20.0, tableCentreZ);

        const double columnTop = m_couchTopY - 40.0;
        const double columnZ = m_couchZMin + std::min(260.0, tableLength * 0.18);
        column->SetXLength(200.0);
        column->SetZLength(200.0);
        column->SetYLength(std::max(60.0, columnTop - floorY));
        column->SetCenter(0.0, 0.5 * (floorY + columnTop), columnZ);

        base->SetXLength(520.0);
        base->SetYLength(40.0);
        base->SetZLength(460.0);
        base->SetCenter(0.0, floorY + 20.0, columnZ);

        floor->SetXLength(7000.0);
        floor->SetYLength(40.0);
        floor->SetZLength(7000.0);
        floor->SetCenter(0.0, floorY - 20.0, tableCentreZ);
        return;
    }

    // 工业 CT / C 臂：z 轴朝上（转台盘面水平），地面是 x-y 平面
    const double zFloor = -(m_ringOuterMm * 0.8 + 300.0);
    const double tableTopZ = m_sampleMinZ - 34.0;

    // 水平病床：沿 y 方向铺开，病人躺在上面
    top->SetXLength(520.0);
    top->SetYLength(tableLength);
    top->SetZLength(46.0);
    top->SetCenter(0.0, tableCentreZ, tableTopZ);

    // 床立柱放在床尾远端（+y），像真实手术床那样悬臂支撑，
    // 不挡在样品/射线束背后（否则转一圈时样品总被那块"长方形"挡住）
    const double columnY = m_couchZMax - std::min(240.0, tableLength * 0.15);
    column->SetXLength(190.0);
    column->SetYLength(190.0);
    column->SetZLength(std::max(80.0, tableTopZ - zFloor));
    column->SetCenter(0.0, columnY, 0.5 * (zFloor + tableTopZ));

    base->SetXLength(520.0);
    base->SetYLength(420.0);
    base->SetZLength(40.0);
    base->SetCenter(0.0, columnY, zFloor + 20.0);

    floor->SetXLength(7000.0);
    floor->SetYLength(7000.0);
    floor->SetZLength(40.0);
    floor->SetCenter(0.0, 0.0, zFloor - 20.0);
}
void ScannerRig::setBeamGeometry(const std::array<double, 3>& sourceMm,
                                 const std::array<double, 3>& detectorCenterMm,
                                 double detectorWidthMm, double detectorHeightMm)
{
    if (m_renderer == nullptr || detectorWidthMm <= 0.0 || detectorHeightMm <= 0.0)
    {
        return;
    }

    const double sad = std::sqrt(sourceMm[0] * sourceMm[0] + sourceMm[1] * sourceMm[1]);
    const double offset = std::sqrt(detectorCenterMm[0] * detectorCenterMm[0] +
                                    detectorCenterMm[1] * detectorCenterMm[1]);
    if (sad < 1.0 || offset < 1.0)
    {
        return;
    }

    const bool geometryChanged = !m_built || std::abs(sad - m_sadMm) > 1.0 ||
                                 std::abs(offset - m_detectorOffsetMm) > 1.0 ||
                                 std::abs(detectorWidthMm - m_detectorWidthMm) > 1.0 ||
                                 std::abs(detectorHeightMm - m_detectorHeightMm) > 1.0;
    if (geometryChanged)
    {
        rebuild(sad, offset, detectorWidthMm, detectorHeightMm);
    }

    // ConeBeamView 的约定：0° 时源在 -y，角度绕 z 正向（逆时针）
    const double angleDeg = std::atan2(sourceMm[0], -sourceMm[1]) * 180.0 / kPi;
    m_rotorTransform->Identity();
    m_turntableTransform->Identity();
    if (simulation::scannerModeRotatesSample(m_mode))
    {
        // 工业 CT：机架不动，样品与转台反向旋转（与"机架正转"在投影上完全等价）
        setSampleRotationDeg(-angleDeg);
    }
    else
    {
        m_rotorTransform->RotateZ(angleDeg);
        m_rotorTransform->Translate(0.0, 0.0, sourceMm[2]);
    }
}

void ScannerRig::resetBeam()
{
    if (m_rotorTransform != nullptr)
    {
        m_rotorTransform->Identity();
        // 展示位：转 35° 让球管与平板错开，一眼能同时看到两者（工业 CT 机架本身不动）
        if (!simulation::scannerModeRotatesSample(m_mode))
        {
            m_rotorTransform->RotateZ(-35.0);
        }
    }
    if (m_turntableTransform != nullptr)
    {
        setSampleRotationDeg(0.0);
    }
}

void ScannerRig::setSampleRotationDeg(double angleDeg)
{
    m_turntableAngleDeg = angleDeg;
    updateStageTransform();
}

void ScannerRig::updateStageTransform()
{
    if (m_turntableTransform == nullptr)
    {
        return;
    }
    // 转台轴线是 z（与投影几何的旋转轴一致）；盘面要贴着样品底面，所以先缩放再平移/旋转
    m_turntableTransform->Identity();
    if (simulation::scannerModeRotatesSample(m_mode))
    {
        const double stageZ = (m_mode == simulation::ScannerMode::CArm)
                                  ? m_sampleMinZ - 20.0   // C 臂：转台放在床面上
                                  : m_sampleMinZ - 14.0;  // 工业 CT：转台在底座上方
        m_turntableTransform->Translate(0.0, 0.0, stageZ);
    }
    m_turntableTransform->RotateZ(m_turntableAngleDeg);
    // 只缩放 x-y（盘面），让盘面始终比样品大一圈，样品不会被"埋"在盘里
    m_turntableTransform->Scale(m_stageScale, m_stageScale, 1.0);
}

void ScannerRig::setSampleBounds(const double bounds[6])
{
    m_sampleMinZ = bounds[4];
    m_sampleMinY = bounds[2];
    m_sampleSpanY = std::max(100.0, bounds[3] - bounds[2]);
    m_couchTopY = bounds[2] - 2.0;
    m_couchZMin = std::min(bounds[4] - 700.0, bounds[4] - 400.0);
    m_couchZMax = std::max(bounds[5] + 700.0, bounds[5] + 400.0);

    // 转台盘面随样品 footprint 缩放：盘面比样品大一圈，样品不会"埋"在盘里
    const double spanXY = std::max(bounds[1] - bounds[0], bounds[3] - bounds[2]);
    const double wantedRadius = std::clamp(spanXY * 0.62, 90.0, 280.0);
    m_stageScale = (m_stageRadiusMm > 1.0) ? std::clamp(wantedRadius / m_stageRadiusMm, 0.7, 2.6)
                                           : 1.0;

    updateFloorAndCouch();
    updateStageTransform();
}

void ScannerRig::setVisible(bool visible)
{
    m_visible = visible;
    applyVisibility();
}

void ScannerRig::applyVisibility()
{
    const bool visible = m_visible;
    const bool usesStage = simulation::scannerModeRotatesSample(m_mode);
    if (m_stator != nullptr)
    {
        m_stator->SetVisibility(visible);
    }
    if (m_rotor != nullptr)
    {
        m_rotor->SetVisibility(visible);
    }
    if (m_turntable != nullptr)
    {
        m_turntable->SetVisibility(visible && usesStage);
    }
    // 工业 CT 没有病床；医学与 C 臂都保留床（C 臂的转台就架在床面上）
    const bool usesCouch = m_mode != simulation::ScannerMode::Industrial;
    for (const auto& actor : {m_couchTop, m_couchColumn, m_couchBase})
    {
        if (actor != nullptr)
        {
            actor->SetVisibility(visible && usesCouch);
        }
    }
    if (m_floor != nullptr)
    {
        m_floor->SetVisibility(visible);
    }
}

void ScannerRig::bounds(double out[6]) const
{
    // 取景框只保证"机架 + 床/转台"好看：地面和立柱底部允许被裁掉一点，
    // 否则地面尺寸会把整机压得很小，失去身临其境的感觉
    const double radius = std::max(m_ringOuterMm + 70.0, 900.0);
    out[0] = -radius;
    out[1] = radius;
    out[2] = -radius * 0.95;
    out[3] = radius * 0.98;
    out[4] = -radius * 0.55;
    out[5] = radius * 0.55;
}

} // namespace precisioncbct::viz
