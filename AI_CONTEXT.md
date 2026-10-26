# 项目启动上下文：PrecisionCBCTSim

## 一、项目概述

我正在从零开发一个 Windows 本地桌面软件，名称为 **PrecisionCBCTSim**，
用于 CBCT（锥形束CT）成像过程的仿真。核心目标：

- 读入三维模型（STL/OBJ，后续支持 DICOM 体数据）
- 提供参数化设置（SAD、SDD、探测器像素尺寸与行列数、投影角度范围与步长、X射线能谱、噪声水平）
- 三维可视化仿真过程（射线源、探测器平面、旋转轨迹、体数据）
- 仿真完成后生成 DR 图（数字X射线影像 / DRR），支持单张预览和序列导出

## 二、确定的技术栈（不要建议替换）

| 层面 | 选型 |
|---|---|
| 语言 | C++（主语言），CUDA（计算内核） |
| 界面 | Qt 6 Widgets，MSVC 2022 64-bit 套件 |
| 三维可视化 | VTK 9，通过 QVTKOpenGLNativeWidget 嵌入 Qt |
| 计算 | 先实现 CPU 射线追踪跑通链路，再替换为 CUDA 内核 |
| 构建 | CMake + MSVC（Visual Studio 2022） |
| 版本控制 | Git，仓库为 Private |
| 打包 | Inno Setup（发布阶段） |

## 三、许可证约束（必须遵守）

- 本项目**不开源**，我的代码为专有许可，仓库保持 Private。
- Qt 使用 **LGPLv3**：必须**动态链接** Qt（.dll），不修改 Qt 源码，保留版权声明。
  **禁止**建议静态链接 Qt 或修改 Qt 源码。
- VTK 为 BSD-3-Clause：只需在 THIRD_PARTY_NOTICES 中保留声明。
- CUDA 为 NVIDIA 专有：只分发运行时 DLL。
- 任何涉及许可证的代码或配置，都必须与上述约束一致。

## 四、已确定的工程结构

PrecisionCBCTSim/
├── app/ # Qt 主程序入口、MainWindow、参数面板、DR 预览、深色主题（Theme）、图标资源
├── visualization/ # VTK 场景、Actor 管理、QVTKOpenGLNativeWidget 封装
├── io/ # 模型/体数据读取、质量检查与修复、布尔合并、体素化
├── simulation/ # 投影几何、CPU 射线追踪、能谱/探测器模型、CUDA 内核（待）
├── export/ # DR 图导出（PNG/TIFF，后续 DICOM）+ 重建体导出（切片序列）
├── settings/ # 参数预设（JSON 保存/加载）
├── tests/ # 数值自检（CTest）
├── samples/ # 示例模型（sample_gear / sample_torusknot / sample_phantom）
├── images/ # 文档配图（images/screens 为截图与插图，images/deco 为徽标/分隔线/色标等浅色装饰）
├── installer/ # Inno Setup 安装包脚本
├── scripts/ # build.ps1 / build-vtk.ps1 / deploy.ps1
├── CMakeLists.txt
├── LICENSE # 专有许可声明
└── THIRD_PARTY_NOTICES.txt

## 五、开发路线图（按阶段推进，不要跳步）

- **Phase 1（当前）**：Qt 主窗口 + VTK 嵌入 → 读入 STL/OBJ 并三维显示 → 参数面板 →
  CPU 射线追踪生成单张 DR 图。目标是跑通"模型→体素→投影→DR"全链路。
- **Phase 2**：多角度仿真、探测器沿轨迹旋转、DR 预览实时刷新、后台线程避免 UI 卡顿。
- **Phase 3**：CUDA 替换射线追踪内核，GPU 体素化，可选能谱/噪声模型。
- **Phase 4**：DICOM 输入、投影序列导出、phantom 几何验证。

## 六、协作方式要求

1. **先给方案再写代码**：涉及架构或模块划分时，先用简短列表说明设计，确认后再写实现。
2. **小步交付**：每次只聚焦一个可编译、可运行的最小增量，不要一次性输出大量未验证代码。
3. **代码规范**：C++17 及以上；头文件用 `#pragma once`；命名空间统一；关键函数写注释说明几何/物理含义。
4. **CMake 优先**：新增源文件时同步更新 CMakeLists.txt，保证项目始终可构建。
5. **性能敏感处标注**：射线追踪、体素化等热点代码，注明当前是 CPU 版还是 CUDA 版，以及预期的优化路径。
6. **不要引入未讨论的新依赖**：如需新增第三方库，先说明理由和许可证，确认后再用。
7. **遇到不确定的 API 或版本差异，明确说明**，不要编造 VTK/Qt/CUDA 的接口。

## 七、当前任务

请从 **Phase 1 第一步**开始：给出 Qt 6 + VTK 9 集成的最小可运行工程骨架
（CMakeLists.txt、main.cpp、MainWindow 类、QVTKOpenGLNativeWidget 嵌入的空白三维视图），
并说明在 Windows + MSVC 2022 下如何配置和构建。先不要实现模型读取和射线追踪。

### 进度记录

