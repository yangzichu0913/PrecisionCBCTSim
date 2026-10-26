#pragma once

#include <QWidget>

#include "simulation/ScanParameters.h"
#include "simulation/ScannerMode.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
namespace precisioncbct::app {

/// 仿真参数面板：只负责界面与参数收集，不参与几何/物理计算。
class ScanParameterPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ScanParameterPanel(QWidget* parent = nullptr);

    /// 当前机架类型
    simulation::ScannerMode scannerMode() const;

    /// 切换机架类型，并把该机架的推荐几何/能谱写进控件（只发一次 parametersChanged）
    void setScannerMode(simulation::ScannerMode mode);

    /// 读取界面当前参数值
    simulation::ScanParameters parameters() const;

    /// 读取能谱与噪声设置
    simulation::SpectralSettings spectralSettings() const;

    /// 皮质骨外壳厚度（mm）；0 表示不做材料分层
    double shellThicknessMm() const;

    /// 设置角度步长（会同步刷新派生信息并发出 parametersChanged）
    void setAngleStepDeg(double stepDeg);

    /// 打开/关闭能谱模型（命令行使用）
    void setSpectralEnabled(bool enabled);

    /// 命令行设置能谱参数（kVp / 入射光子数 / 量子噪声），传 NaN 或负值表示不改该项
    void setSpectralDetail(double kvp, double incidentPhotons, int noiseEnabled);

    /// 命令行设置皮质骨外壳厚度（mm）
    void setShellThicknessMm(double thicknessMm);

    /// 命令行设置探测器模型参数（SPR / PSF 像素 / 串扰比例 / 电子学噪声光子数），
    /// 传负值表示不改该项
    void setDetectorDetail(double scatterRatio, double psfSigmaPixels,
                           double crosstalkFraction, double electronicNoisePhotons);

    /// 把一整套参数写进界面控件（加载预设时使用），完成后只发一次 parametersChanged
    void applySettings(const simulation::ScanParameters& scan,
                       const simulation::SpectralSettings& spectral,
                       double shellThicknessMm);

    /// 把界面参数恢复为内置默认值
    void resetToDefaults();

signals:
    /// 任一参数被修改后发出；接收方通过 parameters() 取完整参数
    void parametersChanged();

    /// 用户在界面上切换了机架类型
    void scannerModeChanged(simulation::ScannerMode mode);

private:
    void wireSignals();
    void updateDerivedInfo();
    void onParameterEdited();
    void applyModeDefaults();

    QComboBox* m_scannerMode = nullptr;
    QDoubleSpinBox* m_sad = nullptr;
    QDoubleSpinBox* m_sdd = nullptr;
    QDoubleSpinBox* m_pixelSize = nullptr;
    QSpinBox* m_columns = nullptr;
    QSpinBox* m_rows = nullptr;
    QDoubleSpinBox* m_startAngle = nullptr;
    QDoubleSpinBox* m_endAngle = nullptr;
    QDoubleSpinBox* m_angleStep = nullptr;

    QCheckBox* m_spectralEnabled = nullptr;
    QDoubleSpinBox* m_tubeVoltage = nullptr;
    QDoubleSpinBox* m_filtration = nullptr;
    QDoubleSpinBox* m_photons = nullptr;
    QCheckBox* m_noiseEnabled = nullptr;
    QDoubleSpinBox* m_shellThickness = nullptr;
    QComboBox* m_material = nullptr;
    QDoubleSpinBox* m_scatterRatio = nullptr;
    QDoubleSpinBox* m_detectorPsf = nullptr;
    QDoubleSpinBox* m_crosstalk = nullptr;
    QDoubleSpinBox* m_electronicNoise = nullptr;

    QLabel* m_infoLabel = nullptr;
    QLabel* m_warningLabel = nullptr;
};

} // namespace precisioncbct::app
