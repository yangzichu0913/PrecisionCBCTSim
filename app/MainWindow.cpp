#include "app/MainWindow.h"

#include "app/DRPreviewWidget.h"
#include "app/ScanParameterPanel.h"
#include "app/Theme.h"
#include "export/DrExporter.h"
#include "export/VolumeExporter.h"
#include "io/MeshQuality.h"
#include "io/VoxelGrid.h"
#include "simulation/ConeBeamView.h"
#include "simulation/CpuProjector.h"
#include "simulation/CudaProjector.h"
#include "simulation/DetectorModel.h"
#include "simulation/FdkReconstructor.h"
#include "simulation/ProjectionSequence.h"
#include "simulation/SpectralProjector.h"
#include "settings/SettingsPreset.h"
#include "visualization/SceneView.h"

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolBar>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <string>

#include <vtkImageData.h>

namespace precisioncbct::app {
namespace {

QString applicationVersion()
{
#ifdef PCSIM_VERSION
    return QString::fromLatin1(PCSIM_VERSION);
#else
    return QStringLiteral("dev");
#endif
}

QString describeMesh(const precisioncbct::io::MeshData& mesh){
    double size[3] = {0.0, 0.0, 0.0};
    mesh.boundsSize(size);
    QString text = QStringLiteral("模型：%1\n三角面片：%2\n包围盒：%3 × %4 × %5 mm")
                       .arg(QFileInfo(QString::fromStdString(mesh.sourcePath)).fileName())
                       .arg(mesh.triangleCount)
                       .arg(size[0], 0, 'f', 1)
                       .arg(size[1], 0, 'f', 1)
                       .arg(size[2], 0, 'f', 1);
    // 网格体检：水密性直接决定体素化能否可靠判定内外
    text += mesh.quality.isWatertight()
                ? QStringLiteral("\n拓扑：水密")
                : QStringLiteral("\n拓扑：不水密（%1）").arg(
                      QString::fromStdString(precisioncbct::io::describeQuality(mesh.quality)));
    return text;
}

/// 把界面上的能谱设置折算成「单一 μ 路径」用的探测器模型上下文
precisioncbct::simulation::DetectorApplication
detectorApplication(const precisioncbct::simulation::SpectralSettings& spectral)
{
    precisioncbct::simulation::DetectorApplication application;
    application.options = spectral.detector;
    application.incidentPhotonsPerPixel = spectral.incidentPhotonsPerPixel;
    application.quantumNoise = spectral.enableNoise;
    application.seed = spectral.seed;
    return application;
}

/// 探测器模型的简短文字描述（用于状态栏与信息面板）
QString describeDetector(const precisioncbct::simulation::DetectorModelOptions& detector)
{
    if (!detector.hasAnyEffect())
    {
        return QStringLiteral("未启用");
    }
    return QStringLiteral("SPR %1、PSF %2 px、串扰 %3、电子学噪声 %4 光子")
        .arg(detector.scatterToPrimaryRatio, 0, 'f', 2)
        .arg(detector.detectorBlurSigmaPixels, 0, 'f', 2)
        .arg(detector.crosstalkFraction, 0, 'f', 3)
        .arg(detector.electronicNoisePhotons, 0, 'f', 0);
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("PrecisionCBCTSim — CBCT 成像仿真"));

    // 初始尺寸按可用屏幕区域收敛：高 DPI（如 125% 缩放）下逻辑分辨率可能远小于 1920x1080，
    // 写死 1360x860 会把右侧参数面板和底部 DR 预览挤出可视区
    const QRect available = screen()->availableGeometry();
    resize(qBound(1024, int(available.width() * 0.94), 1480),
           qBound(660, int(available.height() * 0.94), 940));

    // 右下角归右侧 dock：参数面板能占满整个窗口高度，不必在窄条里滚动
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    buildCentralView();
    buildToolBar();
    buildMenus();
    buildParameterDock();
    buildDrDock();

    // 菜单栏与工具栏之间不要留灰条
    setDocumentMode(true);

    m_modelInfoLabel = new QLabel(QStringLiteral("未加载模型"), this);
    m_modelInfoLabel->setObjectName(QStringLiteral("Hint"));
    statusBar()->addPermanentWidget(m_modelInfoLabel);

    m_viewInfoLabel = new QLabel(QStringLiteral("三维视图就绪（空白场景）"), this);
    m_viewInfoLabel->setObjectName(QStringLiteral("Hint"));
    statusBar()->addPermanentWidget(m_viewInfoLabel);

    // 计算后端一眼可见：GPU 与 CPU 的耗时差 10 倍，用户需要知道跑在哪个后端上
    auto* backendLabel = new QLabel(computeBackendDescription(), this);
    backendLabel->setObjectName(QStringLiteral("Badge"));
    backendLabel->setToolTip(QStringLiteral("射线追踪使用的计算后端（仿真菜单里可切换）"));
    statusBar()->addPermanentWidget(backendLabel);
    statusBar()->setSizeGripEnabled(false);

    // 后台序列的进度与取消入口放在状态栏，避免额外弹窗打断操作
    m_progressBar = new QProgressBar(this);
    m_progressBar->setMinimumWidth(240);
    m_progressBar->setMaximumWidth(340);
    m_progressBar->setVisible(false);
    statusBar()->addPermanentWidget(m_progressBar);

    m_cancelButton = new QPushButton(QStringLiteral("取消"), this);
    m_cancelButton->setVisible(false);
    m_cancelButton->setFixedHeight(24);
    statusBar()->addPermanentWidget(m_cancelButton);
    connect(m_cancelButton, &QPushButton::clicked, this, &MainWindow::cancelSequence);

    connect(&m_sequenceWatcher, &QFutureWatcher<SequenceRun>::finished, this, [this]() {
        applySequenceResult(m_sequenceWatcher.result());
    });

    connect(&m_reconstructionWatcher, &QFutureWatcher<ReconstructionRun>::finished, this, [this]() {
        setBusy(false, QString());
        const ReconstructionRun run = m_reconstructionWatcher.result();

        if (!run.ok && !run.cancelled)
        {
            statusBar()->showMessage(
                run.error.isEmpty() ? QStringLiteral("断层重建失败")
                                    : QStringLiteral("断层重建失败：%1").arg(run.error),
                12000);
            if (m_quitWhenReconstructionDone)
            {
                m_quitWhenReconstructionDone = false;
                QCoreApplication::exit(11);
            }
            return;
        }
        if (run.cancelled)
        {
            statusBar()->showMessage(QStringLiteral("断层重建已取消"), 8000);
            return;
        }

        m_reconstruction = run.volume;
        const double windowMax = std::max(1e-9, run.volume.maxValue());
        // 注意：必须让智能指针活到 setReconstructedVolume 之后，否则临时对象析构后
        // SceneView 里存的就是野指针（实测会立刻 access violation）
        vtkSmartPointer<vtkImageData> image = run.volume.toImageData();
        if (image != nullptr)
        {
            // 显示重建结果时先撤掉源/探测器平面：它的尺度是 2×SDD，
            // 会把重建体压成一个小角，看不清切面
            m_view->clearProjectionGeometry();
            m_view->setReconstructedVolume(image, 0.0, windowMax);
            m_view->resetView();
        }

        const int size = run.volume.dimensions[0];
        const auto centre = run.volume.voxelCenterMm(size / 2, size / 2, size / 2);
        statusBar()->showMessage(
            QStringLiteral("FDK 重建完成：%1³ @ %2 mm · %3 帧 · %4 s · 中心 μ = %5 /mm%6")
                .arg(size)
                .arg(run.volume.spacingMm[0], 0, 'f', 2)
                .arg(run.projectionCount)
                .arg(run.elapsedMs / 1000.0, 0, 'f', 1)
                .arg(run.volume.at(size / 2, size / 2, size / 2), 0, 'f', 5)
                .arg(run.warning.isEmpty() ? QString()
                                           : QStringLiteral(" · ⚠ %1").arg(run.warning)),
            25000);
        m_viewInfoLabel->setText(
            QStringLiteral("重建体 %1³ @ %2 mm，中心 (%3, %4, %5) mm")
                .arg(size)
                .arg(run.volume.spacingMm[0], 0, 'f', 2)
                .arg(centre[0], 0, 'f', 1)
                .arg(centre[1], 0, 'f', 1)
                .arg(centre[2], 0, 'f', 1));

        if (m_quitWhenReconstructionDone)
        {
            m_quitWhenReconstructionDone = false;
            QCoreApplication::exit(0);
        }
    });

