#include "proxy_manager.h"
#include <wininet.h>
#include <winerror.h>

#pragma comment(lib, "wininet.lib")

const std::string ProxyManager::GetProxyRegKey() {
    return "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings";
}

bool ProxyManager::IsProxyEnabled() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, GetProxyRegKey().c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    DWORD enabled = 0;
    DWORD size = sizeof(enabled);
    DWORD type = REG_DWORD;

    bool result = (RegQueryValueExA(hKey, "ProxyEnable", NULL, &type, (LPBYTE)&enabled, &size) == ERROR_SUCCESS) && (enabled != 0);

    RegCloseKey(hKey);
    return result;
}

bool ProxyManager::SetProxyEnabled(bool enabled) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, GetProxyRegKey().c_str(), 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    DWORD value = enabled ? 1 : 0;
    bool result = RegSetValueExA(hKey, "ProxyEnable", 0, REG_DWORD, (const BYTE*)&value, sizeof(value)) == ERROR_SUCCESS;

    RegCloseKey(hKey);

    if (result) {
        NotifySystemChange();
    }

    return result;
}

bool ProxyManager::SetProxyConfig(const std::string& server, int port, const std::string& bypass) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, GetProxyRegKey().c_str(), 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    std::string proxyServer = server + ":" + std::to_string(port);
    bool result = true;

    if (RegSetValueExA(hKey, "ProxyServer", 0, REG_SZ, (const BYTE*)proxyServer.c_str(), proxyServer.length() + 1) != ERROR_SUCCESS) {
        result = false;
    }

    if (RegSetValueExA(hKey, "ProxyOverride", 0, REG_SZ, (const BYTE*)bypass.c_str(), bypass.length() + 1) != ERROR_SUCCESS) {
        result = false;
    }

    RegCloseKey(hKey);

    if (result) {
        NotifySystemChange();
    }

    return result;
}

bool ProxyManager::GetProxyConfig(std::string& server, int& port, std::string& bypass) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, GetProxyRegKey().c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    char buffer[256];
    DWORD size = sizeof(buffer);
    DWORD type = REG_SZ;

    // Get ProxyServer
    if (RegQueryValueExA(hKey, "ProxyServer", NULL, &type, (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
        std::string proxyServer(buffer);
        size_t colonPos = proxyServer.find(':');
        if (colonPos != std::string::npos) {
            server = proxyServer.substr(0, colonPos);
            port = std::atoi(proxyServer.substr(colonPos + 1).c_str());
        }
    }

    // Get ProxyOverride
    size = sizeof(buffer);
    if (RegQueryValueExA(hKey, "ProxyOverride", NULL, &type, (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
        bypass = buffer;
    }

    RegCloseKey(hKey);
    return true;
}

bool ProxyManager::NotifySystemChange() {
    // 通知系统代理设置已更改
    return InternetSetOptionA(NULL, INTERNET_OPTION_SETTINGS_CHANGED, NULL, 0) &&
           InternetSetOptionA(NULL, INTERNET_OPTION_REFRESH, NULL, 0);
}