- **Phase 1 第 1 步（已完成）**：根 `CMakeLists.txt` + `app/`（main.cpp、MainWindow、
  ScanParameterPanel）+ `visualization/`（SceneView 封装 QVTKOpenGLNativeWidget）+
  `simulation/ScanParameters.h` + `scripts/build.ps1`（依赖自检+构建）+
  `scripts/build-vtk.ps1`（下载并编译带 Qt 支持的 VTK 9.3.1）+ README 构建说明。
- **Phase 1 第 2 步（已完成）**：`io/`（MeshLoader 读 STL/OBJ + VoxelGrid 用
  vtkPolyDataToImageStencil 体素化）、`SceneView` 增加 mesh Actor 与三向切面显示、
  MainWindow 接「打开模型 / 体素化预览」+ 命令行 `--open/--voxel-size`、
  `tests/VoxelSanity.cpp`（CTest，解析球/立方体体积与维度校验，4 项判据全通过）。
- **Phase 1 第 3 步（已完成）**：`simulation/`（ConeBeamView 投影几何 + CpuProjector
  索引空间 DDA 射线追踪，Beer-Lambert `I=I0·exp(-μL)`）、`export/DrExporter`（16 位 PNG）、
  `app/DRPreviewWidget` + 底部「DR 预览」、`SceneView` 绘制源/探测器位姿、
  命令行 `--dr` / `--export-dr`、`tests/ProjectionSanity.cpp`（16 项判据）。
- **Phase 2（已完成）**：`simulation/ProjectionSequence`（角度展开、逐帧回调、取消语义、
  倒序扫描支持）、`CpuProjector` 改为按行 `std::thread` 多线程（512² @2mm：0.35s → 约 0.04s/帧）、
  `SceneView::setScanTrajectory` 绘制源/探测器扫描圆弧、`DRPreviewWidget` 帧滑块
  （帧号与角度用 QPainter 画进图像，避免被布局挤压）、MainWindow 用
  `QtConcurrent + QFutureWatcher` 后台生成序列（状态栏进度条 + 取消，进度 100ms 节流）、
  「导出投影序列」逐帧流式写 16 位 PNG、命令行
  `--sequence / --export-sequence / --angle-step / --preview-frame`、
  `tests/SequenceSanity.cpp`（22 项判据，含取消与非法参数）。
- **Phase 3 第 1 步（已完成）**：`simulation/AttenuationTable`（水/骨/PMMA/空气/铝的
  μ/ρ 表，20–150 keV，双对数插值）、`XRaySpectrum`（Kramers 连续谱 + 铝滤过，1 keV 分箱）、
  `SpectralProjector`（谱加权透射 + Poisson 噪声 + 等效 μ_eff）、
  `CpuProjector::computePathLengths` 抽成公共 API（单 μ 与能谱两条路径共用 DDA）、
  参数面板「能谱与噪声」组（kVp / 铝滤过 / 光子数 / 噪声 / 材料，已放入滚动区）、
  命令行 `--spectral / --kvp / --photons / --noise`、`tests/SpectralSanity.cpp`（28 项判据）。
- **Phase 3 第 2 步（已完成）**：`io/MaterialLabeling`（体素级材料标签：按到表面距离分
  「皮质骨外壳 / 内核材料」，自实现 Felzenszwalb 精确欧氏距离变换，支持各向异性体素）、
  `CpuProjector::computeMaterialPathLengths`（DDA 改成带回调的 `traverseGrid` 模板，
  单材料累计总长、多材料按标签分别累计，共用同一份遍历代码）、
  `SpectralProjector` 支持多材料 `T = Σ w_E·exp(−Σ_m μ_m(E)·L_m)`、
  参数面板「皮质骨厚度」+ 摘要显示外壳体素数、命令行 `--shell <mm>`、
  `tests/MaterialLabelingSanity.cpp`（14 项判据）。
- **Phase 3 第 3 步（已完成）**：探测器与散射模型。
  `simulation/DetectorModelParams.h`（`DetectorModelOptions`：SPR、散射核、探测器 PSF、
  串扰比例、电子学噪声；另有 `DetectorApplication` 用于单一 μ 路径）、
  `simulation/DetectorModel.{h,cpp}`（可分离高斯模糊 + 散射叠加 + PSF + Poisson +
  8 邻域串扰 + 高斯电子学噪声，`mixSeed` 派生确定性 RNG；全零参数 = 逐比特恒等变换）、
  `SpectralProjector` 把噪声统一交给探测器模型（避免重复加噪）、
  `ProjectionSequence::runSequence` 新增可选 `DetectorApplication` 让单一 μ 路径也能叠加、
  参数面板「探测器模型」组 + 命令行 `--spr / --psf / --crosstalk / --enoise`、
  `tests/DetectorModelSanity.cpp`（19 项判据）。
