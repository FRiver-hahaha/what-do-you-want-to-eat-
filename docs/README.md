# 文档索引

| 文档 | 内容 |
|---|---|
| [README](../README.md) | 项目概览、快速开始、能力边界 |
| [architecture](architecture.md) | **当前权威架构**：三线合并结果、运行形态、已打通链路、构建与测试现状 |
| [ROADMAP](ROADMAP.md) | 后续实现路线：真实菜单、用户模型/自定义 API、独立 App、高并发与 io_uring |
| [DELIVERY-STATUS](DELIVERY-STATUS.md) | 本轮交付物、验证证据、未完成项、待确认的破坏性操作 |
| [RUNBOOK](RUNBOOK.md) | Linux 服务端（Docker/裸机）与 Windows 客户端运行步骤、局域网排查 |
| [AGENT_win](../AGENT_win.md) | Windows 构建、GUI 验证清单、进度同步与 GitHub 上传 |
| [AGENT_Linux](../AGENT_Linux.md) | Linux 历史交接与环境约束（保留） |
| [AGENT](../AGENT.md) | Agent 能力说明（历史口径，状态见 architecture） |
| [protocol/v1](protocol/v1.md) | 帧格式、错误码、方法白名单 |
| [design/00-master-plan](design/00-master-plan.md) | 阶段 S0–S4、裁决与门禁 |
| [design/01-backend-merge](design/01-backend-merge.md) | P7/P8 与新架构的模块映射 |
| [design/02-windows-app](design/02-windows-app.md) | Windows App 选型、IPC、传输抽象 |
| [design/03-refactor-cleanup](design/03-refactor-cleanup.md) | 冗余清单、四技能整改、风险审查 |
| [design/04-linux-test-plan](design/04-linux-test-plan.md) | 依赖、构建矩阵、测试分层与证据 |

## 阅读顺序建议

1. 新加入：README → architecture → RUNBOOK。
2. 要在 Windows 构建：AGENT_win → design/02-windows-app。
3. 要继续迁服务端业务：design/01-backend-merge → design/03-refactor-cleanup → DELIVERY-STATUS。