    connect(m_drPreview, &DRPreviewWidget::frameChanged, this, &MainWindow::onPreviewFrameChanged);
}

MainWindow::~MainWindow()
{
    // 后台任务持有 this 指针，必须等它结束再析构
    m_cancelRequested = true;
    m_sequenceWatcher.waitForFinished();
}

void MainWindow::buildCentralView()
{
    m_view = new precisioncbct::viz::SceneView(this);
    m_view->setObjectName(QStringLiteral("CentralHost"));
    setCentralWidget(m_view);
}

void MainWindow::buildToolBar()
{
    m_toolBar = new QToolBar(QStringLiteral("主工具栏"), this);
    m_toolBar->setObjectName(QStringLiteral("MainToolBar"));
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);
    m_toolBar->setIconSize(QSize(16, 16));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    // 图标用系统主题图标：不引入额外资源文件，同时保证深色主题下可见
    m_toolBar->setStyleSheet(QStringLiteral("QToolButton { font-weight: 500; }"));
    addToolBar(Qt::TopToolBarArea, m_toolBar);
}

void MainWindow::buildMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));

    QAction* openAction = fileMenu->addAction(QStringLiteral("打开模型(&O)..."));
    openAction->setShortcut(QKeySequence::Open);
    openAction->setStatusTip(QStringLiteral("读取 STL/OBJ 模型（单位假定为 mm）"));
    connect(openAction, &QAction::triggered, this, &MainWindow::openModelDialog);

    fileMenu->addSeparator();

    QAction* savePresetAction = fileMenu->addAction(QStringLiteral("保存参数预设(&S)..."));
    savePresetAction->setStatusTip(QStringLiteral("把当前几何/能谱/探测器参数写成 JSON 文件"));
    connect(savePresetAction, &QAction::triggered, this, &MainWindow::savePresetWithUi);

    QAction* loadPresetAction = fileMenu->addAction(QStringLiteral("加载参数预设(&L)..."));
    loadPresetAction->setStatusTip(QStringLiteral("从 JSON 文件恢复一整套仿真参数"));
    connect(loadPresetAction, &QAction::triggered, this, &MainWindow::loadPresetWithUi);

    fileMenu->addSeparator();

    QAction* quitAction = fileMenu->addAction(QStringLiteral("退出(&X)"));
    quitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    QMenu* simulationMenu = menuBar()->addMenu(QStringLiteral("仿真(&S)"));

    // 工具栏复用同一批 QAction：菜单与按钮永远同步，不需要维护两份状态
    m_toolBar->addAction(openAction);
    m_toolBar->addSeparator();

    QAction* voxelAction = simulationMenu->addAction(QStringLiteral("体素化预览(&V)..."));
    voxelAction->setStatusTip(QStringLiteral("把当前模型光栅化为规则体素网格"));
    connect(voxelAction, &QAction::triggered, this, &MainWindow::voxelizeWithDialog);

    QAction* clearVoxelAction = simulationMenu->addAction(QStringLiteral("清除体素预览(&C)"));
    connect(clearVoxelAction, &QAction::triggered, this, &MainWindow::clearVoxelPreview);

    simulationMenu->addSeparator();

    QAction* drAction = simulationMenu->addAction(QStringLiteral("生成 DR 图(&D)"));
    drAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    drAction->setStatusTip(QStringLiteral("按当前参数与角度生成单张 DR"));
    connect(drAction, &QAction::triggered, this, &MainWindow::generateDrWithUi);

    QAction* exportDrAction = simulationMenu->addAction(QStringLiteral("导出 DR 图(&E)..."));
    exportDrAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    exportDrAction->setStatusTip(QStringLiteral("把当前 DR 导出为 16 位灰度 PNG"));
    connect(exportDrAction, &QAction::triggered, this, &MainWindow::exportDrWithUi);

    simulationMenu->addSeparator();

    QAction* sequenceAction = simulationMenu->addAction(QStringLiteral("生成投影序列(&Q)"));
    sequenceAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    sequenceAction->setStatusTip(
        QStringLiteral("按起始角/终止角/步长生成多角度 DR 序列（后台线程，可取消）"));
    connect(sequenceAction, &QAction::triggered, this, &MainWindow::generateSequenceWithUi);

    QAction* exportSequenceAction = simulationMenu->addAction(QStringLiteral("导出投影序列(&X)..."));
    exportSequenceAction->setStatusTip(
        QStringLiteral("把多角度序列逐帧写为 16 位 PNG（流式写出，不占内存）"));
    connect(exportSequenceAction, &QAction::triggered, this, &MainWindow::exportSequenceWithUi);

    simulationMenu->addSeparator();

    QAction* reconstructAction = simulationMenu->addAction(QStringLiteral("FDK 断层重建(&R)"));
    reconstructAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    reconstructAction->setStatusTip(
        QStringLiteral("按当前角度范围采集投影并做锥束 FDK 重建（后台线程，可取消）"));
    connect(reconstructAction, &QAction::triggered, this, &MainWindow::reconstructWithUi);

    m_toolBar->addAction(voxelAction);
    m_toolBar->addAction(drAction);
    m_toolBar->addAction(sequenceAction);
    m_toolBar->addAction(reconstructAction);
    m_toolBar->addSeparator();
    m_toolBar->addAction(exportDrAction);
    m_toolBar->addAction(exportSequenceAction);

    simulationMenu->addSeparator();

    QAction* gpuAction = simulationMenu->addAction(QStringLiteral("使用 GPU 加速（CUDA）"));
    gpuAction->setCheckable(true);
    gpuAction->setChecked(precisioncbct::simulation::preferredBackend()
                          != precisioncbct::simulation::ComputeBackend::Cpu
                          && precisioncbct::simulation::cudaAvailable());
    gpuAction->setEnabled(precisioncbct::simulation::cudaAvailable()
                          || precisioncbct::simulation::preferredBackend()
                                 != precisioncbct::simulation::ComputeBackend::Cpu);
    gpuAction->setStatusTip(
        precisioncbct::simulation::cudaAvailable()
            ? QStringLiteral("切换射线追踪后端：GPU %1 / CPU 多线程")
                  .arg(QString::fromStdString(
                      precisioncbct::simulation::cudaDeviceDescription()))
            : QStringLiteral("本构建未启用 CUDA 或没有可用设备，只能用 CPU"));
    connect(gpuAction, &QAction::toggled, this, [this](bool checked) {
        if (!configureComputeBackend(checked) && checked)
        {
            // 切不过去就恢复勾选状态
            if (auto* action = qobject_cast<QAction*>(sender()))
            {
                QSignalBlocker blocker(action);
                action->setChecked(false);
            }
        }
        statusBar()->showMessage(
            QStringLiteral("计算后端：%1").arg(computeBackendDescription()), 8000);
    });

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));

    QAction* resetAction = viewMenu->addAction(QStringLiteral("重置相机(&R)"));
    resetAction->setShortcut(QKeySequence(QStringLiteral("F")));
    connect(resetAction, &QAction::triggered,
            m_view, &precisioncbct::viz::SceneView::resetView);

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));

    QAction* aboutAction = helpMenu->addAction(QStringLiteral("关于(&A)"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAboutDialog);
}