- **Phase 3 第 4 步（已完成）**：
  1. 参数预设：`settings/SettingsPreset.{h,cpp}`（Qt Core `QJsonDocument`，`formatVersion: 1`，
     缺失字段沿用原值、未知字段忽略、加载时做范围校验），菜单「保存/加载参数预设」，
     命令行 `--save-preset / --load-preset`，`tests/SettingsPresetSanity.cpp`（30+ 项判据）；
  2. 打包：`scripts/deploy.ps1`（windeployqt + VTK DLL + VC Redist + `--version` 冒烟验证，
     产出 `dist\<Config>` 约 460 MB）、`installer/PrecisionCBCTSim.iss`（Inno Setup 6）；
  3. phantom 定量验收：`tests/ImageQualitySanity.cpp`（均匀性、噪声标定、PMMA/水 CNR、
     PSF 边缘宽度、散射抬升暗区，共 16 项判据）。
- **Phase 3 第 5 步（已完成）**：几何稳健性。
  实测发现 5 类问题（探针见测试文件头部注释）：①单位错误静默通过（0.04mm 模型只得到 1 个体素）
  ②NaN/Inf 顶点静默通过 ③开口网格被 stencil 静默补成实心 ④坐标平移 1e8 mm 时 float32
  精度崩溃（体积少 35%）⑤间距误输 0.05mm 直接分配 805³ ≈ 5.2 亿体素。
  对应实现：`io/MeshQuality`（边界边/非流形边/NaN/退化三角形/坐标量级体检）、
  `io/MeshRepair`（vtkCleanPolyData 焊接 + vtkFillHolesFilter 补洞 + vtkPolyDataNormals
  统一朝向，修复后重新体检）、`io::voxelize` 增加 5 道护栏并把「补了几条边界边」写到
  `VoxelGrid::repairedBoundaryEdges`、`MeshLoader` 在加载阶段拒绝 NaN/Inf、
  MainWindow 在状态栏与模型信息里如实报告拓扑与自动修复、
  `tests/GeometryRobustnessSanity.cpp`（44 项判据）。
  期间试过「多方向射线奇偶 + 多数表决」的稳健体素化，实测方管（上下开口）仍被判成实心
  （从中心向 +x 只穿 1 个面 → 奇数 → 内部），说明没有水密性就没有内外定义，故弃用该路径。
- **Phase 3 第 6 步（已完成）**：引入 **Manifold v3.5.4（Apache-2.0，静态库）** 做布尔运算。
  触发原因是实测到的两个**静默错误**（探针数据）：
  ① 一个 STL 里放多个相互重叠的闭合部件时，`vtkPolyDataToImageStencil` 按扫描线奇偶把重叠区
  算成异或——40mm 与 30mm 立方体偏移 15mm，正确并集 73000 mm³，实测 **60010 且报告"水密"无告警**；
  ② 两个部件共面贴合时 vtkSTLReader 会焊接重合点，产生非流形边，stencil 完全失效（以前直接拒绝）。
  实现：`io/MeshBoolean`（连通分量拆分 → 每壳体转 Manifold → 体积 > 0 求并集、体积 < 0（法向朝内=内腔）
  翻正后求差集 → 回写 vtkPolyData；`contshells()` 用于判断是否需要进布尔路径）、
  `VoxelizationOptions::booleanUnify`（默认自动：多壳体/不水密/非流形时才走）、
  `VoxelGrid` 新增 `usedBooleanMerge / mergedComponents / exactVolumeMm3`，
  MainWindow 在状态栏与体素摘要里汇报"布尔合并 N 个壳体 / 解析体积 X mm³"。
  实测：重叠并集 73000 ✓、共面贴合 128000 ✓、空心腔差集 56000 ✓、分离部件 128000 ✓（均与解析值一致）。
  集成时踩到：Manifold 的 `MANIFOLD_CSG` 在 3.5 已改名 `MANIFOLD_CROSS_SECTION`，
  关掉它才不会引入 Clipper2 子依赖；静态库必须加 `CMAKE_DEBUG_POSTFIX=d`，否则 Debug 安装会覆盖 Release。
- **Phase 3 第 7 步（已完成）**：**FDK 锥束重建**。
  `simulation/FdkReconstructor`（余弦加权 → 频域 ramp 滤波（自写基 2 FFT，补零到 2 倍长度
  避免环绕）→ FDK 反投影，按 z 切片多线程；`addProjection()` 流式累加，避免 360 帧
  512² 投影占 380MB 内存）、`ReconstructedVolume`（POD + `toImageData()`）、
  `export/VolumeExporter`（16 位 PNG 切片序列 + volume.json）、
  `SceneView::setReconstructedVolume / setSliceIndices`（浮点体的灰度窗三向切面）、
  MainWindow「仿真 → FDK 断层重建」（后台线程 + 进度 + 取消）、
  命令行 `--reconstruct / --reconstruct-size / --export-volume`、
  `tests/FdkReconstructionSanity.cpp`（20 项判据）。
  实测：球心 μ 0.019474 vs 真实 0.02（−2.6%）、球内 σ/均值 1.6%、
  μ 翻倍精确翻倍（0% 误差）、球外 0.1%、1.0mm 与 2.0mm 像素都贴近真实值。
