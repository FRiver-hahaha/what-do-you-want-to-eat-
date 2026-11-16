# CHANGELOG — 逐阶段代码修改记录

> 基线 a1e654b（lzh070702/ChatRoom），分支 feat/backend-crop。
> 未修改学长仓库原有前端与 Python Agent。

## P1 — 裁剪聊天功能（+2/−439）

- 删 5 个聊天 handler（13/14/25/26/27）、MessageModel.{h,cc}、file_server 构建目标
- 登录不再拉取 offline_msg/offline_group_msg；注销不再调 removeAll

## P2 — 运行时修复（+9）

- main.cc 提前构造单例（复位先于 accept）；64KB 读缓冲上限；构造时 resetState()

## P3 — 安全加固（+337/−104）

- 凭据全外置 .env（源码移除硬编码 SMTP 授权码）；Redis AUTH；tryLogin CAS 原子登录；
  验证码策略（一次性/300s TTL/限频 60s/错 5 次锁定）；jsonStr/jsonInt/jsonBool 安全取值

## P7 — 后端资产整改（第一轮）

- 删 server/file.cc、client/net/filetransfer.h 死文件；清理 offline_msg 孤儿写入
- client：删私聊/聊天记录/文件传输入口与实现、g_chat 状态、type 13/25 推送展示
- web/index.html 重写（删聊天视图/消息气泡/文件传输全家）
- transit：删文件隧道（FILE_* 常量、file_fd、六个文件函数、@FILE 分支）
- init.sql：删 message/file 表；弱口令与 GRANT ALL 改为最小权限模板（密码经 .env 注入）
- web/main.cc：set_mount_point("/", ".") → "/"，"./static"（不暴露整个工作目录）
- .gitignore 修复：evidence/*.log 按需归档例外；tests/ 测试脚本纳入 Git

## P7 范围纠偏 — 彻底删除群组功能（本轮，最高优先级）

用户决定群组管理也不再需要，全部删除：

- server/service/ChatService.{h,cc}：删 10 个群组路由（15–24）、9 个方法实现、
  GroupModel 成员与 include、offline_group_request 拉取与写入、注销级联 removeFromAll
- server/model/GroupModel.{h,cc}：文件删除
- client/page/group.{h,cc}：文件删除；friend.cc 删"群聊"菜单入口
- client/net/net.cc / page/auth.cc：删 type 17/18/19/25 群组推送展示
- web/index.html：删群组 UI 全家（页面/菜单/模态/成员操作/未读群组项）
- init.sql：删 group_info/group_user 表（6→2 表，仅 user/friend）
- CMakeLists：删 GroupModel.cc、group.cc 编译项
- tests/ 8 个脚本修订：群组断言改为不可达（type 15–24 十项）、message/file/group 表改
  表不存在断言、库名 --mysql-db 参数化、concurrency 默认账号改 bob
- 最终保留路由 13 个（type 0–12）：心跳、账号 7 项、好友 5 项