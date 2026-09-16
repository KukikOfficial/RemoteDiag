// server/ScreenCapture.hpp
#pragma once
#include <vector>
#include <cstdint>
#include "../common/Protocol.hpp"

class ScreenCapture {
public:
    static std::vector<uint8_t> CapturePrimaryMonitor(ScreenShotHeader& outHeader);
};