- **Phase 3 第 8 步（已完成）**：**CUDA GPU 加速**。
  之前误判「本机无 GPU」是错的：**NVIDIA RTX 4060 Laptop（8 GB，CC 8.9）**，CUDA 11.8 已装。
  `simulation/CudaProjector.{h,cu}`（每像素一线程的索引空间 DDA，与 CPU 版逐行对应；
  体素掩膜每线程只上传一次；单一 kernel 同时支持 1~4 层材料累加）、
  `CudaProjectorStub.cpp`（未启用 CUDA 时的同签名空实现，上层无 #ifdef）、
  `CpuProjector` 在 `computePathLengths` / `computeMaterialPathLengths` 里优先走 GPU
  并自动回退、`setPreferredBackend()` 进程级开关、界面「仿真 → 使用 GPU 加速（CUDA）」
  与状态栏后端显示、命令行 `--gpu / --cpu`、`tests/GpuProjectionSanity.cpp`。
  实测：单帧 512² **CPU 58.9 ms → GPU 5.5 ms（11×）**，路径长度最大相对差 **0.0001%**；
  180 帧序列导出 28 s → 16 s（PNG 编码占 12 s，纯投影 16 s → 4 s）。
- **Phase 3 第 9 步（已完成）**：**界面重构**（用户要求"好看比功能更重要"、"参考 VS Code"）。
  1. `app/Theme.{h,cpp}`：统一深色主题——**密度对齐 VS Code**（正文 13px、次要 12px、
     菜单/工具栏 3~4px 内边距、控件 24px 上下、1px 分隔线），**配色用冷灰蓝**
     （`#1b1f24` 底 / `#22272e` 面板 / `#363d47` 边框 / `#d7dde5` 正文 / `#8b96a3` 次要 /
     `#54a8ff` 焦点）。`QFont` 用 9.75pt（= 13px@96dpi）以便高 DPI 缩放。
  2. `MainWindow`：工具栏复用菜单 QAction、`setDocumentMode(true)`、窗口尺寸按
     `availableGeometry` 收敛、状态栏改成"消息 + 徽标"、dock 尺寸显式 `resizeDocks`、
     `setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea)` 让参数面板占满整高。
  3. `ScanParameterPanel` 紧凑化：单位进控件 `suffix`、成对参数并排（SAD/SDD、列/行、
     起止角、kVp/铝滤过、材料/皮质骨、SPR/PSF、串扰/电子噪声）、长说明进 tooltip、
     底部一行「换算结果 + 恢复默认值」；19 行压到 12 行，一屏到底不用滚。
  4. `DRPreviewWidget`：新增自绘 **`FrameStrip` 胶片条**（继承 `QAbstractScrollArea`，
     只绘制可视帧，360~720 帧也不卡），序列整段铺开 + 底部横向滚动条 + 帧号/角度标注，
     点选帧同步三维几何；单帧仍用 `ImageCanvas` 读片屏画布。
  5. `SceneView`：背景改为与主题一致的中性深灰；`resetView()` 按模型包围盒（1.35×）
     取景，不被机架轨迹环拉远；空场景叠加提示（VTK 字体无中文，只能 ASCII）。
  6. 新增 `--screenshot <png>`：启动动作跑完后 `QWidget::grab()` 主窗口再退出。
- **Phase 3 第 10 步（已完成）**：**真实感机架可视化**（用户要求"像站在真设备旁边"）。
  新增 `visualization/ScannerRig.{h,cpp}`：环形外壳（torus = 圆折线 + `vtkTubeFilter`）
  \+ 分模线 + 外圈灯带 + 立柱 + 底座 + 转子支架 + 球管（`vtkCylinderSource`）+
  准直器锥（`vtkConeSource`）+ 焦点亮点 + 平板探测器（外壳 + 深色感光面 + 连接臂）+
  检查床 + 立柱 + 地面；尺寸全部由 SAD/SDD/探测器幅面推导，转子按当前扫描角度
  `RotateZ` 旋转，床面高度按模型最低点自动贴合。
  灯光用 4 盏平行光（主光/补光/轮廓光/正面补光），视角固定为"右前方 3/4、
  略高于等中心"，且用**绝对方向**设置相机（不再对当前相机累加 Azimuth/Elevation，
  否则连续重置会越转越偏）。三维视图变大（DR 预览默认高度降到 250）。
