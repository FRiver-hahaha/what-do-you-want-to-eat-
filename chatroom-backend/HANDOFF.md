# HANDOFF — 给学长的接手说明

## 1. 你拿到的是什么

ChatRoom 后端裁剪版（`chatroom-backend/` 子目录）：基线 a1e654b 上完成
聊天功能删除 + 群组功能删除 + 运行时修复 + 安全加固。
保留账号、好友、在线状态三大能力。

## 2. 保留功能（协议 type 0–12，共 13 个路由）

- 0 心跳
- 1 注册（邮箱+验证码）/ 2 密码登录 / 3 发验证码 / 4 验证码登录 / 5 重置密码
- 6 退出登录 / 7 注销账号（级联删除 user+friend）
- 8 好友列表（含在线状态与拉黑标记）/ 9 好友申请 / 10 审批 / 11 拉黑解除 / 12 删除好友
- 在线状态：登录/退出即时更新，断线自动复位，崩溃重启自动恢复

传输：TCP+UTF-8，一行一条 `\n` 结尾，单条上限 64KB；code=1 成功 code=0 失败。
服务端推送仅一类：好友申请（type9 code2）+ 登录响应 offline[]（离线好友申请）。

## 3. 已删除（前端请勿再调用）

- 聊天：13 私聊 / 14 私聊历史 / 25 群聊 / 26 群聊历史 / 27 待收文件
- 群组：15 群列表 / 16 建群 / 17 邀请 / 18 申请入群 / 19 审批 / 20 成员列表
  / 21 设撤管理员 / 22 踢人 / 23 退群 / 24 解散

以上 type 一律返回 `{"type":0,"code":0,"msg":"异常请求"}`，不落库、无推送。
file_server 二进制与 filetransfer 协议已整体移除；message/file/group_info/group_user
四张表已从 init.sql 删除（存量库如需清理请另行评估）。

## 4. 配置与启动

```bash
cd chatroom-backend
cp .env.example .env && chmod 600 .env && vi .env   # 填 MySQL/Redis/SMTP
set -a && . ./.env && set +a
sudo mysql < init.sql   # 建库 + 2 张表；应用账号按模板建（密码进 .env）
mkdir -p build && cd build && cmake .. && make -j$(nproc)
cd bin && ./server      # Server started on port 8000 worker_threads=4
./transit               # 网页端需启动（10000）
./web                   # 可选（9000，仅托管 bin/static）
```

- 凭据全部走环境变量，源码零硬编码；历史泄露的 SMTP 授权码已移除，部署前到邮箱后台重置
- `CHATROOM_SMTP_DRY_RUN=1` 仅测试用（验证码打日志），生产必须移除
- web 服务只挂载 `./static` 目录，不暴露工作目录（含二进制与配置）

## 5. 数据库

chatroom 库 2 张表（user / friend）；应用账号权限仅 SELECT/INSERT/UPDATE/DELETE。
MySQL 8.4 导入 init.sql 若遇 `--注释` 解析错误，见 KNOWN-ISSUES。

## 6. 回归测试（tests/，已按最终裁剪范围修订）

8 个协议级脚本，共 104 项断言，本轮全绿（证据 evidence/p7b-*.log）：
smoke_cropped 42 / basic 16 / ttl 17 / attack 8 / concurrency 3 / crash 6 / config 5 / cascade 7。
统一参数：`--mysql-socket /run/mysqld/mysqld.sock --mysql-db <测试库>`。
crash_recovery / config_guard 自管服务实例，跑前先停 8000 服务并 source .env。

## 7. 你的后续工作边界

1. 前端适配：注册需带 code 字段；移除聊天/群组/文件全部入口与页面
2. Agent 接入（AGENT.md 的 EAT_SERVER_BASE_URL 指向本服务 8000）
3. 整体联调与演示包装

## 8. 快速排障

- 服务起不来 → stderr FATAL 行（MySQL pool / Redis AUTH，凭据或地址错）
- "已在其他设备登录" → 并发或残留 state；重启服务即复位
- 注册收不到码 → SMTP 空或 DRY_RUN 未开；DRY_RUN=1 时码在服务日志