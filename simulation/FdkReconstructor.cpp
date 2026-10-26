#include "simulation/FdkReconstructor.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <thread>

#include <vtkFloatArray.h>
#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkPointData.h>

namespace precisioncbct::simulation {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
/// 透射率下限：避免 −ln(0) 变成 inf，也避免散粒噪声把极暗像素放大成尖峰
constexpr double kMinTransmission = 1e-6;

/// 原地基 2 FFT（输入长度必须是 2 的幂）。inverse=true 时不做 1/N 归一化。
void fft(std::vector<std::complex<double>>* data, bool inverse)
{
    const size_t n = data->size();
    if (n <= 1)
    {
        return;
    }

    // 位反转置换
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
        {
            j ^= bit;
        }
        j ^= bit;
        if (i < j)
        {
            std::swap((*data)[i], (*data)[j]);
        }
    }

    for (size_t length = 2; length <= n; length <<= 1)
    {
        const double angle = (inverse ? kTwoPi : -kTwoPi) / static_cast<double>(length);
        const std::complex<double> wLength(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += length)
        {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < length / 2; ++k)
            {
                const std::complex<double> u = (*data)[i + k];
                const std::complex<double> v = (*data)[i + k + length / 2] * w;
                (*data)[i + k] = u + v;
                (*data)[i + k + length / 2] = u - v;
                w *= wLength;
            }
        }
    }
}

size_t nextPowerOfTwo(size_t value)
{
    size_t result = 1;
    while (result < value)
    {
        result <<= 1;
    }
    return result;
}

} // namespace

double ReconstructedVolume::minValue() const
{
    if (attenuationPerMm.empty())
    {
        return 0.0;
    }
    return static_cast<double>(*std::min_element(attenuationPerMm.begin(), attenuationPerMm.end()));
}

double ReconstructedVolume::maxValue() const
{
    if (attenuationPerMm.empty())
    {
        return 0.0;
    }
    return static_cast<double>(*std::max_element(attenuationPerMm.begin(), attenuationPerMm.end()));
}

vtkSmartPointer<vtkImageData> ReconstructedVolume::toImageData() const
{
    if (!isValid())
    {
        return nullptr;
    }

    vtkNew<vtkFloatArray> scalars;
    scalars->SetName("attenuationPerMm");
    scalars->SetNumberOfComponents(1);
    scalars->SetNumberOfTuples(static_cast<vtkIdType>(attenuationPerMm.size()));
    for (size_t i = 0; i < attenuationPerMm.size(); ++i)
    {
        scalars->SetValue(static_cast<vtkIdType>(i), attenuationPerMm[i]);
    }

    vtkSmartPointer<vtkImageData> image = vtkSmartPointer<vtkImageData>::New();
    image->SetDimensions(dimensions);
    image->SetSpacing(spacingMm);
    image->SetOrigin(originMm);
    image->GetPointData()->SetScalars(scalars);
    return image;
}

FdkReconstructor::FdkReconstructor(const FdkOptions& options, const ConeBeamView& geometry)
    : m_options(options)
{
    m_geometry = geometry;
    m_geometry.angleDeg = 0.0;
    if (!m_geometry.update())
    {
        m_error = "投影几何非法：需满足 0 < SAD < SDD、像素尺寸 > 0、行列数 > 0";
        return;
    }
    if (options.volumeSize < 8 || options.volumeSize > 1024)
    {
        m_error = "重建体素数需在 8~1024 之间";
        return;
    }
    if (!(options.voxelSizeMm > 0.0))
    {
        m_error = "重建体素尺寸必须大于 0";
        return;
    }

    m_columns = m_geometry.columns;
    m_rows = m_geometry.rows;
    m_pixelSizeMm = m_geometry.pixelSizeMm;
    m_sadMm = m_geometry.sourceAxisDistanceMm;
    m_sddMm = m_geometry.sourceDetectorDistanceMm;

    const int size = options.volumeSize;
    m_volume.assign(static_cast<size_t>(size) * size * size, 0.0);
    m_valid = true;
}

void FdkReconstructor::reset()
{
    std::fill(m_volume.begin(), m_volume.end(), 0.0);
    m_projectionCount = 0;
    m_accumulatedAngleDeg = 0.0;
    m_previousAngleDeg = 0.0;
    m_hasPreviousAngle = false;
    m_deltaBetaRad = 0.0;
}