- **Phase 3 第 11 步（已完成）**：**三种机架类型**（医学 CBCT / 工业 CT / C 型臂）。
  `simulation/ScannerMode.{h,cpp}`：枚举 + 标识（`medical`/`industrial`/`carm`）+
  每种的推荐几何（含角度范围）、能谱、建议体素、"谁在转"（`scannerModeRotatesSample`）。
  界面：参数面板顶部「机架类型」下拉框，切换即套用该机架推荐参数（仍可微调）；
  命令行 `--mode`；参数预设新增 `scannerMode` 字段（旧预设缺字段时保持当前机架）。
  `ScannerRig` 按机架生成三套外观：
  ① 医学（y 轴朝上）：环形外壳 + 转子环形转台 + 两个安装滑块 + 检查床 + 半透明锥束，
     机架绕患者转；
  ② 工业（**z 轴朝上**）：花岗岩底座 + 两根带圆角面板/压盖/灯带的立柱，左柱装球管、
     右柱装平板，射线沿 x 水平穿过（把"沿 y 出束"的总成整体 `RotateZ(-90)` 复用），
     中间是**水平样品转台**（盘面在 x-y 平面、绕 z 自转：圆盘 + 三爪 + 中心卡盘 + 定位环），
     外加半透明锥束显示照射范围；
  ③ C 型臂（**z 轴朝上**）：C 形拱臂（x-z 平面圆弧，跨度正好 -SAD→+offset）+ 两端安装叉
     + 球管/平板 + 水平病床（沿 y 铺开）+ **床面上的水平转台**。
    整条弧臂挂在子装配 `arm` 上，用 `RotateY(-30°)` 给一个**初始倾斜姿态**（球管沉到床下、
     平板升到样品上方，即临床 AP 位）；倾斜只作用在展示姿态上，投影几何不受影响，
     改角度只动 `buildCArm()` 里的 `tiltDeg`。弧臂顶点由**落地立柱 + 托臂**托住（立在 +y 侧，
     不挡视线也不碰射线束）。
    锥束用共用 helper `addBeamCone()`（医学/工业/C 臂同一套，医学 0.20、C 臂 0.22、工业 0.30 透明度）。
    病床立柱放在**床尾远端（+y）悬臂支撑**——早前放在近侧时，从默认机位看就是"桌上多出一块长方形"
    挡在样品背后，样品转一圈都被遮住；转台盘面还会按样品 footprint 自动 `Scale(x,y)`，
    盘面始终比样品大一圈。
  工业 CT 与 C 型臂都是 `scannerModeRotatesSample()==true`：源与探测器不动，
  样品与转台按 -θ 绕 z 自转（与机架正转在投影上等价）。
  取景时这两个模式的 `viewUp` 取 +z，所以画面里"上"就是转台轴线方向。
- **下一步（Phase 4，需先与用户确认）**：DICOM 输入输出（需引入 DCMTK/GDCM）、
  定量分析 UI（ROI 统计 / HU 标定 / MTF / NPS）、GPU 体素化与 GPU 反投影。
- **本机环境基线**：Qt 6.8.3 msvc2022_64 在 `D:\Qt\6.8.3\msvc2022_64`；
  VTK 源码/安装在 `D:\deps\VTK-9.3.1`（`vtk-config.cmake` 位于 `install\lib\cmake\vtk-9.3`）；
  Manifold v3.5.4 源码/安装在 `D:\deps\manifold-v3.5.4`（`manifoldConfig.cmake` 位于
  `install\lib\cmake\manifold`，静态库 manifold.lib / manifoldd.lib）；
  **GPU：NVIDIA GeForce RTX 4060 Laptop，8 GB，CC 8.9；CUDA 11.8 在 `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.8`**；
  MSVC v143 = 14.44.35207（VS 2022 17.14）；CMake 用 pip 版（`%APPDATA%\Python\Python313\Scripts\cmake.exe`）。
- **已实测**：Release 与 Debug 均构建成功；`ctest` **11 个测试全绿**（Release 约 6 s / Debug 约 70 s）；
  360 帧序列导出 76 s（512²，2mm 体素，CPU）；序列预览滑块切换时源/探测器几何同步旋转；
  能谱模型 120 kVp 平均能量 55.1 keV、束硬化 μ_eff 0.0260→0.0207、Poisson 噪声统计符合预期；
  探测器模型全零参数与不带模型逐比特一致；`deploy.ps1` 产出的自包含 dist 在去掉 Qt/VTK/CUDA 的
  PATH 下仍能导出 DR 与重建（含 GPU 加速）；FDK 重建球心 μ 0.019474（真实 0.02）；
  **GPU 单帧 5.5 ms vs CPU 58.9 ms（11×），路径长度最大相对差 0.0001%**（均已实测确认）。
