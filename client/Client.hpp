// client/Client.hpp
#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include "../common/Protocol.hpp"
#include "vector"
#include <map>

#ifdef _WIN32
    #include <winsock2.h>
    using SOCKET_TYPE = SOCKET;
#else
using SOCKET_TYPE = int;
#endif

class Client {
public:
    Client();
    ~Client();
    bool ConnectToServer(const std::string& ip, int port);

    void Disconnect();
    void SendPing();
    void RequestDiagnostics();
    void SendPlaySound(uint32_t soundId);
    void RequestSoundList();
    void SendPlayCustomSound(const std::string& relativePath);
    void ScanLocalSounds(); // Сканирует локальную папку клиента
    void SendAudioBytesToServer(const std::string& relativePath);
    void SendVolumeToServer(uint32_t volume);
    void SendMessageBoxToServer(const std::string& title, const std::string& message, uint32_t iconType);
    void RequestAudioDevices();
    void SendSetAudioDevice(const std::string& deviceName);
    void SendMouseEvent(uint32_t action, float normX, float normY);
    void SendKeyboardEvent(uint32_t vkCode, uint8_t isDown);
    void SendInputBlock(uint8_t enabled);
    void RequestProcessList();
    void SendKillProcess(uint32_t pid);
    void SendPowerAction(uint32_t actionType);
    void SendStartAudioRecord(uint32_t source);
    void SendStopAudioRecord();

    SOCKET_TYPE GetSocket() const { return m_socket; }
    SOCKET_TYPE GetScreenSocket() const { return m_screen_socket; }

    bool RequestScreenShot(std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight);

    std::string m_diagCpuText = "ЦП: нет данных";
    std::string m_diagRamText = "ОЗУ: нет данных";
    std::string m_diagDiskText = "Диск: нет данных";
    std::string m_pingText = "Пинг: -- мс";
    std::map<std::string, std::vector<std::string>> m_soundCategories;
    std::vector<std::string> m_audioDevices;
    // Хранилище процессов: пара <PID, Имя процесса>
    std::vector<std::pair<uint32_t, std::string>> m_processes;

private:
    SOCKET_TYPE m_socket;
    SOCKET_TYPE m_screen_socket;
    void InitNetwork();
    void CleanNetwork();
};
#endif
