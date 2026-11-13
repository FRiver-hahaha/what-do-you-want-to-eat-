-- ChatRoom 数据库初始化脚本
-- 使用方法: sudo mysql < init.sql

-- 创建数据库
CREATE DATABASE IF NOT EXISTS chatroom
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

-- 创建用户并授权
CREATE USER IF NOT EXISTS 'chatserver'@'localhost' IDENTIFIED BY '123456';
GRANT ALL PRIVILEGES ON chatroom.* TO 'chatserver'@'localhost';
FLUSH PRIVILEGES;

--进入数据库
USE `chatroom`;

--创建表
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

CREATE TABLE IF NOT EXISTS `group_info` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '群聊ID',
    `name` VARCHAR(50) NOT NULL COMMENT '群聊名',
    `owner_id` INT UNSIGNED NOT NULL COMMENT '群主ID',
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='群聊信息表';

CREATE TABLE IF NOT EXISTS `group_user` (
    `group_id` INT UNSIGNED NOT NULL COMMENT '群聊ID',
    `user_id` INT UNSIGNED NOT NULL COMMENT '群聊成员ID',
    `role` TINYINT NOT NULL DEFAULT 0 COMMENT '角色: 0.待验证 1.成员 2.管理员 3.群主',
    PRIMARY KEY (`group_id`, `user_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='群聊成员表';

CREATE TABLE IF NOT EXISTS `message` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '消息ID',
    `sender_id` INT UNSIGNED NOT NULL COMMENT '发送者ID',
    `receiver_id` INT UNSIGNED NOT NULL COMMENT '接受者ID',
    `type` TINYINT NOT NULL DEFAULT 0 COMMENT '聊天类型: 0.私聊 1.群聊',
    `content` TEXT NOT NULL COMMENT '发送信息',
    `is_file` TINYINT NOT NULL DEFAULT 0 COMMENT '是否为文件: 0.否 1.是',
    `send_time` DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '发送时间',
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='聊天记录表';

CREATE TABLE IF NOT EXISTS `file` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '文件ID',
    `file` TEXT NOT NULL COMMENT '文件名',
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='文件id表';

