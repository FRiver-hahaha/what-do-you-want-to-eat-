# HANDOFF — 给学长的接手说明

## 1. 你拿到的是什么

ChatRoom 后端裁剪版：在基线 a1e654b 上应用三个接手包补丁（裁剪/运行时修复/安全加固），
保留账号、好友、在线状态、群组管理，删除全部聊天功能。分支 refactor/backend-crop，
改动在工作区未提交（16 文件 +348/−543），验收通过后统一提交。

## 2. 保留功能（协议 type）

- 账号：0 心跳 / 1 注册（邮箱+验证码）/ 2 登录 / 3 发码 / 4 验证码登录 / 5 重置密码 / 6 退出 / 7 注销（级联删）
- 好友：8 列表（含在线/拉黑）/ 9 申请 / 10 审批 / 11 拉黑 / 12 删除
- 群组：15 我的群 / 16 建群 / 17 邀请 / 18 申请入群 / 19 审批 / 20 成员列表 / 21 设撤管理员 / 22 踢人 / 23 退群 / 24 解散
- 传输：TCP+UTF-8，一行一条 \n 结尾，单条上限 64KB；code=1 成功 code=0 失败；
  agree/block/admin 兼容 true/false 与 1/0

## 3. 已删除（前端请勿再调用）

type 13 私聊 / 14 私聊历史 / 25 群聊 / 26 群聊历史 / 27 待收文件——全部返回
{"type":0,"code":0,"msg":"异常请求"}，不落库、无推送。file_server 二进制已不存在。
message/file 表保留结构但零写入，请勿再展示聊天/文件入口。

## 4. 配置与启动

```bash
cp .env.example .env && chmod 600 .env && vi .env   # 填 MySQL/Redis/SMTP
set -a && . ./.env && set +a
mkdir -p build && cd build && cmake .. && make -j$(nproc)
cd bin && ./server     # Server started on port 8000 worker_threads=4
```

- 凭据全部走环境变量，源码零硬编码（历史泄露的 SMTP 授权码已从源码移除，部署前请到邮箱后台重置）
- CHATROOM_SMTP_DRY_RUN=1 仅测试用（验证码打日志），生产必须移除
- 服务端推送只有三类：好友申请（type9 code2）/ 群邀请（type17 code2）/ 入群申请（type18 code2）+ 登录响应 offline[]

## 5. 数据库

chatroom 库 6 张表（init.sql）；本轮 schema 无变更；message/file 只读结构。
MySQL 8.4 导入 init.sql 注意注释格式（见 KNOWN-ISSUES.md 第 1 条）。

## 6. 你的后续工作边界

按本轮任务分工，以下不在后端交付范围：
1. 前端适配（client/web/transit 未动）：注册需带 code 字段、移除聊天/文件菜单、适配新错误文案
2. Agent 接入
3. 整体联调与演示包装

## 7. 回归测试

接手包 tests/ 8 个脚本可随时复跑（协议级、无需前端）：
smoke_cropped 48 / code_security 16+17 / attack 8 / concurrency 3 / crash_recovery 6 /
config_guard 5 / account_cascade 10。本轮全绿证据在 evidence/。
注意：自管服务类脚本（crash_recovery/config_guard）跑之前先停 8000 实例并 source .env。

## 8. 快速排障

- 服务起不来 → 看 stderr 的 FATAL 行（MySQL pool init failed / Redis AUTH failed，凭据或地址错）
- 账号"已在其他设备登录" → 并发/残留 state；重启服务即复位（resetState）
- 注册收不到码 → SMTP 配置为空或 DRY_RUN 未开；DRY_RUN=1 时码在服务日志里
