-- 一次性迁移：为既有 chat 库补索引、外键与离线消息顺序字段
-- 执行前请确认库名（默认 chat），并在低峰期执行（会短暂锁表）。
-- 用法：mysql -uroot -p < docs/migrate_indexes_constraints.sql

USE `chat`;

-- 1. 清理孤儿行，避免添加外键失败；无孤儿行时不影响数据
DELETE f FROM `friend` f
LEFT JOIN `user` u1 ON u1.id = f.userid
LEFT JOIN `user` u2 ON u2.id = f.friendid
WHERE u1.id IS NULL OR u2.id IS NULL;

DELETE f FROM `friend_request` f
LEFT JOIN `user` u1 ON u1.id = f.userid
LEFT JOIN `user` u2 ON u2.id = f.friendid
WHERE u1.id IS NULL OR u2.id IS NULL;

DELETE gu FROM `groupuser` gu
LEFT JOIN `user` u ON u.id = gu.userid
LEFT JOIN `allgroup` g ON g.id = gu.groupid
WHERE u.id IS NULL OR g.id IS NULL;

DELETE o FROM `offlinemessage` o
LEFT JOIN `user` u ON u.id = o.userid
WHERE u.id IS NULL;

-- 2. 离线消息增加自增 id / 创建时间；历史行由 MySQL 依次分配 id
ALTER TABLE `offlinemessage`
  ADD COLUMN `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST,
  ADD COLUMN `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP;
ALTER TABLE `offlinemessage` ADD KEY `idx_offline_user` (`userid`,`id`);
ALTER TABLE `offlinemessage`
  ADD CONSTRAINT `fk_offline_user` FOREIGN KEY (`userid`) REFERENCES `user` (`id`) ON DELETE CASCADE;

-- 3. 群组名称查询与群成员反查索引
ALTER TABLE `allgroup` ADD KEY `idx_group_name` (`groupname`,`id`);
ALTER TABLE `groupuser` ADD KEY `idx_groupuser_user` (`userid`,`groupid`);
ALTER TABLE `groupuser`
  ADD CONSTRAINT `fk_groupuser_group` FOREIGN KEY (`groupid`) REFERENCES `allgroup` (`id`) ON DELETE CASCADE,
  ADD CONSTRAINT `fk_groupuser_user` FOREIGN KEY (`userid`) REFERENCES `user` (`id`) ON DELETE CASCADE;

-- 4. 好友与好友请求的反向索引、外键
ALTER TABLE `friend` ADD KEY `idx_friend_reverse` (`friendid`,`userid`);
ALTER TABLE `friend`
  ADD CONSTRAINT `fk_friend_user` FOREIGN KEY (`userid`) REFERENCES `user` (`id`) ON DELETE CASCADE,
  ADD CONSTRAINT `fk_friend_friend` FOREIGN KEY (`friendid`) REFERENCES `user` (`id`) ON DELETE CASCADE;

ALTER TABLE `friend_request` ADD KEY `idx_friend_request_target` (`friendid`,`userid`);
ALTER TABLE `friend_request`
  ADD CONSTRAINT `fk_request_requester` FOREIGN KEY (`userid`) REFERENCES `user` (`id`) ON DELETE CASCADE,
  ADD CONSTRAINT `fk_request_target` FOREIGN KEY (`friendid`) REFERENCES `user` (`id`) ON DELETE CASCADE;
