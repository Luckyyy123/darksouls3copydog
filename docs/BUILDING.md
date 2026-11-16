# 构建与验证

## 必需工具

- Windows x64、PowerShell 5.1 或更新版本。
- x86_64 MinGW-w64 GCC、GNU assembler、windres；当前源码使用 GNU C11，不宣称 MSVC 可直接编译。
- Python 3.10+：仅用于测试/监测，不是主 DLL 的运行时依赖。
- 可选：.NET 10 SDK，用于构建 ETW helper。

编译器在 PATH 查找；也可以设置 COPYDOG_GCC 或显式传 -Compiler。Python 同理支持 COPYDOG_PYTHON。
不要把工具链、SDK、NuGet cache 或 Python 安装目录提交到仓库。

```powershell
.\build.ps1
.\test.ps1
python -B -m unittest discover -s live_monitor -p 'test_*.py'
```

DLL 使用 `-std=gnu11 -O2 -shared -Wall -Wextra -Werror -Wno-multichar`，显式链接 winmm/psapi/user32；
Windows/CRT 默认库由 MinGW 工具链提供。加载器还链接 advapi32，使用 Unicode console 入口与 asInvoker manifest。
默认去除调试信息，不把本机编译目录写进发布调试信息；-DebugSymbols 构建保留符号并映射路径前缀。

build.ps1 可用 -OutputDirectory / -BuildDirectory 将产物放在仓库外，test.ps1 可用 -OutputDirectory。
默认构建保留 src/build_id.h 中的基线标识；修改行为时应更新该标识，并记录变更。

生成 C 表已经随源码提供，因此常规编译不要求原始游戏资源或私人采集。
重新生成这些表需要另行准备并核实原资源、工具和格式，发布树不捆绑它们。

## 可选监测 helper

```powershell
.\scripts\build-monitor.ps1
```

该命令经 NuGet 恢复声明的 TraceEvent 依赖；这是构建步骤，不是 DLL 的自动更新或游戏运行时下载。
不把本机 NuGet/obj/bin 原样复制进源代码包。第三方归属见 third_party/README.md。

默认测试不启动/注入真实游戏，不调用加载器操作当前游戏。
卸载 mock 源码是额外的开发材料，不包含在默认 test.ps1；不要把运行同名模拟游戏的试验与实际游戏监测混在一起。