void FdkReconstructor::rampFilterRow(const std::vector<double>& input,
                                     std::vector<double>* output) const
{
    // 补零到 2 倍长度再滤，避免循环卷积的环绕污染
    const size_t n = nextPowerOfTwo(input.size() * 2);
    std::vector<std::complex<double>> buffer(n, std::complex<double>(0.0, 0.0));
    for (size_t i = 0; i < input.size(); ++i)
    {
        buffer[i] = std::complex<double>(input[i], 0.0);
    }

    fft(&buffer, false);

    // 频域 ramp：H(f) = |f|，f 以 cycles/mm 计
    for (size_t k = 0; k < n; ++k)
    {
        const double frequency =
            static_cast<double>(k <= n / 2 ? k : n - k) / (static_cast<double>(n) * m_pixelSizeMm);
        double gain = frequency;

        if (m_options.hannWindow)
        {
            // Hann 窗：以奈奎斯特频率为截止
            const double nyquist = 0.5 / m_pixelSizeMm;
            const double ratio = frequency / nyquist;
            gain *= 0.5 * (1.0 + std::cos(kPi * std::min(1.0, ratio)));
        }

        // 频率域的 |f| 增益：与离散 Ram-Lak 核（h0 = 1/(4du²)，h_odd = −1/(π²n²du²)，
        // q = du·conv(p,h)）逐点对拍验证过，正确的系数就是 1（不加 du，也不除 du）。
        // 早期乘了 du，导致重建值随探测器像素尺寸线性漂移：0.4mm → 0.0042，2.0mm → 0.019。
        buffer[k] *= gain;
    }

    fft(&buffer, true);

    output->assign(input.size(), 0.0);
    for (size_t i = 0; i < input.size(); ++i)
    {
        (*output)[i] = buffer[i].real() / static_cast<double>(n);
    }
}

bool FdkReconstructor::addProjection(double angleDeg, const DrProjection& projection)
{
    if (!m_valid)
    {
        return false;
    }
    if (!projection.isValid() || projection.width != m_columns || projection.height != m_rows)
    {
        return false;
    }

    const double beta = angleDeg * kPi / 180.0;

    // 角步长：用相邻帧的角度差估计（第一帧还没有前一帧，先取默认 1°）
    double deltaBeta = 1.0 * kPi / 180.0;
    if (m_hasPreviousAngle)
    {
        double difference = std::fabs(angleDeg - m_previousAngleDeg);
        if (difference > 180.0)
        {
            difference = std::fabs(difference - 360.0); // 跨 0° 的那一步
        }
        if (difference > 1e-6)
        {
            deltaBeta = difference * kPi / 180.0;
        }
    }
    m_deltaBetaRad = deltaBeta;
    m_previousAngleDeg = angleDeg;
    m_hasPreviousAngle = true;
    m_accumulatedAngleDeg += (m_projectionCount == 0 ? 0.0 : deltaBeta * 180.0 / kPi);
    ++m_projectionCount;

    // 1+2. 透射率 → 线积分，并做余弦加权（按行滤波，行间独立）
    std::vector<double> cosineWeighted(static_cast<size_t>(m_columns));
    std::vector<double> filtered(static_cast<size_t>(m_columns));
    std::vector<std::vector<double>> filteredImage(static_cast<size_t>(m_rows),
                                                  std::vector<double>(static_cast<size_t>(m_columns),
                                                                      0.0));

    for (int row = 0; row < m_rows; ++row)
    {
        const double v = (static_cast<double>(row) + 0.5 - 0.5 * m_rows) * m_pixelSizeMm;
        for (int column = 0; column < m_columns; ++column)
        {
            const double u = (static_cast<double>(column) + 0.5 - 0.5 * m_columns) * m_pixelSizeMm;
            const double transmission = std::clamp(
                static_cast<double>(projection.transmission[static_cast<size_t>(row) * m_columns
                                                            + column]),
                kMinTransmission, 1.0);
            const double lineIntegral = -std::log(transmission);
            // 平板探测器的余弦加权：D/√(D²+u²+v²)
            const double weight = m_sddMm / std::sqrt(m_sddMm * m_sddMm + u * u + v * v);
            cosineWeighted[static_cast<size_t>(column)] = lineIntegral * weight;
        }

        // 3. 沿 u 做 ramp 滤波
        rampFilterRow(cosineWeighted, &filtered);
        filteredImage[static_cast<size_t>(row)] = filtered;
    }

    // 4. 反投影（按 z 切片并行）
    const int size = m_options.volumeSize;
    const double voxel = m_options.voxelSizeMm;
    const double cosBeta = std::cos(beta);
    const double sinBeta = std::sin(beta);
    const double halfExtent = 0.5 * static_cast<double>(size) * voxel;

    const auto backproject = [&](int firstSlice, int lastSlice) {
        for (int k = firstSlice; k < lastSlice; ++k)
        {
            const double z = m_options.centerMm[2] - halfExtent + (k + 0.5) * voxel;
            for (int j = 0; j < size; ++j)
            {
                const double y = m_options.centerMm[1] - halfExtent + (j + 0.5) * voxel;
                for (int i = 0; i < size; ++i)
                {
                    const double x = m_options.centerMm[0] - halfExtent + (i + 0.5) * voxel;

                    // 世界坐标 → 「源-探测器」局部坐标：
                    //   x' 沿探测器 u 轴，y' 沿源到探测器方向，z' 沿探测器 v 轴
                    const double xLocal = x * cosBeta + y * sinBeta;
                    const double yLocal = -x * sinBeta + y * cosBeta;
                    const double zLocal = z;

                    const double denominator = m_sadMm + yLocal;
                    if (denominator <= 1e-6)
                    {
                        continue; // 体素落在源后面，跳过
                    }

                    // 锥束投影到平板探测器上的坐标（mm）
                    const double uValue = m_sddMm * xLocal / denominator;
                    const double vValue = m_sddMm * zLocal / denominator;

                    // → 像素索引（像素中心在 (col+0.5) 处，与 ConeBeamView 一致）
                    const double column = uValue / m_pixelSizeMm + 0.5 * m_columns - 0.5;
                    const double row = vValue / m_pixelSizeMm + 0.5 * m_rows - 0.5;
                    if (column < 0.0 || column >= m_columns - 1 || row < 0.0
                        || row >= m_rows - 1)
                    {
                        continue; // 落在探测器之外：该角度对这条射线无贡献
                    }

                    // 双线性插值
                    const int c0 = static_cast<int>(column);
                    const int r0 = static_cast<int>(row);
                    const double tc = column - c0;
                    const double tr = row - r0;
                    const double value =
                        (1.0 - tc) * (1.0 - tr)
                            * filteredImage[static_cast<size_t>(r0)][static_cast<size_t>(c0)]
                        + tc * (1.0 - tr)
                              * filteredImage[static_cast<size_t>(r0)][static_cast<size_t>(c0) + 1]
                        + (1.0 - tc) * tr
                              * filteredImage[static_cast<size_t>(r0) + 1][static_cast<size_t>(c0)]
                        + tc * tr
                              * filteredImage[static_cast<size_t>(r0) + 1]
                                             [static_cast<size_t>(c0) + 1];

                    // FDK 的 1/U² 权重：U = (R+y')/R
                    const double ratio = m_sadMm / denominator;
                    m_volume[(static_cast<size_t>(k) * size + j) * size + i] +=
                        ratio * ratio * value;
                }
            }
        }
    };

    unsigned int threadCount = std::thread::hardware_concurrency();
    threadCount = std::max(1u, std::min<unsigned int>(threadCount, static_cast<unsigned int>(size)));
    if (threadCount <= 1)
    {
        backproject(0, size);
    }
    else
    {
        std::vector<std::thread> workers;
        workers.reserve(threadCount);
        const int slicesPerThread = (size + static_cast<int>(threadCount) - 1)
                                    / static_cast<int>(threadCount);
        for (unsigned int t = 0; t < threadCount; ++t)
        {
            const int first = static_cast<int>(t) * slicesPerThread;
            const int last = std::min(first + slicesPerThread, size);
            if (first >= last)
            {
                break;
            }
            workers.emplace_back(backproject, first, last);
        }
        for (std::thread& worker : workers)
        {
            worker.join();
        }
    }
    return true;
}

