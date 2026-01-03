#include "http_server.h"
#include "webui_resource.h"
#include "resource.h"
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

HTTPServer::HTTPServer() : m_listenSocket(INVALID_SOCKET), m_port(9090), m_running(false), m_thread(NULL) {
}

HTTPServer::~HTTPServer() {
    Stop();
}

bool HTTPServer::Start(int port) {
    if (m_running) {
        return true;  // 已经在运行
    }

    m_port = port;

    // 初始化 Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }

    // 创建监听 socket
    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    // 允许地址重用
    int reuseAddr = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuseAddr, sizeof(reuseAddr));

    // 绑定端口
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(m_port);

    if (bind(m_listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(m_listenSocket);
        WSACleanup();
        return false;
    }

    // 开始监听
    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(m_listenSocket);
        WSACleanup();
        return false;
    }

    m_running = true;

    // 启动服务器线程
    m_thread = CreateThread(NULL, 0, ServerThread, this, 0, NULL);
    if (m_thread == NULL) {
        Stop();
        return false;
    }

    return true;
}

void HTTPServer::Stop() {
    if (!m_running) {
        return;
    }

    m_running = false;

    // 关闭监听 socket 以解除 accept 阻塞
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }

    // 等待线程结束
    if (m_thread) {
        WaitForSingleObject(m_thread, 5000);
        CloseHandle(m_thread);
        m_thread = NULL;
    }

    WSACleanup();
}

DWORD WINAPI HTTPServer::ServerThread(LPVOID lpParam) {
    HTTPServer* server = (HTTPServer*)lpParam;
    server->Run();
    return 0;
}

void HTTPServer::Run() {
    while (m_running) {
        SOCKET clientSocket = accept(m_listenSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            if (m_running) {
                Sleep(100);
            }
            continue;
        }

        HandleClient(clientSocket);
    }
}

void HTTPServer::HandleClient(SOCKET clientSocket) {
    char buffer[4096];
    int recvSize = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

    if (recvSize <= 0) {
        closesocket(clientSocket);
        return;
    }

    buffer[recvSize] = '\0';

    // 解析 HTTP 请求
    char method[16] = {0};
    char path[256] = {0};
    sscanf(buffer, "%s %s", method, path);

    // 只处理 GET 请求
    if (strcmp(method, "GET") != 0) {
        const char* response = "HTTP/1.1 405 Method Not Allowed\r\n"
                              "Content-Type: text/plain\r\n"
                              "Connection: close\r\n\r\n"
                              "Method Not Allowed";
        send(clientSocket, response, strlen(response), 0);
        closesocket(clientSocket);
        return;
    }

    // URL 解码
    std::string decodedPath = URLDecode(path);

    // 移除开头的斜杠
    if (!decodedPath.empty() && decodedPath[0] == '/') {
        decodedPath = decodedPath.substr(1);
    }

    // 默认页面
    if (decodedPath.empty() || decodedPath == "/") {
        decodedPath = "index.html";
    }

    // 获取资源数据
    std::string content = GetResourceData(decodedPath);

    if (content.empty()) {
        // 404 Not Found
        const char* response = "HTTP/1.1 404 Not Found\r\n"
                              "Content-Type: text/html\r\n"
                              "Connection: close\r\n\r\n"
                              "<html><body><h1>404 Not Found</h1></body></html>";
        send(clientSocket, response, strlen(response), 0);
    } else {
        // 构建响应
        std::string mimeType = GetMimeType(decodedPath);

        char header[512];
        sprintf(header, "HTTP/1.1 200 OK\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %d\r\n"
                        "Connection: close\r\n"
                        "Cache-Control: max-age=3600\r\n\r\n",
                mimeType.c_str(), (int)content.size());

        send(clientSocket, header, strlen(header), 0);
        send(clientSocket, content.data(), content.size(), 0);
    }

    closesocket(clientSocket);
}

std::string HTTPServer::GetResourceData(const std::string& path) {
    // 在映射表中查找资源 ID
    std::map<std::string, int>::iterator it = g_webuiResourceMap.find(path);

    if (it == g_webuiResourceMap.end()) {
        return "";
    }

    int resourceId = it->second;

    // 使用整数值查找资源
    HMODULE hModule = GetModuleHandle(NULL);
    HRSRC hRes = FindResourceA(hModule, MAKEINTRESOURCEA(resourceId), "RT_HTML");

    if (hRes == NULL) {
        return "";
    }

    HGLOBAL hLoaded = LoadResource(hModule, hRes);
    if (hLoaded == NULL) {
        return "";
    }

    void* pData = LockResource(hLoaded);
    DWORD size = SizeofResource(hModule, hRes);

    if (pData == NULL || size == 0) {
        return "";
    }

    return std::string((const char*)pData, size);
}

std::string HTTPServer::GetMimeType(const std::string& path) {
    std::string ext;

    size_t dotPos = path.find_last_of('.');
    if (dotPos != std::string::npos) {
        ext = path.substr(dotPos + 1);
    }

    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "js") return "application/javascript; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";
    if (ext == "xml") return "application/xml; charset=utf-8";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    if (ext == "woff") return "font/woff";
    if (ext == "woff2") return "font/woff2";
    if (ext == "ttf") return "font/ttf";
    if (ext == "eot") return "application/vnd.ms-fontobject";
    if (ext == "webmanifest") return "application/manifest+json";

    return "application/octet-stream";
}

std::string HTTPServer::URLDecode(const std::string& url) {
    std::string result;
    for (size_t i = 0; i < url.length(); i++) {
        if (url[i] == '%' && i + 2 < url.length()) {
            char hex[3] = {url[i + 1], url[i + 2], 0};
            char c = (char)strtol(hex, NULL, 16);
            result += c;
            i += 2;
        } else if (url[i] == '+') {
            result += ' ';
        } else {
            result += url[i];
        }
    }
    return result;
}
