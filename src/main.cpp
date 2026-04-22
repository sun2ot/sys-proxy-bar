#include "proxy_manager.h"
#include "tray_icon.h"
#include "settings_dialog.h"
#include "http_server.h"
#include "mihomo_manager.h"
#include <string>
#include <taskschd.h>
#include <sddl.h>

// 全局变量
TrayIcon* g_trayIcon = nullptr;
HTTPServer* g_httpServer = nullptr;
MihomoManager* g_mihomoManager = nullptr;
std::string g_proxyServer = "127.0.0.1";
int g_proxyPort = 7890;
std::string g_proxyBypass = "localhost;127.*;<local>";
const char* kLegacyAutoStartValueName = "SysProxyBar";
const wchar_t* kAutoStartTaskName = L"SysProxyBar";
const UINT_PTR kTrayRetryTimerId = 1;
const UINT kTrayRetryIntervalMs = 1000;
UINT g_taskbarCreatedMessage = 0;

template <typename T>
void SafeRelease(T** value) {
    if (*value) {
        (*value)->Release();
        *value = nullptr;
    }
}

class ScopedComInit {
public:
    ScopedComInit() : m_initialized(false), m_shouldUninitialize(false) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr) || hr == S_FALSE) {
            m_initialized = true;
            m_shouldUninitialize = true;
        } else if (hr == RPC_E_CHANGED_MODE) {
            m_initialized = true;
        }
    }

    ~ScopedComInit() {
        if (m_shouldUninitialize) {
            CoUninitialize();
        }
    }

    bool IsInitialized() const {
        return m_initialized;
    }

private:
    bool m_initialized;
    bool m_shouldUninitialize;
};

std::wstring GetCurrentExePathW() {
    wchar_t exePath[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"";
    }
    return std::wstring(exePath, length);
}

std::wstring GetCurrentUserSidString() {
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return L"";
    }

    DWORD size = 0;
    GetTokenInformation(token, TokenUser, NULL, 0, &size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) {
        CloseHandle(token);
        return L"";
    }

    BYTE* buffer = new BYTE[size];
    if (!GetTokenInformation(token, TokenUser, buffer, size, &size)) {
        delete[] buffer;
        CloseHandle(token);
        return L"";
    }

    TOKEN_USER* tokenUser = reinterpret_cast<TOKEN_USER*>(buffer);
    LPWSTR sidString = NULL;
    std::wstring result;
    if (ConvertSidToStringSidW(tokenUser->User.Sid, &sidString)) {
        result = sidString;
        LocalFree(sidString);
    }

    delete[] buffer;
    CloseHandle(token);
    return result;
}

bool IsLegacyAutoStartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    char buffer[MAX_PATH];
    DWORD size = sizeof(buffer);
    DWORD type = REG_SZ;
    bool result = (RegQueryValueExA(hKey, kLegacyAutoStartValueName, NULL, &type, (LPBYTE)buffer, &size) == ERROR_SUCCESS);

    RegCloseKey(hKey);
    return result;
}

bool RemoveLegacyAutoStart() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    LONG result = RegDeleteValueA(hKey, kLegacyAutoStartValueName);
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

bool GetTaskSchedulerRootFolder(ITaskService** service, ITaskFolder** rootFolder) {
    *service = NULL;
    *rootFolder = NULL;

    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, reinterpret_cast<void**>(service));
    if (FAILED(hr)) {
        return false;
    }

    hr = CoInitializeSecurity(
        NULL,
        -1,
        NULL,
        NULL,
        RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL,
        0,
        NULL
    );
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
        SafeRelease(service);
        return false;
    }

    VARIANT empty;
    VariantInit(&empty);
    hr = (*service)->Connect(empty, empty, empty, empty);
    if (FAILED(hr)) {
        SafeRelease(service);
        return false;
    }

    BSTR folderPath = SysAllocString(L"\\");
    hr = (*service)->GetFolder(folderPath, rootFolder);
    SysFreeString(folderPath);
    if (FAILED(hr)) {
        SafeRelease(service);
        return false;
    }

    return true;
}

