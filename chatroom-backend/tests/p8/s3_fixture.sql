-- P8-S3 测试夹具：仅新增/覆盖测试用户与好友关系，不删除任何数据。
-- 目的：验证排行榜的好友边界、并列名次、无档案与无饮食记录分支。
-- 全部语句幂等，可重复执行。

INSERT INTO `user` (`name`,`email`,`password`,`state`,`agent_uid`) VALUES
  ('测试甲','u_test_rank_a@local.test',REPEAT('a',64),0,'u_test_rank_a'),
  ('测试乙','u_test_rank_b@local.test',REPEAT('b',64),0,'u_test_rank_b'),
  ('测试丙','u_test_rank_c@local.test',REPEAT('c',64),0,'u_test_rank_c'),
  ('测试丁','u_test_rank_d@local.test',REPEAT('d',64),0,'u_test_rank_d')
AS new ON DUPLICATE KEY UPDATE `name`=new.`name`;

-- a/b 档案 activity=较高（无饮食记录时今日基线 71 分，可构造并列）；
-- c 故意不建档案，验证缺档案时回退默认久坐与 user.name。
INSERT INTO `health_profile`
(`agent_uid`,`name`,`gender`,`age`,`height`,`weight`,`goal`,`activity`,
 `diet_style`,`water_target`,`sleep_hours`,`allergies`,`dislikes`,
 `spend_min`,`spend_max`,`note`) VALUES
 ('u_test_rank_a','甲同学','女',30,165,55,'均衡饮食','较高','普通饮食',1800,7,'[]','[]',20,35,'p8-s3 fixture'),
 ('u_test_rank_b','乙同学','男',32,175,70,'均衡饮食','较高','普通饮食',1800,7,'[]','[]',20,35,'p8-s3 fixture'),
 ('u_test_rank_d','丁同学','男',26,172,66,'均衡饮食','较高','普通饮食',1800,7,'[]','[]',20,35,'p8-s3 fixture')
AS new ON DUPLICATE KEY UPDATE `name`=new.`name`,`activity`=new.`activity`,`note`=new.`note`;

-- u_demo 与 a 正向、与 b 反向、与 c 正向，均为正常好友（status=2）。
INSERT INTO `friend` (`user_id`,`friend_id`,`status`)
SELECT d.`id`, a.`id`, 2 FROM `user` d JOIN `user` a
 WHERE d.`agent_uid`='u_demo' AND a.`agent_uid`='u_test_rank_a'
ON DUPLICATE KEY UPDATE `status`=2;

INSERT INTO `friend` (`user_id`,`friend_id`,`status`)
SELECT b.`id`, d.`id`, 2 FROM `user` d JOIN `user` b
 WHERE d.`agent_uid`='u_demo' AND b.`agent_uid`='u_test_rank_b'
ON DUPLICATE KEY UPDATE `status`=2;

INSERT INTO `friend` (`user_id`,`friend_id`,`status`)
SELECT d.`id`, c.`id`, 2 FROM `user` d JOIN `user` c
 WHERE d.`agent_uid`='u_demo' AND c.`agent_uid`='u_test_rank_c'
ON DUPLICATE KEY UPDATE `status`=2;

-- d 为拉黑/待验证关系（status=3）：排行榜必须排除。
INSERT INTO `friend` (`user_id`,`friend_id`,`status`)
SELECT d.`id`, x.`id`, 3 FROM `user` d JOIN `user` x
 WHERE d.`agent_uid`='u_demo' AND x.`agent_uid`='u_test_rank_d'
ON DUPLICATE KEY UPDATE `status`=3;

SELECT u.`agent_uid`, u.`id` FROM `user` u WHERE u.`agent_uid` LIKE 'u_test_rank_%' OR u.`agent_uid`='u_demo' ORDER BY u.`agent_uid`;
SELECT f.`user_id`, f.`friend_id`, f.`status` FROM `friend` f ORDER BY f.`id`;