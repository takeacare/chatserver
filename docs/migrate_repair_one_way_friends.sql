-- 修复历史版本遗留的单向好友关系：
-- 旧版 FriendModel::insert 只写入一个方向，导致一方好友列表缺失另一方。
-- 为每条缺少反向记录的关系补齐反向行；已存在的关系不受影响。
USE chat;

INSERT IGNORE INTO friend (userid, friendid)
SELECT f.friendid, f.userid
FROM friend f
LEFT JOIN friend r
       ON r.userid = f.friendid AND r.friendid = f.userid
WHERE r.userid IS NULL;

-- 修复后应返回 0 行
SELECT f.userid, f.friendid
FROM friend f
LEFT JOIN friend r
       ON r.userid = f.friendid AND r.friendid = f.userid
WHERE r.userid IS NULL;
