Copydog SC23 maintenance branch — experimental baseline
构建标识：SC23-20261116-193139

AI 编写声明：本维护分支绝大多数新增及大幅重写代码由 AI 编写、修改和迭代，
维护者与测试人员主要负责需求、实机反馈和验收；继承的旧 Copydog 代码不归为本次 AI 创作。

1. 保持 copydog.dll、copydog-loader.exe、config.txt 和批处理文件在同一目录。
2. 游戏启动后运行 01_preflight.bat，再按原加载方式运行 02_inject.bat。
3. Shift：自动弹反；同时按原规则暂停自动翻滚。
4. F7：开关自动翻滚。新游戏进程默认关闭，第一次按 F7 开启。
5. 自动背刺遵守原目标、位置、朝向和角色就绪条件。
6. 03_status.bat 查看诊断；04_stop.bat/stop.bat 调用安全卸载。
7. 更新 DLL 后完全退出旧游戏进程，再重新加载。不要把旧驻留版本当成新版。

本版当前达到维护者最低使用要求，但维护者仍不满意，欢迎继续维护或制作 PvE 分支。
当前弹反使用静态许可时机候选，未以距离/CCD接触证明否决，可能对远处真实攻击空弹。
不是 PvE 成品，也没有量化实机成功率。只读监测源码在源码仓库 live_monitor 中，按需构建可选helper。

发布说明没有替未确认的早期 Copydog 上游重新授予许可证。请同时保留 notices 与源码中的来源说明。
详细改动、限制和许可状态请阅读源码仓库 README、docs/KNOWN_ISSUES.md、docs/LICENSING.md。
本二进制包内的许可状态说明为 notices/PROJECT-LICENSING.md。
不执行任何文件哈希、显式CRC或内容摘要校验。
