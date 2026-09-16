cmake_minimum_required(VERSION 3.15)
project(RemoteDiag CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Подключение библиотек сокетов для Windows
if(WIN32)
    set(NET_LIBS "ws2_32")
else()
    set(NET_LIBS "")
endif()

# Автоматический поиск пакетов, установленных через vcpkg
find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

# Указываем CMake, где лежат бэкенды ImGui внутри vcpkg
set(VCPKG_IMGUI_DIR "C:/Users/kukik/.vcpkg-clion/vcpkg/installed/x64-mingw-dynamic/include")

# Указываем стандартные чистые пути к заголовочным файлам
include_directories(
    common
    client
)

# =====================================================================
# 1. Сборка Сервера (DiagServer)
# =====================================================================
add_executable(DiagServer
        server/Main.cpp
        server/Server.cpp
        server/Diagnostics.cpp
        server/Soundboard.cpp
        server/ScreenCapture.hpp
        server/ScreenCapture.cpp
)

target_link_libraries(DiagServer ${NET_LIBS} winmm ole32 powrprof)

# Вшиваем libstdc++ и libgcc (файлы libstdc++-6.dll и libgcc_s не понадобятся)
if(MINGW)
    target_link_options(DiagServer PRIVATE -static-libgcc -static-libstdc++)
endif()

# --- АВТОМАТИЧЕСКОЕ КОПИРОВАНИЕ СКРИПТА ТУННЕЛЯ К СЕРВЕРУ ---
add_custom_command(TARGET DiagServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/server/tunnel_start.bat"
        "$<TARGET_FILE_DIR:DiagServer>/tunnel_start.bat"
        COMMENT "Копирование скрипта туннелирования Pinggy к серверу..."
)

# =====================================================================
# 2. Сборка Клиента (DiagClient)
# =====================================================================
add_executable(DiagClient
    client/Main.cpp
    client/Client.cpp
    client/imgui_impl_glfw.cpp
    client/imgui_impl_opengl3.cpp
)

# Связываем Клиент с сокетами, графикой GLFW, базовым ядром imgui и OpenGL
target_link_libraries(DiagClient
    ${NET_LIBS}
    glfw
    imgui::imgui
    OpenGL::GL
)