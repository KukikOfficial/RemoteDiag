#include "Soundboard.hpp"
#include <iostream>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
// Говорим линкеру подключить системную библиотеку для работы со звуком WinMM
#pragma comment(lib, "winmm.lib")
#endif

namespace fs = std::filesystem;

// Сканирует папку sounds/ и собирает дерево файлов в одну строку, разделенную символами '\n'
// Формат: "ИмяПапки/ИмяФайла.wav\nИмяПапки2/Файл.wav\n"
std::string Soundboard::GetSoundStructureJson(const std::string& baseDir) {
    std::string result;
    if (!fs::exists(baseDir)) {
        fs::create_directory(baseDir);
        // Создадим для примера тестовые папки, если их нет
        fs::create_directory(baseDir + "/Meme");
        fs::create_directory(baseDir + "/Alerts");
        return result;
    }

    for (const auto& entry : fs::recursive_directory_iterator(baseDir)) {
        if (entry.is_regular_file()) {
            // Получаем относительный путь (например, "Meme/tada.wav")
            std::string relativePath = fs::relative(entry.path(), baseDir).string();
            result += relativePath + "\n";
        }
    }
    return result;
}

void Soundboard::PlayCustom(const std::string& filePath) {
    std::string fullPath = "sounds/" + filePath;
    std::cout << "[Soundboard] Воспроизведение файла: " << fullPath << std::endl;

#ifdef _WIN32
    // SND_FILENAME - играем из файла, SND_ASYNC - поток не замерзает во время воспроизведения
    PlaySoundA(fullPath.c_str(), NULL, SND_FILENAME | SND_ASYNC);
#else
    std::cout << "\a[Linux PlayCustom Sound: " << fullPath << "]\n";
#endif
}
void Soundboard::Play(uint32_t soundId) {
    std::cout << "[Soundboard] Воспроизведение звука ID: " << soundId << std::endl;

    if (soundId == 1) {
#ifdef _WIN32
        Beep(523, 300); // Нота До (C5)
#else
        std::cout << "\a[BEEP 1 (523Hz)]\n"; // Терминальный звуковой сигнал
#endif
    } else if (soundId == 2) {
#ifdef _WIN32
        Beep(659, 300); // Нота Ми (E5)
#else
        std::cout << "\a[BEEP 2 (659Hz)]\n";
#endif
    } else {
        // Простая секвенция
        for(int f : {440, 494, 523}) {
#ifdef _WIN32
            Beep(f, 150);
#else
            std::cout << "\a[" << f << "Hz]\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
#endif
        }
    }
}