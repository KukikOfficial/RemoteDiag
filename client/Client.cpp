//client\Client.cpp
#include "Client.hpp"
#include <iostream>
#include <chrono>
#include "Protocol.hpp"
#include <vector>
#include <thread>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;

#ifdef _WIN32
    #include <ws2tcpip.h>
    #include <windows.h> // <-- 1. ДОБАВИТЬ ЭТУ СТРОКУ
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

// 2. ДОБАВИТЬ СТРОКИ НАСТРОЙКИ В КОНСТРУКТОР:
Client::Client() : m_socket(-1) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    InitNetwork();
}

Client::~Client() { Disconnect(); CleanNetwork(); }

void Client::InitNetwork() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

void Client::CleanNetwork() {
#ifdef _WIN32
    WSACleanup();
#endif
}

#include <iostream>
#include <thread>
#include <chrono>

bool Client::ConnectToServer(const std::string& address, int port) {
    // 1. Создаем структуры для DNS-резолвинга домена ngrok
    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;        // Работаем с IPv4
    hints.ai_socktype = SOCK_STREAM;  // TCP-сокеты

    // Преобразуем порт в строку для getaddrinfo
    std::string portStr = std::to_string(port);

    // Разрешаем адрес (работает как для IP, так и для доменов вида 0.tcp.ngrok.io)
    if (getaddrinfo(address.c_str(), portStr.c_str(), &hints, &result) != 0) {
        return false; // Не удалось распознать адрес
    }

    // Получаем заполненную структуру sockaddr_in из результатов резолвинга
    sockaddr_in serverAddr = *(reinterpret_cast<sockaddr_in*>(result->ai_addr));

    // Освобождаем память, выделенную под результат DNS-запроса
    freeaddrinfo(result);

    // 2. Подключаем основной сокет управления
    m_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket == -1 || m_socket == INVALID_SOCKET) {
        return false;
    }

    if (connect(m_socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        if (m_socket != -1) { closesocket(m_socket); m_socket = -1; }
        return false;
    }

    // Даем операционной системе 10 миллисекунд перевести дыхание
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // 3. Подключаем выделенный сокет для стриминга экрана (на тот же адрес и порт)
    m_screen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_screen_socket == -1 || m_screen_socket == INVALID_SOCKET) {
        if (m_socket != -1) { closesocket(m_socket); m_socket = -1; }
        return false;
    }

    if (connect(m_screen_socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        if (m_socket != -1) { closesocket(m_socket); m_socket = -1; }
        if (m_screen_socket != -1) { closesocket(m_screen_socket); m_screen_socket = -1; }
        return false;
    }

    return true;
}

void Client::Disconnect() {
    if (m_socket != -1) { closesocket(m_socket); m_socket = -1; }
    if (m_screen_socket != -1) { closesocket(m_screen_socket); m_screen_socket = -1; }
}

void Client::SendPing() {
    auto start = std::chrono::high_resolution_clock::now();
    PacketHeader header{PacketType::CommandPing, 0};
    send(m_socket, reinterpret_cast<char*>(&header), sizeof(header), 0);

    PacketHeader response;
    recv(m_socket, reinterpret_cast<char*>(&response), sizeof(response), 0);
    auto end = std::chrono::high_resolution_clock::now();

    if (response.type == PacketType::ResponsePing) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        m_pingText = "Пинг: " + std::to_string(elapsed) + " мс"; // <-- Добавлено
        std::cout << "[Успех] Пинг: " << elapsed << " мс" << std::endl;
    }

}

void Client::RequestDiagnostics() {
    PacketHeader header{PacketType::CommandDiagnostics, 0};
    send(m_socket, reinterpret_cast<char*>(&header), sizeof(header), 0);

    PacketHeader response;
    recv(m_socket, reinterpret_cast<char*>(&response), sizeof(response), 0);

    // Строго проверяем совпадение типа и размера новой структуры
    if (response.type == PacketType::ResponseDiagnostics && response.payloadSize == sizeof(DiagnosticsPayload)) {
        DiagnosticsPayload payload{};
        recv(m_socket, reinterpret_cast<char*>(&payload), sizeof(DiagnosticsPayload), 0);

        // Переводим байты в гигабайты
        double totalRamGb = static_cast<double>(payload.totalRamMem) / (1024 * 1024 * 1024);
        double freeRamGb = static_cast<double>(payload.freeRamMem) / (1024 * 1024 * 1024);
        double freeDiskGb = static_cast<double>(payload.freeDiskSpace) / (1024 * 1024 * 1024);

        char cpuBuf[64];
        char ramBuf[128];
        char diskBuf[128];

        snprintf(cpuBuf, sizeof(cpuBuf), "Загрузка ЦП: %.1f %%", payload.cpuUsage);
        snprintf(ramBuf, sizeof(ramBuf), "ОЗУ: Свободно %.2f ГБ из %.2f ГБ", freeRamGb, totalRamGb);
        snprintf(diskBuf, sizeof(diskBuf), "Диск C:\\ Свободно: %.2f ГБ", freeDiskGb);

        m_diagCpuText = cpuBuf;
        m_diagRamText = ramBuf;
        m_diagDiskText = diskBuf;
    }
}