void MainWindow::buildParameterDock()
{
    m_parameters = new ScanParameterPanel(this);

    QDockWidget* dock = new QDockWidget(QStringLiteral("仿真参数"), this);
    dock->setObjectName(QStringLiteral("scanParameterDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    // 参数较多（几何/探测器/角度/能谱/探测器模型），小屏或高 DPI 下需要滚动才能看全
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(m_parameters);
    scroll->setMinimumWidth(300);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    dock->setWidget(scroll);
    // 没有最小宽度时 Qt 会把右侧 dock 压成一条细缝（面板内容会被切掉）
    dock->setMinimumWidth(320);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    addDockWidget(Qt::RightDockWidgetArea, dock);

    // 切换机架类型：三维外观 + 推荐参数一起变，并提示需要重算图像
    connect(m_parameters, &ScanParameterPanel::scannerModeChanged, this,
            [this](precisioncbct::simulation::ScannerMode mode) {
                m_view->setScannerMode(mode);
                m_drPreview->showPlaceholder(QStringLiteral("机架已切换，请重新生成 DR 图"));
                statusBar()->showMessage(
                    QStringLiteral("机架类型：%1 —— %2")
                        .arg(QString::fromUtf8(precisioncbct::simulation::scannerModeLabel(mode)))
                        .arg(QString::fromUtf8(
                            precisioncbct::simulation::scannerModeDescription(mode))),
                    8000);
            });

    // 显式给面板分配宽度：仅靠 sizeHint 时 Qt 可能把 dock 压到只剩标题栏
    resizeDocks({dock}, {336}, Qt::Horizontal);
}

void MainWindow::buildDrDock()
{
    m_drPreview = new DRPreviewWidget(this);
    m_drPreview->setMinimumHeight(300);

    m_drDock = new QDockWidget(QStringLiteral("DR 预览"), this);
    m_drDock->setObjectName(QStringLiteral("drPreviewDock"));
    m_drDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_drDock->setWidget(m_drPreview);
    addDockWidget(Qt::BottomDockWidgetArea, m_drDock);
    // 三维视图是"身临其境"的主画面，底部预览只占够用的高度
    resizeDocks({m_drDock}, {250}, Qt::Vertical);
}

bool MainWindow::openModelFile(const QString& path)
{
    std::string error;
    precisioncbct::io::MeshData mesh = precisioncbct::io::loadMesh(path.toStdString(), &error);
    if (!mesh.isValid())
    {
        statusBar()->showMessage(
            QStringLiteral("打开模型失败：%1").arg(QString::fromStdString(error)), 8000);
        return false;
    }

    m_mesh = mesh;

    // 换模型后之前的体素、投影、几何显示全部失效
    m_voxelGrid = precisioncbct::io::VoxelGrid{};
    m_voxelSpacingMm = 0.0;
    m_dr = precisioncbct::simulation::DrProjection{};
    m_sequenceFrames.clear();
    m_sequenceAngles.clear();
    m_drPreview->showPlaceholder(QStringLiteral("尚未生成 DR 图"));

    m_view->clearVoxelGrid();
    m_view->clearProjectionGeometry();
    m_view->clearScanTrajectory();
    m_geometryCameraFitted = false;
    m_view->setMesh(m_mesh.polyData);
    m_view->resetView();

    updateModelInfo();
    m_viewInfoLabel->setText(QStringLiteral("已显示模型，可用 F 重置相机"));

    const std::string qualityText = precisioncbct::io::describeQuality(m_mesh.quality);
    if (qualityText.empty())
    {
        statusBar()->showMessage(QStringLiteral("已加载 %1").arg(QFileInfo(path).fileName()), 5000);
    }
    else
    {
        // 网格体检结果提前告知：体素化时会被自动修复（补洞），用户需要知道
        statusBar()->showMessage(
            QStringLiteral("已加载 %1 —— %2体素化时会先做自动修复，请确认结果符合预期")
                .arg(QFileInfo(path).fileName())
                .arg(QString::fromStdString(qualityText)),
            20000);
    }
    return true;
}

bool MainWindow::voxelizeCurrentMesh(double spacingMm)
{
    if (!m_mesh.isValid())
    {
        statusBar()->showMessage(QStringLiteral("请先打开模型再体素化"), 6000);
        return false;
    }

    precisioncbct::io::VoxelizationOptions options;
    options.spacingMm = spacingMm;

    QElapsedTimer timer;
    timer.start();

    std::string error;
    precisioncbct::io::VoxelGrid grid =
        precisioncbct::io::voxelize(m_mesh.polyData, options, &error);
    const qint64 elapsedMs = timer.elapsed();

    if (!grid.isValid())
    {
        statusBar()->showMessage(
            QStringLiteral("体素化失败：%1").arg(QString::fromStdString(error)), 8000);
        return false;
    }

    m_voxelGrid = grid;
    m_voxelSpacingMm = spacingMm;

    // 体素变了，之前的 DR 与序列不再对应当前数据
    m_dr = precisioncbct::simulation::DrProjection{};
    m_sequenceFrames.clear();
    m_sequenceAngles.clear();
    m_drPreview->showPlaceholder(QStringLiteral("体素已更新，请重新生成 DR 图"));

    m_view->setVoxelGrid(m_voxelGrid.image);

    // 与解析体积的偏差是判断体素化是否合理的最直接指标
    double modelSize[3] = {0.0, 0.0, 0.0};
    m_mesh.boundsSize(modelSize);
    const double bboxVolume = modelSize[0] * modelSize[1] * modelSize[2];
    const double fillRatio = bboxVolume > 0.0 ? grid.insideVolumeMm3() / bboxVolume : 0.0;

    const QString voxelSummary =
        QStringLiteral("%1×%2×%3 @ %4 mm · 内部体素 %5（%6%）")
            .arg(grid.dimensions[0])
            .arg(grid.dimensions[1])
            .arg(grid.dimensions[2])
            .arg(spacingMm, 0, 'f', 2)
            .arg(grid.insideVoxels)
            .arg(fillRatio * 100.0, 0, 'f', 1);

    m_viewInfoLabel->setText(voxelSummary);
    m_viewInfoLabel->setToolTip(
        QStringLiteral("体素 %1×%2×%3（%4 mm），内部体素 %5 / %6，体积 %7 mm³（占包围盒 %8%）%9")
            .arg(grid.dimensions[0])
            .arg(grid.dimensions[1])
            .arg(grid.dimensions[2])
            .arg(spacingMm, 0, 'f', 3)
            .arg(grid.insideVoxels)
            .arg(grid.totalVoxels)
            .arg(grid.insideVolumeMm3(), 0, 'f', 1)
            .arg(fillRatio * 100.0, 0, 'f', 1)
            .arg(grid.exactVolumeMm3 > 0.0
                     ? QStringLiteral("，解析体积 %1 mm³").arg(grid.exactVolumeMm3, 0, 'f', 1)
                     : QString()));
    if (grid.usedRobustVoxelization || grid.exactVolumeMm3 > 0.0)
    {
        // 几何被自动整理过：必须说出来，否则用户会以为自己看到的是原始模型
        QString detail;
        if (grid.repairedBoundaryEdges > 0)
        {
            detail += QStringLiteral("补洞 %1 条边界边").arg(grid.repairedBoundaryEdges);
        }
        if (grid.usedBooleanMerge)
        {
            if (!detail.isEmpty())
            {
                detail += QStringLiteral("、");
            }
            detail += QStringLiteral("布尔合并 %1 个壳体").arg(grid.mergedComponents);
        }
        if (grid.exactVolumeMm3 > 0.0)
        {
            if (!detail.isEmpty())
            {
                detail += QStringLiteral("；");
            }
            detail += QStringLiteral("Manifold 解析体积 %1 mm³")
                          .arg(grid.exactVolumeMm3, 0, 'f', 1);
        }
        statusBar()->showMessage(
            QStringLiteral("体素化完成（耗时 %1 ms）：%2").arg(elapsedMs).arg(detail), 20000);
    }
    else
    {
        statusBar()->showMessage(QStringLiteral("体素化完成，耗时 %1 ms").arg(elapsedMs), 5000);
    }
    return true;
}

bool MainWindow::generateDrImage()
{
    if (!m_mesh.isValid())
    {
        statusBar()->showMessage(QStringLiteral("请先打开模型再生成 DR"), 6000);
        return false;
    }
    if (!m_voxelGrid.isValid() && !voxelizeCurrentMesh(1.0))
    {
        return false;
    }

    const precisioncbct::simulation::ScanParameters params = m_parameters->parameters();

    precisioncbct::simulation::ConeBeamView view;
    view.sourceAxisDistanceMm = params.sourceAxisDistanceMm;
    view.sourceDetectorDistanceMm = params.sourceDetectorDistanceMm;
    view.pixelSizeMm = params.detectorPixelSizeMm;
    view.columns = params.detectorColumns;
    view.rows = params.detectorRows;
    view.angleDeg = params.startAngleDeg;

    // 必须先 update() 才能拿到源/探测器的位姿，否则下面传给三维视图的是全 0
    if (!view.update())
    {
        statusBar()->showMessage(
            QStringLiteral("投影几何参数非法：需满足 0 < SAD < SDD，像素尺寸与行列数 > 0"), 8000);
        return false;
    }

    const precisioncbct::simulation::ProjectionParameters projectionParams;
    const precisioncbct::simulation::SpectralSettings spectral = m_parameters->spectralSettings();

    // 需要材料分层时先生成标签（失败则回退为单材料）
    const double shellMm = m_parameters->shellThicknessMm();
    if (spectral.enabled && shellMm > 0.0)
    {
        ensureMaterialLabeling(shellMm, spectral.material);
    }

    QElapsedTimer timer;
    timer.start();
    std::string error;
    double meanEnergyKeV = 0.0;
    if (spectral.enabled)
    {
        const precisioncbct::io::MaterialLabeling* labeling = nullptr;
        const precisioncbct::io::VoxelGrid& grid = projectionGrid(spectral, &labeling);

        precisioncbct::simulation::SpectralProjectionOptions options;
        options.spectrum.tubeVoltageKvp = spectral.tubeVoltageKvp;
        options.spectrum.inherentFiltrationAlMm = spectral.inherentFiltrationAlMm;
        options.material = spectral.material;
        options.labeling = labeling;
        options.incidentPhotonsPerPixel = spectral.incidentPhotonsPerPixel;
        options.enableNoise = spectral.enableNoise;
        options.seed = spectral.seed;
        options.detector = spectral.detector;
        const auto spectralResult =
            precisioncbct::simulation::projectConeBeamSpectral(grid, view, options, &error);
        m_dr = precisioncbct::simulation::toDrProjection(spectralResult);
        meanEnergyKeV = spectralResult.meanEnergyKeV;
    }
    else
    {
        m_dr = precisioncbct::simulation::projectConeBeam(m_voxelGrid, view, projectionParams, &error);
        // 探测器的散射/PSF/串扰与能量无关，单一 μ 路径同样可以叠加
        if (m_dr.isValid() && spectral.detector.hasAnyEffect())
        {
            precisioncbct::simulation::DetectorApplication detector = detectorApplication(spectral);
            if (!precisioncbct::simulation::applyDetectorModelToDr(
                    &m_dr, detector.options, detector.incidentPhotonsPerPixel,
                    detector.quantumNoise, detector.seed, &error))
            {
                statusBar()->showMessage(
                    QStringLiteral("生成 DR 失败：%1").arg(QString::fromStdString(error)), 8000);
                return false;
            }
        }
    }
    const qint64 elapsedMs = timer.elapsed();

    if (!m_dr.isValid())
    {
        statusBar()->showMessage(
            QStringLiteral("生成 DR 失败：%1").arg(QString::fromStdString(error)), 8000);
        return false;
    }

    // 把当前视角的源与探测器位姿、扫描轨迹画进三维场景（首次会适配相机）
    m_geometryCameraFitted = false;
    updateProjectionForAngle(view.angleDeg);

    QString physicsText;
    if (spectral.enabled)
    {
        physicsText = QStringLiteral("能谱 %1 kVp / %2 mm Al，平均能量 %3 keV，内核 %4，"
                                     "入射 %5 光子/像素，量子噪声%6%7")
                          .arg(spectral.tubeVoltageKvp, 0, 'f', 1)
                          .arg(spectral.inherentFiltrationAlMm, 0, 'f', 2)
                          .arg(meanEnergyKeV, 0, 'f', 1)
                          .arg(QString::fromLatin1(
                              precisioncbct::simulation::materialName(spectral.material)))
                          .arg(spectral.incidentPhotonsPerPixel, 0, 'f', 0)
                          .arg(spectral.enableNoise ? QStringLiteral("开") : QStringLiteral("关"))
                          .arg(m_labeling.isValid() && m_labelingShellMm > 0.0
                                   ? QStringLiteral("，皮质骨 %1 mm（外壳 %2 体素）")
                                         .arg(m_labelingShellMm, 0, 'f', 2)
                                         .arg(m_labeling.voxelCounts[1])
                                   : QString());
    }
    else
    {
        physicsText = QStringLiteral("单一 μ = %1 /mm（未启用能谱模型）")
                          .arg(projectionParams.attenuationPerMm, 0, 'f', 4);
    }
    if (spectral.detector.hasAnyEffect())
    {
        physicsText += QStringLiteral("\n探测器模型：%1")
                           .arg(describeDetector(spectral.detector));
    }

    const QString summary =
        QStringLiteral("角度 %1°，%2×%3 像素，像素 %4 mm\n%5\n"
                       "最大线积分 %6 mm，最小透射率 %7\n"
                       "生成耗时 %8 ms（CPU 多线程）")
            .arg(view.angleDeg, 0, 'f', 1)
            .arg(m_dr.width)
            .arg(m_dr.height)
            .arg(params.detectorPixelSizeMm, 0, 'f', 3)
            .arg(physicsText)
            .arg(m_dr.maxLineIntegralMm, 0, 'f', 1)
            .arg(m_dr.minTransmission, 0, 'f', 4)
            .arg(elapsedMs);

    m_sequenceFrames.clear();
    m_sequenceAngles.clear();
    m_drPreview->setDisplayImage(precisioncbct::exporter::makeDisplayImage(m_dr), summary);
    m_drDock->show();
    statusBar()->showMessage(QStringLiteral("已生成 DR 图，耗时 %1 ms").arg(elapsedMs), 5000);
    return true;
}

bool MainWindow::exportDrImage(const QString& path)
{
    if (!m_dr.isValid() && !generateDrImage())
    {
        return false;
    }

    QString error;
    if (!precisioncbct::exporter::writeDrPng16(m_dr, path, &error))
    {
        statusBar()->showMessage(QStringLiteral("导出失败：%1").arg(error), 8000);
        return false;
    }

    statusBar()->showMessage(QStringLiteral("已导出 16 位 PNG：%1").arg(path), 6000);
    return true;
}

void MainWindow::openModelDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("打开三维模型"),
        QString(),
        QStringLiteral("三维模型 (*.stl *.obj);;STL 模型 (*.stl);;OBJ 模型 (*.obj);;所有文件 (*)"));
    if (path.isEmpty())
    {
        return;
    }

    if (!openModelFile(path))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("打开模型失败"),
            QStringLiteral("无法读取该文件，请确认它是有效的 STL/OBJ 三角网格。\n\n"
                           "详细原因见状态栏。"));
    }
}

