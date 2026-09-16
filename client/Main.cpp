#include <iostream>
#include <vector>
#include "Protocol.hpp"
#include <thread>
#include <atomic>
#include <cmath>
// Обязательно для Windows перед любыми сетевыми вызовами send/recv
#include <winsock2.h>

#include "Client.hpp"

// Подключаем заголовочные файлы ImGui и GLFW из vcpkg
#include <imgui.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>
#define GL_BGRA 0x80E1

// Переменные для текстуры экрана удаленного ПК
GLuint g_ScreenTexture = 0;
int g_ScreenWidth = 0;
int g_ScreenHeight = 0;
std::vector<uint8_t> g_ScreenPixels;
std::atomic<bool> g_ScreenThreadRunning(false);
std::vector<uint8_t> g_ThreadPixelBuffer; // Временный буфер для потока
std::atomic<bool> g_NewFrameAvailable(false);

// Функция обновления текстуры на GPU
void UpdateScreenTexture(int width, int height, const uint8_t* data) {
    if (width <= 0 || height <= 0 || data == nullptr) return;

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

    const GLenum formatBGRA = 0x80E1;

    // Создаём текстуру только один раз при первом кадре
    if (g_ScreenTexture == 0) {
        glGenTextures(1, &g_ScreenTexture);
        glBindTexture(GL_TEXTURE_2D, g_ScreenTexture);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

        // Выделяем пустую память под текстуру нужного размера
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, formatBGRA, GL_UNSIGNED_BYTE, nullptr);
    } else {
        glBindTexture(GL_TEXTURE_2D, g_ScreenTexture);
    }

    // Быстро перезаписываем существующие пиксели новыми данными из сети
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, formatBGRA, GL_UNSIGNED_BYTE, data);

    glBindTexture(GL_TEXTURE_2D, 0);
}

