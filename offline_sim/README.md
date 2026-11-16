# 离线回放与合成 oracle

本目录包含匿名实录摘录和独立合成 oracle，不能标成“全部为合成数据”，也不能当作当前 DLL 的实机成功率证明。默认运行不需要游戏启动、原始监测日志、账号配置或第三方 Python 包。

## 文件与证据边界

- `trajectories/sc17_85fps_early_native.json` 是 65 次已匿名化的实录起手片段，保留相对观察时序、动画和装备数值。原始资料没有完整队列、骨骼或真实接触时刻，相关未知字段仍为 `null` 或 `false`。
- `test_early_native.py` 将这些观察送入原生 cue 辅助函数，以及 Windows 下的历史 native-cue fixture。单次采样、重复记录、缺读、换招和同 ID 回滚也有回归。其 `[100,140)` 激活窗和已知直线首碰是独立合成 oracle，不是实测装备参数或实录首碰标签。
- `trajectories/sc18_parry_gate.json` 是 35 个历史请求及部分观察结果的匿名摘录，参与者用局部编号，不包含原始 actor 地址、PID、账号或路径。`test_parry_evidence.py` 回放几何权限 gate，并补充 96 组合成 bitmask/decision 输入。对手意图、实际输入来源和物理首碰仍未知。
- `early_native_replay.c` 是供脚本编译的最小 bridge。实际临时可执行文件写入系统 TEMP，运行结束由 `TemporaryDirectory` 清理。

历史 fixture 中的 build 标签和墙钟标记是来源范围信息。没有随目录附带原始 capture。Windows fixture 的输出可能沿用历史的 `production selection` 字样，该入口用于历史逻辑比较；SC23 当前调度仍需由项目主测试入口验证，不能将历史回放计数等同于当前功能的实机效果。

## 运行

需要 Python 3 和可用的 GCC；Windows 集成回放需要 x64 MinGW-w64 GCC。从项目根执行，例如已设置 `COPYDOG_GCC` 后：

```powershell
python -B offline_sim/test_early_native.py --compiler $env:COPYDOG_GCC
python -B offline_sim/test_parry_evidence.py --compiler $env:COPYDOG_GCC
```

编译器选择顺序为 `--compiler`、`COPYDOG_GCC`、PATH 上的 `gcc`。不会查找原作者机器上的父目录 toolchain。两个脚本只使用 Python 标准库，无需 pip 安装。

脚本均按自身文件位置寻找本项目 `src/`、`tests/` 与本目录 fixture，不依赖调用者当前工作目录。原生 cue 的 Windows 集成源为 `../tests/test_native_cue_dispatch.c`。非 Windows 运行会明确跳过 Win32 集成部分，不能据此声称完整 Windows 套件通过。

`test_parry_evidence.py --verify-capture PATH` 是明确可选的来源复核，仅对调用者自行提供的原始日志目录运行；默认回放不会进入该分支，也不需要此目录。该选项不下载、不上传任何资料。

这些回放执行编译和行为断言，不计算或比对文件哈希、摘要或 CRC。

## 本次发布副本验证

两个入口已从发布项目根使用 x64 MinGW-w64 GCC 15.2.0 实际运行，O0/O2 均通过。

- 原生 cue 回放在可见 ID 切换前唤醒并确认 65/65，真实接触时间标签仍是 0/65。
- Windows 历史选择入口在名字 cue 时为 65/65；最早同一记录的发现为 sidecar-first 60/65、main-first 58/65。不能将这两项写成“所有片段均在第一条记录唤醒”。
- 合成 `[100,140)` oracle 覆盖 65/65，另有 8 个窗口边界和 9 个不可信输入检查。
- 几何权限回放包含 35 个历史请求、355 个计划窗口记录和 96 组合成输入。

这次验证只依赖发布树内 `src/`、`tests/`、本目录五个回放文件和显式指定的外部编译器。未读取原始 capture，临时可执行文件已随测试上下文清理。