void Client::SendPlaySound(uint32_t soundId) {
    PacketHeader header{PacketType::CommandPlaySound, sizeof(PlaySoundPayload)};
    send(m_socket, reinterpret_cast<char*>(&header), sizeof(header), 0);

    PlaySoundPayload payload{soundId};
    send(m_socket, reinterpret_cast<char*>(&payload), sizeof(payload), 0);
}

bool Client::RequestScreenShot(std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight) {
    // 1. Отправляем запрос кадра
    PacketHeader header{PacketType::CommandScreenShot, 0};
    if (send(m_screen_socket, reinterpret_cast<char*>(&header), sizeof(header), 0) <= 0) {
        return false;
    }

    // 2. Читаем заголовок ответа пакета
    PacketHeader response;
    if (recv(m_screen_socket, reinterpret_cast<char*>(&response), sizeof(response), 0) <= 0) {
        return false;
    }

    // 3. Если тип пакета совпал — приступаем к разбору полезной нагрузки
    if (response.type == PacketType::ResponseScreenShot) {
        // Читаем заголовок самого скриншота (размеры экрана)
        ScreenShotHeader sh;
        if (recv(m_screen_socket, reinterpret_cast<char*>(&sh), sizeof(sh), 0) <= 0) {
            return false;
        }

        // Выделяем память под пиксели
        outPixels.resize(sh.dataSize);

        // Гарантированно дочитываем весь поток байт из сети порциями
        uint32_t totalBytesRead = 0;
        while (totalBytesRead < sh.dataSize) {
            int bytesRead = recv(m_screen_socket,
                                 reinterpret_cast<char*>(outPixels.data() + totalBytesRead),
                                 sh.dataSize - totalBytesRead, 0);
            if (bytesRead <= 0) {
                return false; // Ошибка сети или разрыв соединения
            }
            totalBytesRead += bytesRead;
        }

        // Передаем размеры наружу
        outWidth = sh.width;
        outHeight = sh.height;
        return true;
    }

    return false;
}

void Client::ScanLocalSounds() {
    m_soundCategories.clear();
    std::string baseDir = "sounds";

    namespace fs = std::filesystem;
    if (!fs::exists(baseDir)) {
        fs::create_directory(baseDir);
        fs::create_directory(baseDir + "/Memes");
        fs::create_directory(baseDir + "/Effects");
        return;
    }

    for (const auto& entry : fs::recursive_directory_iterator(baseDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".wav") {
            std::string relativePath = fs::relative(entry.path(), baseDir).string();
            // Заменяем обратные слэши Windows на прямые для единообразия
            std::replace(relativePath.begin(), relativePath.end(), '\\', '/');

            size_t pos = relativePath.find('/');
            if (pos != std::string::npos) {
                std::string category = relativePath.substr(0, pos);
                std::string filename = relativePath.substr(pos + 1);
                m_soundCategories[category].push_back(filename);
            } else {
                m_soundCategories["Без категории"].push_back(relativePath);
            }
        }
    }
}

void Client::SendAudioBytesToServer(const std::string& relativePath) {
    std::string fullPath = "sounds/" + relativePath;

    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[Ошибка] Не удалось открыть локальный файл: " << fullPath << std::endl;
        return;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> audioBuffer(fileSize);
    if (!file.read(audioBuffer.data(), fileSize)) {
        return;
    }

    PacketHeader header{PacketType::CommandPlayClientAudio, static_cast<uint32_t>(fileSize)};
    send(m_socket, reinterpret_cast<char*>(&header), sizeof(header), 0);
    send(m_socket, audioBuffer.data(), audioBuffer.size(), 0);

    std::cout << "[Успех] Файл " << relativePath << " отправлен. Размер: " << fileSize << " байт." << std::endl;
}

void Client::SendVolumeToServer(uint32_t volume) {
    PacketHeader header{PacketType::CommandSetVolume, sizeof(VolumePayload)};
    send(m_socket, reinterpret_cast<char*>(&header), sizeof(header), 0);

    VolumePayload payload{volume};
    send(m_socket, reinterpret_cast<char*>(&payload), sizeof(payload), 0);
}

void Client::SendMessageBoxToServer(const std::string& title, const std::string& message, uint32_t iconType) {
    PacketHeader header{PacketType::CommandShowMessageBox, sizeof(MessageBoxPayload)};
    send(m_socket, reinterpret_cast<char*>(&header), sizeof(header), 0);

    MessageBoxPayload payload{};
    payload.iconType = iconType;
    snprintf(payload.title, sizeof(payload.title), "%s", title.c_str());
    snprintf(payload.message, sizeof(payload.message), "%s", message.c_str());

    send(m_socket, reinterpret_cast<char*>(&payload), sizeof(payload), 0);
}