void MainWindow::voxelizeWithDialog()
{
    if (!m_mesh.isValid())
    {
        QMessageBox::information(this, QStringLiteral("尚未加载模型"),
                                 QStringLiteral("请先通过“文件 → 打开模型”载入 STL/OBJ。"));
        return;
    }

    bool accepted = false;
    const double spacing = QInputDialog::getDouble(
        this,
        QStringLiteral("体素化预览"),
        QStringLiteral("体素尺寸 (mm)："),
        1.0,   // 默认 1 mm
        0.05,
        50.0,
        3,
        &accepted);
    if (!accepted)
    {
        return;
    }

    if (!voxelizeCurrentMesh(spacing))
    {
        QMessageBox::warning(this, QStringLiteral("体素化失败"),
                             QStringLiteral("体素化未产生有效结果，详细原因见状态栏。"));
    }
}

void MainWindow::clearVoxelPreview()
{
    m_view->clearVoxelGrid();
    m_view->clearProjectionGeometry();
    m_view->clearScanTrajectory();
    m_voxelGrid = precisioncbct::io::VoxelGrid{};
    m_voxelSpacingMm = 0.0;
    m_dr = precisioncbct::simulation::DrProjection{};
    m_sequenceFrames.clear();
    m_sequenceAngles.clear();
    m_drPreview->showPlaceholder(QStringLiteral("尚未生成 DR 图"));
    m_viewInfoLabel->setText(QStringLiteral("已清除体素预览"));
}

