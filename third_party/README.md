# 第三方说明

工具链本体不随源码树分发。使用 MinGW-w64 构建时需要保留其 runtime notices；
对应原文在 `notices/MinGW-w64-runtime.txt`，二进制构建脚本会复制到产物 notices 目录。
GCC runtime 的 GPL 原文与 Runtime Library Exception 也在同目录随附。
这些文本分别说明对应 runtime 组件，不是整个 Copydog 的许可证。

可选 ETW 项目声明 Microsoft.Diagnostics.Tracing.TraceEvent 4.0.0。
其依赖通过 NuGet 恢复，不把本机 cache 和私有安装路径复制进仓库。
重新分发 ETW 发布产物时应同时携带所解析依赖的适用 notices。

独立 ETW 估算参考：https://github.com/tremwil/DS3ConnectionInfo 。保留归属要求，
不虚构该参考实现的标准许可证。完整来源边界见 ../docs/PROVENANCE.md。

FromSoftware-rs、DSAnimStudio、SoulsAssetPipeline 等本机参考树未分发；
资源派生 C 表仍在 src 中，不能把排除参考树当成全部许可已清理完毕。