void Client::RequestAudioDevices() {
    PacketHeader header{PacketType::CommandGetAudioDevices, 0};
    send(m_socket, reinterpret_cast<char*>(&header), sizeof(header), 0);

    PacketHeader response;
    recv(m_socket, reinterpret_cast<char*>(&response), sizeof(response), 0);

    if (response.type == PacketType::ResponseAudioDevices && response.payloadSize > 0) {
        std::vector<char> buffer(response.payloadSize + 1, 0);
        recv(m_socket, buffer.data(), response.payloadSize, 0);

        std::string rawList(buffer.data());
        std::stringstream ss(rawList);
        std::string line;

        m_audioDevices.clear();
        while (std::getline(ss, line)) {
            if (!line.empty()) m_audioDevices.push_back(line);
        }
    }
}

void Client::SendSetAudioDevice(const std::string& deviceName) {
    PacketHeader header{PacketType::CommandSetAudioDevice, sizeof(SetAudioDevicePayload)};
    send(m_socket, reinterpret_cast<char*>(&header), sizeof(header), 0);

    SetAudioDevicePayload payload{};
    // Заполняем весь массив нулями перед копированием
    std::fill(std::begin(payload.deviceName), std::end(payload.deviceName), 0);

    // Копируем имя устройства в буфер
    snprintf(payload.deviceName, sizeof(payload.deviceName), "%s", deviceName.c_str());

    send(m_socket, reinterpret_cast<char*>(&payload), sizeof(payload), 0);
    std::cout << "[Клиент] Отправлена команда на смену аудиовыхода: " << deviceName << std::endl;
}

void Client::SendMouseEvent(uint32_t action, float normX, float normY) {
    PacketHeader header{PacketType::CommandMouseEvent, sizeof(MouseEventPayload)};
    send(m_socket, reinterpret_cast<char*>(&header), sizeof(header), 0);

    MouseEventPayload payload{action, normX, normY};
    send(m_socket, reinterpret_cast<char*>(&payload), sizeof(payload), 0);
}

void Client::SendKeyboardEvent(uint32_t vkCode, uint8_t isDown) {
    PacketHeader header{PacketType::CommandKeyboardEvent, sizeof(KeyboardEventPayload)};
    send(m_socket, reinterpret_cast<char*>(&header), sizeof(header), 0);

    KeyboardEventPayload payload{vkCode, isDown};
    send(m_socket, reinterpret_cast<char*>(&payload), sizeof(payload), 0);
}

void Client::SendInputBlock(uint8_t enabled) {
    PacketHeader header{PacketType::CommandSetInputBlock, sizeof(InputBlockPayload)};
    send(m_socket, reinterpret_cast<char*>(&header), sizeof(header), 0);

    InputBlockPayload payload{enabled};
    send(m_socket, reinterpret_cast<char*>(&payload), sizeof(payload), 0);
}

void Client::RequestProcessList() {
    PacketHeader header{PacketType::CommandGetProcessList, 0};
    send(m_socket, reinterpret_cast<char*>(&header), sizeof(header), 0);

    PacketHeader response;
    recv(m_socket, reinterpret_cast<char*>(&response), sizeof(response), 0);

    if (response.type == PacketType::ResponseProcessList && response.payloadSize > 0) {
        std::vector<char> buffer(response.payloadSize + 1, 0);

        // Гарантированно вычитываем длинный текстовый список процессов
        uint32_t totalRead = 0;
        while (totalRead < response.payloadSize) {
            int r = recv(m_socket, buffer.data() + totalRead, response.payloadSize - totalRead, 0);
            if (r <= 0) break;
            totalRead += r;
        }

        std::string rawData(buffer.data());
        std::stringstream ss(rawData);
        std::string line;

        m_processes.clear();
        while (std::getline(ss, line)) {
            if (line.empty()) continue;
            size_t pos = line.find('|');
            if (pos != std::string::npos) {
                uint32_t pid = std::stoul(line.substr(0, pos));
                std::string name = line.substr(pos + 1);
                m_processes.push_back({pid, name});
            }
        }
    }
}

void Client::SendKillProcess(uint32_t pid) {
    PacketHeader header{PacketType::CommandKillProcess, sizeof(KillProcessPayload)};
    send(m_socket, reinterpret_cast<char*>(&header), sizeof(header), 0);

    KillProcessPayload payload{pid};
    send(m_socket, reinterpret_cast<char*>(&payload), sizeof(payload), 0);
}

void Client::SendPowerAction(uint32_t actionType) {
    PacketHeader header{PacketType::CommandPowerAction, sizeof(PowerActionPayload)};
    send(m_socket, reinterpret_cast<char*>(&header), sizeof(header), 0);

    PowerActionPayload payload{actionType};
    send(m_socket, reinterpret_cast<char*>(&payload), sizeof(payload), 0);
}

void Client::SendStartAudioRecord(uint32_t source) {
    PacketHeader header{PacketType::CommandStartAudioRecord, sizeof(AudioRecordPayload)};
    send(m_socket, reinterpret_cast<char*>(&header), sizeof(header), 0);

    AudioRecordPayload payload{source};
    send(m_socket, reinterpret_cast<char*>(&payload), sizeof(payload), 0);
}

void Client::SendStopAudioRecord() {
    PacketHeader header{PacketType::CommandStopAudioRecord, 0};
    send(m_socket, reinterpret_cast<char*>(&header), sizeof(header), 0);
}
