-- 仅用于测试前置复位：把 u_demo 的档案恢复为服务端默认值（不影响业务表结构）。
UPDATE `health_profile` SET
  `name`='我',
  `gender`='女',
  `age`=28,
  `height`=170,
  `weight`=60,
  `goal`='均衡饮食',
  `activity`='久坐',
  `diet_style`='普通饮食',
  `water_target`=1800,
  `sleep_hours`=7,
  `allergies`='["花生"]',
  `dislikes`='["香菜"]',
  `spend_min`=20,
  `spend_max`=35,
  `note`='下午容易口渴，晚餐不宜过饱'
WHERE `agent_uid`='u_demo';