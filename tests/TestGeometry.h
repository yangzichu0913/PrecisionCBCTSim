#pragma once

// 测试用几何生成（不依赖 VTK 的写端，保证只验证仓库内的读取/计算路径）。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace pcsim::test {

struct Vec3
{
    double x, y, z;
    Vec3 operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
    Vec3 operator*(double scale) const { return {x * scale, y * scale, z * scale}; }
    Vec3 normalized() const
    {
        const double length = std::sqrt(x * x + y * y + z * z);
        return {x / length, y / length, z / length};
    }
};

using Triangle = std::array<Vec3, 3>;

/// 二进制 STL 写出
inline void writeBinaryStl(const std::filesystem::path& path, const std::vector<Triangle>& triangles)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        throw std::runtime_error("无法写入临时 STL: " + path.string());
    }

    char header[80] = {};
    const std::string banner = "pcsim test geometry";
    std::copy(banner.begin(), banner.end(), header);
    out.write(header, sizeof(header));

    const auto count = static_cast<std::uint32_t>(triangles.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const Triangle& tri : triangles)
    {
        // 法向量置零：VTK 读入后自行计算，体素化不依赖 STL 里的法向量
        const float values[12] = {
            0.0f, 0.0f, 0.0f,
            static_cast<float>(tri[0].x), static_cast<float>(tri[0].y), static_cast<float>(tri[0].z),
            static_cast<float>(tri[1].x), static_cast<float>(tri[1].y), static_cast<float>(tri[1].z),
            static_cast<float>(tri[2].x), static_cast<float>(tri[2].y), static_cast<float>(tri[2].z),
        };
        out.write(reinterpret_cast<const char*>(values), sizeof(values));
        const std::uint16_t attribute = 0;
        out.write(reinterpret_cast<const char*>(&attribute), sizeof(attribute));
    }
}

/// 正二十面体细分球（全部三角形、闭合、无退化面）
inline std::vector<Triangle> makeIcosphere(double radius, int subdivisions)
{
    const double t = (1.0 + std::sqrt(5.0)) / 2.0;
    std::vector<Vec3> vertices = {
        {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
        {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
        {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1},
    };
    for (Vec3& v : vertices)
    {
        v = v.normalized();
    }

    std::vector<std::array<int, 3>> faces = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1},
    };

    for (int level = 0; level < subdivisions; ++level)
    {
        std::map<std::pair<int, int>, int> midpoints;
        const auto midpoint = [&](int a, int b) {
            const std::pair<int, int> key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
            const auto found = midpoints.find(key);
            if (found != midpoints.end())
            {
                return found->second;
            }
            vertices.push_back(((vertices[a] + vertices[b]) * 0.5).normalized());
            const int index = static_cast<int>(vertices.size()) - 1;
            midpoints.emplace(key, index);
            return index;
        };

        std::vector<std::array<int, 3>> subdivided;
        subdivided.reserve(faces.size() * 4);
        for (const auto& face : faces)
        {
            const int a = midpoint(face[0], face[1]);
            const int b = midpoint(face[1], face[2]);
            const int c = midpoint(face[2], face[0]);
            subdivided.push_back({face[0], a, c});
            subdivided.push_back({face[1], b, a});
            subdivided.push_back({face[2], c, b});
            subdivided.push_back({a, b, c});
        }
        faces.swap(subdivided);
    }

    std::vector<Triangle> triangles;
    triangles.reserve(faces.size());
    for (const auto& face : faces)
    {
        triangles.push_back({vertices[face[0]] * radius,
                             vertices[face[1]] * radius,
                             vertices[face[2]] * radius});
    }
    return triangles;
}

/// 立方体（边长 side），12 个三角面，法向朝外
inline std::vector<Triangle> makeCube(double side)
{
    const double h = side * 0.5;
    const Vec3 p[8] = {
        {-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h},
        {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h},
    };
    const int quads[6][4] = {
        {0, 3, 2, 1}, // -z
        {4, 5, 6, 7}, // +z
        {0, 1, 5, 4}, // -y
        {3, 7, 6, 2}, // +y
        {0, 4, 7, 3}, // -x
        {1, 2, 6, 5}, // +x
    };

    std::vector<Triangle> triangles;
    triangles.reserve(12);
    for (const auto& quad : quads)
    {
        triangles.push_back({p[quad[0]], p[quad[1]], p[quad[2]]});
        triangles.push_back({p[quad[0]], p[quad[2]], p[quad[3]]});
    }
    return triangles;
}

} // namespace pcsim::test
