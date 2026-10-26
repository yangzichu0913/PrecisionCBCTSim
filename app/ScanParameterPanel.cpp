#include "app/ScanParameterPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <initializer_list>

namespace precisioncbct::app {
namespace {

QDoubleSpinBox* makeDoubleSpinBox(double minimum, double maximum, double step,
                                  int decimals, double value, const QString& suffix = QString())
{
    auto* box = new QDoubleSpinBox();
    box->setRange(minimum, maximum);
    box->setSingleStep(step);
    box->setDecimals(decimals);
    // 关闭逐字符触发，避免用户输入过程中反复刷新状态
    box->setKeyboardTracking(false);
    if (!suffix.isEmpty())
    {
        // 单位跟数值放在一起：标签就能只写参数名，左对齐整齐、也不容易看错数量级
        box->setSuffix(QStringLiteral(" ") + suffix);
    }
    box->setValue(value);
    box->setMinimumWidth(92);
    box->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return box;
}

QSpinBox* makeIntSpinBox(int minimum, int maximum, int value, const QString& suffix = QString())
{
    auto* box = new QSpinBox();
    box->setRange(minimum, maximum);
    box->setKeyboardTracking(false);
    if (!suffix.isEmpty())
    {
        box->setSuffix(QStringLiteral(" ") + suffix);
    }
    box->setValue(value);
    box->setMinimumWidth(92);
    box->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return box;
}

/// 表单统一间距与标签对齐
QFormLayout* makeForm(QWidget* parent)
{
    auto* form = new QFormLayout(parent);
    form->setContentsMargins(8, 6, 8, 4);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(5);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    return form;
}

/// 把两个控件并排放进一行：参数面板宽度有限，成对参数（起止角、SPR/PSF）同排
/// 能省掉近一半的行数，用户不必为了看一眼默认值滚半天
QWidget* pair(QWidget* first, QWidget* second)
{
    auto* host = new QWidget();
    auto* layout = new QHBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(first);
    layout->addWidget(second);
    return host;
}

} // namespace

ScanParameterPanel::ScanParameterPanel(QWidget* parent)
    : QWidget(parent)
{
    const simulation::ScanParameters defaults;
    const simulation::SpectralSettings defaults_spectral;

    // 保证停靠面板不会被主窗口挤成细条
    setMinimumWidth(300);

    // ---------------- 机架类型：医学 / 工业 / C 型臂
    auto* modeBox = new QGroupBox(QStringLiteral("机架类型"), this);
    auto* modeForm = makeForm(modeBox);
    m_scannerMode = new QComboBox(modeBox);
    for (const auto mode : {simulation::ScannerMode::Medical, simulation::ScannerMode::Industrial,
                            simulation::ScannerMode::CArm})
    {
        m_scannerMode->addItem(QString::fromUtf8(simulation::scannerModeLabel(mode)),
                               static_cast<int>(mode));
    }
    m_scannerMode->setToolTip(
        QStringLiteral("切换机架形态会同时套用该机架的推荐几何/能谱，切换后仍可手动微调"));
    modeForm->addRow(m_scannerMode);

    auto* geometryBox = new QGroupBox(QStringLiteral("几何"), this);
    auto* geometryForm = makeForm(geometryBox);
    m_sad = makeDoubleSpinBox(50.0, 2000.0, 10.0, 1, defaults.sourceAxisDistanceMm,
                              QStringLiteral("mm"));
    m_sdd = makeDoubleSpinBox(100.0, 3000.0, 10.0, 1, defaults.sourceDetectorDistanceMm,
                              QStringLiteral("mm"));
    m_sad->setToolTip(QStringLiteral("SAD：X 射线源到旋转轴（等中心）的距离"));
    m_sdd->setToolTip(QStringLiteral("SDD：X 射线源到探测器平面的距离，必须大于 SAD"));
    geometryForm->addRow(QStringLiteral("SAD / SDD"), pair(m_sad, m_sdd));

    auto* detectorBox = new QGroupBox(QStringLiteral("探测器"), this);
    auto* detectorForm = makeForm(detectorBox);
    m_pixelSize = makeDoubleSpinBox(0.05, 2.0, 0.01, 3, defaults.detectorPixelSizeMm,
                                    QStringLiteral("mm"));
    m_columns = makeIntSpinBox(64, 4096, defaults.detectorColumns, QStringLiteral("px"));
    m_rows = makeIntSpinBox(64, 4096, defaults.detectorRows, QStringLiteral("px"));
    m_columns->setToolTip(QStringLiteral("横向（u）像素数"));
    m_rows->setToolTip(QStringLiteral("纵向（v）像素数"));
    detectorForm->addRow(QStringLiteral("像素尺寸"), m_pixelSize);
    detectorForm->addRow(QStringLiteral("列 U / 行 V"), pair(m_columns, m_rows));

    auto* angleBox = new QGroupBox(QStringLiteral("扫描角度"), this);
    auto* angleForm = makeForm(angleBox);
    m_startAngle = makeDoubleSpinBox(-360.0, 360.0, 1.0, 1, defaults.startAngleDeg,
                                     QStringLiteral("deg"));
    m_endAngle = makeDoubleSpinBox(-360.0, 360.0, 1.0, 1, defaults.endAngleDeg,
                                   QStringLiteral("deg"));
    m_angleStep = makeDoubleSpinBox(0.05, 90.0, 0.1, 2, defaults.angleStepDeg,
                                    QStringLiteral("deg"));
    m_angleStep->setToolTip(QStringLiteral("角度步长决定序列帧数，也影响重建角度密度"));
    angleForm->addRow(QStringLiteral("起 / 止角"), pair(m_startAngle, m_endAngle));
    angleForm->addRow(QStringLiteral("角度步长"), m_angleStep);

    auto* spectralBox = new QGroupBox(QStringLiteral("能谱与噪声"), this);
    auto* spectralForm = makeForm(spectralBox);
    m_spectralEnabled = new QCheckBox(QStringLiteral("启用能谱"), spectralBox);
    m_tubeVoltage = makeDoubleSpinBox(40.0, 150.0, 5.0, 1, defaults_spectral.tubeVoltageKvp,
                                      QStringLiteral("kVp"));
    m_filtration = makeDoubleSpinBox(0.0, 10.0, 0.5, 2, defaults_spectral.inherentFiltrationAlMm,
                                     QStringLiteral("mm"));
    m_filtration->setToolTip(QStringLiteral("固有铝滤过厚度（mm Al）"));
    m_photons = makeDoubleSpinBox(1.0e3, 1.0e8, 1.0e4, 0, defaults_spectral.incidentPhotonsPerPixel);
    m_photons->setToolTip(QStringLiteral("每个探测器像素的入射光子数，决定量子噪声水平"));
    m_noiseEnabled = new QCheckBox(QStringLiteral("量子噪声"), spectralBox);
    m_shellThickness = makeDoubleSpinBox(0.0, 20.0, 0.5, 2, 0.0, QStringLiteral("mm"));
    m_material = new QComboBox(spectralBox);
    for (const auto material : {simulation::Material::Water, simulation::Material::CorticalBone,
                                simulation::Material::PMMA, simulation::Material::Air}) {
        m_material->addItem(QString::fromLatin1(simulation::materialName(material)),
                            static_cast<int>(material));
    }
    m_noiseEnabled->setChecked(defaults_spectral.enableNoise);
    spectralForm->addRow(pair(m_spectralEnabled, m_noiseEnabled));
    spectralForm->addRow(QStringLiteral("kVp / 铝滤过"), pair(m_tubeVoltage, m_filtration));
    spectralForm->addRow(QStringLiteral("光子/像素"), m_photons);
    spectralForm->addRow(QStringLiteral("内 / 皮质骨"), pair(m_material, m_shellThickness));

    spectralBox->setToolTip(
        QStringLiteral("Kramers 连续谱 + 铝滤过 + μ(E) 表；皮质骨厚度 > 0 时按「外壳骨 + 内核材料」"
                       "分层；关闭能谱时使用单一 μ = 0.02 /mm。"));

    auto* detectorModelBox = new QGroupBox(QStringLiteral("探测器模型"), this);
    detectorModelBox->setToolTip(
        QStringLiteral("散射 → PSF → 量子噪声 → 串扰 → 电子学噪声，按此顺序作用；"
                       "全部为 0 时与关闭探测器模型完全一致。"));
    auto* detectorModelForm = makeForm(detectorModelBox);
    m_scatterRatio = makeDoubleSpinBox(0.0, 2.0, 0.05, 2,
                                       defaults_spectral.detector.scatterToPrimaryRatio);
    m_detectorPsf = makeDoubleSpinBox(0.0, 10.0, 0.1, 2,
                                      defaults_spectral.detector.detectorBlurSigmaPixels,
                                      QStringLiteral("px"));
    m_crosstalk = makeDoubleSpinBox(0.0, 0.49, 0.01, 2,
                                    defaults_spectral.detector.crosstalkFraction);
    m_electronicNoise = makeDoubleSpinBox(0.0, 1.0e5, 100.0, 0,
                                          defaults_spectral.detector.electronicNoisePhotons);
    detectorModelForm->addRow(QStringLiteral("SPR / PSF"), pair(m_scatterRatio, m_detectorPsf));
    detectorModelForm->addRow(QStringLiteral("串扰 / 电子噪声"),
                              pair(m_crosstalk, m_electronicNoise));

    m_infoLabel = new QLabel(this);
    m_infoLabel->setObjectName(QStringLiteral("DerivedInfo"));
    m_infoLabel->setWordWrap(true);

    m_warningLabel = new QLabel(QStringLiteral("SDD 必须大于 SAD（探测器位于等中心外侧）"), this);
    m_warningLabel->setObjectName(QStringLiteral("ValidationWarning"));
    m_warningLabel->setWordWrap(true);

    auto* resetButton = new QPushButton(QStringLiteral("恢复默认值"), this);
    resetButton->setObjectName(QStringLiteral("Ghost"));
    resetButton->setToolTip(QStringLiteral("把全部参数恢复为内置默认值"));
    connect(resetButton, &QPushButton::clicked, this, &ScanParameterPanel::resetToDefaults);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addWidget(modeBox);
    layout->addWidget(geometryBox);
    layout->addWidget(detectorBox);
    layout->addWidget(angleBox);
    layout->addWidget(spectralBox);
    layout->addWidget(detectorModelBox);
    layout->addWidget(m_warningLabel);

    // 底部一行放「换算结果 + 恢复默认值」，省掉一整行高度，面板更少滚动
    auto* footer = new QWidget(this);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(8);
    footerLayout->addWidget(m_infoLabel, 1);
    footerLayout->addWidget(resetButton, 0, Qt::AlignBottom);
    layout->addWidget(footer);
    layout->addStretch(1);

    wireSignals();
    updateDerivedInfo();
    applyModeDefaults();
}

void ScanParameterPanel::resetToDefaults()
{
    // 恢复默认 = 回到当前机架的推荐值，而不是永远回到医学 CT 的参数
    const simulation::ScannerMode mode = scannerMode();
    applySettings(simulation::scannerModeGeometry(mode), simulation::scannerModeSpectral(mode),
                  0.0);
}

simulation::ScanParameters ScanParameterPanel::parameters() const
{
    simulation::ScanParameters params;
    params.sourceAxisDistanceMm = m_sad->value();
    params.sourceDetectorDistanceMm = m_sdd->value();
    params.detectorPixelSizeMm = m_pixelSize->value();
    params.detectorColumns = m_columns->value();
    params.detectorRows = m_rows->value();
    params.startAngleDeg = m_startAngle->value();
    params.endAngleDeg = m_endAngle->value();
    params.angleStepDeg = m_angleStep->value();
    return params;
}

simulation::SpectralSettings ScanParameterPanel::spectralSettings() const
{
    simulation::SpectralSettings settings;
    settings.enabled = m_spectralEnabled->isChecked();
    settings.tubeVoltageKvp = m_tubeVoltage->value();
    settings.inherentFiltrationAlMm = m_filtration->value();
    settings.incidentPhotonsPerPixel = m_photons->value();
    settings.enableNoise = m_noiseEnabled->isChecked();
    settings.material = static_cast<simulation::Material>(m_material->currentData().toInt());
    settings.detector.scatterToPrimaryRatio = m_scatterRatio->value();
    settings.detector.detectorBlurSigmaPixels = m_detectorPsf->value();
    settings.detector.crosstalkFraction = m_crosstalk->value();
    settings.detector.electronicNoisePhotons = m_electronicNoise->value();
    return settings;
}

double ScanParameterPanel::shellThicknessMm() const
{
    return m_shellThickness->value();
}

void ScanParameterPanel::setSpectralEnabled(bool enabled)
{
    m_spectralEnabled->setChecked(enabled);
}

void ScanParameterPanel::setSpectralDetail(double kvp, double incidentPhotons, int noiseEnabled)
{
    if (kvp > 0.0)
    {
        m_tubeVoltage->setValue(kvp);
    }
    if (incidentPhotons > 0.0)
    {
        m_photons->setValue(incidentPhotons);
    }
    if (noiseEnabled >= 0)
    {
        m_noiseEnabled->setChecked(noiseEnabled != 0);
    }
    updateDerivedInfo();
    emit parametersChanged();
}

void ScanParameterPanel::setShellThicknessMm(double thicknessMm)
{
    if (thicknessMm >= 0.0)
    {
        m_shellThickness->setValue(thicknessMm);
    }
}

void ScanParameterPanel::setDetectorDetail(double scatterRatio, double psfSigmaPixels,
                                           double crosstalkFraction, double electronicNoisePhotons)
{
    if (scatterRatio >= 0.0) { m_scatterRatio->setValue(scatterRatio); }
    if (psfSigmaPixels >= 0.0) { m_detectorPsf->setValue(psfSigmaPixels); }
    if (crosstalkFraction >= 0.0) { m_crosstalk->setValue(crosstalkFraction); }
    if (electronicNoisePhotons >= 0.0) { m_electronicNoise->setValue(electronicNoisePhotons); }
    updateDerivedInfo();
    emit parametersChanged();
}

void ScanParameterPanel::applySettings(const simulation::ScanParameters& scan,
                                       const simulation::SpectralSettings& spectral,
                                       double shellThicknessMm)
{
    // 批量写入时屏蔽逐控件信号，末尾统一发一次，避免每改一个值就重建投影
    const QSignalBlocker blocker(this);
    m_sad->setValue(scan.sourceAxisDistanceMm);
    m_sdd->setValue(scan.sourceDetectorDistanceMm);
    m_pixelSize->setValue(scan.detectorPixelSizeMm);
    m_columns->setValue(scan.detectorColumns);
    m_rows->setValue(scan.detectorRows);
    m_startAngle->setValue(scan.startAngleDeg);
    m_endAngle->setValue(scan.endAngleDeg);
    m_angleStep->setValue(scan.angleStepDeg);

    m_spectralEnabled->setChecked(spectral.enabled);
    m_tubeVoltage->setValue(spectral.tubeVoltageKvp);
    m_filtration->setValue(spectral.inherentFiltrationAlMm);
    m_photons->setValue(spectral.incidentPhotonsPerPixel);
    m_noiseEnabled->setChecked(spectral.enableNoise);
    m_shellThickness->setValue(shellThicknessMm);
    m_scatterRatio->setValue(spectral.detector.scatterToPrimaryRatio);
    m_detectorPsf->setValue(spectral.detector.detectorBlurSigmaPixels);
    m_crosstalk->setValue(spectral.detector.crosstalkFraction);
    m_electronicNoise->setValue(spectral.detector.electronicNoisePhotons);

    const int materialIndex = m_material->findData(static_cast<int>(spectral.material));
    if (materialIndex >= 0)
    {
        m_material->setCurrentIndex(materialIndex);
    }

    updateDerivedInfo();
    emit parametersChanged();
}

void ScanParameterPanel::wireSignals()
{
    for (QDoubleSpinBox* box : {m_sad, m_sdd, m_pixelSize, m_startAngle, m_endAngle, m_angleStep})
    {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this](double) { onParameterEdited(); });
    }
    for (QSpinBox* box : {m_columns, m_rows})
    {
        connect(box, &QSpinBox::valueChanged, this, [this](int) { onParameterEdited(); });
    }
    connect(m_scannerMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                applyModeDefaults();
                emit scannerModeChanged(scannerMode());
            });
}

