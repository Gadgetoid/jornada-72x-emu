#include <windows.h>

static void send_command(HANDLE port, HANDLE log, const char *command) {
    DWORD done;
    WriteFile(port, command, (DWORD)strlen(command), &done, NULL);
    DWORD started = GetTickCount();
    char buffer[256];
    while (GetTickCount() - started < 3000) {
        DWORD received = 0;
        if (!ReadFile(port, buffer, sizeof buffer, &received, NULL)) break;
        if (received) WriteFile(log, buffer, received, &done, NULL);
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show) {
    (void)instance;
    (void)previous;
    (void)show;
    HANDLE log = CreateFileW(L"\\atresult.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    HANDLE port = CreateFileW(command_line && command_line[0] ? command_line : L"COM6:", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    DWORD done;
    if (port == INVALID_HANDLE_VALUE) {
        WriteFile(log, "open failed\r\n", 13, &done, NULL);
        CloseHandle(log);
        return 1;
    }
    COMMTIMEOUTS timeouts = { MAXDWORD, 0, 200, 0, 0 };
    SetCommTimeouts(port, &timeouts);
    send_command(port, log, "AT\r");
    send_command(port, log, "ATI3\r");
    send_command(port, log, "ATI0\r");
    WriteFile(log, "[end]\r\n", 7, &done, NULL);
    CloseHandle(port);
    CloseHandle(log);
    return 0;
}
