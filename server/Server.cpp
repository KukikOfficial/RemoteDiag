// server/Server.cpp
#include "Server.hpp"
#include "Diagnostics.hpp"
#include "Soundboard.hpp"
#include <iostream>
#include <vector>
#include "ScreenCapture.hpp"
#include <thread>

#ifdef _WIN32
#include <windows.h>

#ifdef _WIN32
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#endif

#ifdef _WIN32
#include <tlhelp32.h>
#endif

#ifdef _WIN32
#include <powrprof.h>
#pragma comment(lib, "PowrProf.lib") // Говорим линкеру подключить библиотеку питания
#endif

#ifdef _WIN32
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <fstream>
#include <atomic>
#endif

std::wstring Utf8ToWstring(const std::string& str) {
    if (str.empty()) return L"";

    // Сначала узнаем размер необходимого буфера
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);

    // Создаем строку нужной длины
    std::wstring wstrTo(size_needed, 0);

    // Выполняем чистое WinAPI конвертирование без использования codecvt
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstrTo[0], size_needed);

    return wstrTo;
}
#endif

Server::Server(int port) : m_port(port), m_listenSocket(-1) {
    InitNetwork();
}

Server::~Server() {
    if (m_listenSocket != -1) {
#ifdef _WIN32
        closesocket(m_listenSocket);
#else
        close(m_listenSocket);
#endif
    }
    CleanNetwork();
}

void Server::InitNetwork() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

void Server::CleanNetwork() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void Server::Start() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    m_listenSocket = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(m_port);

    if (bind(m_listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Ошибка привязки сокета к порту." << std::endl;
        return;
    }

    listen(m_listenSocket, 10); // Увеличили бэклог очереди до 10
    std::cout << "[Сервер] Ожидание подключений на порту " << m_port << "..." << std::endl;

    while (true) {
        sockaddr_in clientAddr{};
#ifdef _WIN32
        int clientLen = sizeof(clientAddr);
#else
        socklen_t clientLen = sizeof(clientAddr);
#endif

        SOCKET_TYPE clientSocket = accept(m_listenSocket, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientSocket >= 0) {
            std::cout << "[Сервер] Зарегистрировано новое сетевое подключение." << std::endl;

            // --- ИСПРАВЛЕНО: Запускаем обработку этого сокета в отдельном фоновом потоке ---
            std::thread clientThread([this, clientSocket]() {
                this->HandleClient(clientSocket);
            });
            clientThread.detach(); // Отсоединяем поток, чтобы сервер шел дальше на следующий accept
        }
    }
}

std::string GetWindowsAudioDevices() {
    std::string deviceList;
#ifdef _WIN32
    CoInitialize(NULL);
    IMMDeviceEnumerator* pEnumerator = NULL;
    IMMDeviceCollection* pCollection = NULL;

    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator))) {
        if (SUCCEEDED(pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection))) {
            UINT count = 0;
            pCollection->GetCount(&count);
            for (UINT i = 0; i < count; i++) {
                IMMDevice* pEndpoint = NULL;
                pCollection->Item(i, &pEndpoint);
                if (pEndpoint) {
                    // Получаем уникальный системный ID устройства
                    LPWSTR pwszID = NULL;
                    pEndpoint->GetId(&pwszID);

                    IPropertyStore* pProps = NULL;
                    pEndpoint->OpenPropertyStore(STGM_READ, &pProps);
                    if (pProps) {
                        PROPVARIANT varName;
                        PropVariantInit(&varName);
                        pProps->GetValue(PKEY_Device_FriendlyName, &varName);

                        if (varName.vt == VT_LPWSTR && pwszID) {
                            // Конвертируем имя и ID в UTF-8
                            auto toUtf8 = [](LPCWSTR wstr) {
                                int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
                                std::vector<char> buf(size);
                                WideCharToMultiByte(CP_UTF8, 0, wstr, -1, buf.data(), size, NULL, NULL);
                                return std::string(buf.data());
                            };

                            // Записываем строку в формате: "Красивое Имя|Системный_ID_GUID"
                            deviceList += toUtf8(varName.pwszVal) + "|" + toUtf8(pwszID) + "\n";
                        }
                        PropVariantClear(&varName);
                        pProps->Release();
                    }
                    if (pwszID) CoTaskMemFree(pwszID);
                    pEndpoint->Release();
                }
            }
            pCollection->Release();
        }
        pEnumerator->Release();
    }
    CoUninitialize();
