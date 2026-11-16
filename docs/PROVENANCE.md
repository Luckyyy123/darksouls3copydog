# 来源与署名记录

## 早期 Copydog

维护者提供的早期源码副本包含 `copydog.c`（404行）、`copydog.h`（105行）、`gamedata.c`（581行），
另有构建、注入和卸载脚本。它本来就有弹反、翻滚、背刺、复制和网络相关实现。
本分支继承这一基础；作者、原 GitHub 链接及原许可证尚未确认，不给它虚构一个来源 URL。
个人接收位置不公开。

## 本维护分支

新增或大幅改造了模块化构建、地址/钩子生命周期、动作事务、原生时钟、自动弹反分类/调度、
连续几何预测、子弹/地面范围威胁、监测、离线回归及发布整理。
README 中的改动表是功能对照，不是版权独立性或“全部原创”的结论。

维护者负责需求、测试反馈与验收。开发中使用 Codex、Claude Opus 5.5 和 DeepSeek Flash；
生成代码、修改、审核建议及反例由主控智能体筛选和整合，维护者负责实机反馈与验收；
并非所有模型结论均被采纳，也不表示维护者逐行人工审查过全部源码。

## 生成数据与外部参考

- `prediction_data.h`、`phase_attack_data.h`、`dynamic_parry_data.h`、弹道参数等含游戏资源派生表；
  这些表参与编译，不声称没有第三方或游戏派生数据进入 DLL。
- 原始 HKX/FLVER/PARAM、解包档案、模型导出与私有实录不随包分发。
- 本机曾参考 FromSoftware-rs、DSAnimStudio/DSAS、SoulsAssetPipeline/SoulsFormats 等项目或工具；
  这些完整参考树、工具程序集与原始导出工程不随本树分发。
- 未直接链接参考库，不等于已经证明绝无转写或其它派生内容。参考工具的许可不能替代资源本身的许可。

## 工具链与可选监测

- 主 C/S 构建使用 MinGW-w64；runtime notices 原文在 `third_party/notices/MinGW-w64-runtime.txt`。
- 主 DLL 的 Windows 导入依赖为系统/CRT 库；它不直接动态链接上述 Rust/C#参考库。
- 可选 ETW helper 使用 Microsoft.Diagnostics.Tracing.TraceEvent 4.0.0，构建通过 NuGet 恢复依赖。
- ETW 延迟估算参考 tremwil 的 DS3ConnectionInfo / ETWPingMonitor：
  https://github.com/tremwil/DS3ConnectionInfo 。其上游说明允许复用部分实现并要求署名；
  这里保留归属，不把它擅自称为一个已确认的标准 MIT 项目。

此记录说明已知来源与未知边界，不是对全部许可关系的法律结论。