void MainWindow::generateDrWithUi()
{
    if (!m_mesh.isValid())
    {
        QMessageBox::information(this, QStringLiteral("尚未加载模型"),
                                 QStringLiteral("请先通过“文件 → 打开模型”载入 STL/OBJ。"));
        return;
    }

    if (!generateDrImage())
    {
        QMessageBox::warning(this, QStringLiteral("生成 DR 失败"),
                             QStringLiteral("未生成有效投影，详细原因见状态栏。"));
    }
}

void MainWindow::exportDrWithUi()
{
    const QString suggested = QStringLiteral("dr_%1deg.png")
                                  .arg(m_parameters->parameters().startAngleDeg, 0, 'f', 0);
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出 DR 图（16 位 PNG）"),
        suggested,
        QStringLiteral("PNG 图像 (*.png)"));
    if (path.isEmpty())
    {
        return;
    }

    if (!exportDrImage(path))
    {
        QMessageBox::warning(this, QStringLiteral("导出失败"),
                             QStringLiteral("无法写出 PNG，详细原因见状态栏。"));
    }
}

void MainWindow::updateModelInfo()
{
    if (!m_mesh.isValid())
    {
        m_modelInfoLabel->setText(QStringLiteral("未加载模型"));
        m_modelInfoLabel->setToolTip(QString());
        return;
    }

    // 状态栏只放最要紧的一行（文件名 + 面片数），完整网格体检放在提示里，
    // 否则长文本会把状态栏右侧的徽标挤掉
    const QString detail = describeMesh(m_mesh);
    m_modelInfoLabel->setText(QStringLiteral("%1 · %2 面片")
                                  .arg(QFileInfo(QString::fromStdString(m_mesh.sourcePath)).fileName())
                                  .arg(m_mesh.triangleCount));
    m_modelInfoLabel->setToolTip(detail);
}

void MainWindow::updateProjectionForAngle(double angleDeg)
{
    if (!m_mesh.isValid())
    {
        return;
    }

    const precisioncbct::simulation::ScanParameters params = m_parameters->parameters();

    precisioncbct::simulation::ConeBeamView view;
    view.sourceAxisDistanceMm = params.sourceAxisDistanceMm;
    view.sourceDetectorDistanceMm = params.sourceDetectorDistanceMm;
    view.pixelSizeMm = params.detectorPixelSizeMm;
    view.columns = params.detectorColumns;
    view.rows = params.detectorRows;
    view.angleDeg = angleDeg;
    if (!view.update())
    {
        return;
    }

    m_view->setProjectionGeometry(view.sourcePositionMm, view.detectorCenterMm,
                                  view.detectorUAxis, view.detectorVAxis,
                                  params.detectorWidthMm(), params.detectorHeightMm());
    m_view->setScanTrajectory(params.sourceAxisDistanceMm,
                              params.sourceDetectorDistanceMm - params.sourceAxisDistanceMm,
                              params.startAngleDeg, params.endAngleDeg);

    // 只在几何首次出现时把相机拉到「模型 + 源 + 探测器」的整体范围；
    // 之后切帧不再改动相机，避免拖动滑块时视角跳动。
    if (!m_geometryCameraFitted)
    {
        m_geometryCameraFitted = true;
        m_view->resetView();
    }
}

void MainWindow::setBusy(bool busy, const QString& message)
{
    m_busy = busy;
    m_progressBar->setVisible(busy);
    m_cancelButton->setVisible(busy);
    if (busy)
    {
        m_progressBar->setRange(0, 0); // 未知总数时先显示忙碌动画
        if (!message.isEmpty())
        {
            statusBar()->showMessage(message);
        }
        setCursor(Qt::BusyCursor);
    }
    else
    {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
        setCursor(Qt::ArrowCursor);
    }
}

bool MainWindow::startSequence(const QString& exportDir, bool keepFrames, bool quitWhenDone)
{
    if (m_busy)
    {
        statusBar()->showMessage(QStringLiteral("已有后台任务在运行，请等待或先取消"), 6000);
        return false;
    }
    if (!m_mesh.isValid())
    {
        statusBar()->showMessage(QStringLiteral("请先打开模型再生成投影序列"), 6000);
        return false;
    }
    if (!m_voxelGrid.isValid() && !voxelizeCurrentMesh(1.0))
    {
        return false;
    }
    if (!exportDir.isEmpty())
    {
        QDir dir(exportDir);
        if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        {
            statusBar()->showMessage(
                QStringLiteral("无法创建导出目录：%1").arg(exportDir), 8000);
            return false;
        }
    }

    const precisioncbct::simulation::ScanParameters scan = m_parameters->parameters();
    const precisioncbct::simulation::SpectralSettings spectral = m_parameters->spectralSettings();

    // 材料分层必须在 UI 线程先建好（后台线程只读），失败则回退单材料
    if (spectral.enabled)
    {
        const double shellMm = m_parameters->shellThicknessMm();
        if (shellMm > 0.0)
        {
            ensureMaterialLabeling(shellMm, spectral.material);
        }
        else
        {
            m_labeling = precisioncbct::io::MaterialLabeling{};
            m_labelingShellMm = -1.0;
        }
    }

    m_cancelRequested = false;
    m_quitWhenSequenceDone = quitWhenDone;
    setBusy(true, QStringLiteral("正在生成投影序列…"));

    m_view->setScanTrajectory(scan.sourceAxisDistanceMm,
                              scan.sourceDetectorDistanceMm - scan.sourceAxisDistanceMm,
                              scan.startAngleDeg, scan.endAngleDeg);

    // 后台线程只读体素网格与参数；busy 期间界面不再改动它们
    m_sequenceWatcher.setFuture(QtConcurrent::run([this, scan, spectral, exportDir, keepFrames]() {
        return runSequenceOnWorker(scan, spectral, exportDir, keepFrames);
    }));
    return true;
}

