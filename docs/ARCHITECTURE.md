# 主要结构

| 目录/文件 | 职责 |
|---|---|
| `src/copydog.c` | 观察/轮询、`PollCombatActions` 队列仲裁、单一弹反分派、其它动作连接 |
| `src/parry_diagnostic.inc` | SC23 原生动作 episode、许可分类、时间决策、最终 admission；沿用历史文件名 |
| `src/defense.c` | 请求、等待动画、active、cancelled 生命周期；因果确认许可独立于真实动作观察 |
| `src/dynamic_parry.h` | 相位速率、墙钟步长、半开有效窗与离散观察点的数学 |
| `src/dynamic_parry_data.h` | 分离的伤害/许可交集、独立许可候选、自身工具有效段 |
| `src/prediction*.c/.h` | 姿态、形状、移动/旋转外推、连续相交、可信范围与预算 |
| `src/combat*.c/.h` | 子弹、范围威胁、过期票据与其它战斗模型 |
| `src/control.c` | 背刺、处决、输入/方向与状态保护 |
| `src/runtime.c`, `hook_lifetime.c`, `receive_bridge.c` | 运行布局、钩子/线程生命周期及共存接收路径 |
| `src/loader.c` | Windows x64 加载器、配置、preflight/status/unload |
| `tests/` | 生产入口模拟内存回归，以及显式历史 solver 对比 |
| `offline_sim/` | 匿名实录片段和合成 oracle 的最小回放闭包 |
| `live_monitor/` | 本机只读采样、事件解释、PID 重连与正常结束 |

## 当前生产弹反链

`PollingThreadProc → PollCombatActions → CheckFrameTargets → TryFrameAutomaticParry → TryDiagnosticParry → DispatchDefense`。

分类、计时与提交不是三个能够各自出手的模块。只有最终 admission 和共享队列空闲后才进入一次原生尝试。
关闭 `CopydogDiagnosticParryEnabled` 会停用新弹反路径，不会回切历史 solver。

当前许可候选来自资源事件，并不是物理碰撞已成立的证明。自动弹反路径没有调用旧 solver 的几何 shadow；
预测模块保留给其它模型和离线比较，不能由预测计数反推本轮弹反一定由 CCD 决定。

`*Legacy*ForOffline` 入口用于已有历史回归，生产 worker 不调用。它们的存在不等于生产存在回切。

## 队列规则

已提交的弹反、翻滚、背刺和处决在排空之前拥有自动动作队列。
最终外部状态核验在 `CopydogDefenseBegin` 之前完成，避免把自己刚预留的事务当成 Busy 自拒。
真实原生尝试不论返回成功或失败均占用本帧，等待/读取拒绝不消耗攻击机会。
观察者可以更新，新的输入与 SetFacing 等副作用不能越过队列所有权。
