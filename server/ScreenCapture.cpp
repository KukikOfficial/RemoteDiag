// server/ScreenCapture.cpp
#include "ScreenCapture.hpp"
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

std::vector<uint8_t> ScreenCapture::CapturePrimaryMonitor(ScreenShotHeader& outHeader) {
    std::vector<uint8_t> buffer;
#ifdef _WIN32
    // 1. Получаем HDC всего рабочего стола через GetDC(NULL)
    HDC hdcScreen = GetDC(NULL);
    if (!hdcScreen) {
        std::cerr << "[Ошибка] Не удалось получить HDC экрана" << std::endl;
        return buffer;
    }

    // 2. Создаем совместимый контекст в памяти
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    // Берем полный размер экрана без уменьшения, чтобы BitBlt отработал попиксельно
    HBITMAP hbmp = CreateCompatibleBitmap(hdcScreen, width, height);
    HGDIOBJ oldBmp = SelectObject(hdcMem, hbmp);

    // 3. Копируем экран один к одному через BitBlt (это гарантированно работает на Win 10/11)
    BitBlt(hdcMem, 0, 0, width, height, hdcScreen, 0, 0, SRCCOPY);

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height; // Сверху вниз, чтобы картинка не была перевернутой
    bi.biPlanes = 1;
    bi.biBitCount = 32;    // Формат BGRA
    bi.biCompression = BI_RGB;

    DWORD dwBmpSize = width * height * 4;
    buffer.resize(dwBmpSize);

    // 4. Извлекаем пиксели из hbmp, используя hdcScreen
    int linesCopied = GetDIBits(hdcScreen, hbmp, 0, height, buffer.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    if (linesCopied <= 0) {
        std::cerr << "[Ошибка] GetDIBits вернул 0 строк!" << std::endl;
    } else {
        std::cout << "[Успех] Кадр захвачен. Размер: " << dwBmpSize << " байт." << std::endl;
    }

    // Заполняем актуальный заголовок пакета
    outHeader.width = width;
    outHeader.height = height;
    outHeader.dataSize = dwBmpSize;

    // Очистка ресурсов в правильном порядке
    SelectObject(hdcMem, oldBmp);
    DeleteObject(hbmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
#else
    outHeader.width = 400;
    outHeader.height = 300;
    outHeader.dataSize = 400 * 300 * 4;
    buffer.resize(outHeader.dataSize, 128);
#endif
    return buffer;
}

