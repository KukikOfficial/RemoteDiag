#pragma once
#include <cstdint>

enum class PacketType : uint16_t {
    CommandPing = 1,
    ResponsePing,
    CommandDiagnostics,
    ResponseDiagnostics,
    CommandPlaySound,
    ResponsePlaySound,
    CommandScreenShot,
    ResponseScreenShot,
    CommandPlayClientAudio = 12,
    CommandSetVolume = 13,    // Изменение громкости
    CommandShowMessageBox = 14, // Вызов MessageBox
    CommandGetAudioDevices = 15, // Запрос списка динамиков
    ResponseAudioDevices = 16,    // Ответ со списком динамиков (сырой текст с разделением '\n')
    CommandSetAudioDevice = 17,    // Команда на переключение устройства
    CommandMouseEvent = 18,      // Передача движения/клика мыши
    CommandKeyboardEvent = 19,  // Передача нажатия клавиши
    CommandSetInputBlock = 20,   // Включение/выключение блокировки ввода
    CommandGetProcessList = 21,  // Запрос списка процессов
    ResponseProcessList = 22,    // Ответ со списком процессов
    CommandKillProcess = 23,      // Команда "убить" процесс по PID
    CommandPowerAction = 24, // Команда управления питанием (выключение, рестарт, сон)
    CommandStartAudioRecord = 25, // Начать запись (payload укажет источник)
    CommandStopAudioRecord = 26   // Остановить запись и сохранить файл
};

#pragma pack(push, 1)

struct PacketHeader {
    PacketType type;
    uint32_t payloadSize; // Здесь будет передаваться точный размер .wav файла в байтах
};

struct DiagnosticsPayload {
    uint64_t totalRamMem;
    uint64_t freeRamMem;
    uint64_t freeDiskSpace;
    double cpuUsage;
    char processList[256];
};

struct PlaySoundPayload {
    uint32_t soundId;
};

struct ScreenShotHeader {
    uint32_t width;
    uint32_t height;
    uint32_t dataSize;
};

// Структура для изменения громкости
struct VolumePayload {
    uint32_t volumeLevel; // От 0 до 100
};

// Структура для отправки сообщения
struct MessageBoxPayload {
    uint32_t iconType;
    char title[128];   // Заголовок окна
    char message[512]; // Текст сообщения
};

// Структура для переключения устройства
struct SetAudioDevicePayload {
    char deviceName[256]; // Имя целевого аудиоустройства
};

// Структура для передачи событий мыши
struct MouseEventPayload {
    uint32_t actionType; // 0 - движение, 1 - левая кнопка вниз, 2 - левая вверх, 3 - правая вниз, 4 - правая вверх
    float normalizedX;   // Координата X от 0.0 до 1.0 относительно окна
    float normalizedY;   // Координата Y от 0.0 до 1.0 относительно окна
};

// Структура для передачи клавиш клавиатуры
struct KeyboardEventPayload {
    uint32_t vkCode; // Виртуальный код клавиши Windows (Virtual Key)
    uint8_t isDown;  // 1 - нажата, 0 - отпущена
};

// Структура блокировки ввода
struct InputBlockPayload {
    uint8_t blockEnabled; // 1 - заблокировать, 0 - разблокировать
};

// Структура уничтожения процесса
struct KillProcessPayload {
    uint32_t pid; // ID процесса для закрытия
};

// Структура для передачи команды питания
struct PowerActionPayload {
    uint32_t actionType; // 1 - Выключение, 2 - Перезагрузка, 3 - Гибернация
};

// Структура управления аудиозахватом
struct AudioRecordPayload {
    uint32_t recordSource; // 1 - Только Микрофон, 2 - Только Экран (Звук ПК), 3 - Микрофон + Звук ПК вместе
};

#pragma pack(pop)