void ScreenReceiverWorker(Client* client) {
    int width = 0, height = 0;
    std::vector<uint8_t> localBuffer;

    while (g_ScreenThreadRunning) {
        if (client->GetScreenSocket() != -1) {
            // Вызываем наш обновленный метод
            if (client->RequestScreenShot(localBuffer, width, height)) {
                g_ThreadPixelBuffer = localBuffer;
                g_ScreenWidth = width;
                g_ScreenHeight = height;
                g_NewFrameAvailable = true; // Сигнализируем основному циклу обновить текстуру
            } else {
                // Если произошел сбой чтения, даем сокету отдохнуть
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        // Задержка кадровой частоты (30 FPS)
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
}

int main() {
    // 1. Инициализация графического окна GLFW
    if (!glfwInit()) return -1;
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Панель удаленного доступа", NULL, NULL);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Вертикальная синхронизация

    // 2. Инициализация контекста ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImFontConfig config;
    config.MergeMode = false;

    // Загружаем шрифт Arial из папки Windows Fonts с диапазоном кириллических символов
    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 16.0f, &config, io.Fonts->GetGlyphRangesCyrillic());
    if (font == NULL) {
        std::cout << "Не удалось загрузить русский шрифт, используется стандартный." << std::endl;
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // 3. Подключение к нашему серверу
    Client client;
    bool isConnected = false;
    //bool isConnected = client.ConnectToServer("127.0.0.1", 8080);

    // Флаг, чтобы поток не запускался дважды
    bool isThreadStarted = false;
    std::thread screenThread;
    // if (isConnected) {
    //     g_ScreenThreadRunning = true;
    //     std::thread screenThread(ScreenReceiverWorker, &client);
    //     screenThread.detach(); // Отсоединяем поток, чтобы он работал независимо
    // }


    // Буферы для вывода данных диагностики на форму
    std::string diagText = "Нажмите 'Запустить диагностику'";

    // Главный графический цикл панели управления
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Старт кадра ImGui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (g_NewFrameAvailable) {
            UpdateScreenTexture(g_ScreenWidth, g_ScreenHeight, g_ThreadPixelBuffer.data());
            g_NewFrameAvailable = false;
        }

        // --- ОКНО 1: ПАНЕЛЬ УПРАВЛЕНИЯ КОМАНДАМИ ---
        ImGui::Begin("Главная панель управления");

        ImGui::Text("Статус подключения: %s", isConnected ? "ПОДКЛЮЧЕНО" : "ОТКЛЮЧЕНО");

        if (!isConnected && ImGui::Button("Подключиться к серверу")) {
            isConnected = client.ConnectToServer("127.0.0.1", 8080);

            client.ScanLocalSounds(); // Клиент сам находит свои звуки на диске

            if (isConnected && !isThreadStarted) {
                g_ScreenThreadRunning = true;
                // Запускаем поток БЕЗ .detach(), сохраним его управляемым
                screenThread = std::thread(ScreenReceiverWorker, &client);
                isThreadStarted = true;
            }
        }

        ImGui::Separator();

        if (ImGui::Button("Проверить Пинг", ImVec2(200, 30))) {
            // Код вызова ping
            client.SendPing();
        }
        ImGui::Text("%s", client.m_pingText.c_str());

        ImGui::Separator();
        ImGui::Text("Удаленное воспроизведение звука (Soundboard):");

        // Делаем кнопки активными только если есть подключение к серверу
        if (!isConnected) {
            ImGui::BeginDisabled(); // Отключаем кнопки, если сервер оффлайн
        }

        if (ImGui::Button("Воспроизвести Звук 1 (Нота До)", ImVec2(250, 30))) {
            client.SendPlaySound(1);
        }
        ImGui::SetItemTooltip("Отправляет команду на сервер: писк Beep 523 Гц");

        if (ImGui::Button("Воспроизвести Звук 2 (Нота Ми)", ImVec2(250, 30))) {
            client.SendPlaySound(2);
        }
        ImGui::SetItemTooltip("Отправляет команду на сервер: писк Beep 659 Гц");

        // Сдвигаем следующую кнопку чуть ниже для акцента
        ImGui::Spacing();

        if (ImGui::Button("Запустить Секвенцию (Мелодия)", ImVec2(250, 30))) {
            client.SendPlaySound(3); // Любой ID, отличный от 1 и 2, запустит цикл нот
        }
        ImGui::SetItemTooltip("Воспроизводит последовательность нот Ля-Си-До");

        if (!isConnected) {
            ImGui::EndDisabled();
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Подключитесь к серверу для управления звуком.");
        }

        ImGui::Separator();

        if (ImGui::Button("Запустить диагностику", ImVec2(200, 30))) {
            client.RequestDiagnostics();
            diagText = "Данные успешно обновлены!";
        }

        ImGui::TextWrapped("%s", diagText.c_str());

        ImGui::Separator();
        ImGui::Text("Показания датчиков сервера:");

        // Выводим обновляемые данные из класса клиента
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", client.m_diagCpuText.c_str());
        ImGui::Text("%s", client.m_diagRamText.c_str());
        ImGui::Text("%s", client.m_diagDiskText.c_str());
        ImGui::Separator();
        ImGui::Text("Управление громкостью удаленного ПК:");

        // Статическая переменная для хранения текущего значения ползунка
        static int currentVolume = 50;
        if (!isConnected) ImGui::BeginDisabled();

        // Если пользователь двигает ползунок, отправляем новую громкость на сервер
        if (ImGui::SliderInt("Громкость %", &currentVolume, 0, 100)) {
            client.SendVolumeToServer(static_cast<uint32_t>(currentVolume));
        }

        if (!isConnected) ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::Text("Отправка уведомлений (MessageBox):");

        static int selectedIcon = 0;
        const char* icons[] = { "Информация (Info)", "Ошибка (Error)", "Предупреждение (Warning)", "Вопрос (Question)" };

        static char msgTitle[64] = "Внимание!";
        static char msgText[256] = "Ваш компьютер находится под удаленным контролем.";

        if (!isConnected) ImGui::BeginDisabled();

        ImGui::InputText("Заголовок окна", msgTitle, IM_ARRAYSIZE(msgTitle));
        ImGui::InputTextMultiline("Текст сообщения", msgText, IM_ARRAYSIZE(msgText), ImVec2(0, 45));

        // Теперь компилятор видит selectedIcon, когда отрисовывает выпадающий список
        ImGui::Combo("Иконка окна", &selectedIcon, icons, IM_ARRAYSIZE(icons));

        if (ImGui::Button("Отправить на экран ПК", ImVec2(-1, 30))) {
            // Теперь здесь переменная гарантированно определена и известна компилятору
            client.SendMessageBoxToServer(msgTitle, msgText, static_cast<uint32_t>(selectedIcon));
        }

        if (!isConnected) ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::Text("Устройства вывода звука удаленного ПК:");

        if (!isConnected) ImGui::BeginDisabled();

        if (ImGui::Button("Обновить список динамиков", ImVec2(-1, 25))) {
            client.RequestAudioDevices();
        }

        static int selectedDeviceIdx = 0;
        if (!client.m_audioDevices.empty()) {
            std::vector<std::string> cleanNames;
            std::vector<const char*> devicePtrs;

            // Обрезаем ID для красивого отображения в интерфейсе ImGui
            for (const auto& rawName : client.m_audioDevices) {
                size_t pos = rawName.find('|');
                if (pos != std::string::npos) {
                    cleanNames.push_back(rawName.substr(0, pos));
                } else {
                    cleanNames.push_back(rawName);
                }
                devicePtrs.push_back(cleanNames.back().c_str());
            }

            if (ImGui::Combo("Выбрать выход", &selectedDeviceIdx, devicePtrs.data(), static_cast<int>(devicePtrs.size()))) {
                // Шлём серверу полную строку "Имя|ID" - сервер сам разберется
                client.SendSetAudioDevice(client.m_audioDevices[selectedDeviceIdx]);
            }
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Нажмите кнопку обновления для загрузки устройств.");
        }

        if (!isConnected) ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::Text("Безопасность удаленного ПК:");

        static bool isInputBlocked = false;
        if (!isConnected) ImGui::BeginDisabled();

        // Кнопка переключения триггера блокировки физической мыши/клавиатуры сервера
        if (ImGui::Checkbox("Заблокировать ввод пользователя на сервере", &isInputBlocked)) {
            client.SendInputBlock(isInputBlocked ? 1 : 0);
        }

        if (!isConnected) ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::Text("Состояние удаленного ПК (Питание):");

        if (!isConnected) ImGui::BeginDisabled();

        // Кнопка выключения с предупреждающим цветом
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.0f, 0.0f, 1.0f));
        if (ImGui::Button("Выключить ПК", ImVec2(150, 30))) {
            client.SendPowerAction(1); // 1 - Shutdown
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        if (ImGui::Button("Перезагрузить ПК", ImVec2(150, 30))) {
            client.SendPowerAction(2); // 2 - Reboot
        }

        ImGui::SameLine();

        if (ImGui::Button("Гибернация (Сон)", ImVec2(150, 30))) {
            client.SendPowerAction(3); // 3 - Hibernate
        }

        if (!isConnected) ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::Text("Удаленный аудиозахват (Прослушка):");

        static int selectedAudioSource = 0; // 0 - Микрофон, 1 - Звук экрана (ПК)
        const char* audioSources[] = { "Физический микрофон", "Звук экрана (Динамики ПК)" };
        static bool isRecordingActive = false;

        if (!isConnected) ImGui::BeginDisabled();

        ImGui::Combo("Источник звука", &selectedAudioSource, audioSources, IM_ARRAYSIZE(audioSources));

        if (!isRecordingActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.5f, 0.0f, 1.0f));
            if (ImGui::Button("Включить запись звука", ImVec2(-1, 30))) {
                isRecordingActive = true;
                // Передаем 1 для микрофона, 2 для loopback-экрана
                client.SendStartAudioRecord(selectedAudioSource == 0 ? 1 : 2);
            }
            ImGui::PopStyleColor();
        } else {
            // Мигающий красный текст-индикатор записи
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, sinf((float)glfwGetTime() * 5.0f) * 0.5f + 0.5f), "● ИДЕТ ЗАПИСЬ ЗВУКА НА СЕРВЕРЕ...");

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.0f, 0.0f, 1.0f));
            if (ImGui::Button("Остановить и сохранить .wav", ImVec2(-1, 30))) {
                isRecordingActive = false;
                client.SendStopAudioRecord();
            }
            ImGui::PopStyleColor();
        }

        if (!isConnected) ImGui::EndDisabled();

        // --- КОНЕЦ ОКНА 1 ---
        ImGui::End();

        // --- ОКНО 2: ПРОСМОТР ЭКРАНА (SCREEN SHARING) ---
