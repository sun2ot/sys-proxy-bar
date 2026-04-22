#include "tray_icon.h"
#include "resource.h"
#include <stdio.h>

TrayIcon::TrayIcon(HWND hwnd)
    : m_hwnd(hwnd), m_iconEnabled(NULL), m_iconDisabled(NULL), m_tunEnabled(false), m_isAdded(false) {
    ZeroMemory(&m_nid, sizeof(m_nid));
    LoadIcons();
    UpdateIconState(false);
}

TrayIcon::~TrayIcon() {
    Remove();
    if (m_iconEnabled) DestroyIcon(m_iconEnabled);
    if (m_iconDisabled) DestroyIcon(m_iconDisabled);
}

bool TrayIcon::LoadIcons() {
    // 从资源加载自定义图标
    m_iconEnabled = LoadIconA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDI_PROXY_ON));
    m_iconDisabled = LoadIconA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDI_PROXY_OFF));

    // 如果自定义图标加载失败，使用系统图标作为后备
    if (m_iconEnabled == NULL) {
        printf("Warning: Failed to load IDI_PROXY_ON, using system icon\n");
        m_iconEnabled = LoadIcon(NULL, IDI_INFORMATION);
    }
    if (m_iconDisabled == NULL) {
        printf("Warning: Failed to load IDI_PROXY_OFF, using system icon\n");
        m_iconDisabled = LoadIcon(NULL, IDI_ERROR);
    }

    return (m_iconEnabled != NULL) && (m_iconDisabled != NULL);
}

void TrayIcon::UpdateIconState(bool proxyEnabled) {
    m_nid.hIcon = proxyEnabled ? m_iconEnabled : m_iconDisabled;
    strcpy_s(m_nid.szTip, proxyEnabled ? "系统代理 - 已开启" : "系统代理 - 已关闭");
}

HICON TrayIcon::CreateColorIcon(COLORREF color) {
    // 此函数已弃用，保留以免编译错误
    return LoadIcon(NULL, IDI_APPLICATION);
}

bool TrayIcon::Add() {
    if (m_isAdded) {
        return true;
    }

    m_nid.cbSize = sizeof(NOTIFYICONDATAA);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = ID_TRAY_ICON;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAY_ICON;
    if (m_nid.hIcon == NULL) {
        UpdateIconState(false);
    }

    if (!Shell_NotifyIconA(NIM_ADD, &m_nid)) {
        return false;
    }

#ifdef NOTIFYICON_VERSION_4
    m_nid.uVersion = NOTIFYICON_VERSION_4;
#else
    m_nid.uVersion = NOTIFYICON_VERSION;
#endif
    Shell_NotifyIconA(NIM_SETVERSION, &m_nid);
    m_isAdded = true;
    return true;
}

bool TrayIcon::Remove() {
    if (!m_isAdded) {
        return true;
    }

    m_nid.uFlags = 0;
    if (!Shell_NotifyIconA(NIM_DELETE, &m_nid)) {
        return false;
    }

    m_isAdded = false;
    return true;
}

bool TrayIcon::Update(bool proxyEnabled) {
    UpdateIconState(proxyEnabled);

    if (!m_isAdded) {
        return true;
    }

    m_nid.uFlags = NIF_ICON | NIF_TIP;

    if (!Shell_NotifyIconA(NIM_MODIFY, &m_nid)) {
        m_isAdded = false;
        return false;
    }

    return true;
}

bool TrayIcon::IsAdded() const {
    return m_isAdded;
}

void TrayIcon::SetTunEnabled(bool tunEnabled) {
    m_tunEnabled = tunEnabled;
}

void TrayIcon::ShowContextMenu() {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    AppendMenuA(hMenu, MF_STRING, IDM_TOGGLE_PROXY, "切换代理");
    AppendMenuA(hMenu, MF_STRING | (m_tunEnabled ? MF_CHECKED : MF_UNCHECKED), IDM_TOGGLE_TUN, "TUN Mode");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, IDM_SETTINGS, "设置...");
    AppendMenuA(hMenu, MF_STRING, IDM_AUTOSTART, "开机自启动");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, IDM_OPEN_WEBUI, "打开面板");
    AppendMenuA(hMenu, MF_STRING, IDM_RESTART_MIHOMO, "重启 Mihomo");
    AppendMenuA(hMenu, MF_STRING, IDM_OPEN_CONFIG_DIR, "打开配置文件");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, IDM_EXIT, "退出");

    SetForegroundWindow(m_hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_RIGHTALIGN, pt.x, pt.y, 0, m_hwnd, NULL);
    DestroyMenu(hMenu);
}

LRESULT TrayIcon::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_TRAY_ICON:
        if (lParam == WM_RBUTTONUP) {
            ShowContextMenu();
        } else if (lParam == WM_LBUTTONDOWN) {
            // 单击左键：打开 webui
            PostMessage(m_hwnd, WM_COMMAND, IDM_OPEN_WEBUI, 0);
        } else if (lParam == WM_LBUTTONDBLCLK) {
            PostMessage(m_hwnd, WM_COMMAND, IDM_TOGGLE_PROXY, 0);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_TOGGLE_PROXY:
        case IDM_TOGGLE_TUN:
        case IDM_SETTINGS:
        case IDM_AUTOSTART:
        case IDM_OPEN_WEBUI:
        case IDM_RESTART_MIHOMO:
        case IDM_OPEN_CONFIG_DIR:
        case IDM_EXIT:
            PostMessage(m_hwnd, WM_COMMAND, wParam, lParam);
            break;
        }
        break;
    }
    return 0;
}