MainWindow::SequenceRun MainWindow::runSequenceOnWorker(
    const precisioncbct::simulation::ScanParameters& scan,
    const precisioncbct::simulation::SpectralSettings& spectral,
    const QString& exportDir, bool keepFrames)
{
    SequenceRun run;
    run.exportDir = exportDir;

    const precisioncbct::simulation::ProjectionParameters projectionParams;

    auto lastProgress = std::chrono::steady_clock::now();
    const auto visitor = [&](int index, int count, double angleDeg,
                             const precisioncbct::simulation::DrProjection& projection) {
        if (m_cancelRequested.load(std::memory_order_relaxed))
        {
            return false;
        }

        if (keepFrames)
        {
            run.frames.push_back(precisioncbct::exporter::makeDisplayImage(projection));
            run.anglesDeg.push_back(angleDeg);
        }

        if (!exportDir.isEmpty())
        {
            const QString path = QStringLiteral("%1/dr_%2.png")
                                     .arg(exportDir)
                                     .arg(index, 4, 10, QLatin1Char('0'));
            QString writeError;
            if (!precisioncbct::exporter::writeDrPng16(projection, path, &writeError))
            {
                run.error = writeError;
                return false;
            }
        }

        // 进度回调节流：每帧都投递会淹没 UI 事件队列（大序列下明显拖慢吞吐）
        const auto now = std::chrono::steady_clock::now();
        const bool isLastFrame = (index + 1 == count);
        if (isLastFrame
            || std::chrono::duration<double, std::milli>(now - lastProgress).count() >= 100.0)
        {
            lastProgress = now;
            QMetaObject::invokeMethod(
                this,
                [this, index, count, angleDeg]() {
                    m_progressBar->setRange(0, count);
                    m_progressBar->setValue(index + 1);
                    m_progressBar->setFormat(QStringLiteral("%1/%2 帧 · %3°")
                                                 .arg(index + 1)
                                                 .arg(count)
                                                 .arg(angleDeg, 0, 'f', 1));
                },
                Qt::QueuedConnection);
        }
        return true;
    };

    std::string error;
    precisioncbct::simulation::SpectralProjectionOptions spectralOptions;
    precisioncbct::simulation::DetectorApplication detector = detectorApplication(spectral);
    const bool useSpectral = spectral.enabled;
    const precisioncbct::io::MaterialLabeling* labeling = nullptr;
    const precisioncbct::io::VoxelGrid* grid = &m_voxelGrid;
    if (useSpectral)
    {
        spectralOptions.spectrum.tubeVoltageKvp = spectral.tubeVoltageKvp;
        spectralOptions.spectrum.inherentFiltrationAlMm = spectral.inherentFiltrationAlMm;
        spectralOptions.material = spectral.material;
        spectralOptions.incidentPhotonsPerPixel = spectral.incidentPhotonsPerPixel;
        spectralOptions.enableNoise = spectral.enableNoise;
        spectralOptions.seed = spectral.seed;
        spectralOptions.detector = spectral.detector;
        grid = &projectionGrid(spectral, &labeling);
        spectralOptions.labeling = labeling;
    }

    const precisioncbct::simulation::SequenceResult result =
        precisioncbct::simulation::runSequence(*grid, scan, projectionParams, visitor, &error,
                                              useSpectral ? &spectralOptions : nullptr,
                                              useSpectral ? nullptr : &detector);

    run.frameCount = result.frameCount;
    run.completedFrames = result.completedFrames;
    run.width = result.width;
    run.height = result.height;
    run.maxLineIntegralMm = result.maxLineIntegralMm;
    run.elapsedMs = result.elapsedMs;
    run.cancelled = result.cancelled;
    run.ok = result.ok;
    if (!run.ok && run.error.isEmpty())
    {
        run.error = QString::fromStdString(error);
    }
    return run;
}

void MainWindow::applySequenceResult(const SequenceRun& run)
{
    setBusy(false, QString());

    if (!run.ok && !run.cancelled)
    {
        const QString message = run.error.isEmpty() ? QStringLiteral("生成投影序列失败")
                                                    : QStringLiteral("生成投影序列失败：%1").arg(run.error);
        statusBar()->showMessage(message, 10000);
        if (m_quitWhenSequenceDone)
        {
            m_quitWhenSequenceDone = false;
            QCoreApplication::exit(6);
        }
        return;
    }

    const double seconds = run.elapsedMs / 1000.0;
    const double firstAngle = run.anglesDeg.isEmpty() ? 0.0 : run.anglesDeg.front();
    const double lastAngle = run.anglesDeg.isEmpty() ? 0.0 : run.anglesDeg.back();
    const QString summary =
        QStringLiteral("角度 %1° → %2°，共 %3 帧，%4×%5，μ = 0.0200 /mm\n"
                       "最大线积分 %6 mm，总耗时 %7 s（CPU 多线程）%8")
            .arg(firstAngle, 0, 'f', 1)
            .arg(lastAngle, 0, 'f', 1)
            .arg(run.completedFrames)
            .arg(run.width)
            .arg(run.height)
            .arg(run.maxLineIntegralMm, 0, 'f', 1)
            .arg(seconds, 0, 'f', 2)
            .arg(run.cancelled ? QStringLiteral("（已取消）") : QString());
    m_sequenceFrames = run.frames;
    m_sequenceAngles = run.anglesDeg;
    m_dr = precisioncbct::simulation::DrProjection{}; // 单帧缓存与序列不再对应

    if (!run.frames.isEmpty())
    {
        m_drPreview->setFrames(run.frames, run.anglesDeg, summary);
        m_drDock->show();
        if (!run.anglesDeg.isEmpty())
        {
            updateProjectionForAngle(run.anglesDeg.front());
        }
    }
    else
    {
        const QString text = run.exportDir.isEmpty()
                                 ? QStringLiteral("序列已生成，但未保留帧（仅导出模式）")
                                 : QStringLiteral("已导出 %1 帧到 %2").arg(run.completedFrames).arg(run.exportDir);
        m_drPreview->showPlaceholder(text);
    }

    statusBar()->showMessage(
        QStringLiteral("%1%2 帧，耗时 %3 s")
            .arg(run.cancelled ? QStringLiteral("已取消：完成 ") : QStringLiteral("投影序列完成，"))
            .arg(run.completedFrames)
            .arg(seconds, 0, 'f', 2),
        8000);

    if (m_quitWhenSequenceDone)
    {
        m_quitWhenSequenceDone = false;
        QCoreApplication::exit(0);
    }
}

void MainWindow::cancelSequence()
{
    if (!m_busy)
    {
        return;
    }
    m_cancelRequested = true;
    statusBar()->showMessage(QStringLiteral("正在取消…"), 3000);
}

bool MainWindow::setAngleStepOverride(double stepDeg)
{
    if (!(stepDeg > 0.0))
    {
        statusBar()->showMessage(QStringLiteral("角度步长必须大于 0"), 8000);
        return false;
    }
    m_parameters->setAngleStepDeg(stepDeg);
    return true;
}

void MainWindow::setScannerMode(simulation::ScannerMode mode)
{
    m_parameters->setScannerMode(mode);
    m_view->setScannerMode(mode);
}

bool MainWindow::showSequenceFrame(int index)
{
    if (m_sequenceFrames.isEmpty() || index < 0 || index >= m_sequenceFrames.size())
    {
        statusBar()->showMessage(QStringLiteral("指定帧超出范围（当前序列 %1 帧）")
                                     .arg(m_sequenceFrames.size()),
                                 8000);
        return false;
    }
    m_drPreview->setCurrentFrame(index);
    return true;
}

bool MainWindow::configureSpectral(bool enabled, double kvp, double photons, int noise)
{
    m_parameters->setSpectralEnabled(enabled);
    m_parameters->setSpectralDetail(kvp, photons, noise);
    return true;
}

void MainWindow::configureShellThickness(double thicknessMm)
{
    m_parameters->setShellThicknessMm(thicknessMm);
}

void MainWindow::configureDetector(double scatterRatio, double psfSigmaPixels,
                                   double crosstalkFraction, double electronicNoisePhotons)
{
    m_parameters->setDetectorDetail(scatterRatio, psfSigmaPixels, crosstalkFraction,
                                    electronicNoisePhotons);
}

bool MainWindow::configureComputeBackend(bool useGpu)
{
    if (!useGpu)
    {
        precisioncbct::simulation::setPreferredBackend(
            precisioncbct::simulation::ComputeBackend::Cpu);
        return true;
    }
    if (!precisioncbct::simulation::cudaAvailable())
    {
        statusBar()->showMessage(
            QStringLiteral("GPU 不可用：%1")
                .arg(QString::fromStdString(precisioncbct::simulation::lastCudaError())),
            10000);
        return false;
    }
    precisioncbct::simulation::setPreferredBackend(
        precisioncbct::simulation::ComputeBackend::Gpu);
    return true;
}

QString MainWindow::computeBackendDescription() const
{
    using precisioncbct::simulation::cudaAvailable;
    using precisioncbct::simulation::cudaDeviceDescription;
    using precisioncbct::simulation::preferredBackend;
    using precisioncbct::simulation::ComputeBackend;

    if (preferredBackend() == ComputeBackend::Cpu)
    {
        return QStringLiteral("CPU（多线程）");
    }
    if (cudaAvailable())
    {
        return QStringLiteral("GPU %1").arg(QString::fromStdString(cudaDeviceDescription()));
    }
    return QStringLiteral("CPU（多线程，未检测到可用 GPU）");
}