bool IsScheduledAutoStartEnabled() {
    ScopedComInit comInit;
    if (!comInit.IsInitialized()) {
        return false;
    }

    ITaskService* service = NULL;
    ITaskFolder* rootFolder = NULL;
    IRegisteredTask* registeredTask = NULL;

    if (!GetTaskSchedulerRootFolder(&service, &rootFolder)) {
        return false;
    }

    BSTR taskName = SysAllocString(kAutoStartTaskName);
    HRESULT hr = rootFolder->GetTask(taskName, &registeredTask);
    SysFreeString(taskName);

    SafeRelease(&registeredTask);
    SafeRelease(&rootFolder);
    SafeRelease(&service);
    return SUCCEEDED(hr);
}

bool SetScheduledAutoStart(bool enable) {
    ScopedComInit comInit;
    if (!comInit.IsInitialized()) {
        return false;
    }

    ITaskService* service = NULL;
    ITaskFolder* rootFolder = NULL;

    if (!GetTaskSchedulerRootFolder(&service, &rootFolder)) {
        return false;
    }

    bool success = false;
    BSTR taskName = SysAllocString(kAutoStartTaskName);

    if (!enable) {
        HRESULT hr = rootFolder->DeleteTask(taskName, 0);
        success = (SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
        SysFreeString(taskName);
        SafeRelease(&rootFolder);
        SafeRelease(&service);
        return success;
    }

    std::wstring exePath = GetCurrentExePathW();
    std::wstring userSid = GetCurrentUserSidString();
    if (exePath.empty() || userSid.empty()) {
        SysFreeString(taskName);
        SafeRelease(&rootFolder);
        SafeRelease(&service);
        return false;
    }

    ITaskDefinition* task = NULL;
    IRegistrationInfo* registrationInfo = NULL;
    IPrincipal* principal = NULL;
    ITaskSettings* settings = NULL;
    ITriggerCollection* triggers = NULL;
    ITrigger* trigger = NULL;
    IActionCollection* actions = NULL;
    IAction* action = NULL;
    IExecAction* execAction = NULL;
    IRegisteredTask* registeredTask = NULL;

    HRESULT hr = service->NewTask(0, &task);
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = task->get_RegistrationInfo(&registrationInfo);
    if (FAILED(hr)) {
        goto cleanup;
    }

    {
        BSTR author = SysAllocString(L"SysProxyBar");
        registrationInfo->put_Author(author);
        SysFreeString(author);
    }

    hr = task->get_Principal(&principal);
    if (FAILED(hr)) {
        goto cleanup;
    }

    {
        BSTR principalId = SysAllocString(L"SysProxyBarPrincipal");
        BSTR principalUser = SysAllocString(userSid.c_str());
        principal->put_Id(principalId);
        hr = principal->put_UserId(principalUser);
        SysFreeString(principalUser);
        SysFreeString(principalId);
        if (FAILED(hr)) {
            goto cleanup;
        }
    }

    hr = principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = task->get_Settings(&settings);
    if (FAILED(hr)) {
        goto cleanup;
    }

    settings->put_StartWhenAvailable(VARIANT_TRUE);
    settings->put_Enabled(VARIANT_TRUE);
    settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
    settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
    settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);

    hr = task->get_Triggers(&triggers);
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = triggers->Create(TASK_TRIGGER_LOGON, &trigger);
    if (FAILED(hr)) {
        goto cleanup;
    }

    {
        BSTR triggerId = SysAllocString(L"SysProxyBarLogonTrigger");
        hr = trigger->put_Id(triggerId);
        SysFreeString(triggerId);
        if (FAILED(hr)) {
            goto cleanup;
        }
    }

    hr = trigger->put_Enabled(VARIANT_TRUE);
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = task->get_Actions(&actions);
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = actions->Create(TASK_ACTION_EXEC, &action);
    if (FAILED(hr)) {
        goto cleanup;
    }

    hr = action->QueryInterface(IID_IExecAction, reinterpret_cast<void**>(&execAction));
    if (FAILED(hr)) {
        goto cleanup;
    }

    {
        BSTR actionPath = SysAllocString(exePath.c_str());
        hr = execAction->put_Path(actionPath);
        SysFreeString(actionPath);
        if (FAILED(hr)) {
            goto cleanup;
        }
    }

    {
        VARIANT userIdValue;
        VARIANT emptyValue;
        VARIANT sddlValue;
        VariantInit(&userIdValue);
        VariantInit(&emptyValue);
        VariantInit(&sddlValue);

        userIdValue.vt = VT_BSTR;
        userIdValue.bstrVal = SysAllocString(userSid.c_str());

        hr = rootFolder->RegisterTaskDefinition(
            taskName,
            task,
            TASK_CREATE_OR_UPDATE,
            userIdValue,
            emptyValue,
            TASK_LOGON_INTERACTIVE_TOKEN,
            sddlValue,
            &registeredTask
        );

        VariantClear(&userIdValue);
        if (FAILED(hr)) {
            goto cleanup;
        }
    }

    success = true;

cleanup:
    SysFreeString(taskName);
    SafeRelease(&registeredTask);
    SafeRelease(&execAction);
    SafeRelease(&action);
    SafeRelease(&actions);
    SafeRelease(&trigger);
    SafeRelease(&triggers);
    SafeRelease(&settings);
    SafeRelease(&principal);
    SafeRelease(&registrationInfo);
    SafeRelease(&task);
    SafeRelease(&rootFolder);
    SafeRelease(&service);
    return success;
}

