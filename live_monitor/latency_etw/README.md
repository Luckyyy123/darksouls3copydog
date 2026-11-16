# 可选网络 ETW 侧车

这是独立的 C# 控制台采集器。正常构建 Copydog DLL 不需要它。
需要 Windows、.NET 10 SDK 和还原 NuGet 包的网络连接：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-monitor.ps1
```

构建后 `Start-Monitor.ps1` 从本目录 `bin/Release/net10.0-windows/LatencyEtw.exe`
寻找它。`bin/`、`obj/` 和运行采集结果不纳入源码包。开启 ETW 会话可能需要管理员权限。
本发布整理只提供源文件和项目声明，没有附带 NuGet 或 .NET 二进制；如另行分发这些依赖，
应同时保留对应包要求的许可和通知。

项目直接使用 `Microsoft.Diagnostics.Tracing.TraceEvent` 4.0.0，依赖及版本由 csproj 和 NuGet 还原决定。
连接时序估计参考 [tremwil/DS3ConnectionInfo](https://github.com/tremwil/DS3ConnectionInfo)，
保留其参考来源；这不代表整个项目已确认采用某种统一开源许可证。
这里的网络观测值不能直接当作本地输入延迟，也不能把单程时延固定认作 RTT/2。
