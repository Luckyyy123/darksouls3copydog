# Copydog — SC23 维护分支

这是基于早期 Dark Souls III Copydog 源码持续修复、拆分和重写的维护分支。
原项目已经包含弹反、翻滚、背刺等功能；本分支不把这些原有功能或上游代码冒称为原创。

**AI 编写声明：本维护分支新增及大幅重写的代码，绝大多数由生成式 AI 编写、修改和迭代完成。**
主要使用 Codex、Claude 和 DeepSeek 协作；维护者与测试人员主要负责需求、实机测试反馈、
功能取舍和验收，不将这些新增实现描述为维护者亲自手写的成果。
这一说明不把继承的旧 Copydog 上游代码归为本次 AI 创作，也不表示全部代码经过人工逐行审查。
“绝大多数”是开发方式的定性说明，没有逐行统计百分比；具体参与方式见 [AI 使用说明](docs/AI_USAGE.md)。

**当前定位是可用测试基线，不是成熟稳定版。** 维护者实测认为 SC23 达到了自己的最低要求，
但对准确性、结构和覆盖范围仍不满意。公开整理这份代码，是希望后来人能够继续维护、
重构算法、补充测试，或者制作真正的 **PvE 专用分支**。不承诺长期持续更新。

基线构建：`SC23-20261116-193139`。源码整理没有改变该基线的游戏行为；构建脚本、文档、
监测解释器路径和测试目录经过独立发布整理。

## 相比早期源码，具体改了什么

以下依据维护者提供的原始 `copydog.c`、`copydog.h`、`gamedata.c` 与当前实现对照整理。
原始副本没有找到 README、LICENSE 或明确作者头；目前未能确认原 GitHub 仓库链接。
大幅修改不代表上游权利自动消失，许可状态见下方说明。

| 部分 | 本分支的具体改动 | 当前边界 |
|---|---|---|
| 自动弹反入口 | SC23 使用一条生产决策链：`CheckFrameTargets → TryFrameAutomaticParry → TryDiagnosticParry → DispatchDefense`；旧求解器改为显式 `*ForOffline` 历史比较入口，生产入口不会回切旧算法 | 历史实现仍可用于离线比较，不代表生产在混用两套出手逻辑 |
| 起手识别 | 引入原生动画轨、动作名称、可见 ID 的分离观察；原生攻击已开始但可见 ID 仍为移动时，也能开始观察 | 无法从两个姿态恢复任意未来动画；证据未知时仍可能漏掉攻击 |
| 可弹分类 | 修正诊断阶段仅凭动画编号尾数 `30000–49999` 就认定攻击的问题；结合武器、完整动作 ID 和资源许可，排除滑步、翻滚残留与射弩等误触发 | 分类表有覆盖边界，不能宣称所有武器、敌人或战技均已支持 |
| 每帧时间判定 | 使用原生相位、实测墙钟更新间隔和自身工具有效段，计算“现在弹 / 再等 / 本刀跳过”；修正慢刀起手即花掉唯一提交机会的问题 | 不固定按 60 FPS，也不把用户显示的网络延迟直接当本地输入延迟 |
| 资源时间窗 | 敌方可弹许可与伤害事件分开保存，不再因两个区间只在端点相接而静默丢掉三类首刀候选；长许可带明确标记 onset-only 候选 | 许可候选不是已验证的物理伤害接触 TTI |
| 动画代际与取消 | 处理取消、变招、同 ID 重启和单次回滚；记录已观察的原生槽，阻止退休旧记录被回读后再次获得提交机会 | 仍无法预知对手在提交之后才进行的取消 |
| 确认与清理 | 将“是否能归因给本次请求”与“真实动作是否结束”分开，修复 unknown 动作值令 CANCELLED 无法退出的状态链；未知基线不伪造确认或标定 | 看到自身抬盾不等于证明输入来源，更不等于弹反命中 |
| 动作仲裁 | 防御、背刺和正面处决共享队列所有权；追击遵守同帧仲裁和自身恢复冷却；已提交事务排空后才允许下一次自动动作；失败的原生尝试也占用本帧，被队列占用拒绝时不先改朝向 | 不把普通走路、跑步视作已拥有输入队列；原生调用失败仍可能已有朝向副作用 |
| 自动翻滚 | 恢复近战、弹道和已建模地面范围威胁的防御入口；保留方向计算、后续攻击重新判定及角色可执行条件 | 爆炸、延迟子弹和地形估算都有模型限制，不保证所有落地爆炸均已覆盖 |
| 背刺与处决 | 保留原背刺几何/就绪条件，增加重复提交及忙碌保护；恢复 SC23 诊断总旁路曾关闭的入口；正面处决使用原生请求而非拿普通 R1 冒充 | 仍依赖目标状态、位置与游戏是否接受请求 |
| 生命周期与构建 | 拆分地址、钩子、线程、接收桥、配置、预测和队列模块；提供 x64 MinGW 构建、加载器和可清理的监测工具 | 只验证当前布局，不能保证兼容所有历史游戏版本或其它 Mod |
| 监测与测试 | 区分观察、时间判定、提交尝试、调用返回、动画确认和战斗结算；进程退出清理后等待新 PID；补充 O0/O2 回归、匿名历史片段和合成碰撞用例 | 测试通过率不能转换为实机弹反成功率 |

