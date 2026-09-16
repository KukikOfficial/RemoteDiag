// server/Diagnostics.cpp
#include "Diagnostics.hpp"
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <sysinfoapi.h>
#else
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif

DiagnosticsPayload Diagnostics::CollectMetrics() {
    DiagnosticsPayload payload{};

#ifdef _WIN32
    // RAM Info
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    payload.totalRamMem = memInfo.ullTotalPhys;
    payload.freeRamMem = memInfo.ullAvailPhys;
    payload.cpuUsage = 15.4; // Заглушка. Для реального ЦП на Windows требуется GetSystemTimes

    // Disk Info
    ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes;
    GetDiskFreeSpaceExA("C:\\", &freeBytesAvailable, &totalNumberOfBytes, NULL);
    payload.freeDiskSpace = freeBytesAvailable.QuadPart;

    snprintf(payload.processList, sizeof(payload.processList), "svchost.exe, chrome.exe, explorer.exe");
#else
    // Linux Metrics
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        payload.totalRamMem = si.totalram * si.mem_unit;
        payload.freeRamMem = si.freeram * si.mem_unit;
    }
    payload.cpuUsage = 12.5; // Упрощено

    struct statvfs vfs;
    if (statvfs("/", &vfs) == 0) {
        payload.freeDiskSpace = vfs.f_bsize * vfs.f_bavail;
    }
    snprintf(payload.processList, sizeof(payload.processList), "init, systemd, bash, sshd");
#endif

    return payload;
}