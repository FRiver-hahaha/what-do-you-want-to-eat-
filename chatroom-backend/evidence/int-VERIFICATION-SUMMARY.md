# P6 集成环境验证汇总（eat-integration 分支 feat/backend-crop）

> 环境：目标仓库 main b5887a2 + chatroom-backend/ 子目录（P0–P5 已验收代码迁移）
> 验证日期：2026-11-13；全部为本轮真实执行输出（对应 int-*.log）

## 构建
rm -rf 后干净构建：cmake 0 / make 0 / error 0；产物 server/client/transit/web（无 file_server）。

## 测试（96 项断言全过）

| 测试 | 结果 |
|---|---|
| smoke_cropped（独立验证库 chatroom_int，与旧库完全隔离） | 48/48 |
| code_security basic | 16/16 |
| attack_lifecycle | 8/8 |
| concurrency | 3/3（8 路成功 1） |
| crash_recovery | 6/6 |
| config_guard | 5/5 |
| account_cascade | 10/10 |

## 与原 P4/P5 环境的差异说明

1. 目标仓库无 C++ 构建链——本轮在其工作副本内独立完成干净构建与全部测试（同 WSL 环境，编译器/依赖与 P4 完全一致，故未重复 42 项基线与 TTL 17 项——裁剪版测试面见上表 96 项）
2. 代码为逐字节迁移（tar 工作树），与已验收工作区一致，git 层面无合并改写
3. smoke_cropped 采用新建独立库 chatroom_int 执行（避开对旧 chatroom 库的数据清理操作）；旧库保留原状
4. 过程记录：code_security 一次同秒重跑因时间戳邮箱撞已注册账号 14/2（无效执行，以首次 16/16 落盘为准）；account_cascade 首跑误用普通用户致 sql() 空 5 FAIL（root 重跑 10/10）——均为执行方式问题，非代码缺陷
