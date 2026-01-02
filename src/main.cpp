#include "proxy_manager.h"
#include "tray_icon.h"
#include "settings_dialog.h"
#include <string>

// 全局变量
TrayIcon* g_trayIcon = nullptr;
std::string g_proxyServer = "127.0.0.1";
int g_proxyPort = 7890;
std::string g_proxyBypass = "localhost;127.*;<local>";

// 设置开机自启动
bool SetAutoStart(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    if (enable) {
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);

        if (RegSetValueExA(hKey, "SysProxyBar", 0, REG_SZ, (const BYTE*)exePath, strlen(exePath) + 1) != ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return false;
        }
    } else {
        RegDeleteValueA(hKey, "SysProxyBar");
    }

    RegCloseKey(hKey);
    return true;
}

bool IsAutoStartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    char buffer[MAX_PATH];
    DWORD size = sizeof(buffer);
    DWORD type = REG_SZ;
    bool result = (RegQueryValueExA(hKey, "SysProxyBar", NULL, &type, (LPBYTE)buffer, &size) == ERROR_SUCCESS);

    RegCloseKey(hKey);
    return result;
}

// 加载配置
void LoadConfig() {
    ProxyManager::GetProxyConfig(g_proxyServer, g_proxyPort, g_proxyBypass);

    // 如果没有配置，使用默认值
    if (g_proxyServer.empty()) {
        g_proxyServer = "127.0.0.1";
        g_proxyPort = 7890;
        g_proxyBypass = "localhost;127.*;<local>";
        ProxyManager::SetProxyConfig(g_proxyServer, g_proxyPort, g_proxyBypass);
    }
}

// 切换代理状态
void ToggleProxy() {
    bool currentState = ProxyManager::IsProxyEnabled();
    bool newState = !currentState;

    if (ProxyManager::SetProxyEnabled(newState)) {
        if (g_trayIcon) {
            g_trayIcon->Update(newState);
        }
    }
}

// 窗口过程
LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        g_trayIcon = new TrayIcon(hwnd);
        if (!g_trayIcon->Add()) {
            MessageBoxA(hwnd, "无法创建托盘图标", "错误", MB_ICONERROR);
            PostQuitMessage(1);
        }

        // 初始化图标状态
        g_trayIcon->Update(ProxyManager::IsProxyEnabled());
        break;

    case WM_TRAY_ICON:
        if (g_trayIcon) {
            g_trayIcon->HandleMessage(message, wParam, lParam);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_TOGGLE_PROXY:
            ToggleProxy();
            break;

        case IDM_SETTINGS: {
            if (SettingsDialog::Show(hwnd, g_proxyServer, g_proxyPort, g_proxyBypass)) {
                // 用户点击了OK，保存配置
                ProxyManager::SetProxyConfig(g_proxyServer, g_proxyPort, g_proxyBypass);
                MessageBoxA(hwnd, "配置已保存！", "成功", MB_OK | MB_ICONINFORMATION);
            }
            break;
        }

        case IDM_AUTOSTART: {
            bool currentState = IsAutoStartEnabled();
            bool newState = !currentState;
            if (SetAutoStart(newState)) {
                const char* message = newState ? "开机自启动已启用" : "开机自启动已禁用";
                MessageBoxA(hwnd, message, "系统代理", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxA(hwnd, "设置开机自启动失败", "错误", MB_OK | MB_ICONERROR);
            }
            break;
        }

        case IDM_EXIT:
            PostQuitMessage(0);
            break;
        }
        break;

    case WM_DESTROY:
        if (g_trayIcon) {
            delete g_trayIcon;
            g_trayIcon = nullptr;
        }
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcA(hwnd, message, wParam, lParam);
    }
    return 0;
}

// 主函数
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 加载配置
    LoadConfig();

    // 注册窗口类
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = 0;
    wc.lpfnWndProc = WindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "SysProxyBarClass";
    wc.hIconSm = NULL;

    if (!RegisterClassExA(&wc)) {
        MessageBoxA(NULL, "窗口类注册失败", "错误", MB_ICONERROR);
        return 1;
    }

    // 创建隐藏窗口
    HWND hwnd = CreateWindowExA(
        0,
        "SysProxyBarClass",
        "系统代理控制",
        0,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) {
        MessageBoxA(NULL, "窗口创建失败", "错误", MB_ICONERROR);
        return 1;
    }

    // 消息循环
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}