- **踩过的坑（勿重犯）**：
  1. `QCommandLineParser` 的 `-h/--help`、`-v/--version` 是内置短名，自定义选项复用它会导致
     整个长选项注册失败（报 `Unknown option`）。
  2. VTK 的 `GetNumberOfPolys/GetBounds` 非 const；`vtkImageStencil` 默认把 **stencil 外部**
     置为背景值（想要内外掩膜需先把画布填 1）。
  3. DDA 遍历时射线穿过体素棱/角会出现多轴 tNext 并列，必须同时前进这些轴，
     否则下一轮判定退化而提前结束（曾导致中心射线只算出一半长度）。
  4. `vtkPlaneSource` 三点重合/共线会报 `Bad plane coordinate system`；用位姿前必须
     `ConeBeamView::update()`。
  5. 本机（远程桌面/受限 OpenGL）下 VTK 的 CPU ray-cast 体渲染缺少
     `vtkRayCastImageDisplayHelper` 覆盖实现会崩溃，故体素预览采用切面方案。
  6. QLabel 若用默认 sizeHint 承载大 pixmap，会把同布局里的滑块/文字挤掉；
     图像标签要用 `QSizePolicy::Ignored`，并把帧号直接画进图像。
  7. 每个序列帧都往 UI 线程投递进度事件会拖慢吞吐；进度更新要做节流（本项目 100ms）。
  8. 参数面板的 `QDoubleSpinBox` 有取值范围：命令行覆盖角度步长时会被夹到上限（90°），
     写脚本时注意不要误以为步长被忽略。
  9. 能谱模型的 `spectrum.bins()[i].weight` 在 generate() 里已归一化（和为 1），
     谱加权透射直接 `Σ w·exp(-μL)` 即为 T，不要再除一次总权重。
  10. 噪声必须用「种子 + 像素序号」派生的确定性 RNG，否则结果随线程数变化、无法回归测试。
  11. 参数变多后右侧面板会超出窗口高度，容器必须放进 `QScrollArea`（否则参数被裁掉）。
  12. `vtkImageEuclideanDistance` 在本项目不可直接用：它要求输出为 double、`Initialize=true` 时
      把非零体素置为 `MaximumDistance`，实测得到 0/1 而非距离。改用自实现的
      Felzenszwalb 一维抛物线下包络（三次扫描），精确且不依赖额外 VTK 模块。
  13. `io` 不能依赖 `simulation` 库（会循环依赖）：材料枚举放在只有枚举的
      `simulation/MaterialTypes.h` 里，双方都只包含该头。
      `SimulationPreset` 需要同时用到 `ScanParameters` 与 Qt Core，因此单独放进
      `settings/` 库（依赖方向：settings → simulation）。
  14. 需要「整幅图像邻域信息」的处理（散射模糊、PSF、串扰）不能在逐像素的并行循环里做：
      `SpectralProjector` 先算期望值，循环结束后再整幅处理，否则只能拿到半个邻域。
  15. 探测器模型与 Poisson 噪声必须二选一，否则会重复加噪：有探测器参数时由它负责
      Poisson（`enableNoise` 只是开关），否则退回逐像素 Poisson。
  16. `windeployqt --compiler-runtime` 在普通 PowerShell 里失效（没有 `VCINSTALLDIR`），
      要自己从 `VC\Redist\MSVC\<ver>\x64\Microsoft.VC143.CRT` 拷 MSVC 运行库。
  17. Windows PowerShell 5 会把原生程序写到 stderr 的警告当成**终止性错误**（
      `$ErrorActionPreference=Stop` 时脚本直接中断）：`windeployqt` 段要临时改 Continue 并 `2>$null`。
  18. GUI 子系统程序用 `& .\app.exe` 调用时 PowerShell **不会等待**，`$LASTEXITCODE` 与
      随后的文件检查都不可信；批处理验证必须用 `Start-Process -Wait -PassThru`。
  19. 测试可执行若链接 Qt，ctest 单独运行时可能报 0xc0000135（找不到 Qt6Core.dll）；
      在 `tests/CMakeLists.txt` 用 `ENVIRONMENT_MODIFICATION` 给它注入
      `$<TARGET_FILE_DIR:Qt6::Core>`。
  20. `vtkOBBTree` 在 **FiltersGeneral**、`vtkFillHolesFilter` 在 **FiltersModeling**，
      `vtkCleanPolyData / vtkPolyDataNormals / vtkFeatureEdges` 在 **FiltersCore**。
      用到哪个就要在根 `find_package(VTK COMPONENTS ...)` 和对应库的 `target_link_libraries`
      里同时加上，否则报 LNK2019 找不到 `New()`。
  21. `vtkPolyDataToImageStencil` 对**不水密**网格会把开口「补上」，静默给出错误体积
      （40mm 方管被算成实心 67240 体素）。没有水密性就没有内外定义，
      「多方向射线奇偶 + 多数表决」也救不了（方管中心向 +x 只穿 1 个面 → 奇数 → 内部）。
      正确做法是先补洞再严格校验水密，仍不水密就拒绝。
  22. STL 顶点是 **float32**：坐标平移 1e8 mm 的 40mm 立方体，舍入步长已超过 1mm，
      体素化体积会少 35%。判据：`max|坐标| × 1.2e-7 > 2 × 体素` 就该拒绝。
  23. `vtkImageStencil` 路径统计的是「节点落在内部」的体素，比体素中心采样多算约半个体素
      （40mm 立方体 @1mm 得 67240 而非解析 64000，偏差 5%，属既有约定）。
  24. `vtkSTLReader` **默认会焊接重合点**（Merging）。两个共面贴合的部件因此会变成**单个**
      非流形连通体（不是两个壳体），所以「多壳体才需要布尔」的判断不成立——
      必须同时检查非流形边，且 `unifyComponents` 不能因为 `components==1` 就早退。
  25. Manifold 输入必须先 `MeshGL::Merge()`（未焊接的三角形汤一律返回 `NotManifold`）。
      法向朝内的闭合壳体会给出**负体积**，`+`（并集）不会自动挖空，必须翻正绕向后用 `-`（差集）。
  26. Manifold 3.5 的选项名是 `MANIFOLD_CROSS_SECTION`（不是旧的 `MANIFOLD_CSG`）；
      关掉它才不会引入 Clipper2。静态库要加 `CMAKE_DEBUG_POSTFIX=d`，否则 Debug 安装覆盖 Release。
      安装后要用 `install\lib\cmake\manifold`（build 树里的 config 没有版本号，`find_package(manifold 3.0)` 会失败）。
  27. **FDK 的绝对量纲由「ramp 滤波归一化 + 反投影常数」共同决定，两者都必须对**：
      频域 ramp 的增益就是 `|f|`（**不乘 du、也不除 du**），反投影权重用 `Δβ`。
      只测一种探测器像素尺寸发现不了错误——早期 ramp 乘了 du、反投影又多乘 1/2，
      在 2.0mm 像素下两个错误恰好抵消（所以单测全绿），换到 0.4mm 像素整体偏小 5 倍。
      现在 `fdk_sanity` 同时验证 1.0mm 与 2.0mm 两种采样。
  28. 重建视野必须夹在「探测器在等中心处的 FOV」以内：超出部分被截断，
      会产生严重杯状伪影（实测 115mm 视野 vs 102mm FOV，中心 μ 掉到真实值的 1/4）。
  29. `vtkLookupTable` 默认是彩虹色（`HueRange 0~0.67`），要灰度必须显式
      `SetHueRange(0,0) + SetSaturationRange(0,0) + SetValueRange(0,1)`。
  30. `vtkSmartPointer<T>` 的临时对象在语句结束时析构：`vtkImageData* p = f().toImageData();`
      会立刻悬垂（实测 access violation）。要存成局部 `vtkSmartPointer` 变量再用。
  31. 用 PowerShell 的 `Set-Content -Encoding UTF8` 改含中文的源文件会把 UTF-8 当 ANSI 读、
      二次编码成乱码（字符串字面量会被破坏）。改文件一律用编辑器/`[System.IO.File]::ReadAllText`
      + `WriteAllText(..., UTF8Encoding($false))`。
  32. **CUDA kernel 参数不能传 host 数组的指针**：`double x[3]` 作为参数会退化成 `double*`，
      device 读的是 host 内存 → illegal memory access。必须用 `double3/int3` 之类的 POD 按值传。
      而且这种错误会被"GPU 失败自动回退 CPU"掩盖成"CPU/GPU 结果完全一致"，看起来一切正常。
      所以测试必须显式断言 `lastCudaError()` 为空（或某成功标志），不能只看结果对不对。
  33. **nvcc 前端不接受 UTF-8 多字节字符串字面量**（`-Xcompiler /utf-8` 只作用于 host 编译器），
      会报 `missing closing quote`。`.cu` 文件必须纯 ASCII（注释也不能有中文）。
  34. CUDA 11.8 与 MSVC 14.44 有两道版本门：nvcc 自己的检查（`-allow-unsupported-compiler`）
      和 MSVC STL 的 `static_assert`（错误码 STL1002，`-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH`）。
  35. 本机 VS **没有** CUDA 的 MSBuild 集成（`BuildCustomizations` 里没有 `CUDA*.props`），
      所以 CMake 的 `enable_language(CUDA)` 一定报 `No CUDA toolset found`。
      改用 nvcc 直接编译 `.cu` → `.obj`（`file(GENERATE)` 生成 .cmd，里面先 call
      `vcvars64.bat` 准备好 MSVC 环境），再把 obj 加进静态库。
  36. VS 生成器**不展开 `add_custom_command(OUTPUT ...)` 里的 `$<CONFIG>`**（会生成
      `xxx_(CONFIG).obj` 这种废名字），要在配置期按 `CMAKE_CONFIGURATION_TYPES` 循环各生成一条命令，
      再用 `$<$<CONFIG:...>:...>` 选择对应 obj。另外 CMake list 展开成 `a;b`，
      传给 nvcc 会被当成一个参数（`Unknown option '-a;-b'`），必须用空格分隔的普通字符串。
  37. CUDA 上下文是线程局部的：GPU 求解器实例必须在创建它的线程里用（本项目用
      `thread_local` 缓存，后台序列线程各自持有一份，体素掩膜每线程上传一次）。
  38. **深色主题必须在 `QApplication` 建好后、任何控件创建前**应用（`applyTheme`），
      否则先创建的控件保留旧调色板，界面会"一半深一半浅"。
      `QVTKOpenGLNativeWidget` 不受 QSS 影响，3D 背景色要在 `vtkRenderer` 里单独设。
  39. **`QMainWindow` 右下角默认归底部 dock**，右侧参数面板就只剩半屏高、要滚很久。
      要 `setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea)`，参数面板才能占满整高。
  40. dock 的 `setMinimumWidth()` 只是下限，实际宽度仍受 `sizeHint` 影响；要让面板稳定
      站住宽度得再调 `resizeDocks({dock}, {w}, Qt::Horizontal)`。高 DPI 下窗口逻辑分辨率
      可能远小于 1920×1080（本机 125% 缩放 → 1536×864），初始 `resize()` 必须按
      `screen()->availableGeometry()` 收敛，不能写死像素。
  41. **VTK 的 `vtkTextActor` 内置字体不含中文字形**，中英混排会被整段吞掉（只剩 ASCII 部分）。
      3D 视口里的叠加文字要么纯 ASCII，要么给 `vtkTextProperty` 指定带中文的字体文件。
  42. 界面验收别用系统截图（受 DPI 缩放、窗口遮挡、多显示器影响，实测把正常布局误判成裁切）。
      程序内置 `--screenshot <png>`：启动动作跑完后 `QWidget::grab()` 整个主窗口再退出，
      结果与显示器/缩放无关，可直接逐张比对。
  43. 序列预览不要用"每帧一个子控件"的做法（360~720 帧时布局与重绘都明显卡）。
      用 `QAbstractScrollArea` 自绘、只画可视范围内的帧即可，也不需要缓存整段缩略图
      （512² × 720 ≈ 190 MB）。缩略图尺寸被封顶时要把整行垂直居中，否则上方留白像布局出错。
  44. 字号别用 `setPixelSize`（高 DPI 下会变成蚂蚁字）。用磅值：13px @96dpi = 9.75pt，
      Qt 会按屏幕 DPI 自动换算；QSS 里的 `font-size: 13px` 也是逻辑像素，两者一致。
  45. `vtkAssembly` **没有** `RemoveAllParts()`：要先把 `GetParts()` 里的项收集到
      vector，再逐个 `RemovePart()`（边遍历边删会崩）。
  46. 场景灯光别把 `SetPositional(true)` 和"把方向写成单位向量"混着用——
      位置 (1, 0.8, 0.9) 意味着光源离焦点 1 mm，整台设备会渲染成一团黑。
      要么用平行光（不设 positional），要么把位置按场景尺度放大。
  47. `vtkRenderer::ResetCamera()` 之后再 `camera->Azimuth()/Elevation()` 是**相对**操作：
      重置视角被连续调用两次就会越转越偏（实测第二次到了 -64°/32°）。
      要可重复的机位就把方向算成绝对值（`SetFocalPoint` + `SetPosition` + `SetViewUp`），
      最后再 `ResetCamera(bounds)` 求距离。
  48. 应用图标分两处接入，缺一不可：可执行文件图标走 Windows 资源（`app/app.rc` 里
      `ICON "appicon.ico"` + `enable_language(RC)`，rc.exe 按 `/I` 找图标，故给 `app.rc`
      设了 `INCLUDE_DIRECTORIES`）；窗口/任务栏图标走 Qt（`app/resources.qrc` 里的
      `:/icons/appicon.png` + `QApplication::setWindowIcon`）。.rc 只用 ASCII，避免 rc.exe
      的代码页问题。安装包的 `SetupIconFile` / `UninstallDisplayIcon` 引用同一枚 ico。
  49. 文档类文件（README / AI_CONTEXT / NOTICES）一律存成 **UTF-8 带 BOM**：中文 Windows
      下无 BOM 的 UTF-8 会被编辑器按 GBK 打开，整篇变乱码（表现为满屏"？"）。
      `create` 工具写出的文件没有 BOM，写完要 `[System.IO.File]::WriteAllText(..., UTF8Encoding($true))` 补一次。
  50. 用 Pillow 合成带中文的配图时**不要**把 Python 源码用 here-string 管道喂给 python
      （PowerShell 管道会按 ANSI 传输，中文字面量到手就变成 `?`）。正确做法：先用
      `WriteAllText` 落一个 UTF-8 的 `.py` 文件，再 `python file.py`。

