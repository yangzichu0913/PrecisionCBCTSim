#include "app/MainWindow.h"

#include "app/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVTKOpenGLNativeWidget.h>

int main(int argc, char* argv[])
{
    // QVTKOpenGLNativeWidget 需要一个带深度缓冲、兼容 profile 的默认 OpenGL 格式，
    // 且必须在任何 QOpenGLWidget 创建上下文之前设置，否则可能出现黑屏。
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PrecisionCBCTSim"));
    QApplication::setApplicationDisplayName(QStringLiteral("PrecisionCBCTSim"));
    QApplication::setOrganizationName(QStringLiteral("PrecisionCBCTSim"));
    // 窗口与任务栏图标（资源由 app/resources.qrc 编译进可执行文件）
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/appicon.png")));

    // 统一深色主题：必须在任何控件创建前应用，否则先创建的控件会保留默认调色板
    precisioncbct::app::applyTheme(app);
#ifdef PCSIM_VERSION
    QApplication::setApplicationVersion(QString::fromLatin1(PCSIM_VERSION));
#endif

    // 命令行入口：便于脚本化/批量检查（例如 --open model.stl --voxel-size 1）
    // 注意：-h/--help 与 -v/--version 已被 Qt 内置占用，自定义短选项不可再用这两个字母，
    // 否则同名的长选项会注册失败（addOption 遇到命名冲突会整体跳过）。
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("CBCT 成像仿真工具"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption openOption(
        {QStringLiteral("o"), QStringLiteral("open")},
        QStringLiteral("启动后直接打开模型文件（STL/OBJ）"),
        QStringLiteral("file"));
    const QCommandLineOption voxelOption(
        QStringLiteral("voxel-size"),
        QStringLiteral("启动后按指定体素尺寸做体素化预览（单位 mm）"),
        QStringLiteral("mm"));
    const QCommandLineOption exportDrOption(
        QStringLiteral("export-dr"),
        QStringLiteral("生成 DR 并导出为 16 位 PNG，然后退出"),
        QStringLiteral("file"));
    const QCommandLineOption drOption(
        QStringLiteral("dr"),
        QStringLiteral("启动后立即按当前参数生成一张 DR（不导出、不退出）"));
    const QCommandLineOption sequenceOption(
        QStringLiteral("sequence"),
        QStringLiteral("启动后在后台生成投影序列并在界面中预览"));
    const QCommandLineOption exportSequenceOption(
        QStringLiteral("export-sequence"),
        QStringLiteral("生成投影序列并逐帧写出 16 位 PNG 到指定目录，完成后退出"),
        QStringLiteral("dir"));
    const QCommandLineOption angleStepOption(
        QStringLiteral("angle-step"),
        QStringLiteral("覆盖角度步长（度），便于快速试算小序列"),
        QStringLiteral("deg"));
    const QCommandLineOption previewFrameOption(
        QStringLiteral("preview-frame"),
        QStringLiteral("序列生成后跳到指定帧（0 起），便于脚本化截图"),
        QStringLiteral("index"));
    const QCommandLineOption spectralOption(
        QStringLiteral("spectral"),
        QStringLiteral("启用能谱 + 量子噪声模型（Phase 3）"));
    const QCommandLineOption kvpOption(
        QStringLiteral("kvp"),
        QStringLiteral("管电压 kVp（配合 --spectral）"),
        QStringLiteral("kv"));
    const QCommandLineOption photonsOption(
        QStringLiteral("photons"),
        QStringLiteral("每像素入射光子数（决定噪声水平）"),
        QStringLiteral("count"));
    const QCommandLineOption noiseOption(
        QStringLiteral("noise"),
        QStringLiteral("1/0 打开或关闭量子噪声"),
        QStringLiteral("flag"));
    const QCommandLineOption shellOption(
        QStringLiteral("shell"),
        QStringLiteral("皮质骨外壳厚度 mm（>0 时启用体素级材料分层）"),
        QStringLiteral("mm"));
    const QCommandLineOption sprOption(
        QStringLiteral("spr"),
        QStringLiteral("散射/初级比 SPR（探测器模型，典型 0.2~0.6）"),
        QStringLiteral("ratio"));
    const QCommandLineOption psfOption(
        QStringLiteral("psf"),
        QStringLiteral("探测器 PSF 高斯标准差（像素）"),
        QStringLiteral("sigma"));
    const QCommandLineOption crosstalkOption(
        QStringLiteral("crosstalk"),
        QStringLiteral("像素串扰比例 [0, 0.5)"),
        QStringLiteral("fraction"));
    const QCommandLineOption enoiseOption(
        QStringLiteral("enoise"),
        QStringLiteral("电子学噪声标准差（光子数）"),
        QStringLiteral("photons"));
    const QCommandLineOption savePresetOption(
        QStringLiteral("save-preset"),
        QStringLiteral("把当前参数写成 JSON 预设，然后退出"),
        QStringLiteral("file"));
    const QCommandLineOption loadPresetOption(
        QStringLiteral("load-preset"),
        QStringLiteral("启动后先加载 JSON 预设参数（之后显式指定的选项仍然生效）"),
        QStringLiteral("file"));
    const QCommandLineOption reconstructOption(
        QStringLiteral("reconstruct"),
        QStringLiteral("按当前角度范围采集投影并做 FDK 锥束重建（留在界面里查看）"));
    const QCommandLineOption reconstructSizeOption(
        QStringLiteral("reconstruct-size"),
        QStringLiteral("重建体素数（立方，默认 128）"),
        QStringLiteral("n"));
    const QCommandLineOption exportVolumeOption(
        QStringLiteral("export-volume"),
        QStringLiteral("重建并把体数据写成 16 位 PNG 切片序列到指定目录，完成后退出"),
        QStringLiteral("dir"));
    const QCommandLineOption gpuOption(
        QStringLiteral("gpu"),
        QStringLiteral("强制使用 GPU（CUDA）做射线追踪；不可用时报警并退回 CPU"));
    const QCommandLineOption cpuOption(
        QStringLiteral("cpu"),
        QStringLiteral("强制使用 CPU 多线程做射线追踪"));
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("启动动作完成后把主窗口抓图存成 PNG 并退出（界面回归检查用）"),
        QStringLiteral("file"));
    const QCommandLineOption modeOption(
        QStringLiteral("mode"),
        QStringLiteral("机架类型：medical（医学 CBCT，默认）/ industrial（工业 CT）/ carm（C 型臂）"),
        QStringLiteral("type"));
    parser.addOption(openOption);
    parser.addOption(voxelOption);
    parser.addOption(exportDrOption);
    parser.addOption(drOption);
    parser.addOption(sequenceOption);
    parser.addOption(exportSequenceOption);
    parser.addOption(angleStepOption);
    parser.addOption(previewFrameOption);
    parser.addOption(spectralOption);
    parser.addOption(kvpOption);
    parser.addOption(photonsOption);
    parser.addOption(noiseOption);
    parser.addOption(shellOption);
    parser.addOption(sprOption);
    parser.addOption(psfOption);
    parser.addOption(crosstalkOption);
    parser.addOption(enoiseOption);
    parser.addOption(savePresetOption);
    parser.addOption(loadPresetOption);
    parser.addOption(reconstructOption);
    parser.addOption(reconstructSizeOption);
    parser.addOption(exportVolumeOption);
    parser.addOption(gpuOption);
    parser.addOption(cpuOption);
    parser.addOption(screenshotOption);
    parser.addOption(modeOption);
    parser.process(app);

    precisioncbct::app::MainWindow window;
    window.show();

    // 机架类型要在其它配置之前生效：它会把该机架的推荐几何/能谱写进参数面板
    if (parser.isSet(modeOption))
    {
        precisioncbct::simulation::ScannerMode mode =
            precisioncbct::simulation::ScannerMode::Medical;
        const QByteArray modeText = parser.value(modeOption).toUtf8();
        if (!precisioncbct::simulation::parseScannerMode(modeText.constData(), &mode))
        {
            QCoreApplication::exit(18);
        }
        else
        {
            window.setScannerMode(mode);
        }
    }

    // 计算后端：命令行优先（--cpu / --gpu），否则按可用性自动选择
    if (parser.isSet(cpuOption))
    {
        window.configureComputeBackend(false);
    }
    else if (parser.isSet(gpuOption) && !window.configureComputeBackend(true))
    {
        QCoreApplication::exit(15);
    }

    if (parser.isSet(openOption))
    {
        const QString path = parser.value(openOption);
        const QString voxelText = parser.value(voxelOption);
        const QString exportDrPath = parser.value(exportDrOption);
        const bool generateDr = parser.isSet(drOption);
        const bool generateSequence = parser.isSet(sequenceOption);
        const QString exportSequenceDir = parser.value(exportSequenceOption);
        const QString angleStepText = parser.value(angleStepOption);
        const QString previewFrameText = parser.value(previewFrameOption);
        const bool spectral = parser.isSet(spectralOption);
        const bool hasKvp = parser.isSet(kvpOption);
        const bool hasPhotons = parser.isSet(photonsOption);
        const bool hasNoise = parser.isSet(noiseOption);
        const double kvp = hasKvp ? parser.value(kvpOption).toDouble() : -1.0;
        const double photons = hasPhotons ? parser.value(photonsOption).toDouble() : -1.0;
        const int noise = hasNoise ? parser.value(noiseOption).toInt() : -1;
        const double shellMm = parser.isSet(shellOption) ? parser.value(shellOption).toDouble() : -1.0;
        const double spr = parser.isSet(sprOption) ? parser.value(sprOption).toDouble() : -1.0;
        const double psf = parser.isSet(psfOption) ? parser.value(psfOption).toDouble() : -1.0;
        const double crosstalk =
            parser.isSet(crosstalkOption) ? parser.value(crosstalkOption).toDouble() : -1.0;
        const double enoise = parser.isSet(enoiseOption) ? parser.value(enoiseOption).toDouble() : -1.0;
        const QString savePresetPath = parser.value(savePresetOption);
        const QString loadPresetPath = parser.value(loadPresetOption);
        const bool doReconstruct = parser.isSet(reconstructOption);
        const QString reconstructSizeText = parser.value(reconstructSizeOption);
        const QString exportVolumeDir = parser.value(exportVolumeOption);

        // 等窗口与 OpenGL 上下文就绪后再加载模型，否则首次渲染可能拿不到上下文
        QTimer::singleShot(0, &window,
                           [&window, path, voxelText, exportDrPath, generateDr,
                            generateSequence, exportSequenceDir, angleStepText,
                            previewFrameText, spectral, kvp, photons, noise, shellMm,
                            spr, psf, crosstalk, enoise, savePresetPath, loadPresetPath,
                            doReconstruct, reconstructSizeText, exportVolumeDir]() {
            // 先套用预设，再用显式选项覆盖，符合「预设是起点」的直觉
            if (!loadPresetPath.isEmpty() && !window.loadSettingsPreset(loadPresetPath))
            {
                QCoreApplication::exit(9);
                return;
            }
            window.configureSpectral(spectral, kvp, photons, noise);
            window.configureShellThickness(shellMm);
            window.configureDetector(spr, psf, crosstalk, enoise);
            if (!window.openModelFile(path))
            {
                QCoreApplication::exit(2);
                return;
            }
            if (!angleStepText.isEmpty())
            {
                bool ok = false;
                const double step = angleStepText.toDouble(&ok);
                if (!ok || !window.setAngleStepOverride(step))
                {
                    QCoreApplication::exit(8);
                    return;
                }
            }
            if (!voxelText.isEmpty())
            {
                bool ok = false;
                const double spacingMm = voxelText.toDouble(&ok);
                if (!ok || !window.voxelizeCurrentMesh(spacingMm))
                {
                    QCoreApplication::exit(3);
                    return;
                }
            }
            // 体素化之后再存预设，这样 voxelSizeMm 记录的是实际使用的体素尺寸
            if (!savePresetPath.isEmpty() && !window.saveSettingsPreset(savePresetPath))
            {
                QCoreApplication::exit(10);
                return;
            }
            if (!exportDrPath.isEmpty())
            {
                if (!window.exportDrImage(exportDrPath))
                {
                    QCoreApplication::exit(4);
                    return;
                }
                // 批处理语义：导出成功即退出，退出码 0
                QCoreApplication::exit(0);
                return;
            }
            if (!exportSequenceDir.isEmpty())
            {
                if (!window.startSequence(exportSequenceDir, false, true))
                {
                    QCoreApplication::exit(6);
                }
                return;
            }
            if (!reconstructSizeText.isEmpty())
            {
                bool ok = false;
                const int size = reconstructSizeText.toInt(&ok);
                if (!ok || !window.setReconstructionSize(size))
                {
                    QCoreApplication::exit(12);
                    return;
                }
            }
            if (!exportVolumeDir.isEmpty())
            {
                // 批处理：重建 + 导出切片序列，完成后退出
                if (!window.startReconstruction(exportVolumeDir, true))
                {
                    QCoreApplication::exit(13);
                }
                return;
            }
            if (doReconstruct && !window.startReconstruction(QString(), false))
            {
                QCoreApplication::exit(14);
                return;
            }
            if (generateSequence)
            {
                if (!window.startSequence(QString(), true, false))
                {
                    QCoreApplication::exit(7);
                    return;
                }
                if (!previewFrameText.isEmpty())
                {
                    // 序列在后台生成；等它完成后由 finished 关心跳帧，这里用定时轮询避免阻塞事件循环
                    const int target = previewFrameText.toInt();
                    auto* timer = new QTimer(&window);
                    timer->setInterval(200);
                    QObject::connect(timer, &QTimer::timeout, &window, [&window, timer, target]() {
                        if (window.showSequenceFrame(target))
                        {
                            timer->stop();
                            timer->deleteLater();
                        }
                    });
                    timer->start();
                }
                return;
            }
            if (generateDr && !window.generateDrImage())
            {
                QCoreApplication::exit(5);
                return;
            }
            // 只要求保存预设时按批处理语义直接退出（否则窗口会一直挂着）
            if (!savePresetPath.isEmpty() && exportDrPath.isEmpty() && exportSequenceDir.isEmpty()
                && !generateSequence && !generateDr)
            {
                QCoreApplication::exit(0);
            }
        });
    }

    // 界面回归检查：等启动动作跑完后抓主窗口，比用系统截图更可靠（不受缩放/遮挡影响）
    if (parser.isSet(screenshotOption))
    {
        const QString shotPath = parser.value(screenshotOption);
        auto* timer = new QTimer(&window);
        timer->setSingleShot(true);
        timer->setInterval(3000);
        QObject::connect(timer, &QTimer::timeout, &window, [&window, shotPath]() {
            const bool saved = window.grab().save(shotPath);
            QCoreApplication::exit(saved ? 0 : 17);
        });
        timer->start();
    }

    return QApplication::exec();
}
