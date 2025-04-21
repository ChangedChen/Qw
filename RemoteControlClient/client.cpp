// ======== client.cpp（被控端）=========
#define WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <gdiplus.h>
#include <iostream>
#include <thread>
#include <fstream>
#include <vector>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

enum InputType {
    MOUSE_MOVE = 1,
    MOUSE_LEFT_DOWN = 2,
    MOUSE_LEFT_UP = 3,
    MOUSE_RIGHT_DOWN = 4,
    MOUSE_RIGHT_UP = 5,
    MOUSE_WHEEL = 6,
    SCREEN_DATA = 9
};

struct MouseEvent {
    int type;
    float x;
    float y;
    int wheel_delta;
};

struct FDataHeader {
    int type;
    int payload_size;
};

int screenWidth = 0;
int screenHeight = 0;
bool running = true;
SOCKET sock = INVALID_SOCKET;

void EnableDPIAwareness() {
    HMODULE shcore = LoadLibraryA("Shcore.dll");
    if (shcore) {
        typedef HRESULT(WINAPI* SetDpiAwarenessFunc)(int);
        SetDpiAwarenessFunc SetDpiAwareness = (SetDpiAwarenessFunc)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (SetDpiAwareness) SetDpiAwareness(2);
        FreeLibrary(shcore);
    }
    else {
        HMODULE user32 = LoadLibraryA("user32.dll");
        if (user32) {
            typedef BOOL(WINAPI* SetDPIAwareFunc)();
            SetDPIAwareFunc SetDPIAware = (SetDPIAwareFunc)GetProcAddress(user32, "SetProcessDPIAware");
            if (SetDPIAware) SetDPIAware();
            FreeLibrary(user32);
        }
    }
}

void HandleMouseMove(const MouseEvent& event) {
    int x = static_cast<int>(event.x * screenWidth);
    int y = static_cast<int>(event.y * screenHeight);
    SetCursorPos(x, y);
}

void HandleMouseButton(const MouseEvent& event) {
    int x = static_cast<int>(event.x * screenWidth);
    int y = static_cast<int>(event.y * screenHeight);
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dx = x * (65535 / screenWidth);
    input.mi.dy = y * (65535 / screenHeight);
    input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    switch (event.type) {
    case MOUSE_LEFT_DOWN:
        input.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
        break;
    case MOUSE_LEFT_UP:
        input.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
        break;
    case MOUSE_RIGHT_DOWN:
        input.mi.dwFlags |= MOUSEEVENTF_RIGHTDOWN;
        break;
    case MOUSE_RIGHT_UP:
        input.mi.dwFlags |= MOUSEEVENTF_RIGHTUP;
        break;
    }

    SendInput(1, &input, sizeof(INPUT));
}

void HandleMouseWheel(const MouseEvent& event) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = event.wheel_delta;
    SendInput(1, &input, sizeof(INPUT));
}

HBITMAP CaptureScreenBitmap() {
    HDC hScreen = GetDC(NULL);
    HDC hMemDC = CreateCompatibleDC(hScreen);
    screenWidth = GetSystemMetrics(SM_CXSCREEN);
    screenHeight = GetSystemMetrics(SM_CYSCREEN);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, screenWidth, screenHeight);
    SelectObject(hMemDC, hBitmap);
    BitBlt(hMemDC, 0, 0, screenWidth, screenHeight, hScreen, 0, 0, SRCCOPY);
    ReleaseDC(NULL, hScreen);
    DeleteDC(hMemDC);
    return hBitmap;
}

bool SaveBitmapToJPG(HBITMAP hBitmap, const WCHAR* filename) {
    Bitmap bmp(hBitmap, nullptr);
    CLSID clsid;
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return false;
    std::vector<BYTE> buffer(size);
    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(buffer.data());
    GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(pImageCodecInfo[i].MimeType, L"image/jpeg") == 0) {
            clsid = pImageCodecInfo[i].Clsid;
            break;
        }
    }
    return bmp.Save(filename, &clsid, nullptr) == Ok;
}

void CaptureAndSend(SOCKET sock) {
    while (running) {
        HBITMAP hBitmap = CaptureScreenBitmap();
        SaveBitmapToJPG(hBitmap, L"screen.jpg");
        DeleteObject(hBitmap);
        std::ifstream file("screen.jpg", std::ios::binary);
        file.seekg(0, std::ios::end);
        int size = static_cast<int>(file.tellg());
        file.seekg(0);
        std::vector<char> data(size);
        file.read(data.data(), size);
        FDataHeader header{ SCREEN_DATA, size };
        send(sock, (char*)&header, sizeof(header), 0);
        send(sock, data.data(), size, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // 60FPS
    }
}

void ReceiveInputEvents(SOCKET sock) {
    char buffer[1024];
    while (running) {
        FDataHeader header;
        int ret = recv(sock, (char*)&header, sizeof(header), 0);
        if (ret <= 0) {
            std::cerr << "连接已断开或接收错误\n";
            running = false;
            break;
        }
        if (header.payload_size > sizeof(buffer)) continue;

        switch (header.type) {
        case MOUSE_MOVE:
        case MOUSE_LEFT_DOWN:
        case MOUSE_LEFT_UP:
        case MOUSE_RIGHT_DOWN:
        case MOUSE_RIGHT_UP:
        case MOUSE_WHEEL:
            if (header.payload_size == sizeof(MouseEvent)) {
                MouseEvent event;
                recv(sock, (char*)&event, sizeof(event), 0);
                if (header.type == MOUSE_MOVE)
                    HandleMouseMove(event);
                else if (header.type == MOUSE_WHEEL)
                    HandleMouseWheel(event);
                else
                    HandleMouseButton(event);
            }
            break;
        default:
            int remaining = header.payload_size;
            while (remaining > 0) {
                int toRead = min(remaining, sizeof(buffer));
                int bytes = recv(sock, buffer, toRead, 0);
                if (bytes <= 0) break;
                remaining -= bytes;
            }
            break;
        }
    }
}

int main() {
    EnableDPIAwareness();
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(8080);
    server.sin_addr.s_addr = inet_addr("127.0.0.1"); // 修改为主控端 IP

    std::cout << "正在连接到控制端...\n";
    if (connect(sock, (sockaddr*)&server, sizeof(server)) != 0) {
        std::cerr << "连接失败: " << WSAGetLastError() << std::endl;
        closesocket(sock);
        WSACleanup();
        GdiplusShutdown(gdiplusToken);
        return 1;
    }

    std::cout << "已连接到控制端，开始远程桌面共享\n";

    std::thread captureThread(CaptureAndSend, sock);
    std::thread inputThread(ReceiveInputEvents, sock);
    captureThread.join();
    inputThread.join();

    closesocket(sock);
    WSACleanup();
    GdiplusShutdown(gdiplusToken);
    return 0;
}
