# DELIVERY — ChatRoom 后端裁剪交付说明（P7 最终版）

> 依据接手包 DELIVERY.md 模板填写，所有数据为本轮实测真实值。

## 1. 源码与版本

| 项 | 值 |
|---|---|
| 上游仓库 | https://github.com/lzh070702/ChatRoom |
| 基线 commit | a1e654bde68ee9731c8254fed61ddc030ca926dc |
| 交付分支 | feat/backend-crop（学长仓库 FRiver-hahaha/what-do-you-want-to-eat- 的 PR #1） |
| 交付位置 | chatroom-backend/ 子目录（目标仓库其余文件零改动） |
| 交付日期 | 2026-11-16 |
| 上游许可证 | **无 LICENSE 文件**（见 NOTICE，未编造许可证，合并前建议取得上游授权） |

## 2. 构建环境（本轮实测）

WSL2 Ubuntu 26.04.1 / g++ 15.2.0 / CMake 4.2.3 / MySQL 8.4.13 / Redis 8.0.5 /
hiredis 1.2.0 / nlohmann-json 3.11.3 / glog 0.6.0 / libcurl 8.18.0 / OpenSSL 3.5.5 / Python 3.14.4。

## 3. 构建

`rm -rf build && cmake .. && make -j$(nproc)`：退出码 0，error 0 行。
产物：server / client / transit / web + static/index.html（file_server 不存在）。

## 4. 启动

```bash
cd chatroom-backend
cp .env.example .env && chmod 600 .env && vi .env
set -a && . ./.env && set +a
sudo mysql < init.sql          # 2 张表 + 最小权限账号模板
cd build/bin && ./server       # 8000；./transit 10000；./web 9000（仅托管 ./static）
```

## 5. 数据库依赖

库 chatroom；表 **user / friend 两张**（message/file/group_info/group_user 已删除）；
应用账号权限仅 SELECT/INSERT/UPDATE/DELETE；密码经 CHATROOM_MYSQL_PASSWORD 注入。

## 6. 功能清单

保留（13 路由，type 0–12）：心跳、注册（验证码）、密码/验证码登录、重置密码、退出、
注销（级联删 user+friend）、好友列表（含在线状态）、申请、审批、拉黑、解除、删除。
删除：全部聊天（13/14/25/26/27）、全部群组（15–24）、文件传输、消息推送。

## 7. 接口协议

TCP+UTF-8、`\n` 分帧、64KB 上限；code=1 成功 / code=0 失败；
服务端推送仅 type9 code2（好友申请）+ 登录 offline[]。已删 type 统一返回异常请求。

## 8. 测试结果（本轮真实执行，104 项全绿）

| 脚本 | 结果 |
|---|---|
| smoke_cropped（42）| PASS=42 FAIL=0 |
| code_security basic（16）/ ttl（17）| 16/16、17/17 |
| attack_lifecycle（8）| 8/8 |
| concurrency（3，8 路仅 1 成功）| 3/3 |
| crash_recovery（6）| 6/6 |
| config_guard（5）| 5/5 |
| account_cascade（7，修订版）| 7/7 |

证据：evidence/p7b-01…08（含 p7b-02-group-scan 全仓群组符号扫描与表结构）。
历史阶段测试（P0–P6，含群组断言）见 TEST-REPORT 第 2 节，仅作审计留痕。

关键指标：群组路由 15–24 无响应；chatserver 权限最小化；
60MB 攻击连接被断开 RSS 稳定；fd 31→61→31；CLOSE-WAIT=0；kill -9 重启 state 复位。

## 9. 未完成项

- 前端深度适配（web/index.html 已移除聊天/群组入口，但样式未重新设计）
- client 为最小裁剪（好友/账号操作保留），未做 UI 优化
- 密码 SHA256 无盐、链路明文、单实例限制（详见 KNOWN-ISSUES）
- 上游许可证缺失（NOTICE 已声明，合并 PR 前建议确认授权）

## 10. 交付清单

代码（16+ 文件改动）+ tests/（8 脚本，修订版）+ evidence/（p7b-* 9 份 +
历史 p7/int/baseline 系列）+ 6 份文档（DELIVERY/BUILD-REPORT/TEST-REPORT/
CHANGELOG/HANDOFF/KNOWN-ISSUES）+ NOTICE + README。

验收签署：

| 角色 | 姓名 | 日期 | 结论 |
|---|---|---|---|
| 开发 | CodeArts Agent | 2026-11-16 | 待签 |
| 验收 | M | 2026-11-16 | 待签 |