ImGui::Begin("Экран удаленного ПК");

if (isConnected) {
    if (g_ScreenTexture != 0) {
        ImVec2 screenPos = ImGui::GetCursorScreenPos();
        ImVec2 availSize = ImGui::GetContentRegionAvail();

        ImGui::Image((ImTextureID)(uintptr_t)g_ScreenTexture, availSize);

        // Перехватываем управление, если мышь наведена или окно активно
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGuiIO& io = ImGui::GetIO();

            float relativeX = io.MousePos.x - screenPos.x;
            float relativeY = io.MousePos.y - screenPos.y;
            float normX = relativeX / availSize.x;
            float normY = relativeY / availSize.y;

            // --- ОБРАБОТКА МЫШИ ---
            if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
                client.SendMouseEvent(0, normX, normY);
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))   client.SendMouseEvent(1, normX, normY);
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))  client.SendMouseEvent(2, normX, normY);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))  client.SendMouseEvent(3, normX, normY);
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) client.SendMouseEvent(4, normX, normY);

            // --- ИСПРАВЛЕННЫЙ БЛОК: ОБРАБОТКА КЛАВИАТУРЫ (ImGuiKey) ---
            // Перебираем именованные клавиши ImGui по стандарту vcpkg/ImGui
            for (int key_idx = ImGuiKey_NamedKey_BEGIN; key_idx < ImGuiKey_NamedKey_END; key_idx++) {
                ImGuiKey key = static_cast<ImGuiKey>(key_idx);

                // Проверяем нажатие клавиши
                if (ImGui::IsKeyPressed(key, false)) {
                    // Преобразуем внутренний код ImGui в стандартный Virtual Key Windows (VK)
                    // Для большинства стандартных клавиш в Windows-окружении vcpkg делает маппинг 1 в 1,
                    // но для безопасности получаем системный скан-код через io.KeyMap
                    uint32_t vkCode = static_cast<uint32_t>(key);

                    // Небольшая корректировка для буквенно-цифрового диапазона
                    if (key >= ImGuiKey_0 && key <= ImGuiKey_9) vkCode = '0' + (key - ImGuiKey_0);
                    else if (key >= ImGuiKey_A && key <= ImGuiKey_Z) vkCode = 'A' + (key - ImGuiKey_A);
                    else if (key == ImGuiKey_Space) vkCode = 0x20; // VK_SPACE
                    else if (key == ImGuiKey_Enter) vkCode = 0x0D; // VK_RETURN
                    else if (key == ImGuiKey_Backspace) vkCode = 0x08; // VK_BACK
                    else if (key == ImGuiKey_Escape) vkCode = 0x1B; // VK_ESCAPE

                    client.SendKeyboardEvent(vkCode, 1); // 1 - Нажата
                }

                // Проверяем отпускание клавиши
                if (ImGui::IsKeyReleased(key)) {
                    uint32_t vkCode = static_cast<uint32_t>(key);

                    if (key >= ImGuiKey_0 && key <= ImGuiKey_9) vkCode = '0' + (key - ImGuiKey_0);
                    else if (key >= ImGuiKey_A && key <= ImGuiKey_Z) vkCode = 'A' + (key - ImGuiKey_A);
                    else if (key == ImGuiKey_Space) vkCode = 0x20;
                    else if (key == ImGuiKey_Enter) vkCode = 0x0D;
                    else if (key == ImGuiKey_Backspace) vkCode = 0x08;
                    else if (key == ImGuiKey_Escape) vkCode = 0x1B;

                    client.SendKeyboardEvent(vkCode, 0); // 0 - Отпущена
                }
            }
        } // <-- Закрытие if (IsItemHovered)
    } else {
        ImGui::Text("Подключение установлено. Ожидание первого кадра...");
    }
} else {
    ImGui::Text("Нет подключения к серверу. Трансляция недоступна.");
}

