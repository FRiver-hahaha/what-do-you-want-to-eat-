# P0 基线测试说明

- 测试：smoke_baseline.py --port 8000 --mysql-socket /run/mysqld/mysqld.sock
- 结果：PASS=42 FAIL=0（A 账号 7 + B 好友 8 + C 群组 17 + D 聊天 5 + E 在线 3 + F 离线链路 2）
- 说明：该脚本 stdout 当时未重定向落盘，完整逐项输出（含每项响应报文）见 P0 验收会话记录，已获用户验收。
  本目录 00-baseline-original-server.log 为当时原始基线服务（a1e654b 构建产物）的真实运行日志。
- 对照参考：接手包 evidence/00-baseline-original.log 为前轮验证者同脚本 42/42 记录，两轮结论一致。
- P1 阶段同脚本在裁剪版上的对照结果：37 PASS + 5 FAIL（D 组聊天不可达，裁剪预期）。