与源码对应的入口及后续方向见 [架构](docs/ARCHITECTURE.md)、[已知问题](docs/KNOWN_ISSUES.md) 和 [PvE 改造路线](docs/PVE_ROADMAP.md)。

## 当前使用方式

- Windows x64；使用与当前实现布局兼容的 DS3。
- 按住 **Shift** 启用自动弹反判定，同时按原规则暂停自动翻滚。
- **F7** 切换自动翻滚；新游戏进程默认关闭，首次按 F7 开启。
- 自动背刺沿用原目标、距离、朝向与角色可执行条件。
- 替换 DLL 后重新启动游戏再加载，避免把旧驻留版本当作新版本测试。

**本版本保留临时诊断策略：自动弹反不以距离和 CCD 伤害接触证明作为出手否决条件。**
可弹分类、时间调度、受击/死亡、可读状态、按键与共享队列互斥仍保留。
因此，对远处真实挥击仍可能空弹。其它预测/翻滚模块仍包含碰撞计算，不能把整个项目称为“没有 CCD”。

仓库还保留原项目的部分复制、追击和参数控制代码，以及游戏网络收发钩子；
`atkmult` 的示例值为 1。不要把此分支描述为纯离线程序、PvE 成品或仅提供被动观察的工具。

## 从源码构建

需要 Windows、PowerShell、Windows x64 MinGW-w64 GCC（包含 GNU assembler 和 windres）。
默认在 PATH 查找编译器，也支持 `COPYDOG_GCC` 或 `-Compiler`，不依赖维护者的工具链目录。

```powershell
.\build.ps1
# 编译器没有加入 PATH 时：
.\build.ps1 -Compiler 'C:\toolchains\w64devkit\bin\gcc.exe'
```

产物在 `dist/`：DLL、加载器、原配置、四个编号批处理入口、`stop.bat` 别名与 notices。
默认发布构建不含调试信息；需要调试时使用 `-DebugSymbols`，路径前缀映射会去掉构建机项目目录。
加载器使用 console 子系统与 `asInvoker` manifest；如果游戏处于更高权限级别，加载器需要匹配其权限。
链接库和完整步骤见 [构建说明](docs/BUILDING.md)。

## 测试与监测

```powershell
.\test.ps1
python -B -m unittest discover -s live_monitor -p 'test_*.py'
```

测试另需 Python 3.10+，回放与 Python 监测只使用标准库。
测试 C 文件只使用自有模拟内存，默认回放不读取原始会话、游戏安装文件或维护者工作目录。
匿名历史观察与合成 oracle 的边界见 [offline_sim/README.md](offline_sim/README.md)。

只读监测可以用 `live_monitor/Start-Monitor.ps1` 启动，使用 `Stop-Monitor.ps1` 正常结束。
可选 ETW helper 需要 .NET 10 SDK 构建、对应运行时以及采集 kernel ETW 所需权限，见 [监测说明](live_monitor/README.md)。
监测日志默认只保留在本机，不要把原始会话直接提交到 GitHub。

## 欢迎接手维护或制作 PvE 分支

维护者明确欢迎并允许后来人继续使用、维护和修改其**有权授权的本分支新增实现**，包括 PvE 专用改造；
请保留原 Copydog 与本维护分支的来源记录，明确标注你的修改。
具体贡献建议见 [CONTRIBUTING.md](CONTRIBUTING.md)。

**上游仓库和许可尚未核实，因此本整理没有替全部继承代码重新授予 MIT/GPL 等整仓许可证。**
维护者的上述许可意愿不改变上游、第三方或资源权利人的既有权利。
授权范围、资源派生表与第三方依赖分别记在 [许可状态](docs/LICENSING.md) 和 [来源记录](docs/PROVENANCE.md)。

---

English summary: this is an experimental maintenance branch of an old DS3 Copydog source snapshot.
Most new and substantially rewritten code in this maintenance branch was generated and iterated by AI;
the maintainer and testers primarily provided requirements, gameplay feedback and acceptance decisions.
This does not attribute inherited upstream code to AI or claim a measured line-count percentage.
SC23 is a minimum usable baseline, not a finished or fully measured implementation. Further maintenance
and PvE forks are welcome within the rights described above. The original upstream URL/license remains
unverified. Native parry timing currently uses static permission candidates without requiring CCD contact proof.

