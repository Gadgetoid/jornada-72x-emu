#include <windows.h>
#include <winsock.h>

#define ATTEMPT_MILLISECONDS 180000
#define RETRY_MILLISECONDS   2000

static int parse_arguments(const WCHAR *command_line, char *address, int address_size, unsigned short *port) {
    int length = 0;
    while (*command_line == L' ') command_line++;
    while (*command_line && *command_line != L' ' && length < address_size - 1) address[length++] = (char)*command_line++;
    address[length] = 0;
    unsigned value = 0;
    while (*command_line == L' ') command_line++;
    while (*command_line >= L'0' && *command_line <= L'9') value = value * 10 + (unsigned)(*command_line++ - L'0');
    *port = (unsigned short)value;
    return length && value;
}

static SOCKET connect_with_retries(const char *address, unsigned short port) {
    struct sockaddr_in target;
    memset(&target, 0, sizeof target);
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    target.sin_addr.s_addr = inet_addr(address);
    DWORD started = GetTickCount();
    while (GetTickCount() - started < ATTEMPT_MILLISECONDS) {
        SOCKET connection = socket(AF_INET, SOCK_STREAM, 0);
        if (connection != INVALID_SOCKET && connect(connection, (struct sockaddr *)&target, sizeof target) == 0) return connection;
        if (connection != INVALID_SOCKET) closesocket(connection);
        Sleep(RETRY_MILLISECONDS);
    }
    return INVALID_SOCKET;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show) {
    (void)instance;
    (void)previous;
    (void)show;
    char address[32];
    unsigned short port;
    if (!command_line || !parse_arguments(command_line, address, sizeof address, &port)) return 2;
    WSADATA data;
    if (WSAStartup(MAKEWORD(1, 1), &data)) return 3;
    SOCKET connection = connect_with_retries(address, port);
    if (connection == INVALID_SOCKET) return 4;
    static const char greeting[] = "hello from the jornada\n";
    send(connection, greeting, sizeof greeting - 1, 0);
    char reply[64];
    int received = recv(connection, reply, sizeof reply - 1, 0);
    if (received > 0) send(connection, reply, received, 0);
    closesocket(connection);
    WSACleanup();
    return 0;
}
