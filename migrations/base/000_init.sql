-- ChatRoom 后端数据库初始化脚本（裁剪版：仅账号 / 好友，聊天与群组功能已删除）
-- 使用方法: sudo mysql < init.sql
-- 说明: 建库建表可直接执行；应用账号按下文模板手动创建，密码经环境变量注入。

-- 创建数据库
CREATE DATABASE IF NOT EXISTS chatroom
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

-- 应用账号模板（不要直接执行，替换占位符后再执行）：
--   1. 将 <强密码> 替换为实际强密码（不要写入本文件 / 源码 / 文档 / 日志）
--   2. 服务运行时通过环境变量 CHATROOM_MYSQL_PASSWORD 注入（见 .env.example）
--   3. 权限遵循最小化原则：仅授予运行所需的 SELECT / INSERT / UPDATE / DELETE
--
-- CREATE USER 'chatserver'@'localhost' IDENTIFIED BY '<强密码>';
-- GRANT SELECT, INSERT, UPDATE, DELETE ON chatroom.* TO 'chatserver'@'localhost';
-- FLUSH PRIVILEGES;

USE `chatroom`;

-- 创建表（注：聊天 / 群组相关表已随功能裁剪删除，仅保留 user / friend 两张表）

CREATE TABLE IF NOT EXISTS `user` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '用户ID',
    `name` VARCHAR(50) NOT NULL COMMENT '用户名',
    `email` VARCHAR(25) UNIQUE NOT NULL COMMENT '邮箱（唯一）',
    `password` CHAR(64) NOT NULL COMMENT '密码哈希',
    `state` TINYINT NOT NULL DEFAULT 0 COMMENT '状态: 0.离线 1.在线',
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='用户表';

CREATE TABLE IF NOT EXISTS `friend` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '关系ID',
    `user_id` INT UNSIGNED NOT NULL COMMENT '用户ID',
    `friend_id` INT UNSIGNED NOT NULL COMMENT '好友ID',
    `status` TINYINT NOT NULL DEFAULT 0 COMMENT '关系状态: 0.用户待验证 1.好友待验证 2.正常 3.拉黑',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_user_friend` (`user_id`, `friend_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='好友关系表';
