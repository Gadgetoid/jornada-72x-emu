#include <windows.h>

#define RUN_MILLISECONDS 120000

static HANDLE open_port(const WCHAR *name) {
    HANDLE port = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (port == INVALID_HANDLE_VALUE) return port;
    DCB settings;
    settings.DCBlength = sizeof settings;
    GetCommState(port, &settings);
    settings.BaudRate = CBR_115200;
    settings.ByteSize = 8;
    settings.Parity = NOPARITY;
    settings.StopBits = ONESTOPBIT;
    settings.fBinary = TRUE;
    settings.fOutxCtsFlow = FALSE;
    settings.fOutxDsrFlow = FALSE;
    settings.fOutX = FALSE;
    settings.fInX = FALSE;
    SetCommState(port, &settings);
    COMMTIMEOUTS timeouts = { MAXDWORD, 0, 100, 0, 0 };
    SetCommTimeouts(port, &timeouts);
    return port;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show) {
    (void)instance;
    (void)previous;
    (void)show;
    HANDLE port = open_port(command_line && command_line[0] ? command_line : L"COM4:");
    if (port == INVALID_HANDLE_VALUE) return 1;
    DWORD started = GetTickCount();
    while (GetTickCount() - started < RUN_MILLISECONDS) {
        BYTE buffer[256];
        DWORD received = 0, sent = 0;
        if (!ReadFile(port, buffer, sizeof buffer, &received, NULL)) break;
        if (received) WriteFile(port, buffer, received, &sent, NULL);
    }
    CloseHandle(port);
    return 0;
}
