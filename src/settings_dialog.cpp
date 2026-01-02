#include "settings_dialog.h"
#include "proxy_manager.h"
#include "resource.h"
#include <stdio.h>
#include <vector>

static std::string* g_server = nullptr;
static int* g_port = nullptr;
static std::string* g_bypass = nullptr;

bool SettingsDialog::Show(HWND hwnd, std::string& server, int& port, std::string& bypass) {
    g_server = &server;
    g_port = &port;
    g_bypass = &bypass;

    INT_PTR result = DialogBoxParamA(GetModuleHandle(NULL), (LPCSTR)MAKEINTRESOURCE(IDD_SETTINGS), hwnd, DialogProc, 0);

    g_server = nullptr;
    g_port = nullptr;
    g_bypass = nullptr;

    return result == IDOK;
}

INT_PTR CALLBACK SettingsDialog::DialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
        return OnInitDialog(hwnd);

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            OnOK(hwnd);
            EndDialog(hwnd, IDOK);
            return TRUE;

        case IDCANCEL:
            OnCancel(hwnd);
            EndDialog(hwnd, IDCANCEL);
            return TRUE;

        case IDC_ADD_BYPASS:
            OnAddBypass(hwnd);
            return TRUE;

        case IDC_REMOVE_BYPASS:
            OnRemoveBypass(hwnd);
            return TRUE;

        case IDC_CLEAR_BYPASS:
            OnClearBypass(hwnd);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// 分割字符串为列表
static std::vector<std::string> SplitBypassList(const std::string& bypass) {
    std::vector<std::string> items;
    std::string item;
    for (char c : bypass) {
        if (c == ';') {
            if (!item.empty()) {
                items.push_back(item);
                item.clear();
            }
        } else {
            item += c;
        }
    }
    if (!item.empty()) {
        items.push_back(item);
    }
    return items;
}

// 合并列表为字符串
static std::string JoinBypassList(const std::vector<std::string>& items) {
    std::string result;
    for (size_t i = 0; i < items.size(); i++) {
        if (i > 0) result += ";";
        result += items[i];
    }
    return result;
}

BOOL SettingsDialog::OnInitDialog(HWND hwnd) {
    // 设置对话框标题
    SetWindowTextA(hwnd, "代理设置");

    // 设置当前值
    char buffer[256];
    SetDlgItemTextA(hwnd, IDC_SERVER, g_server->c_str());
    sprintf_s(buffer, "%d", *g_port);
    SetDlgItemTextA(hwnd, IDC_PORT, buffer);

    // 解析并显示绕过列表
    std::vector<std::string> bypassItems = SplitBypassList(*g_bypass);
    HWND hList = GetDlgItem(hwnd, IDC_BYPASS_LIST);
    for (const auto& item : bypassItems) {
        SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)item.c_str());
    }

    // 清空输入框
    SetDlgItemTextA(hwnd, IDC_BYPASS_INPUT, "");

    // 设置窗口标题
    SetWindowTextA(hwnd, "代理设置");

    // 设置焦点
    SetFocus(GetDlgItem(hwnd, IDC_SERVER));
    return FALSE;
}

void SettingsDialog::OnAddBypass(HWND hwnd) {
    char buffer[256];
    GetDlgItemTextA(hwnd, IDC_BYPASS_INPUT, buffer, sizeof(buffer));

    // 检查是否为空
    if (strlen(buffer) == 0) {
        MessageBoxA(hwnd, "请输入绕过规则", "提示", MB_OK | MB_ICONWARNING);
        return;
    }

    // 检查是否已存在
    HWND hList = GetDlgItem(hwnd, IDC_BYPASS_LIST);
    int count = (int)SendMessageA(hList, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; i++) {
        char existing[256];
        SendMessageA(hList, LB_GETTEXT, i, (LPARAM)existing);
        if (strcmp(existing, buffer) == 0) {
            MessageBoxA(hwnd, "该规则已存在", "提示", MB_OK | MB_ICONWARNING);
            return;
        }
    }

    // 添加到列表
    SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)buffer);
    SetDlgItemTextA(hwnd, IDC_BYPASS_INPUT, "");
    SetFocus(GetDlgItem(hwnd, IDC_BYPASS_INPUT));
}

void SettingsDialog::OnRemoveBypass(HWND hwnd) {
    HWND hList = GetDlgItem(hwnd, IDC_BYPASS_LIST);
    int selected = (int)SendMessageA(hList, LB_GETCURSEL, 0, 0);

    if (selected == LB_ERR) {
        MessageBoxA(hwnd, "请先选择要删除的规则", "提示", MB_OK | MB_ICONWARNING);
        return;
    }

    SendMessageA(hList, LB_DELETESTRING, selected, 0);
}

void SettingsDialog::OnClearBypass(HWND hwnd) {
    if (MessageBoxA(hwnd, "确定要清空所有绕过规则吗？", "确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        HWND hList = GetDlgItem(hwnd, IDC_BYPASS_LIST);
        SendMessageA(hList, LB_RESETCONTENT, 0, 0);
    }
}

void SettingsDialog::OnOK(HWND hwnd) {
    char buffer[256];

    // 获取服务器
    GetDlgItemTextA(hwnd, IDC_SERVER, buffer, sizeof(buffer));
    if (strlen(buffer) == 0) {
        MessageBoxA(hwnd, "请输入代理服务器地址", "错误", MB_OK | MB_ICONERROR);
        return;
    }
    *g_server = buffer;

    // 获取端口
    GetDlgItemTextA(hwnd, IDC_PORT, buffer, sizeof(buffer));
    int port = atoi(buffer);
    if (port <= 0 || port > 65535) {
        MessageBoxA(hwnd, "请输入有效的端口号 (1-65535)", "错误", MB_OK | MB_ICONERROR);
        return;
    }
    *g_port = port;

    // 获取绕过列表
    HWND hList = GetDlgItem(hwnd, IDC_BYPASS_LIST);
    std::vector<std::string> bypassItems;
    int count = (int)SendMessageA(hList, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; i++) {
        char item[256];
        SendMessageA(hList, LB_GETTEXT, i, (LPARAM)item);
        bypassItems.push_back(item);
    }
    *g_bypass = JoinBypassList(bypassItems);

    // 直接保存到全局变量，不重复调用SetProxyConfig
    // 因为ProxyManager::SetProxyConfig已经在设置时调用了
}

void SettingsDialog::OnCancel(HWND hwnd) {
    // 不保存
}
