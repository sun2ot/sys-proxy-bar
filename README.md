# SysProxyBar

一个轻量级的Windows系统代理控制工具。

## 功能特性

- ✅ 快速开启/关闭系统代理
- ✅ 配置代理服务器地址和端口
- ✅ 图形化配置绕过列表（支持列表管理）
- ✅ 任务栏托盘图标显示状态（蓝色=已开启，红色=已关闭）
- ✅ 开机自启动支持
- ✅ 极其轻量，使用C++原生开发
- ✅ 完全静态链接，无需额外依赖

## 界面预览

**托盘菜单**：
- 切换代理
- 设置（图形化配置界面）
- 开机自启动
- 退出

**设置对话框**：
- 代理服务器配置
- 端口配置
- 绕过列表管理（添加/删除/清空）

## 系统要求

- Windows 7 或更高版本
- TDM-GCC 10.3.0（用于编译）
- CMake 3.15 或更高版本

## 快速开始

### 编译项目

**安装编译环境**：
```powershell
scoop install tdm-gcc
```

**编译**：
```bash
build-tdm.bat
```

**输出**：`build\bin\SysProxyBar.exe` (约750KB)

### 运行程序

双击 `SysProxyBar.exe` 或使用：
```bash
run.bat
```

## 使用说明

### 1. 切换代理状态
- 右键点击托盘图标，选择"切换代理"
- 或双击托盘图标快速切换
- 🔵 蓝色图标 = 代理已开启
- ❌ 红色图标 = 代理已关闭

### 2. 配置代理
- 右键点击托盘图标 → "设置"
- 在对话框中配置：
  - **代理服务器**：如 `127.0.0.1`
  - **端口**：如 `7890`
  - **绕过列表**：
    - 在输入框输入规则（如 `localhost`）
    - 点击"Add"添加到列表
    - 选中列表项，点击"Remove"删除
    - 点击"Clear All"清空所有规则
- 点击"OK"保存配置

### 3. 开机自启动
- 右键托盘图标 → "开机自启动"
- 可随时启用或禁用

### 4. 退出程序
- 右键托盘图标 → "退出"

## 技术实现

- **语言**: C++17
- **API**: WinAPI
- **代理控制**: 通过修改Windows注册表实现
- **系统通知**: NOTIFYICONDATA
- **构建工具**: CMake + TDM-GCC
- **字符编码**: GBK（中文支持）

## 工作原理

Windows系统代理设置存储在注册表中：
```
HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Internet Settings
```

主要键值：
- `ProxyEnable` (DWORD): 0=关闭, 1=开启
- `ProxyServer` (SZ): 代理服务器地址和端口
- `ProxyOverride` (SZ): 绕过列表（分号分隔）

程序通过读写这些注册表项来控制系统代理，并调用 `InternetSetOption` 通知系统刷新设置。

**开机自启动**：
```
HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run
```
- 值名称：`SysProxyBar`
- 值数据：程序exe文件的完整路径

## 默认配置

- **服务器**: 127.0.0.1
- **端口**: 7890
- **绕过列表**: localhost;127.*;<local>

## 项目结构

```
sys-proxy-bar/
├── src/
│   ├── main.cpp              # 主程序
│   ├── proxy_manager.*       # 代理管理器（注册表操作）
│   ├── tray_icon.*           # 托盘图标
│   ├── settings_dialog.*     # 设置对话框
│   ├── resource.h            # 资源定义
│   └── resource.rc           # 资源文件
├── build-tdm.bat             # TDM-GCC构建脚本
├── build-vs.bat              # Visual Studio构建脚本
├── run.bat                   # 运行脚本
├── CMakeLists.txt            # CMake配置
└── README.md                 # 本文件
```

## 编译说明

### 使用TDM-GCC（推荐）

```bash
# 1. 安装TDM-GCC
scoop install tdm-gcc

# 2. 运行构建脚本
build-tdm.bat

# 3. 输出文件
build\bin\SysProxyBar.exe
```

## 常见问题

**Q: 中文显示乱码？**
A: 确保编译时使用了 `-fexec-charset=GBK` 选项（已在CMakeLists.txt中配置）

**Q: 程序无法启动？**
A: 确保系统是Windows 7或更高版本，并且没有安全软件拦截

**Q: 代理不生效？**
A: 检查配置是否正确，或查看注册表中的设置

**Q: 任务管理器看不到自启动项？**
A: 程序使用注册表实现自启动，不在启动文件夹中。可以通过注册表编辑器查看：
`HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`

## 自定义图标

当前使用Windows系统图标。如需自定义：

1. 准备 `.ico` 格式图标文件（建议32x32像素）
2. 将图标文件放在 `src/res/icons/` 目录
3. 修改 `src/tray_icon.cpp` 中的 `LoadIcons()` 函数
4. 重新编译
