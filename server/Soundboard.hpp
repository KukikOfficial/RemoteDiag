#ifndef SOUNDBOARD_HPP
#define SOUNDBOARD_HPP

#include <string>
#include <vector>
#include <cstdint>

class Soundboard {
public:
    static void Play(uint32_t soundId);
    static void PlayCustom(const std::string& filePath);
    static std::string GetSoundStructureJson(const std::string& baseDir);
};

#endif