bool IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID adminGroup = NULL;

    if (AllocateAndInitializeSid(&ntAuthority, 2,
                                 SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS,
                                 0, 0, 0, 0, 0, 0,
                                 &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }

    return isAdmin == TRUE;
}

bool RelaunchAsAdmin() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    HINSTANCE result = ShellExecuteA(NULL, "runas", exePath, NULL, NULL, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

void UpdateTrayState() {
    if (!g_trayIcon) {
        return;
    }

    g_trayIcon->Update(ProxyManager::IsProxyEnabled());
    if (g_mihomoManager) {
        g_trayIcon->SetTunEnabled(g_mihomoManager->IsTunEnabled());
    }
}

bool EnsureTrayIconAdded(HWND hwnd) {
    if (!g_trayIcon) {
        return false;
    }

    if (g_trayIcon->IsAdded() || g_trayIcon->Add()) {
        KillTimer(hwnd, kTrayRetryTimerId);
        UpdateTrayState();
        return true;
    }

    SetTimer(hwnd, kTrayRetryTimerId, kTrayRetryIntervalMs, NULL);
    return false;
}

// 设置开机自启动
bool SetAutoStart(bool enable) {
    if (!SetScheduledAutoStart(enable)) {
        return false;
    }

    return RemoveLegacyAutoStart();
}

bool IsAutoStartEnabled() {
    return IsScheduledAutoStartEnabled() || IsLegacyAutoStartEnabled();
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
    if (g_taskbarCreatedMessage != 0 && message == g_taskbarCreatedMessage) {
        EnsureTrayIconAdded(hwnd);
        return 0;
    }

    switch (message) {
    case WM_CREATE:
        g_trayIcon = new TrayIcon(hwnd);
        EnsureTrayIconAdded(hwnd);

        // 初始化图标状态
        UpdateTrayState();

        // 初始化并启动 mihomo
        g_mihomoManager = new MihomoManager();
        if (!g_mihomoManager->Initialize()) {
            MessageBoxA(hwnd, "Mihomo 初始化失败", "警告", MB_OK | MB_ICONWARNING);
        } else if (!g_mihomoManager->Start()) {
            MessageBoxA(hwnd, "Mihomo 启动失败", "警告", MB_OK | MB_ICONWARNING);
        }
        UpdateTrayState();
        EnsureTrayIconAdded(hwnd);
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

        case IDM_TOGGLE_TUN: {
            if (!g_mihomoManager) {
                MessageBoxA(hwnd, "Mihomo 管理器未初始化", "错误", MB_OK | MB_ICONERROR);
                break;
            }

            bool enableTun = !g_mihomoManager->IsTunEnabled();
            if (g_mihomoManager->SetTunEnabled(enableTun)) {
                UpdateTrayState();
                MessageBoxA(hwnd,
                    enableTun ? "TUN Mode 已开启，Mihomo 已重启" : "TUN Mode 已关闭，Mihomo 已重启",
                    "成功", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxA(hwnd,
                    enableTun ? "TUN Mode 开启失败" : "TUN Mode 关闭失败",
                    "错误", MB_OK | MB_ICONERROR);
            }
            break;
        }

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
                char currentExe[MAX_PATH];
                GetModuleFileNameA(NULL, currentExe, MAX_PATH);
                if (newState) {
                    char message[512];
                    sprintf_s(message, sizeof(message),
                        "开机自启动已启用\n\n启动路径: %s\n\n注意: 更新版本后会自动指向新版本",
                        currentExe);
                    MessageBoxA(hwnd, message, "系统代理", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxA(hwnd, "开机自启动已禁用", "系统代理", MB_OK | MB_ICONINFORMATION);
                }
            } else {
                MessageBoxA(hwnd, "设置开机自启动失败", "错误", MB_OK | MB_ICONERROR);
            }
            break;
        }

        case IDM_OPEN_WEBUI: {
            // 启动 HTTP 服务器（如果还没启动）
            if (!g_httpServer) {
                g_httpServer = new HTTPServer();

                // 固定使用端口 12345
                const int WEBUI_PORT = 12345;

                if (g_httpServer->Start(WEBUI_PORT)) {
                    // 服务器启动成功，在浏览器中打开
                    char url[64];
                    sprintf(url, "http://localhost:%d", WEBUI_PORT);
                    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOW);
                } else {
                    // 端口冲突，启动失败
                    MessageBoxA(hwnd,
                        "端口12345冲突，启动失败！\n\n"
                        "可能原因：\n"
                        "1. 端口 12345 被其他程序占用\n"
                        "2. 防火墙阻止了网络访问\n\n"
                        "请检查端口占用或关闭占用该端口的程序。",
                        "WebUI 启动失败", MB_OK | MB_ICONERROR);
                    delete g_httpServer;
                    g_httpServer = nullptr;
                }
            } else {
                // 服务器已在运行，直接打开浏览器
                ShellExecuteA(NULL, "open", "http://localhost:12345", NULL, NULL, SW_SHOW);
            }
            break;
        }

        case IDM_RESTART_MIHOMO: {
            if (g_mihomoManager) {
                if (g_mihomoManager->Restart()) {
                    MessageBoxA(hwnd, "Mihomo 已重启", "成功", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxA(hwnd, "Mihomo 重启失败", "错误", MB_OK | MB_ICONERROR);
                }
            }
            break;
        }

        case IDM_OPEN_CONFIG_DIR: {
            if (g_mihomoManager) {
                std::string configPath = g_mihomoManager->GetConfigPath();
                if (!configPath.empty()) {
                    // Select the config file in explorer
                    ShellExecuteA(NULL, "open", "explorer.exe", ("/select," + configPath).c_str(), NULL, SW_SHOW);
                } else {
                    MessageBoxA(hwnd, "无法获取配置文件路径", "错误", MB_OK | MB_ICONERROR);
                }
            }
            break;
        }

        case IDM_EXIT:
            // Destroy window first (triggers WM_DESTROY for cleanup)
            DestroyWindow(hwnd);
            break;
        }
        break;

    case WM_TIMER:
        if (wParam == kTrayRetryTimerId) {
            EnsureTrayIconAdded(hwnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, kTrayRetryTimerId);
        // 停止 mihomo
        if (g_mihomoManager) {
            g_mihomoManager->Stop();
            delete g_mihomoManager;
            g_mihomoManager = nullptr;
        }
        // 清理托盘图标
        if (g_trayIcon) {
            delete g_trayIcon;
            g_trayIcon = nullptr;
        }
        // 清理 HTTP 服务器
        if (g_httpServer) {
            delete g_httpServer;
            g_httpServer = nullptr;
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
    if (!IsRunningAsAdmin()) {
        if (RelaunchAsAdmin()) {
            return 0;
        }

        MessageBoxA(NULL,
            "SysProxyBar 需要管理员权限启动，以支持 TUN Mode 和相关网络配置。",
            "需要管理员权限", MB_OK | MB_ICONWARNING);
        return 1;
    }

    if (IsAutoStartEnabled()) {
        SetAutoStart(true);
    }

    // 单实例检查
    const char* mutexName = "Global\\SysProxyBar_SingleInstance_Mutex";
    HANDLE hMutex = CreateMutexA(NULL, TRUE, mutexName);

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // 已经有一个实例在运行
        MessageBoxA(NULL, "SysProxyBar 已经在运行中", "提示", MB_OK | MB_ICONINFORMATION);
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

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
        // 如果类已注册，可能是上次崩溃残留
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            MessageBoxA(NULL, "窗口类注册失败", "错误", MB_ICONERROR);
            if (hMutex) CloseHandle(hMutex);
            return 1;
        }
    }

    g_taskbarCreatedMessage = RegisterWindowMessageA("TaskbarCreated");

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
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    // 消息循环
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    // 清理互斥体
    if (hMutex) CloseHandle(hMutex);

    return (int)msg.wParam;
}
