#include <windows.h>
#include "LogiaPhysics.h"

// Global World Instance
LogiaWorld world;

// Forward declaration of the Window Procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow) {
    const char CLASS_NAME[] = "LogiaWindowClass";

    WNDCLASS wc = { };
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, "Logia Physics Engine",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) return 0;

    // Add some test bodies (Position, Mass, Radius)
    world.addBody(RigidBody(Vec2(200, 50), 10.0f, 20.0f));
    world.addBody(RigidBody(Vec2(250, -100), 15.0f, 20.0f));
    world.addBody(RigidBody(Vec2(300, -300), 5.0f, 20.0f));

    ShowWindow(hwnd, nCmdShow);

    // Setup a Win32 Timer to fire every ~16ms (roughly 60 FPS)
    SetTimer(hwnd, 1, 16, NULL);

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_TIMER: {
            // Step physics and force the window to redraw
            world.step(0.016f);
            InvalidateRect(hwnd, NULL, TRUE); // TRUE clears the background
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // Draw Floor
            HBRUSH floorBrush = CreateSolidBrush(RGB(34, 139, 34)); // Forest Green
            RECT floorRect = { 0, 500, 800, 600 };
            FillRect(hdc, &floorRect, floorBrush);
            DeleteObject(floorBrush);

            // Draw Physics Bodies
            HBRUSH bodyBrush = CreateSolidBrush(RGB(0, 150, 255));
            SelectObject(hdc, bodyBrush);
            
            for (const auto& body : world.bodies) {
                // Assuming a radius of 20 for visual purposes
                int x = static_cast<int>(body.position.x);
                int y = static_cast<int>(body.position.y);
                Ellipse(hdc, x - 20, y - 20, x + 20, y + 20);
            }
            
            DeleteObject(bodyBrush);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}


