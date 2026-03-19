#include "ply_loader.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> Split(const std::string& s) {
    std::istringstream iss(s);
    std::vector<std::string> out;
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

} // namespace

bool LoadAsciiPlyFromMemory(const char* data, size_t size, PlyCloud& out, std::string& err) {
    std::string content(data, size);
    std::istringstream iss(content);

    std::string line;
    if (!std::getline(iss, line) || line != "ply") {
        err = "Not a PLY file";
        return false;
    }

    bool ascii = false;
    int vertexCount = 0;
    std::vector<std::string> props;

    while (std::getline(iss, line)) {
        if (line == "end_header") break;
        auto t = Split(line);
        if (t.size() >= 3 && t[0] == "format" && t[1] == "ascii") ascii = true;
        if (t.size() >= 3 && t[0] == "element" && t[1] == "vertex") vertexCount = std::stoi(t[2]);
        if (t.size() >= 3 && t[0] == "property") props.push_back(t.back());
    }

    if (!ascii) {
        err = "Only ASCII PLY supported in this demo";
        return false;
    }
    if (vertexCount <= 0) {
        err = "No vertex element found";
        return false;
    }

    auto findProp = [&](const char* name) {
        auto it = std::find(props.begin(), props.end(), std::string(name));
        return it == props.end() ? -1 : static_cast<int>(std::distance(props.begin(), it));
    };

    const int ix = findProp("x");
    const int iy = findProp("y");
    const int iz = findProp("z");
    if (ix < 0 || iy < 0 || iz < 0) {
        err = "Need x/y/z properties";
        return false;
    }

    const int ir = std::max(findProp("red"), findProp("r"));
    const int ig = std::max(findProp("green"), findProp("g"));
    const int ib = std::max(findProp("blue"), findProp("b"));
    const int is = std::max(findProp("scale"), findProp("scaling"));
    const int io = std::max(findProp("opacity"), findProp("alpha"));

    out.points.clear();
    out.points.reserve(vertexCount);

    for (int i = 0; i < vertexCount && std::getline(iss, line); ++i) {
        if (line.empty()) { --i; continue; }
        auto t = Split(line);
        if ((int)t.size() < (int)props.size()) continue;

        GaussianPoint p{};
        p.x = std::stof(t[ix]);
        p.y = std::stof(t[iy]);
        p.z = std::stof(t[iz]);

        if (ir >= 0) p.r = std::stof(t[ir]) / (std::stof(t[ir]) > 1.0f ? 255.0f : 1.0f);
        if (ig >= 0) p.g = std::stof(t[ig]) / (std::stof(t[ig]) > 1.0f ? 255.0f : 1.0f);
        if (ib >= 0) p.b = std::stof(t[ib]) / (std::stof(t[ib]) > 1.0f ? 255.0f : 1.0f);
        if (is >= 0) p.scale = std::max(1e-4f, std::stof(t[is]));
        if (io >= 0) p.opacity = std::clamp(std::stof(t[io]), 0.0f, 1.0f);
        out.points.push_back(p);
    }

    if (out.points.empty()) {
        err = "Parsed zero points";
        return false;
    }
    return true;
}
