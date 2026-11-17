# 贡献指南

## 分支与提交

- 从 `main` 或当前迭代分支切出：`feat/<scope>`、`fix/<scope>`、`chore/<scope>`。
- 禁止直接向 `main` 推送；所有改动走 Pull Request。
- 提交信息使用 Conventional Commits：
  - `feat(server): add meal.create frame handler`
  - `fix(client): start reader thread before handshake`
  - `docs: update Windows build steps`
  - `chore(ci): cache muduo build`
- 一个提交只做一件事；重构与功能变更分开。

## PR 门禁

PR 描述必须包含：

1. 变更目的与影响范围（服务端 / 客户端 / Agent / 文档）。
2. 验证命令与**原始输出**（不是“应该可以”）。
3. 回滚方式。
4. 若涉及未在目标平台验证的内容，明确标注“未验证”。

必须通过：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DEAT_MUDUO_ROOT=<muduo>
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
python -m unittest agent.tests.test_smoke
node --check app.js && node --check bridge.js && node --check agent-adapter.js
```

涉及数据库的改动还必须附迁移前后校验（行数、抽样、`EXPLAIN`、回滚验证）。

## 代码风格

- C++17；4 空格缩进；文件名与命名空间遵循现有布局；公共接口必须有清晰注释说明约束。
- Python：标准库优先；类型标注；避免引入第三方运行时依赖。
- JS：不使用构建工具；改动需通过 `node --check`。
- 数据库：只允许增量迁移，必须成对提供 rollback；禁止在迁移中 DROP 来源不明的表。
- 序列化只允许 JSON；禁止 protobuf/MessagePack/CBOR。

## 安全底线

- 不提交 `.env`、Key、密码、token、数据库转储、日志中的明文凭据。
- 不新增服务端 LLM 调用、模型域名出网、代理中转接口。
- Redis 仅服务端连接，禁止 pub/sub、禁止第二实例。
- 客户端只保留一条业务 TCP；不新增隐藏端口。

## 目录约定

- 共享协议只放在 `shared/protocol/`，server/client/bench 必须同源。
- 服务端业务按 `server/business/<domain>/` 组织，禁止把 SQL 写进网络层。
- 测试放 `tests/{protocol,unit,integration,load}`；不把临时脚本提交到根目录。
- `chatroom-backend/` 为退役基线，禁止新增功能，只允许迁移取用。