simulation::ScannerMode ScanParameterPanel::scannerMode() const
{
    if (m_scannerMode == nullptr || m_scannerMode->currentIndex() < 0)
    {
        return simulation::ScannerMode::Medical;
    }
    return static_cast<simulation::ScannerMode>(m_scannerMode->currentData().toInt());
}

void ScanParameterPanel::setScannerMode(simulation::ScannerMode mode)
{
    const int index = m_scannerMode->findData(static_cast<int>(mode));
    if (index < 0 || index == m_scannerMode->currentIndex())
    {
        applyModeDefaults();
        return;
    }
    {
        const QSignalBlocker blocker(m_scannerMode);
        m_scannerMode->setCurrentIndex(index);
    }
    applyModeDefaults();
    emit scannerModeChanged(mode);
}

void ScanParameterPanel::applyModeDefaults()
{
    const simulation::ScannerMode mode = scannerMode();
    const QString detail =
        QStringLiteral("%1。建议体素尺寸 ≈ %2 mm。")
            .arg(QString::fromUtf8(simulation::scannerModeDescription(mode)))
            .arg(simulation::scannerModeVoxelSizeMm(mode), 0, 'f', 2);
    m_scannerMode->setToolTip(detail);

    // 切机架就把该机架的推荐几何/能谱写进控件；用户随后仍可自行微调
    applySettings(simulation::scannerModeGeometry(mode),
                  simulation::scannerModeSpectral(mode), 0.0);
}

void ScanParameterPanel::updateDerivedInfo()
{
    const simulation::ScanParameters params = parameters();

    m_infoLabel->setText(QStringLiteral("有效面积 %1 × %2 mm² · %3 帧")
                             .arg(params.detectorWidthMm(), 0, 'f', 1)
                             .arg(params.detectorHeightMm(), 0, 'f', 1)
                             .arg(params.projectionCount()));

    m_warningLabel->setVisible(params.sourceDetectorDistanceMm <= params.sourceAxisDistanceMm);
}

void ScanParameterPanel::setAngleStepDeg(double stepDeg)
{
    m_angleStep->setValue(stepDeg);
    // setValue 会触发 onParameterEdited（若值确实变化），这里保证派生信息一定刷新
    updateDerivedInfo();
}

void ScanParameterPanel::onParameterEdited()
{
    updateDerivedInfo();
    emit parametersChanged();
}

} // namespace precisioncbct::app
