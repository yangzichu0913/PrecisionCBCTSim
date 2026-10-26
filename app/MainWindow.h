#pragma once

#include <QFutureWatcher>
#include <QImage>
#include <QMainWindow>
#include <QString>
#include <QVector>

#include <atomic>

#include "io/MaterialLabeling.h"
#include "io/MeshLoader.h"
#include "io/VoxelGrid.h"
#include "simulation/CpuProjector.h"
#include "simulation/FdkReconstructor.h"
#include "simulation/ScanParameters.h"
#include "simulation/ScannerMode.h"

class QDockWidget;
class QLabel;
class QProgressBar;
class QPushButton;

namespace precisioncbct::viz {
class SceneView;
}

namespace precisioncbct::app {

class DRPreviewWidget;
class ScanParameterPanel;

/// 主窗口：中央为 VTK 三维视图，右侧为仿真参数面板，底部为 DR 预览。
///
/// 数据流：STL/OBJ → 体素网格 → 锥束投影（单帧 / 多角度序列）→ DR 显示 + 16 位 PNG 导出。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// 打开并显示模型（菜单与命令行共用，不弹窗；失败时返回 false）。
    bool openModelFile(const QString& path);

    /// 对当前模型做 CPU 体素化并显示结果（不弹窗；失败时返回 false）。
    bool voxelizeCurrentMesh(double spacingMm);

    /// 按当前参数面板的几何生成一张 DR（必要时自动以 1mm 体素化）。不弹窗。
    bool generateDrImage();

    /// 导出当前 DR 为 16 位 PNG。不弹窗。
    bool exportDrImage(const QString& path);

    /// 在后台生成多角度投影序列（可选同时把每帧写成 16 位 PNG）。
    /// keepFrames=false 时只在内存里保留缩略帧，适合纯导出场景。
    /// quitWhenDone=true 时序列结束后自动退出（命令行批处理）。
    bool startSequence(const QString& exportDir, bool keepFrames, bool quitWhenDone);

    /// 请求取消正在运行的后台序列
    void cancelSequence();

    /// 覆盖角度步长（命令行 / 脚本使用），参数非法时返回 false
    bool setAngleStepOverride(double stepDeg);

    /// 切换机架类型（医学 CBCT / 工业 CT / C 型臂）：同步三维机架外观与推荐参数
    void setScannerMode(simulation::ScannerMode mode);

    /// 在预览面板中跳到序列的第 index 帧（同时刷新三维几何）；越界或无序列时返回 false
    bool showSequenceFrame(int index);

    /// 命令行配置能谱模型：enabled 控制开关，kvp/photons/noise < 0 表示保持界面当前值
    bool configureSpectral(bool enabled, double kvp, double photons, int noise);

    /// 命令行设置皮质骨外壳厚度（mm）；< 0 表示不改
    void configureShellThickness(double thicknessMm);

    /// 执行一次 FDK 断层重建（后台线程）：按当前参数生成完整投影序列并重建。
    /// 完成后在三维视图里显示三个正交切面。exportDir 非空时同时导出 16 位 PNG 切片。
    /// quitWhenDone=true 时重建结束后自动退出（命令行批处理）。
    bool startReconstruction(const QString& exportDir, bool quitWhenDone);

    /// 覆盖重建体素数（命令行使用）
    bool setReconstructionSize(int volumeSize);

    /// 设定计算后端（命令行使用）；GPU 不可用时返回 false
    bool configureComputeBackend(bool useGpu);

    /// 当前实际使用的后端描述（用于状态栏："GPU: NVIDIA ... " / "CPU（多线程）"）
    QString computeBackendDescription() const;

    /// 命令行配置探测器模型（SPR / PSF 像素 / 串扰比例 / 电子学噪声光子数）；负值表示不改
    void configureDetector(double scatterRatio, double psfSigmaPixels,
                           double crosstalkFraction, double electronicNoisePhotons);

    /// 保存当前参数为 JSON 预设（不弹窗）
    bool saveSettingsPreset(const QString& path);

