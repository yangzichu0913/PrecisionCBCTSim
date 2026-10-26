#include "simulation/ScannerMode.h"

#include <cctype>
#include <cstring>

namespace precisioncbct::simulation {
namespace {

bool equalsIgnoreCase(const char* a, const char* b)
{
    if (a == nullptr || b == nullptr)
    {
        return false;
    }
    while (*a != '\0' && *b != '\0')
    {
        const int ca = std::tolower(static_cast<unsigned char>(*a));
        const int cb = std::tolower(static_cast<unsigned char>(*b));
        if (ca != cb)
        {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

} // namespace

const char* scannerModeId(ScannerMode mode)
{
    switch (mode)
    {
    case ScannerMode::Industrial:
        return "industrial";
    case ScannerMode::CArm:
        return "carm";
    case ScannerMode::Medical:
    default:
        return "medical";
    }
}

bool parseScannerMode(const char* text, ScannerMode* out)
{
    if (text == nullptr || out == nullptr)
    {
        return false;
    }
    if (equalsIgnoreCase(text, "medical") || equalsIgnoreCase(text, "med"))
    {
        *out = ScannerMode::Medical;
        return true;
    }
    if (equalsIgnoreCase(text, "industrial") || equalsIgnoreCase(text, "industry") ||
        equalsIgnoreCase(text, "ind"))
    {
        *out = ScannerMode::Industrial;
        return true;
    }
    if (equalsIgnoreCase(text, "carm") || equalsIgnoreCase(text, "c-arm") ||
        equalsIgnoreCase(text, "c"))
    {
        *out = ScannerMode::CArm;
        return true;
    }
    return false;
}

const char* scannerModeLabel(ScannerMode mode)
{
    switch (mode)
    {
    case ScannerMode::Industrial:
        return "工业 CT";
    case ScannerMode::CArm:
        return "C 型臂 CBCT";
    case ScannerMode::Medical:
    default:
        return "医学 CBCT";
    }
}

const char* scannerModeDescription(ScannerMode mode)
{
    switch (mode)
    {
    case ScannerMode::Industrial:
        return "源与探测器固定在门架上，样品放在转台上自转（微焦点、小视野、高分辨）";
    case ScannerMode::CArm:
        return "C 形弧臂连接球管与平板，绕手术床旋转（术中锥束扫描）";
    case ScannerMode::Medical:
    default:
        return "患者卧位，环形机架 + 检查床绕患者旋转（常规锥束扫描）";
    }
}

ScanParameters scannerModeGeometry(ScannerMode mode)
{
    ScanParameters params;
    switch (mode)
    {
    case ScannerMode::Industrial:
        // 微焦点工业 CT：小像素、大放大比、样品紧贴射线源
        params.sourceAxisDistanceMm = 300.0;
        params.sourceDetectorDistanceMm = 900.0;
        params.detectorPixelSizeMm = 0.2;
        params.detectorColumns = 1024;
        params.detectorRows = 1024;
        params.startAngleDeg = 0.0;
        params.endAngleDeg = 359.0;
        params.angleStepDeg = 0.5;
        break;
    case ScannerMode::CArm:
        // 术中 C 臂：源离患者更远、探测器更小，扫描角度常为短扫描
        params.sourceAxisDistanceMm = 600.0;
        params.sourceDetectorDistanceMm = 1100.0;
        params.detectorPixelSizeMm = 0.3;
        params.detectorColumns = 512;
        params.detectorRows = 512;
        params.startAngleDeg = -45.0;
        params.endAngleDeg = 225.0;
        params.angleStepDeg = 1.0;
        break;
    case ScannerMode::Medical:
    default:
        params.sourceAxisDistanceMm = 500.0;
        params.sourceDetectorDistanceMm = 1000.0;
        params.detectorPixelSizeMm = 0.4;
        params.detectorColumns = 512;
        params.detectorRows = 512;
        params.startAngleDeg = 0.0;
        params.endAngleDeg = 359.0;
        params.angleStepDeg = 1.0;
        break;
    }
    return params;
}

SpectralSettings scannerModeSpectral(ScannerMode mode)
{
    SpectralSettings settings;
    switch (mode)
    {
    case ScannerMode::Industrial:
        // 工业射线管常用更高管电压、更厚滤过，追求穿透与对比度
        settings.tubeVoltageKvp = 120.0;
        settings.inherentFiltrationAlMm = 1.0;
        settings.incidentPhotonsPerPixel = 2.0e5;
        break;
    case ScannerMode::CArm:
        settings.tubeVoltageKvp = 90.0;
        settings.inherentFiltrationAlMm = 2.5;
        settings.incidentPhotonsPerPixel = 5.0e4;
        break;
    case ScannerMode::Medical:
    default:
        settings.tubeVoltageKvp = 100.0;
        settings.inherentFiltrationAlMm = 2.5;
        settings.incidentPhotonsPerPixel = 1.0e5;
        break;
    }
    return settings;
}

double scannerModeVoxelSizeMm(ScannerMode mode)
{
    switch (mode)
    {
    case ScannerMode::Industrial:
        return 0.25;
    case ScannerMode::CArm:
        return 1.0;
    case ScannerMode::Medical:
    default:
        return 1.0;
    }
}

bool scannerModeRotatesSample(ScannerMode mode)
{
    // 工业 CT 与 C 臂都是"源和探测器不动，样品在转台上转"；
    // 医学 CBCT 才是机架绕患者转
    return mode == ScannerMode::Industrial || mode == ScannerMode::CArm;
}

} // namespace precisioncbct::simulation