ImGui::End();

        // --- ОКНО 3: ПОЛЬЗОВАТЕЛЬСКИЙ САУНДБОРД ---
        ImGui::Begin("Пользовательский Саундборд (Локальные звуки)");

        if (ImGui::Button("Обновить локальные папки", ImVec2(-1, 30))) {
            client.ScanLocalSounds();
        }
        ImGui::Separator();

        if (client.m_soundCategories.empty()) {
            ImGui::Text("Положите .wav файлы в папку 'sounds/' рядом с клиентом.");
        }

        for (const auto& [category, files] : client.m_soundCategories) {
            if (ImGui::CollapsingHeader(category.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                for (const auto& filename : files) {
                    std::string buttonLabel = "  " + filename;

                    if (ImGui::Button(buttonLabel.c_str(), ImVec2(200, 40))) {
                        std::string fullRelPath = category + "/" + filename;
                        // Шлём сырые байты файла по сети
                        client.SendAudioBytesToServer(fullRelPath);
                    }
                    ImGui::SameLine();
                }
                ImGui::NewLine();
            }
        }

        ImGui::End();

        // --- ОКНО 4: ДИСПЕТЧЕР ЗАДАЧ ---
        ImGui::Begin("Удаленный Диспетчер задач");

        if (isConnected) {
            if (ImGui::Button("Обновить дерево процессов", ImVec2(-1, 30))) {
                client.RequestProcessList();
            }
            ImGui::Separator();

            // Создаем красивую таблицу с прокруткой и фиксированными заголовками
            if (ImGui::BeginTable("ProcessTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 400))) {
                ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Имя процесса", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Действие", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < client.m_processes.size(); i++) {
                    const auto& [pid, name] = client.m_processes[i];

                    ImGui::TableNextRow();

                    // Колонка 1: PID
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%u", pid);

                    // Колонка 2: Название
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", name.c_str());

                    // Колонка 3: Кнопка закрытия
                    ImGui::TableSetColumnIndex(2);
                    // Создаем уникальный ID для каждой кнопки на основе PID
                    std::string killBtnLabel = "Снять задачу##" + std::to_string(pid);

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.0f, 0.0f, 1.0f));
                    if (ImGui::Button(killBtnLabel.c_str(), ImVec2(-1, 20))) {
                        client.SendKillProcess(pid);
                        // Сразу же обновляем список, чтобы закрытый процесс исчез из таблицы
                        client.RequestProcessList();
                    }
                    ImGui::PopStyleColor();
                }
                ImGui::EndTable();
            }
        } else {
            ImGui::Text("Подключитесь к серверу для просмотра процессов.");
        }

        ImGui::End();

        // Рендеринг графики
        ImGui::Render();

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);

        // Очищаем экран перед выводами ImGui
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Рисуем интерфейс поверх очищенного экрана
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Очистка ресурсов графики
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    g_ScreenThreadRunning = false;
    if (isThreadStarted && screenThread.joinable()) {
        screenThread.join(); // Ожидаем завершения фонового потока перед выходом
    }

    return 0;
}