    /// 从 JSON 预设加载参数（不弹窗）
    bool loadSettingsPreset(const QString& path);

private slots:
    void showAboutDialog();
    void openModelDialog();
    void voxelizeWithDialog();
    void clearVoxelPreview();
    void generateDrWithUi();
    void exportDrWithUi();
    void generateSequenceWithUi();
    void exportSequenceWithUi();
    void reconstructWithUi();
    void savePresetWithUi();
    void loadPresetWithUi();
    void onPreviewFrameChanged(int index);

private:
    /// 后台线程的产出，随 QFuture 一起回到 UI 线程
    struct SequenceRun
    {
        QVector<QImage> frames;
        QVector<double> anglesDeg;
        int frameCount = 0;
        int completedFrames = 0;
        int width = 0;
        int height = 0;
        double maxLineIntegralMm = 0.0;
        double elapsedMs = 0.0;
        bool cancelled = false;
        bool ok = false;
        QString error;
        QString exportDir;
    };

    void buildCentralView();
    void buildToolBar();
    void buildMenus();
    void buildParameterDock();
    void buildDrDock();
    void updateModelInfo();
    void updateProjectionForAngle(double angleDeg);
    void setBusy(bool busy, const QString& message);
    void applySequenceResult(const SequenceRun& run);
    SequenceRun runSequenceOnWorker(const simulation::ScanParameters& scan,
                                    const simulation::SpectralSettings& spectral,
                                    const QString& exportDir, bool keepFrames);

    /// 后台线程的重建产出
    struct ReconstructionRun
    {
        simulation::ReconstructedVolume volume;
        QString exportDir;
        QString warning;
        int projectionCount = 0;
        double elapsedMs = 0.0;
        bool cancelled = false;
        bool ok = false;
        QString error;
    };

    ReconstructionRun runReconstructionOnWorker(const simulation::ScanParameters& scan,
                                                const simulation::SpectralSettings& spectral,
                                                const QString& exportDir);

    /// 按需生成（并缓存）体素级材料标签；缓存键为「体素网格代次 + 壳厚 + 内核材料」
    bool ensureMaterialLabeling(double shellThicknessMm, simulation::Material coreMaterial);
    const precisioncbct::io::VoxelGrid& projectionGrid(const simulation::SpectralSettings& spectral,
                                                       const io::MaterialLabeling** labeling) const;

    precisioncbct::viz::SceneView* m_view = nullptr;
    ScanParameterPanel* m_parameters = nullptr;
    DRPreviewWidget* m_drPreview = nullptr;
    QDockWidget* m_drDock = nullptr;
    QToolBar* m_toolBar = nullptr;
    QLabel* m_viewInfoLabel = nullptr;
    QLabel* m_modelInfoLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QPushButton* m_cancelButton = nullptr;

    precisioncbct::io::MeshData m_mesh;
    precisioncbct::io::VoxelGrid m_voxelGrid;
    precisioncbct::simulation::DrProjection m_dr;
    double m_voxelSpacingMm = 0.0;

    // 材料标签缓存（体素网格换新或参数变化时重建）
    precisioncbct::io::MaterialLabeling m_labeling;
    precisioncbct::io::VoxelGrid m_labeledGrid;
    double m_labelingShellMm = -1.0;
    precisioncbct::simulation::Material m_labelingCore = precisioncbct::simulation::Material::Water;

    QVector<QImage> m_sequenceFrames;
    QVector<double> m_sequenceAngles;
    QFutureWatcher<SequenceRun> m_sequenceWatcher;
    QFutureWatcher<ReconstructionRun> m_reconstructionWatcher;
    std::atomic_bool m_cancelRequested{false};
    bool m_busy = false;
    bool m_quitWhenSequenceDone = false;
    bool m_quitWhenReconstructionDone = false;
    bool m_geometryCameraFitted = false;
    int m_reconstructionSize = 128;
    simulation::ReconstructedVolume m_reconstruction;
};

} // namespace precisioncbct::app