ReconstructedVolume FdkReconstructor::volume() const
{
    ReconstructedVolume result;
    if (!m_valid || m_projectionCount == 0)
    {
        return result;
    }

    const int size = m_options.volumeSize;
    const double voxel = m_options.voxelSizeMm;
    result.dimensions[0] = size;
    result.dimensions[1] = size;
    result.dimensions[2] = size;
    result.spacingMm[0] = voxel;
    result.spacingMm[1] = voxel;
    result.spacingMm[2] = voxel;
    for (int axis = 0; axis < 3; ++axis)
    {
        result.originMm[axis] = m_options.centerMm[axis] - 0.5 * size * voxel;
    }

    // 反投影权重：Δβ（360° 扫描）。这个常数用「解析球」标定并用测试钉死——
    // 与 ramp 一起决定绝对量纲，两者必须同时正确才能得到真实的 μ：
    // 早期 ramp 多了 du 因子、这里又多乘了 1/2，在 du=2.0 时两个错误恰好抵消，
    // 换到 0.4mm 像素就整体偏小 5 倍。测试现在同时检查 1.0mm 与 2.0mm 两种采样。
    const double scale = m_deltaBetaRad;    result.attenuationPerMm.resize(m_volume.size());
    for (size_t i = 0; i < m_volume.size(); ++i)
    {
        result.attenuationPerMm[i] = static_cast<float>(m_volume[i] * scale);
    }
    return result;
}

} // namespace precisioncbct::simulation