bool MainWindow::setReconstructionSize(int volumeSize)
{
    if (volumeSize < 8 || volumeSize > 512)
    {
        return false;
    }
    m_reconstructionSize = volumeSize;
    return true;
}

bool MainWindow::startReconstruction(const QString& exportDir, bool quitWhenDone)
{
    if (m_busy)
    {
        statusBar()->showMessage(QStringLiteral("有任务正在运行，请先取消"), 6000);
        return false;
    }
    if (!m_mesh.isValid())
    {
        statusBar()->showMessage(QStringLiteral("请先打开模型再重建"), 6000);
        return false;
    }
    if (!m_voxelGrid.isValid() && !voxelizeCurrentMesh(1.0))
    {
        return false;
    }

    if (!exportDir.isEmpty())
    {
        QDir dir(exportDir);
        if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        {
            statusBar()->showMessage(
                QStringLiteral("无法创建导出目录：%1").arg(exportDir), 8000);
            return false;
        }
    }

    const precisioncbct::simulation::ScanParameters scan = m_parameters->parameters();
    const precisioncbct::simulation::SpectralSettings spectral = m_parameters->spectralSettings();
    if (scan.projectionCount() < 2)
    {
        statusBar()->showMessage(
            QStringLiteral("投影帧数不足（至少 2 帧），请检查角度范围与步长"), 8000);
        return false;
    }

    // 材料分层必须在 UI 线程先建好（后台线程只读）
    if (spectral.enabled)
    {
        const double shellMm = m_parameters->shellThicknessMm();
        if (shellMm > 0.0)
        {
            ensureMaterialLabeling(shellMm, spectral.material);
        }
        else
        {
            m_labeling = precisioncbct::io::MaterialLabeling{};
            m_labelingShellMm = -1.0;
        }
    }

    m_cancelRequested = false;
    m_quitWhenReconstructionDone = quitWhenDone;
    setBusy(true, QStringLiteral("正在采集投影并重建…"));
    m_progressBar->setRange(0, scan.projectionCount());
    m_progressBar->setValue(0);

    m_reconstructionWatcher.setFuture(
        QtConcurrent::run([this, scan, spectral, exportDir]() {
            return runReconstructionOnWorker(scan, spectral, exportDir);
        }));
    return true;
}

MainWindow::ReconstructionRun MainWindow::runReconstructionOnWorker(
    const precisioncbct::simulation::ScanParameters& scan,
    const precisioncbct::simulation::SpectralSettings& spectral,
    const QString& exportDir)
{
    ReconstructionRun run;
    run.exportDir = exportDir;

    const precisioncbct::simulation::ProjectionParameters projectionParams;

    // 重建几何必须与投影几何完全一致，否则会出现系统性偏差
    precisioncbct::simulation::ConeBeamView geometry;
    geometry.sourceAxisDistanceMm = scan.sourceAxisDistanceMm;
    geometry.sourceDetectorDistanceMm = scan.sourceDetectorDistanceMm;
    geometry.pixelSizeMm = scan.detectorPixelSizeMm;
    geometry.columns = scan.detectorColumns;
    geometry.rows = scan.detectorRows;
    if (!geometry.update())
    {
        run.error = QStringLiteral("投影几何非法，无法重建");
        return run;
    }

    precisioncbct::simulation::FdkOptions options;
    options.volumeSize = m_reconstructionSize;
    // 重建视野：取「模型尺寸」与「探测器在等中心处的视野」中较小者。
    // 超出探测器视野的部分会被截断，导致严重的杯状伪影（实测 115mm 视野 vs 102mm FOV
    // 时中心 μ 掉到真实值的 1/4），所以必须夹住并把超视野这件事报出来。
    double modelExtentMm = 0.0;
    for (int axis = 0; axis < 3; ++axis)
    {
        modelExtentMm = std::max(modelExtentMm,
                                 static_cast<double>(m_voxelGrid.dimensions[axis])
                                     * m_voxelGrid.spacingMm[axis]);
    }
    const double fovMm = std::min(scan.detectorWidthMm(), scan.detectorHeightMm())
                         * scan.sourceAxisDistanceMm / scan.sourceDetectorDistanceMm;
    const double effectiveExtentMm =
        (modelExtentMm > 0.0) ? std::min(1.1 * modelExtentMm, fovMm) : fovMm;
    options.voxelSizeMm = effectiveExtentMm / static_cast<double>(m_reconstructionSize);

    if (modelExtentMm > fovMm)
    {
        run.warning = QStringLiteral("模型尺寸 %1 mm 超过探测器视野 %2 mm（SAD/SDD = %3/%4），"
                                     "重建边缘会有截断伪影：可增大 SDD、减小 SAD 或缩小模型")
                          .arg(modelExtentMm, 0, 'f', 1)
                          .arg(fovMm, 0, 'f', 1)
                          .arg(scan.sourceAxisDistanceMm, 0, 'f', 0)
                          .arg(scan.sourceDetectorDistanceMm, 0, 'f', 0);
    }

    precisioncbct::simulation::FdkReconstructor reconstructor(options, geometry);
    if (!reconstructor.isValid())
    {
        run.error = QString::fromStdString(reconstructor.error());
        return run;
    }

    std::string error;
    precisioncbct::simulation::SpectralProjectionOptions spectralOptions;
    const bool useSpectral = spectral.enabled;
    const precisioncbct::io::MaterialLabeling* labeling = nullptr;
    const precisioncbct::io::VoxelGrid* grid = &m_voxelGrid;
    if (useSpectral)
    {
        spectralOptions.spectrum.tubeVoltageKvp = spectral.tubeVoltageKvp;
        spectralOptions.spectrum.inherentFiltrationAlMm = spectral.inherentFiltrationAlMm;
        spectralOptions.material = spectral.material;
        spectralOptions.incidentPhotonsPerPixel = spectral.incidentPhotonsPerPixel;
        spectralOptions.enableNoise = spectral.enableNoise;
        spectralOptions.seed = spectral.seed;
        spectralOptions.detector = spectral.detector;
        grid = &projectionGrid(spectral, &labeling);
        spectralOptions.labeling = labeling;
    }
    precisioncbct::simulation::DetectorApplication detector = detectorApplication(spectral);

    auto lastProgress = std::chrono::steady_clock::now();
    const auto visitor = [&](int index, int count, double angleDeg,
                             const precisioncbct::simulation::DrProjection& projection) {
        if (m_cancelRequested.load(std::memory_order_relaxed))
        {
            return false;
        }
        reconstructor.addProjection(angleDeg, projection);
        ++run.projectionCount;

        const auto now = std::chrono::steady_clock::now();
        if (index + 1 == count
            || std::chrono::duration<double, std::milli>(now - lastProgress).count() >= 150.0)
        {
            lastProgress = now;
            QMetaObject::invokeMethod(
                this,
                [this, index, count, angleDeg]() {
                    m_progressBar->setRange(0, count);
                    m_progressBar->setValue(index + 1);
                    m_progressBar->setFormat(QStringLiteral("采集 %1/%2 帧 · %3°")
                                                 .arg(index + 1)
                                                 .arg(count)
                                                 .arg(angleDeg, 0, 'f', 1));
                },
                Qt::QueuedConnection);
        }
        return true;
    };

    const precisioncbct::simulation::SequenceResult sequence =
        precisioncbct::simulation::runSequence(*grid, scan, projectionParams, visitor, &error,
                                              useSpectral ? &spectralOptions : nullptr,
                                              useSpectral ? nullptr : &detector);
    run.elapsedMs = sequence.elapsedMs;
    run.cancelled = sequence.cancelled;
    if (!sequence.ok && !sequence.cancelled)
    {
        run.error = error.empty() ? QStringLiteral("投影序列生成失败")
                                  : QString::fromStdString(error);
        return run;
    }
    if (sequence.cancelled)
    {
        return run;
    }

    run.volume = reconstructor.volume();
    if (!run.volume.isValid())
    {
        run.error = QStringLiteral("重建结果为空（投影帧数不足？）");
        return run;
    }

    if (!exportDir.isEmpty())
    {
        std::string exportError;
        if (!precisioncbct::exporter::writeVolumeSlices(run.volume, exportDir.toStdString(),
                                                       &exportError))
        {
            run.error = QString::fromStdString(exportError);
            return run;
        }
    }

    run.ok = true;
    return run;
}

