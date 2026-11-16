-- ============================================================================
-- P8-S1 正式迁移（仅限独立测试库 chatroom_p8）
-- 数据来源：agent/data/service_client.py MOCK_FOODS（main@fc34b0fe）逐字段复制
-- 语义：
--   ① 16 行即演示数据集，营养值全部为人工估算，无 serving/unit/allergen 字段；
--   ② 默认策略 INSERT IGNORE —— 首次写入；后续重跑不覆盖运维对 price/available
--      的人工调整（可重复执行且尊重人工变更）；
--   ③ 如需强制回滚到文档基线，改用文末注释中的 ON DUPLICATE KEY UPDATE 变体，
--      且必须先审批（会覆盖人工修改）。
-- 依赖：foods 表已由 001_p8_business.draft.sql Step 2 创建。
-- ============================================================================

INSERT IGNORE INTO `foods`
(`id`,`name`,`category`,`description`,`calories`,`protein`,`fiber`,`veggies`,`water`,
 `nature`,`keywords`,`image`,`meal_types`,`price`,`available`)
VALUES
('lemon-chicken','柠檬鸡胸能量碗','高蛋白 · 清爽','烤鸡胸、牛油果、藜麦与时令蔬菜',460,38,9,320,250,'recommend','鸡胸 沙拉 碗 轻食 蛋白','https://images.unsplash.com/photo-1546069901-ba9599a7e63c?auto=format&fit=crop&w=600&q=82','["午餐","晚餐"]',30,1),
('tomato-beef','番茄牛腩盖饭','热乎 · 饱腹','番茄牛腩、杂粮米饭与焯拌时蔬',620,32,7,180,180,'recommend','米饭 饭 牛肉 牛腩 盖饭 番茄','https://images.unsplash.com/photo-1512058564366-18510be2db19?auto=format&fit=crop&w=600&q=82','["午餐","晚餐"]',28,1),
('mushroom-soup','菌菇鸡汤面','清淡 · 汤食','清鸡汤、三种菌菇、细面与小青菜',430,24,6,160,450,'recommend','面 汤 菌菇 鸡汤 清淡','https://images.unsplash.com/photo-1547592166-23ac45744acd?auto=format&fit=crop&w=600&q=82','["早餐","午餐","晚餐"]',24,1),
('shrimp-pasta','蒜香虾仁意面','高蛋白 · 换口味','虾仁、樱桃番茄与全麦意面',560,35,8,140,160,'recommend','面 意面 虾 海鲜 番茄','https://images.unsplash.com/photo-1563379926898-05f4575a45d8?auto=format&fit=crop&w=600&q=82','["午餐","晚餐"]',36,1),
('vegetable-rice','时蔬豆腐拌饭','植物蛋白 · 多蔬菜','香煎豆腐、烤南瓜、西兰花与杂粮饭',410,22,12,360,180,'recommend','米饭 饭 豆腐 素食 拌饭 蔬菜','https://images.unsplash.com/photo-1512621776951-a57141f2eefd?auto=format&fit=crop&w=600&q=82','["午餐","晚餐"]',25,1),
('pepper-chicken','藤椒鸡丝拌面','微辣 · 快手','嫩鸡丝、黄瓜丝与荞麦面',520,30,7,140,180,'recommend','面 鸡丝 辣 荞麦 凉面','https://images.unsplash.com/photo-1473093295043-cdd812d0e601?auto=format&fit=crop&w=600&q=82','["午餐","晚餐"]',26,1),
('tomato-egg-rice','番茄鸡蛋盖饭','家常 · 顺口','番茄炒蛋、杂粮米饭与小份青菜',480,18,5,160,150,'recommend','米饭 饭 番茄 鸡蛋 盖饭 家常','https://images.unsplash.com/photo-1504674900247-0877df9cc836?auto=format&fit=crop&w=600&q=82','["早餐","午餐","晚餐"]',20,1),
('salmon-quinoa','三文鱼藜麦碗','高蛋白 · 优质脂肪','烤三文鱼、藜麦、牛油果与羽衣甘蓝',520,34,8,240,200,'recommend','三文鱼 鱼 藜麦 沙拉 轻食','https://images.unsplash.com/photo-1467003909585-2f8a72700288?auto=format&fit=crop&w=600&q=82','["午餐","晚餐"]',42,1),
('steamed-fish-rice','清蒸鲈鱼配米饭','清淡 · 蒸食','清蒸鲈鱼、杂粮米饭与白灼菜心',470,33,4,200,200,'recommend','米饭 饭 鱼 清蒸 少油','https://images.unsplash.com/photo-1467003909585-2f8a72700288?auto=format&fit=crop&w=600&q=82','["午餐","晚餐"]',34,1),
('chicken-salad','鸡胸蔬菜沙拉','低脂 · 高纤维','煎鸡胸、混合生菜、番茄与油醋汁',380,32,10,300,220,'recommend','沙拉 蔬菜 鸡胸 轻食 减脂','https://images.unsplash.com/photo-1512621776951-a57141f2eefd?auto=format&fit=crop&w=600&q=82','["早餐","午餐","晚餐"]',28,1),
('yangzhou-fried-rice','扬州炒饭','热乎 · 放纵','火腿、虾仁、鸡蛋与香米炒饭',680,20,3,90,100,'indulge','米饭 饭 炒饭 火腿 虾仁','https://images.unsplash.com/photo-1512058564366-18510be2db19?auto=format&fit=crop&w=600&q=82','["午餐","晚餐"]',22,1),
('beef-fried-rice','牛肉炒饭','热乎 · 放纵','黑椒牛肉、香米与葱花',720,26,2,60,100,'indulge','米饭 饭 炒饭 牛肉 黑椒','https://images.unsplash.com/photo-1544025162-d76694265947?auto=format&fit=crop&w=600&q=82','["午餐","晚餐"]',26,1),
('fried-chicken-burger','炸鸡汉堡','快餐 · 放纵','脆皮炸鸡、芝士与汉堡面包',780,30,2,40,80,'indulge','炸鸡 汉堡 快餐 薯条','https://images.unsplash.com/photo-1568901346375-23c9450c58cd?auto=format&fit=crop&w=600&q=82','["午餐","晚餐"]',32,1),
('cheese-cake','芝士蛋糕','甜点 · 放纵','浓郁芝士、饼干底与莓果',420,8,1,0,40,'indulge','甜点 蛋糕 芝士 甜品','https://images.unsplash.com/photo-1551024506-0bccd828d307?auto=format&fit=crop&w=600&q=82','["加餐"]',18,1),
('bubble-tea','珍珠奶茶','饮品 · 放纵','红茶、奶盖与黑糖珍珠',460,4,0,0,100,'indulge','奶茶 饮料 甜 珍珠 黑糖','https://images.unsplash.com/photo-1544145945-f90425340c7e?auto=format&fit=crop&w=600&q=82','["加餐"]',16,1),
('malatang','麻辣烫','热辣 · 放纵','自选蔬菜、丸子与麻辣汤底',640,24,6,260,300,'indulge','麻辣烫 辣 火锅 丸子 汤','https://images.unsplash.com/photo-1547592166-23ac45744acd?auto=format&fit=crop&w=600&q=82','["午餐","晚餐"]',30,1);

-- ----------------------------------------------------------------------------
-- 自检
-- ----------------------------------------------------------------------------
SELECT COUNT(*) AS total, SUM(available = 1) AS available FROM `foods`;      -- 期望 16 / 16
SELECT nature, COUNT(*) AS n FROM `foods` GROUP BY nature;                    -- 期望 recommend 10 / indulge 6

-- ----------------------------------------------------------------------------
-- 【可选，需审批】强制回到文档基线（会覆盖 price/available 人工调整）：
-- INSERT INTO `foods` (...) VALUES (...), (...)
--   ON DUPLICATE KEY UPDATE
--     `name`=VALUES(`name`), `category`=VALUES(`category`),
--     `description`=VALUES(`description`), `calories`=VALUES(`calories`),
--     `protein`=VALUES(`protein`), `fiber`=VALUES(`fiber`),
--     `veggies`=VALUES(`veggies`), `water`=VALUES(`water`),
--     `nature`=VALUES(`nature`), `keywords`=VALUES(`keywords`),
--     `image`=VALUES(`image`), `meal_types`=VALUES(`meal_types`),
--     `price`=VALUES(`price`), `available`=VALUES(`available`);
-- ----------------------------------------------------------------------------