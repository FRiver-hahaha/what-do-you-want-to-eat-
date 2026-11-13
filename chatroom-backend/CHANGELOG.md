# CHANGELOG — 逐阶段代码修改记录

> 基线 a1e654b，分支 refactor/backend-crop，三个补丁按接手包规定顺序应用。
> 累计：16 文件，+348 / −543。未修改 client/ web/ transit/，未改任何接口约定。

## P1 — 裁剪聊天功能（patches/01-crop-chat.patch，+2/−439）

- CMakeLists.txt（−9）：移除 MessageModel.cc 编译项；整体移除 file_server 目标
- server/model/MessageModel.cc（−120）/ MessageModel.h（−25）：文件删除
- server/service/ChatService.cc（−277/+2）：删 5 个 handler 注册与实现
  （oneChat/queryHistory/groupChat/queryGroupHistory/pendingFiles/flushMsgList）；
  登录不再拉取 offline_msg/offline_group_msg；注销不再调 removeAll
- server/service/ChatService.h（−8）：删 6 个方法声明 + MessageModel 成员 + 头文件引用
- 效果：路由 28→23；type 13/14/25/26/27 无路由无响应不落库；message/file 表零写入

## P2 — 运行时修复（patches/02-bugfix-runtime.patch，+9）

- server/main.cc（+3）：ChatService::instance() 提前至 accept 之前构造（B3：复位先于首个连接）
- server/net/Connection.h（+1）/ Connection.cc（+3）：读缓冲上限 kMaxBufSize=64KB，
  超长/畸形报文按连接异常关闭（B6：内存放大防护）
- server/service/ChatService.cc（+2）：构造函数调用 m_user_model.resetState()
  （B3 原代码定义了但从未调用——上次进程异常退出残留 state=1 导致账号锁死）
- 效果：kill -9 后重启账号自动恢复；60MB 攻击连接被断开且 RSS 稳定

## P3 — 安全加固（patches/03-security-hardening.patch，+337/−104）

- 新增 .env.example（38 行配置模板，敏感项留空）、.gitignore（排除 .env/build/bin/files/*.log）
- 新增 server/common/Config.h：envOr/envInt/envBool 环境变量读取
- server/database/MySQL.{h,cc}（+14）：updateAffected() 支持条件原子更新
- server/database/Redis.{h,cc}（+30）：init 增加密码参数，AUTH %b 二进制安全认证，
  失败 LOG(ERROR)+返回 false；LPUSH/RPOP 改 %b 防注入（B7）
- server/email/EmailSender.{h,cc}（+42）：删除硬编码发件账号与授权码（历史泄露凭据出源码），
  改环境变量注入；DRY_RUN 模式验证码打日志供自动化测试
- server/main.cc（+21）：端口/线程数环境变量化
- server/model/UserModel.{h,cc}（+10）：tryLogin()——UPDATE ... WHERE state=0 原子抢占（B4：并发登录）
- server/service/ChatService.{h,cc}：jsonStr/jsonInt/jsonBool 安全取值 + try/catch 包裹 handle
  （B1/B2：字段缺失/类型异常不再 assert 打死进程）；consumeCode() 验证码策略
  （一次性 / TTL 300s / 限频 60s / 错 5 次锁定，B5）；MySQL/Redis 连接参数全部来自环境变量；
  注册流程强制校验验证码（先判重后校验）
- 效果：源码零凭据；凭据错误启动即失败且日志可定位；8 路并发登录仅 1 成功

## 未做（按边界）

- 未修改 client/ web/ transit/（前端由后续负责人处理）
- 未新增产品功能、未改接口约定、未动 schema
- 未提交 Git（待验收授权）