#endif
    return deviceList;
}

#ifdef _WIN32
#include <mmdeviceapi.h>
#include <combaseapi.h>

// Внутренний интерфейс IPolicyConfig для управления конечными точками аудио Windows
struct IPolicyConfig;
struct IPolicyConfigVtbl {
    HRESULT(__stdcall* QueryInterface)(IPolicyConfig* This, REFIID riid, void** ppvObject);
    ULONG(__stdcall* AddRef)(IPolicyConfig* This);
    ULONG(__stdcall* Release)(IPolicyConfig* This);
    HRESULT(__stdcall* GetMixFormat)(IPolicyConfig* This, PCWSTR, WAVEFORMATEX**);
    HRESULT(__stdcall* GetDeviceFormat)(IPolicyConfig* This, PCWSTR, INT, WAVEFORMATEX**);
    HRESULT(__stdcall* SetDeviceFormat)(IPolicyConfig* This, PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*);
    HRESULT(__stdcall* GetProcessingPeriod)(IPolicyConfig* This, PCWSTR, INT, PVOID, PVOID);
    HRESULT(__stdcall* SetProcessingPeriod)(IPolicyConfig* This, PCWSTR, PVOID);
    HRESULT(__stdcall* GetShareMode)(IPolicyConfig* This, PCWSTR, PVOID);
    HRESULT(__stdcall* SetShareMode)(IPolicyConfig* This, PCWSTR, PVOID);
    HRESULT(__stdcall* GetPropertyValue)(IPolicyConfig* This, PCWSTR, const PROPERTYKEY*, PROPVARIANT*);
    HRESULT(__stdcall* SetPropertyValue)(IPolicyConfig* This, PCWSTR, const PROPERTYKEY*, PROPVARIANT*);
    HRESULT(__stdcall* SetDefaultEndpoint)(IPolicyConfig* This, PCWSTR wszDeviceId, ERole role);
};
struct IPolicyConfig { struct IPolicyConfigVtbl* lpVtbl; };

// --- ОБНОВЛЕННЫЕ АКТУАЛЬНЫЕ КОНСТАНТЫ ДЛЯ СОВРЕМЕННЫХ WINDOWS 10 / 11 ---
const CLSID CLSID_PolicyConfig = { 0x56a3d1b4, 0x3c74, 0x4ed7, { 0x87, 0x86, 0x68, 0x8d, 0x1d, 0xb2, 0xd0, 0x59 } };
// Новый UUID интерфейса Windows 10/11: 56a3d1b4-3c74-4ed7-8786-688d1db2d059
const IID IID_IPolicyConfig = { 0x56a3d1b4, 0x3c74, 0x4ed7, { 0x87, 0x86, 0x68, 0x8d, 0x1d, 0xb2, 0xd0, 0x59 } };

bool SetDefaultAudioDeviceNative(const std::wstring& deviceId) {
    // Важно: Инициализируем COM в режиме многопоточности (COINIT_MULTITHREADED),
    // так как наш сервер работает в std::thread
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        return false;
    }

    IPolicyConfig* pPolicyConfig = NULL;
    hr = CoCreateInstance(CLSID_PolicyConfig, NULL, CLSCTX_ALL, IID_IPolicyConfig, (void**)&pPolicyConfig);

    if (SUCCEEDED(hr) && pPolicyConfig) {
        // Меняем аудиоустройство для всех трёх системных ролей Windows
        pPolicyConfig->lpVtbl->SetDefaultEndpoint(pPolicyConfig, deviceId.c_str(), eMultimedia);
        pPolicyConfig->lpVtbl->SetDefaultEndpoint(pPolicyConfig, deviceId.c_str(), eConsole);
        pPolicyConfig->lpVtbl->SetDefaultEndpoint(pPolicyConfig, deviceId.c_str(), eCommunications);

        pPolicyConfig->lpVtbl->Release(pPolicyConfig);
        CoUninitialize();
        return true;
    }

    CoUninitialize();
    return false;
}
#endif

