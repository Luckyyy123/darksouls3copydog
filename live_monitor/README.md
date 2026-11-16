# 诊断监测

此目录不随 DLL 自动运行。Windows、Python 3.10+；不需要第三方 Python 包。
启动前可设置 `COPYDOG_PYTHON`，否则使用 PATH 中的 `python.exe`。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\live_monitor\Start-Monitor.ps1 -Hours 2
powershell -NoProfile -ExecutionPolicy Bypass -File .\live_monitor\Start-Monitor.ps1 -Status
powershell -NoProfile -ExecutionPolicy Bypass -File .\live_monitor\Stop-Monitor.ps1
python -B -m unittest discover -s live_monitor -p "test_*.py"
```

启动脚本建立独立会话，后台记录动作、队列提交、动画确认、远程武器和地面范围威胁。
主监测按 `DarkSoulsIII.exe` 的名称、PID 和存活状态追踪游戏，不校验文件哈希。
游戏退出会释放本轮句柄、清空该轮归属；会话未到截止时间时等待下一游戏 PID。
到时或收到 STOP 文件后有序退出。停止脚本只等待已登记且身份匹配的监测进程，
超时会明确报告；不要以强杀游戏作为结束监测的方法。

结果保存在 `sessions/`，`active_session.json` 是本机运行状态，两者都不应提交到公开仓库。
日志可能含网络地址、动作、角色信息和指针；分享前提取必要片段并匿名化。
“请求过”“动画确认”“弹反实际成功”是不同证据，自动请求和手动输入也不能混为一谈。

可选网络 ETW 子进程需要单独构建，见 [latency_etw](latency_etw/README.md)。
缺少它时启动脚本报告 `network_startup_error`，其他已就绪的采集器仍可运行。
ETW 权限不足也会单独报错；不要把缺失测量当成零延迟。显示侧车不等于端到端输入延迟。
本目录只诊断，不会自动加载 DLL；旧版本游戏、未载入本分支 DLL 或布局不匹配会使字段缺失。
