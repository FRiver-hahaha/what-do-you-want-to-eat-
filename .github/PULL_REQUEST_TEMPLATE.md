## 变更说明

<!-- 做了什么、为什么 -->

## 影响范围

- [ ] 服务端 `server/`
- [ ] 共享协议 `shared/protocol/`
- [ ] 客户端 `client/`
- [ ] Agent `agent/`
- [ ] 迁移 `migrations/`
- [ ] 文档

## 验证证据

<!-- 粘贴命令与原始输出；没有证据的“应该可以”不会被合并 -->

```bash
ctest --test-dir build --output-on-failure
python -m unittest agent.tests.test_smoke
```

## 平台验证

- [ ] Linux
- [ ] Windows（若涉及客户端）
- [ ] 明确标注未验证项

## 风险与回滚

<!-- 是否有破坏性变更、如何回滚 -->

## 检查清单

- [ ] 未提交密钥、`.env`、`dist/`、构建产物
- [ ] 重构与功能变更未混在同一提交
- [ ] 文档已同步（architecture / RUNBOOK / AGENT_win）