### 构建顺序（本机）

```powershell
.\scripts\build-vtk.ps1 -Configs Release          # 只需一次
.\scripts\build-manifold.ps1                      # 只需一次（Release + Debug）
.\scripts\build.ps1 -VtkDir D:\deps\VTK-9.3.1\install -Configuration Release
.\scripts\deploy.ps1 -QtDir D:\Qt\6.8.3\msvc2022_64 -VtkDir D:\deps\VTK-9.3.1\install
```

### 测试与运行（本机）

```powershell
$env:PATH = "D:\deps\VTK-9.3.1\install\bin;$env:PATH"
& "$env:APPDATA\Python\Python313\Scripts\ctest.exe" --test-dir build -C Release --output-on-failure

# GPU 测试需要 cudart64_*.dll 可见
$env:PATH = "D:\deps\VTK-9.3.1\install\bin;$env:CUDA_PATH\bin;$env:PATH"
.\build\bin\Release\pcsim_gpu_sanity.exe

$env:PATH = "D:\Qt\6.8.3\msvc2022_64\bin;D:\deps\VTK-9.3.1\install\bin;$env:CUDA_PATH\bin;$env:PATH"
.\build\bin\Release\PrecisionCBCTSim.exe --open build\demo_sphere.stl --voxel-size 2 --spectral --dr
```

批处理式验证要等进程真正结束：

```powershell
$p = Start-Process .\build\bin\Release\PrecisionCBCTSim.exe -ArgumentList '--open','build\demo_sphere.stl','--voxel-size','2','--spectral','--spr','0.3','--export-dr','build\dr_x.png' -Wait -PassThru
"exit=$($p.ExitCode)"
```