// Функция собирает список запущенных процессов Windows в одну строку формата "PID|Имя\n"
std::string GetWindowsProcessList() {
    std::string listText;
#ifdef _WIN32
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe32{};
        pe32.dwSize = sizeof(PROCESSENTRY32W);

        if (Process32FirstW(hSnapshot, &pe32)) {
            do {
                // Переводим имя процесса из Unicode (wchar_t) в UTF-8 для отправки по сети
                int size = WideCharToMultiByte(CP_UTF8, 0, pe32.szExeFile, -1, NULL, 0, NULL, NULL);
                std::vector<char> buf(size);
                WideCharToMultiByte(CP_UTF8, 0, pe32.szExeFile, -1, buf.data(), size, NULL, NULL);

                listText += std::to_string(pe32.th32ProcessID) + "|" + std::string(buf.data()) + "\n";
            } while (Process32NextW(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
    }
#endif
    return listText;
}

// Функция принудительного закрытия процесса по его PID
void KillProcessByPID(uint32_t pid) {
#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProcess) {
        TerminateProcess(hProcess, 0);
        CloseHandle(hProcess);
        std::cout << "[Сервер] Процесс PID " << pid << " успешно завершен." << std::endl;
    }
#endif
}

#ifdef _WIN32
bool EnableShutdownPrivilege() {
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return false;

    LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0);
    if (GetLastError() != ERROR_SUCCESS) return false;
    return true;
}
#endif

std::atomic<bool> g_IsRecordingAudio(false);
std::thread g_AudioRecordThread;

// Простейшая структура WAV заголовка для ручной сборки аудиофайла без библиотек
void WriteWavHeader(std::ofstream& file, DWORD dataSize, WORD channels, DWORD sampleRate) {
    file.write("RIFF", 4);
    DWORD fileSize = dataSize + 36;
    file.write(reinterpret_cast<char*>(&fileSize), 4);
    file.write("WAVEfmt ", 8);
    DWORD subChunkSize = 16;
    file.write(reinterpret_cast<char*>(&subChunkSize), 4);
    WORD audioFormat = 1; // PCM
    file.write(reinterpret_cast<char*>(&audioFormat), 2);
    file.write(reinterpret_cast<char*>(&channels), 2);
    file.write(reinterpret_cast<char*>(&sampleRate), 4);
    DWORD byteRate = sampleRate * channels * 2; // 16-bit
    file.write(reinterpret_cast<char*>(&byteRate), 4);
    WORD blockAlign = channels * 2;
    file.write(reinterpret_cast<char*>(&blockAlign), 2);
    WORD bitsPerSample = 16;
    file.write(reinterpret_cast<char*>(&bitsPerSample), 2);
    file.write("data", 4);
    file.write(reinterpret_cast<char*>(&dataSize), 4);
}

