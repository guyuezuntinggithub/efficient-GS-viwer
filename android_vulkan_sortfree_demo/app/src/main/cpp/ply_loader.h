#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct GaussianPoint {
    float x{}, y{}, z{};
    float r{1.f}, g{1.f}, b{1.f};
    float scale{0.02f};
    float opacity{0.8f};
};

struct PlyCloud {
    std::vector<GaussianPoint> points;
};

bool LoadAsciiPlyFromMemory(const char* data, size_t size, PlyCloud& out, std::string& err);