void MainWindow::reconstructWithUi()
{
    if (m_busy)
    {
        statusBar()->showMessage(QStringLiteral("有任务正在运行，请先取消"), 6000);
        return;
    }
    startReconstruction(QString(), false);
}

bool MainWindow::saveSettingsPreset(const QString& path)
{
    precisioncbct::settings::SimulationPreset preset;
    preset.scan = m_parameters->parameters();
    preset.spectral = m_parameters->spectralSettings();
    preset.shellThicknessMm = m_parameters->shellThicknessMm();
    preset.voxelSizeMm = m_voxelSpacingMm > 0.0 ? m_voxelSpacingMm : 1.0;
    preset.scannerMode = precisioncbct::simulation::scannerModeId(m_parameters->scannerMode());

    std::string error;
    if (!precisioncbct::settings::saveToFile(path.toStdString(), preset, &error))
    {
        statusBar()->showMessage(
            QStringLiteral("保存参数预设失败：%1").arg(QString::fromStdString(error)), 8000);
        return false;
    }
    statusBar()->showMessage(QStringLiteral("已保存参数预设：%1").arg(path), 5000);
    return true;
}

bool MainWindow::loadSettingsPreset(const QString& path)
{
    precisioncbct::settings::SimulationPreset preset;
    preset.scan = m_parameters->parameters();
    preset.spectral = m_parameters->spectralSettings();
    preset.shellThicknessMm = m_parameters->shellThicknessMm();

    std::string error;
    if (!precisioncbct::settings::loadFromFile(path.toStdString(), &preset, &error))
    {
        statusBar()->showMessage(
            QStringLiteral("加载参数预设失败：%1").arg(QString::fromStdString(error)), 8000);
        return false;
    }

    m_parameters->applySettings(preset.scan, preset.spectral, preset.shellThicknessMm);

    // 预设里带了机架类型时一并切过去（老预设没有这个字段则保持当前机架）
    precisioncbct::simulation::ScannerMode mode = m_parameters->scannerMode();
    if (precisioncbct::simulation::parseScannerMode(preset.scannerMode.c_str(), &mode))
    {
        setScannerMode(mode);
    }

    // 参数变了，材料标签与已有 DR 都失效
    m_labeling = precisioncbct::io::MaterialLabeling{};
    m_labelingShellMm = -1.0;
    statusBar()->showMessage(QStringLiteral("已加载参数预设：%1").arg(path), 5000);
    return true;
}

void MainWindow::savePresetWithUi()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存参数预设"),
        QStringLiteral("preset.json"), QStringLiteral("JSON 预设 (*.json)"));
    if (!path.isEmpty())
    {
        saveSettingsPreset(path);
    }
}

void MainWindow::loadPresetWithUi()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("加载参数预设"),
        QString(), QStringLiteral("JSON 预设 (*.json)"));
    if (!path.isEmpty())
    {
        loadSettingsPreset(path);
    }
}

bool MainWindow::ensureMaterialLabeling(double shellThicknessMm,
                                        precisioncbct::simulation::Material coreMaterial)
{
    if (m_labeling.isValid() && m_labelingShellMm == shellThicknessMm
        && m_labelingCore == coreMaterial)
    {
        return true;
    }

    precisioncbct::io::LabelingOptions options;
    options.shellThicknessMm = shellThicknessMm;
    options.coreMaterial = coreMaterial;

    std::string error;
    precisioncbct::io::MaterialLabeling labeling;
    if (!precisioncbct::io::labelMaterials(m_voxelGrid, options, &labeling, &error))
    {
        statusBar()->showMessage(
            QStringLiteral("材料标签失败：%1").arg(QString::fromStdString(error)), 8000);
        m_labeling = precisioncbct::io::MaterialLabeling{};
        m_labelingShellMm = -1.0;
        return false;
    }

    m_labeling = labeling;
    m_labelingShellMm = shellThicknessMm;
    m_labelingCore = coreMaterial;

    // 标签体数据与掩膜网格同尺寸/spacing，只替换标量
    m_labeledGrid = m_voxelGrid;
    m_labeledGrid.image = m_labeling.labels;
    return true;
}

const precisioncbct::io::VoxelGrid& MainWindow::projectionGrid(
    const precisioncbct::simulation::SpectralSettings& spectral,
    const precisioncbct::io::MaterialLabeling** labeling) const
{
    if (labeling != nullptr)
    {
        *labeling = nullptr;
    }
    if (!spectral.enabled || m_labelingShellMm <= 0.0 || !m_labeling.isValid())
    {
        return m_voxelGrid;
    }
    if (labeling != nullptr)
    {
        *labeling = &m_labeling;
    }
    return m_labeledGrid;
}

void MainWindow::onPreviewFrameChanged(int index)
{
    if (index < 0 || index >= m_sequenceAngles.size())
    {
        return;
    }
    updateProjectionForAngle(m_sequenceAngles.at(index));
}

void MainWindow::generateSequenceWithUi()
{
    if (!m_mesh.isValid())
    {
        QMessageBox::information(this, QStringLiteral("尚未加载模型"),
                                 QStringLiteral("请先通过“文件 → 打开模型”载入 STL/OBJ。"));
        return;
    }

    const auto params = m_parameters->parameters();
    const int frameCount = params.projectionCount();
    if (frameCount <= 0)
    {
        QMessageBox::warning(this, QStringLiteral("角度参数无效"),
                             QStringLiteral("角度步长必须大于 0。"));
        return;
    }

    // 512² 的 8 位预览帧约 256 KB/帧，提示用户大序列的内存占用
    const double memoryMb = frameCount * params.detectorColumns * params.detectorRows / (1024.0 * 1024.0);
    if (memoryMb > 512.0)
    {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("序列较大"),
            QStringLiteral("%1 帧 × %2×%3 大约需要 %4 MB 内存用于预览。\n"
                           "可以改用「导出投影序列」流式写盘。是否继续？")
                .arg(frameCount)
                .arg(params.detectorColumns)
                .arg(params.detectorRows)
                .arg(memoryMb, 0, 'f', 0));
        if (answer != QMessageBox::Yes)
        {
            return;
        }
    }

    startSequence(QString(), true, false);
}

void MainWindow::exportSequenceWithUi()
{
    if (!m_mesh.isValid())
    {
        QMessageBox::information(this, QStringLiteral("尚未加载模型"),
                                 QStringLiteral("请先通过“文件 → 打开模型”载入 STL/OBJ。"));
        return;
    }

    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择投影序列输出目录"));
    if (dir.isEmpty())
    {
        return;
    }

    startSequence(dir, false, false);
}

void MainWindow::showAboutDialog()
{
    QMessageBox::about(
        this,
        QStringLiteral("关于 PrecisionCBCTSim"),
        QStringLiteral("<b>PrecisionCBCTSim</b> %1<br/>"
                       "CBCT（锥形束 CT）成像仿真工具<br/><br/>"
                       "界面框架：Qt %2（LGPLv3，动态链接）<br/>"
                       "三维渲染：VTK 9（BSD-3-Clause）<br/><br/>"
                       "本软件为专有软件，详见 LICENSE 与 THIRD_PARTY_NOTICES.txt。")
            .arg(applicationVersion(), QString::fromLatin1(qVersion())));
}

} // namespace precisioncbct::app