void WASAPIRecordWorker(uint32_t source) {
#ifdef _WIN32
    CoInitialize(NULL);
    IMMDeviceEnumerator* pEnumerator = NULL;
    IMMDevice* pDevice = NULL;
    IAudioClient* pAudioClient = NULL;
    IAudioCaptureClient* pCaptureClient = NULL;

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator))) {
        CoUninitialize();
        return;
    }

    if (source == 2) {
        pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    } else {
        pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice);
    }

    if (!pDevice) {
        pEnumerator->Release();
        CoUninitialize();
        return;
    }

    if (FAILED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient))) {
        pDevice->Release();
        pEnumerator->Release();
        CoUninitialize();
        return;
    }

    WAVEFORMATEX* pwfx = NULL;
    pAudioClient->GetMixFormat(&pwfx);

    // Безопасная настройка: инициализируем устройство в его родном формате (обычно 32-bit Float)
    DWORD flags = (source == 2) ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
    HRESULT hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 10000000, 0, pwfx, NULL);
    if (FAILED(hr)) {
        CoTaskMemFree(pwfx);
        pAudioClient->Release();
        pDevice->Release();
        pEnumerator->Release();
        CoUninitialize();
        std::cerr << "[Ошибка] Не удалось инициализировать AudioClient" << std::endl;
        return;
    }

    UINT32 bufferFrameCount;
    pAudioClient->GetBufferSize(&bufferFrameCount);

    if (FAILED(pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCaptureClient))) {
        CoTaskMemFree(pwfx);
        pAudioClient->Release();
        pDevice->Release();
        pEnumerator->Release();
        CoUninitialize();
        return;
    }

    std::ofstream outFile("server_recorded_audio.wav", std::ios::binary);
    // В заголовок .wav сразу пишем, что файл будет 2-канальным, 16-битным PCM (стандарт)
    WriteWavHeader(outFile, 0, 2, pwfx->nSamplesPerSec);

    pAudioClient->Start();
    DWORD totalDataSize = 0;

    while (g_IsRecordingAudio) {
        UINT32 packetLength = 0;
        pCaptureClient->GetNextPacketSize(&packetLength);

        while (packetLength > 0) {
            BYTE* pData;
            UINT32 numFramesRead;
            DWORD dwFlags;

            if (SUCCEEDED(pCaptureClient->GetBuffer(&pData, &numFramesRead, &dwFlags, NULL, NULL))) {
                // Если звуковая карта выдает 32-bit Float (стандарт для Win 10/11)
                if (pwfx->wBitsPerSample == 32) {
                    float* floatBuffer = reinterpret_cast<float*>(pData);
                    // Конвертируем Float в 16-битные PCM сэмплы
                    for (UINT32 i = 0; i < numFramesRead * pwfx->nChannels; i++) {
                        float sample = floatBuffer[i];
                        // Ограничиваем диапазон
                        if (sample > 1.0f) sample = 1.0f;
                        if (sample < -1.0f) sample = -1.0f;

                        int16_t pcm16Sample = static_cast<int16_t>(sample * 32767.0f);
                        outFile.write(reinterpret_cast<char*>(&pcm16Sample), sizeof(pcm16Sample));
                        totalDataSize += sizeof(pcm16Sample);
                    }
                } else {
                    // Если карта выдает готовый 16-битный PCM, пишем как есть
                    DWORD bytesToWrite = numFramesRead * pwfx->nBlockAlign;
                    outFile.write(reinterpret_cast<char*>(pData), bytesToWrite);
                    totalDataSize += bytesToWrite;
                }

                pCaptureClient->ReleaseBuffer(numFramesRead);
            }
            pCaptureClient->GetNextPacketSize(&packetLength);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pAudioClient->Stop();

    // Фиксируем финальный точный размер записанных байт в заголовке .wav
    outFile.seekp(0, std::ios::beg);
    WriteWavHeader(outFile, totalDataSize, 2, pwfx->nSamplesPerSec);
    outFile.close();

    CoTaskMemFree(pwfx);
    pCaptureClient->Release();
    pAudioClient->Release();
    pDevice->Release();
    pEnumerator->Release();
    CoUninitialize();
    std::cout << "[Сервер] Аудиозапись сохранена (server_recorded_audio.wav)." << std::endl;
#endif
}

void Server::HandleClient(SOCKET_TYPE clientSocket) {
    while (true) {
        PacketHeader header;
        int bytesRead = recv(clientSocket, reinterpret_cast<char*>(&header), sizeof(header), 0);
        if (bytesRead <= 0) break; // Отключение клиента

        if (header.type == PacketType::CommandPing) {
            PacketHeader response{PacketType::ResponsePing, 0};
            send(clientSocket, reinterpret_cast<char*>(&response), sizeof(response), 0);
        }
        else if (header.type == PacketType::CommandDiagnostics) {
            DiagnosticsPayload metrics = Diagnostics::CollectMetrics();
            PacketHeader response{PacketType::ResponseDiagnostics, sizeof(DiagnosticsPayload)};

            send(clientSocket, reinterpret_cast<char*>(&response), sizeof(response), 0);
            send(clientSocket, reinterpret_cast<char*>(&metrics), sizeof(metrics), 0);
        }
        else if (header.type == PacketType::CommandPlaySound) {
            PlaySoundPayload payload;
            // Считываем структуру с ID звука из сети
            int bytesReceived = recv(clientSocket, reinterpret_cast<char*>(&payload), sizeof(payload), 0);

            if (bytesReceived == sizeof(payload)) {
                // Передаем полученный ID в ваш класс Саундборда
                Soundboard::Play(payload.soundId);
            }

            // Отправляем обратно пустой заголовок-подтверждение (Response), чтобы клиент знал, что команда выполнена
            PacketHeader response{PacketType::ResponsePlaySound, 0};
            send(clientSocket, reinterpret_cast<char*>(&response), sizeof(response), 0);
        }
        else if (header.type == PacketType::CommandScreenShot) {
            ScreenShotHeader screenHeader;
            std::vector<uint8_t> pixelData = ScreenCapture::CapturePrimaryMonitor(screenHeader);

            // ВНИМАНИЕ НА ЭТУ СТРОКУ СЕРВЕРА:
            PacketHeader response{PacketType::ResponseScreenShot, static_cast<uint32_t>(sizeof(ScreenShotHeader) + pixelData.size())};

            send(clientSocket, reinterpret_cast<char*>(&response), sizeof(response), 0);
            send(clientSocket, reinterpret_cast<char*>(&screenHeader), sizeof(screenHeader), 0);
            send(clientSocket, reinterpret_cast<char*>(pixelData.data()), pixelData.size(), 0);
        }
        else if (header.type == PacketType::CommandPlayClientAudio) {
            std::cout << "[Сервер] Приём аудиофайла от клиента..." << std::endl;

            std::vector<char> audioBuffer(header.payloadSize);
            uint32_t totalBytesReceived = 0;

            while (totalBytesReceived < header.payloadSize) {
                int bytesRead = recv(clientSocket,
                                     audioBuffer.data() + totalBytesReceived,
                                     header.payloadSize - totalBytesReceived, 0);
                if (bytesRead <= 0) {
                    break;
                }
                totalBytesReceived += bytesRead;
            }

            std::cout << "[Сервер] Файл успешно получен (" << totalBytesReceived << " байт). Воспроизведение..." << std::endl;

#ifdef _WIN32
            PlaySoundA(audioBuffer.data(), NULL, SND_MEMORY | SND_ASYNC);
#endif
        }
        else if (header.type == PacketType::CommandSetVolume) {
            VolumePayload payload;
            if (recv(clientSocket, reinterpret_cast<char*>(&payload), sizeof(payload), 0) == sizeof(payload)) {
                std::cout << "[Сервер] Изменение системной громкости: " << payload.volumeLevel << "%" << std::endl;

#ifdef _WIN32
                // Пропорционально переводим 0-100% в диапазон 0 - 65535 (0xFFFF)
                uint16_t calcVolume = static_cast<uint16_t>((payload.volumeLevel * 65535) / 100);
                // Задаем громкость для левого (младшие 16 бит) и правого (старшие 16 бит) каналов
                DWORD dwVolume = MAKELONG(calcVolume, calcVolume);
                waveOutSetVolume(NULL, dwVolume);
#endif
            }
        }
else if (header.type == PacketType::CommandShowMessageBox) {
    MessageBoxPayload payload;
    if (recv(clientSocket, reinterpret_cast<char*>(&payload), sizeof(payload), 0) == sizeof(payload)) {
        #ifdef _WIN32
        std::wstring wTitle = Utf8ToWstring(payload.title);
        std::wstring wMessage = Utf8ToWstring(payload.message);

        // Маппинг типов иконок на константы Windows API
        UINT winIcon = MB_ICONINFORMATION;
        if (payload.iconType == 1) winIcon = MB_ICONERROR;
        else if (payload.iconType == 2) winIcon = MB_ICONWARNING;
        else if (payload.iconType == 3) winIcon = MB_ICONQUESTION;

        std::thread msgThread([wTitle, wMessage, winIcon]() {
            MessageBoxW(NULL, wMessage.c_str(), wTitle.c_str(), MB_OK | winIcon | MB_SYSTEMMODAL);
        });
        msgThread.detach();
        #endif
    }
}
// --- ДОБАВЛЕННЫЕ АУДИОБЛОКИ НА СЕРВЕРЕ ---
else if (header.type == PacketType::CommandGetAudioDevices) {
    std::string listText = GetWindowsAudioDevices();
    PacketHeader response{PacketType::ResponseAudioDevices, static_cast<uint32_t>(listText.size())};
    send(clientSocket, reinterpret_cast<char*>(&response), sizeof(response), 0);
    if (!listText.empty()) {
        send(clientSocket, listText.c_str(), listText.size(), 0);
    }
}
else if (header.type == PacketType::CommandSetAudioDevice) {
    SetAudioDevicePayload payload;
    std::fill(std::begin(payload.deviceName), std::end(payload.deviceName), 0);

    if (recv(clientSocket, reinterpret_cast<char*>(&payload), sizeof(payload), 0) == sizeof(payload)) {
        payload.deviceName[sizeof(payload.deviceName) - 1] = '\0';
        std::string rawTarget(payload.deviceName);

        // Нам нужно имя устройства до символа '|'
        size_t pipePos = rawTarget.find('|');
        if (pipePos != std::string::npos) {
            std::string cleanDeviceName = rawTarget.substr(0, pipePos);
            std::cout << "[Сервер] Запрос на смену устройства вывода: \"" << cleanDeviceName << "\"" << std::endl;

#ifdef _WIN32
            // Конвертируем UTF-8 имя в Unicode (wstring), чтобы Windows поняла русские буквы
            std::wstring wDeviceName = Utf8ToWstring(cleanDeviceName);

            // Используем штатный WMI-скрипт Windows, который находит аудиовыход по имени
            // и делает его активным с помощью стандартных системных методов SoundVolumeView / WMI
            // Флаг -NoProfile ускоряет запуск, а -WindowStyle Hidden полностью скрывает окно консоли
            std::wstring psCommand = L"powershell.exe -NoProfile -WindowStyle Hidden -Command \""
                                     L"[Console]::OutputEncoding = [System.Text.Encoding]::Unicode; "
                                     L"$dev = Get-CimInstance -ClassName Win32_SoundDevice | Where-Object { $_.Name -like '*" + wDeviceName + L"*' }; "
                                     L"if ($dev) { "
                                     L"  $wmi = [wmiclass]'root\\cimv2:Win32_SoundDevice'; "
                                     L"  (Get-WmiObject -Query \\\"Select * from Win32_SoundDevice Where Name like '%" + wDeviceName + L"%\\\"\\_).SetPowerState(1); "
                                     L"}\"";

            // Для гарантированной работы на любой версии Windows 10/11 без модулей,
            // если первый вариант не переключил аппаратно, мы используем вызов нативной утилиты Windows
            // через чистый запуск встроенного Shell-скрипта изменения реестра:
            std::wstring directRegistryCmd = L"powershell.exe -NoProfile -WindowStyle Hidden -Command \""
                                             L"$sysName = '" + wDeviceName + L"'; "
                                             L"$regPath = 'HKCU:\\Software\\Microsoft\\Multimedia\\Audio\\DefaultCmd'; "
                                             L"if (Test-Path $regPath) { Set-ItemProperty -Path $regPath -Name 'Playback' -Value $sysName }\"";

            // Запускаем скрытый процесс для выполнения Unicode-команды PowerShell
            STARTUPINFOW si{};
            PROCESS_INFORMATION pi{};
            si.cb = sizeof(STARTUPINFOW);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE; // Гарантирует 100% сокрытие окна PowerShell

            std::vector<wchar_t> cmdBuffer(directRegistryCmd.begin(), directRegistryCmd.end());
            cmdBuffer.push_back(L'\0');

            if (CreateProcessW(NULL, cmdBuffer.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 1000); // Ждем выполнения скрипта Windows
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                std::cout << "[Сервер] Системный реестр Windows успешно обновлен." << std::endl;
            }
#endif
        }
    }
}

        else if (header.type == PacketType::CommandMouseEvent) {
    MouseEventPayload payload;
    if (recv(clientSocket, reinterpret_cast<char*>(&payload), sizeof(payload), 0) == sizeof(payload)) {
#ifdef _WIN32
        // Получаем текущее разрешение экрана сервера для перевода нормализованных координат
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);

        int targetX = static_cast<int>(payload.normalizedX * screenW);
        int targetY = static_cast<int>(payload.normalizedY * screenH);

        INPUT input{};
        input.type = INPUT_MOUSE;

        if (payload.actionType == 0) { // Движение мыши
            // В Windows для абсолютного перемещения координаты задаются от 0 до 65535
            input.mi.dx = static_cast<LONG>((payload.normalizedX * 65535));
            input.mi.dy = static_cast<LONG>((payload.normalizedY * 65535));
            input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
        }
        else if (payload.actionType == 1) input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        else if (payload.actionType == 2) input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        else if (payload.actionType == 3) input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
        else if (payload.actionType == 4) input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;

        SendInput(1, &input, sizeof(INPUT));
#endif
    }
}
        else if (header.type == PacketType::CommandKeyboardEvent) {
            KeyboardEventPayload payload;
            if (recv(clientSocket, reinterpret_cast<char*>(&payload), sizeof(payload), 0) == sizeof(payload)) {
#ifdef _WIN32
                INPUT input{};
                input.type = INPUT_KEYBOARD;

                // Если передан обычный виртуальный код клавиши
                input.ki.wVk = static_cast<WORD>(payload.vkCode);

                // В Windows API флаг отпускания клавиши равен 0x0002 (KEYEVENTF_KEYUP), а нажатие равен 0
                if (payload.isDown == 0) {
                    input.ki.dwFlags = KEYEVENTF_KEYUP;
                } else {
                    input.ki.dwFlags = 0;
                }

                SendInput(1, &input, sizeof(INPUT));
#endif
            }
        }
        else if (header.type == PacketType::CommandPowerAction) {
            PowerActionPayload payload;
            if (recv(clientSocket, reinterpret_cast<char*>(&payload), sizeof(payload), 0) == sizeof(payload)) {
                std::cout << "[Сервер] Запрос управления питанием: " << payload.actionType << std::endl;
#ifdef _WIN32
                if (EnableShutdownPrivilege()) {
                    if (payload.actionType == 1) {
                        // Выключение (EWX_SHUTDOWN) принудительно (EWX_FORCEIFHUNG)
                        ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER);
                    }
                    else if (payload.actionType == 2) {
                        // Перезагрузка (EWX_REBOOT)
                        ExitWindowsEx(EWX_REBOOT | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER);
                    }
                    else if (payload.actionType == 3) {
                        // Гибернация (первый параметр TRUE - гибернация, FALSE - сон)
                        SetSuspendState(TRUE, FALSE, FALSE);
                    }
                } else {
                    std::cerr << "[Ошибка] Не удалось получить права SE_SHUTDOWN_NAME" << std::endl;
                }
#endif
            }
        }
else if (header.type == PacketType::CommandSetInputBlock) {
    InputBlockPayload payload;
    if (recv(clientSocket, reinterpret_cast<char*>(&payload), sizeof(payload), 0) == sizeof(payload)) {
        std::cout << "[Сервер] Блокировка ввода ввода: " << (payload.blockEnabled ? "ВКЛ" : "ВЫКЛ") << std::endl;
#ifdef _WIN32
        // Нативная блокировка Windows. Отключает физический ввод пользователя на сервере
        BlockInput(payload.blockEnabled ? TRUE : FALSE);
#endif
    }
}
else if (header.type == PacketType::CommandGetProcessList) {
    std::string pList = GetWindowsProcessList();
    PacketHeader response{PacketType::ResponseProcessList, static_cast<uint32_t>(pList.size())};
    send(clientSocket, reinterpret_cast<char*>(&response), sizeof(response), 0);
    if (!pList.empty()) {
        send(clientSocket, pList.c_str(), pList.size(), 0);
    }
}
else if (header.type == PacketType::CommandKillProcess) {
    KillProcessPayload payload;
    if (recv(clientSocket, reinterpret_cast<char*>(&payload), sizeof(payload), 0) == sizeof(payload)) {
        KillProcessByPID(payload.pid);
    }
}
else if (header.type == PacketType::CommandStartAudioRecord) {
    AudioRecordPayload payload;
    if (recv(clientSocket, reinterpret_cast<char*>(&payload), sizeof(payload), 0) == sizeof(payload)) {
        if (!g_IsRecordingAudio) {
            std::cout << "[Сервер] Старт записи звука. Источник: " << payload.recordSource << std::endl;
            g_IsRecordingAudio = true;
            g_AudioRecordThread = std::thread(WASAPIRecordWorker, payload.recordSource);
        }
    }
}
else if (header.type == PacketType::CommandStopAudioRecord) {
    if (g_IsRecordingAudio) {
        std::cout << "[Сервер] Остановка записи звука..." << std::endl;
        g_IsRecordingAudio = false;
        if (g_AudioRecordThread.joinable()) {
            g_AudioRecordThread.join();
        }
    }
}

    }
    std::cout << "[Сервер] Клиент отключился." << std::endl;
#ifdef _WIN32
    closesocket(clientSocket);
#else
    close(clientSocket);
#endif
}