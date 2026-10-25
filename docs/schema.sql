-- ChatServer 数据库结构（MySQL 8）
-- 用法：mysql -uroot -p < docs/schema.sql
-- 可通过 server.conf 的 db_name 更换库名，需与下方保持一致。

CREATE DATABASE IF NOT EXISTS `chat` DEFAULT CHARACTER SET utf8mb4;
USE `chat`;

-- 用户表：登录账号为 email；name/email 均唯一
CREATE TABLE IF NOT EXISTS `user` (
  `id` INT NOT NULL AUTO_INCREMENT,
  `name` VARCHAR(50) NOT NULL,
  `email` VARCHAR(120) DEFAULT NULL,
  `password` VARCHAR(128) NOT NULL,
  `state` ENUM('online','offline') NOT NULL DEFAULT 'offline',
  PRIMARY KEY (`id`),
  UNIQUE KEY `name` (`name`),
  UNIQUE KEY `uk_email` (`email`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 好友关系：双向存储（A->B 与 B->A 各一行）
CREATE TABLE IF NOT EXISTS `friend` (
  `userid` INT NOT NULL,
  `friendid` INT NOT NULL,
  PRIMARY KEY (`userid`,`friendid`),
  KEY `idx_friend_reverse` (`friendid`,`userid`),
  CONSTRAINT `fk_friend_user` FOREIGN KEY (`userid`) REFERENCES `user` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_friend_friend` FOREIGN KEY (`friendid`) REFERENCES `user` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 待处理好友请求：userid 为发起方，friendid 为接收方
CREATE TABLE IF NOT EXISTS `friend_request` (
  `userid` INT NOT NULL COMMENT '发起方用户id',
  `friendid` INT NOT NULL COMMENT '接收方用户id',
  PRIMARY KEY (`userid`,`friendid`),
  KEY `idx_friend_request_target` (`friendid`,`userid`),
  CONSTRAINT `fk_request_requester` FOREIGN KEY (`userid`) REFERENCES `user` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_request_target` FOREIGN KEY (`friendid`) REFERENCES `user` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='待处理好友请求';

-- 群组表
CREATE TABLE IF NOT EXISTS `allgroup` (
  `id` INT NOT NULL AUTO_INCREMENT,
  `groupname` VARCHAR(50) NOT NULL,
  `groupdesc` VARCHAR(200) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  KEY `idx_group_name` (`groupname`,`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 群成员表：grouprole 为 creator / normal
CREATE TABLE IF NOT EXISTS `groupuser` (
  `groupid` INT NOT NULL,
  `userid` INT NOT NULL,
  `grouprole` ENUM('creator','normal') NOT NULL DEFAULT 'normal',
  PRIMARY KEY (`groupid`,`userid`),
  KEY `idx_groupuser_user` (`userid`,`groupid`),
  CONSTRAINT `fk_groupuser_group` FOREIGN KEY (`groupid`) REFERENCES `allgroup` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_groupuser_user` FOREIGN KEY (`userid`) REFERENCES `user` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 离线消息：message 为裸 JSON（不带分帧长度头）
-- id 用于保证登录补发顺序；登录时按 id 取出并只删除已取出的行
CREATE TABLE IF NOT EXISTS `offlinemessage` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `userid` INT NOT NULL,
  `message` MEDIUMTEXT NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_offline_user` (`userid`,`id`),
  CONSTRAINT `fk_offline_user` FOREIGN KEY (`userid`) REFERENCES `user` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
