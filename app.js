const storageKeys = {
  profile: "eat-health-profile-v2",
  meals: "eat-health-meals-v2",
  history: "eat-health-history-v2",
  points: "eat-health-points-v1",
  motion: "eat-agent-motion",
  cover: "eat-champion-cover-v1"
};

const defaultProfile = {
  name: "我",
  height: 170,
  weight: 60,
  age: 28,
  goal: "均衡饮食",
  activity: "久坐",
  waterTarget: 1800,
  sleep: 7,
  spend_min: 20,
  spend_max: 35,
  note: "下午容易口渴，晚餐不宜过饱"
};

const defaultMeals = [
  { type: "早餐", time: "08:20", text: "燕麦酸奶、蓝莓、水煮蛋", protein: 22, fiber: 6, veggies: 0, water: 300, penalty: 0 },
  { type: "午餐", time: "12:18", text: "柠檬鸡胸能量碗、无糖茶", protein: 38, fiber: 9, veggies: 320, water: 650, penalty: 0 }
];

const foods = [
  { id: "lemon-chicken", name: "柠檬鸡胸能量碗", category: "高蛋白 · 清爽", description: "烤鸡胸、牛油果、藜麦与时令蔬菜", calories: 460, protein: 38, fiber: 9, veggies: 320, water: 250, price: 30, nature: "recommend", keywords: "鸡胸 沙拉 碗 轻食 蛋白", image: "https://images.unsplash.com/photo-1546069901-ba9599a7e63c?auto=format&fit=crop&w=600&q=82" },
  { id: "tomato-beef", name: "番茄牛腩盖饭", category: "热乎 · 饱腹", description: "番茄牛腩、杂粮米饭与焯拌时蔬", calories: 620, protein: 32, fiber: 7, veggies: 180, water: 180, price: 28, nature: "recommend", keywords: "米饭 饭 牛肉 牛腩 盖饭 番茄", image: "https://images.unsplash.com/photo-1512058564366-18510be2db19?auto=format&fit=crop&w=600&q=82" },
  { id: "mushroom-soup", name: "菌菇鸡汤面", category: "清淡 · 汤食", description: "清鸡汤、三种菌菇、细面与小青菜", calories: 430, protein: 24, fiber: 6, veggies: 160, water: 450, price: 24, nature: "recommend", keywords: "面 汤 菌菇 鸡汤 清淡", image: "https://images.unsplash.com/photo-1547592166-23ac45744acd?auto=format&fit=crop&w=600&q=82" },
  { id: "shrimp-pasta", name: "蒜香虾仁意面", category: "高蛋白 · 换口味", description: "虾仁、樱桃番茄与全麦意面", calories: 560, protein: 35, fiber: 8, veggies: 140, water: 160, price: 36, nature: "recommend", keywords: "面 意面 虾 海鲜 番茄", image: "https://images.unsplash.com/photo-1563379926898-05f4575a45d8?auto=format&fit=crop&w=600&q=82" },
  { id: "vegetable-rice", name: "时蔬豆腐拌饭", category: "植物蛋白 · 多蔬菜", description: "香煎豆腐、烤南瓜、西兰花与杂粮饭", calories: 410, protein: 22, fiber: 12, veggies: 360, water: 180, price: 25, nature: "recommend", keywords: "米饭 饭 豆腐 素食 拌饭 蔬菜", image: "https://images.unsplash.com/photo-1512621776951-a57141f2eefd?auto=format&fit=crop&w=600&q=82" },
  { id: "pepper-chicken", name: "藤椒鸡丝拌面", category: "微辣 · 快手", description: "嫩鸡丝、黄瓜丝与荞麦面", calories: 520, protein: 30, fiber: 7, veggies: 140, water: 180, price: 26, nature: "recommend", keywords: "面 鸡丝 辣 荞麦 凉面", image: "https://images.unsplash.com/photo-1473093295043-cdd812d0e601?auto=format&fit=crop&w=600&q=82" },
  { id: "tomato-egg-rice", name: "番茄鸡蛋盖饭", category: "家常 · 顺口", description: "番茄炒蛋、杂粮米饭与小份青菜", calories: 480, protein: 18, fiber: 5, veggies: 160, water: 150, price: 20, nature: "recommend", keywords: "米饭 饭 番茄 鸡蛋 盖饭 家常", image: "https://images.unsplash.com/photo-1504674900247-0877df9cc836?auto=format&fit=crop&w=600&q=82" },
  { id: "salmon-quinoa", name: "三文鱼藜麦碗", category: "高蛋白 · 优质脂肪", description: "烤三文鱼、藜麦、牛油果与羽衣甘蓝", calories: 520, protein: 34, fiber: 8, veggies: 240, water: 200, price: 42, nature: "recommend", keywords: "三文鱼 鱼 藜麦 沙拉 轻食", image: "https://images.unsplash.com/photo-1467003909585-2f8a72700288?auto=format&fit=crop&w=600&q=82" },
  { id: "steamed-fish-rice", name: "清蒸鲈鱼配米饭", category: "清淡 · 蒸食", description: "清蒸鲈鱼、杂粮米饭与白灼菜心", calories: 470, protein: 33, fiber: 4, veggies: 200, water: 200, price: 34, nature: "recommend", keywords: "米饭 饭 鱼 清蒸 少油", image: "https://images.unsplash.com/photo-1467003909585-2f8a72700288?auto=format&fit=crop&w=600&q=82" },
  { id: "chicken-salad", name: "鸡胸蔬菜沙拉", category: "低脂 · 高纤维", description: "煎鸡胸、混合生菜、番茄与油醋汁", calories: 380, protein: 32, fiber: 10, veggies: 300, water: 220, price: 28, nature: "recommend", keywords: "沙拉 蔬菜 鸡胸 轻食 减脂", image: "https://images.unsplash.com/photo-1512621776951-a57141f2eefd?auto=format&fit=crop&w=600&q=82" },
  { id: "yangzhou-fried-rice", name: "扬州炒饭", category: "热乎 · 放纵", description: "火腿、虾仁、鸡蛋与香米炒饭", calories: 680, protein: 20, fiber: 3, veggies: 90, water: 100, price: 22, nature: "indulge", keywords: "米饭 饭 炒饭 火腿 虾仁", image: "https://images.unsplash.com/photo-1512058564366-18510be2db19?auto=format&fit=crop&w=600&q=82" },
  { id: "beef-fried-rice", name: "牛肉炒饭", category: "热乎 · 放纵", description: "黑椒牛肉、香米与葱花", calories: 720, protein: 26, fiber: 2, veggies: 60, water: 100, price: 26, nature: "indulge", keywords: "米饭 饭 炒饭 牛肉 黑椒", image: "https://images.unsplash.com/photo-1544025162-d76694265947?auto=format&fit=crop&w=600&q=82" },
  { id: "fried-chicken-burger", name: "炸鸡汉堡", category: "快餐 · 放纵", description: "脆皮炸鸡、芝士与汉堡面包", calories: 780, protein: 30, fiber: 2, veggies: 40, water: 80, price: 32, nature: "indulge", keywords: "炸鸡 汉堡 快餐 薯条", image: "https://images.unsplash.com/photo-1568901346375-23c9450c58cd?auto=format&fit=crop&w=600&q=82" },
  { id: "cheese-cake", name: "芝士蛋糕", category: "甜点 · 放纵", description: "浓郁芝士、饼干底与莓果", calories: 420, protein: 8, fiber: 1, veggies: 0, water: 40, price: 18, nature: "indulge", keywords: "甜点 蛋糕 芝士 甜品", image: "https://images.unsplash.com/photo-1551024506-0bccd828d307?auto=format&fit=crop&w=600&q=82" },
  { id: "bubble-tea", name: "珍珠奶茶", category: "饮品 · 放纵", description: "红茶、奶盖与黑糖珍珠", calories: 460, protein: 4, fiber: 0, veggies: 0, water: 100, price: 16, nature: "indulge", keywords: "奶茶 饮料 甜 珍珠 黑糖", image: "https://images.unsplash.com/photo-1544145945-f90425340c7e?auto=format&fit=crop&w=600&q=82" },
  { id: "malatang", name: "麻辣烫", category: "热辣 · 放纵", description: "自选蔬菜、丸子与麻辣汤底", calories: 640, protein: 24, fiber: 6, veggies: 260, water: 300, price: 30, nature: "indulge", keywords: "麻辣烫 辣 火锅 丸子 汤", image: "https://images.unsplash.com/photo-1547592166-23ac45744acd?auto=format&fit=crop&w=600&q=82" }
];

const seedHistory = [
  { date: "2026-11-10", meals: 3, summary: "全麦吐司、番茄牛腩、清炒时蔬", score: 80 },
  { date: "2026-11-09", meals: 3, summary: "燕麦酸奶、鸡胸沙拉、菌菇汤", score: 84 },
  { date: "2026-11-07", meals: 2, summary: "鸡蛋三明治、牛肉盖饭", score: 76 },
  { date: "2026-11-05", meals: 3, summary: "牛奶燕麦、虾仁意面、白灼菜心", score: 82 },
  { date: "2026-11-02", meals: 3, summary: "豆浆、时蔬豆腐拌饭、玉米", score: 79 },
  { date: "2026-10-28", meals: 2, summary: "鸡丝拌面、青菜、苹果", score: 74 },
  { date: "2026-10-19", meals: 3, summary: "全麦面包、鸡胸沙拉、豆腐汤", score: 81 }
];

const rankings = {
  week: [
    { name: "林小满", score: 92, note: "连续 7 天" },
    { name: "周宁", score: 88, note: "连续 6 天" },
    { name: "陈可", score: 85, note: "蔬果达标 6 天" },
    { name: "你", score: 82, note: "连续 5 天", current: true },
    { name: "高远", score: 79, note: "饮水达标 4 天" }
  ],
  today: [
    { name: "周宁", score: 94, note: "三餐已记录" },
    { name: "你", score: 82, note: "已记录 2 餐", current: true },
    { name: "林小满", score: 80, note: "已记录 2 餐" },
    { name: "陈可", score: 77, note: "已记录 2 餐" },
    { name: "高远", score: 73, note: "已记录 1 餐" }
  ]
};

const defaultPointsState = {
  week: "",
  baseEarned: 0,
  spent: 0,
  checkins: {},
  interactions: {},
  healthAwards: {},
  owned: ["aurora"],
  selected: "aurora"
};

const shopThemes = [
  { id: "aurora", name: "轻雾极光", description: "保留当前的浅色极光与星座网格", price: 0 },
  { id: "sage", name: "森间早餐", description: "柔和鼠尾草绿与清晨薄雾", price: 120 },
  { id: "peach", name: "蜜桃餐桌", description: "低饱和杏桃色与暖白光线", price: 180 },
  { id: "sky", name: "晴空餐垫", description: "安静天蓝色与清透云层", price: 260 }
];

const viewTitles = {
  today: "今天吃了什么",
  report: "健康报告",
  profile: "健康档案",
  friends: "好友",
  history: "历史记录",
  shop: "积分商城"
};

const monthLabels = ["一月", "二月", "三月", "四月", "五月", "六月", "七月", "八月", "九月", "十月", "十一月", "十二月"];
const weekdayLabels = ["周日", "周一", "周二", "周三", "周四", "周五", "周六"];
const mealOrder = ["早餐", "午餐", "晚餐", "加餐"];

let profile = { ...defaultProfile, ...loadStored(storageKeys.profile, defaultProfile) };
let meals = loadStored(storageKeys.meals, defaultMeals);
let history = loadStored(storageKeys.history, seedHistory);
let pointsState = { ...defaultPointsState, ...loadStored(storageKeys.points, defaultPointsState) };
let currentWeeklyRank = 4;
let guessIndex = 0;
let currentGuessFoodId = "lemon-chicken";
let draftProfile = { ...profile };
let historyFilter = "week";
let activeRanking = "week";
let toastTimer;
let replyTimer;
let menuDrag = null;
let suppressMenuClick = false;
let activeView = "today";
let previousView = "today";
let remoteWeekly = null;
let remoteFriends = [];
let remoteRanking = [];
let currentUserKey = "";
let championCoverSrc = (loadStored(storageKeys.cover, { src: "" }) || {}).src || "";
let coverDraftSrc = "";
let randomFoodId = "";
let randomReturnView = "today";
let searchTimer;
let socialTimer = null;
let editingMealIndex = -1;
let deletingMealIndex = -1;

const MEAL_TYPES = ["早餐", "午餐", "下午茶", "晚餐", "加餐"];

const elements = {
  appShell: document.getElementById("appShell"),
  workspace: document.getElementById("workspace"),
  pageTitle: document.getElementById("pageTitle"),
  mainContent: document.getElementById("mainContent"),
  conversation: document.getElementById("conversation"),
  chatInput: document.getElementById("chatInput"),
  mealTimeline: document.getElementById("mealTimeline"),
  mealCount: document.getElementById("mealCount"),
  scoreValue: document.getElementById("scoreValue"),
  scoreCircle: document.getElementById("scoreCircle"),
  scoreStatus: document.getElementById("scoreStatus"),
  scoreDelta: document.getElementById("scoreDelta"),
  reportSummary: document.getElementById("reportSummary"),
  reportBasis: document.getElementById("reportBasis"),
  leaderboardList: document.getElementById("leaderboardList"),
  historyList: document.getElementById("historyList"),
  toast: document.getElementById("toast"),
  settingsDialog: document.getElementById("settingsDialog"),
  motionToggle: document.getElementById("motionToggle"),
  foodModule: document.getElementById("foodModule"),
  sidebarFoodViewport: document.getElementById("sidebarFoodViewport"),
  expandedMenu: document.getElementById("expandedMenu"),
  expandedMenuList: document.getElementById("expandedMenuList"),
  menuDropZone: document.getElementById("menuDropZone"),
  sidebarToggle: document.getElementById("sidebarToggle"),
  detailsCollapseButton: document.getElementById("detailsCollapseButton"),
  userInfoDialog: document.getElementById("userInfoDialog"),
  waterRange: document.getElementById("waterRange"),
  waterOutput: document.getElementById("waterOutput"),
  sleepRange: document.getElementById("sleepRange"),
  sleepOutput: document.getElementById("sleepOutput"),
  healthNote: document.getElementById("healthNote")
};

function loadStored(key, fallback) {
  try {
    const value = localStorage.getItem(key);
    return value ? JSON.parse(value) : structuredClone(fallback);
  } catch {
    return structuredClone(fallback);
  }
}

function saveStored(key, value) {
  try {
    localStorage.setItem(key, JSON.stringify(value));
  } catch {
    showToast("浏览器未允许本地存储，本次修改仍然有效");
  }
}

function icon(name) {
  return `<svg class="icon" aria-hidden="true"><use href="#icon-${name}"></use></svg>`;
}

function escapeHtml(value) {
  return String(value).replace(/[&<>'"]/g, character => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "'": "&#39;", '"': "&quot;" })[character]);
}

function showToast(message) {
  clearTimeout(toastTimer);
  elements.toast.querySelector("span").textContent = message;
  elements.toast.classList.add("show");
  toastTimer = setTimeout(() => elements.toast.classList.remove("show"), 2600);
}

function todayKey() {
  const now = new Date();
  return `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, "0")}-${String(now.getDate()).padStart(2, "0")}`;
}

function getWeekKey(date = new Date()) {
  const mondayOffset = (date.getDay() + 6) % 7;
  const monday = new Date(date.getFullYear(), date.getMonth(), date.getDate() - mondayOffset);
  return `${monday.getFullYear()}-${String(monday.getMonth() + 1).padStart(2, "0")}-${String(monday.getDate()).padStart(2, "0")}`;
}

function getWeeklyAverage() {
  const week = getWeekKey();
  const entries = history.filter(item => item.date >= week && item.date <= todayKey());
  return entries.length ? Math.round(entries.reduce((sum, item) => sum + item.score, 0) / entries.length) : 0;
}

function ensurePointsState() {
  const currentWeek = getWeekKey();
  if (!pointsState.week) {
    pointsState = { ...pointsState, week: currentWeek, baseEarned: 0 };
  } else if (pointsState.week !== currentWeek) {
    pointsState = {
      ...defaultPointsState,
      week: currentWeek,
      baseEarned: 0,
      spent: 0,
      checkins: { ...(pointsState.checkins || {}) },
      interactions: { ...(pointsState.interactions || {}) },
      healthAwards: { ...(pointsState.healthAwards || {}) },
      owned: [...new Set(pointsState.owned || ["aurora"])],
      selected: pointsState.selected || "aurora"
    };
  }
  saveStored(storageKeys.points, pointsState);
  return pointsState;
}

function getCheckinStreak() {
  const checkins = pointsState.checkins || {};
  let streak = 0;
  const cursor = new Date();
  for (;;) {
    const key = `${cursor.getFullYear()}-${String(cursor.getMonth() + 1).padStart(2, "0")}-${String(cursor.getDate()).padStart(2, "0")}`;
    if (!checkins[key]) break;
    streak += 1;
    cursor.setDate(cursor.getDate() - 1);
  }
  return streak;
}

function getMealPoints() {
  const types = new Set(meals.filter(meal => ["早餐", "午餐", "晚餐"].includes(meal.type)).map(meal => meal.type));
  return { count: types.size, points: types.size * 10 + (types.size === 3 ? 20 : 0) };
}

function getDailyPoints() {
  ensurePointsState();
  const today = todayKey();
  return (pointsState.checkins[today] ? 10 : 0) + getMealPoints().points + (pointsState.interactions[today] || 0) + (pointsState.healthAwards[today] || 0);
}

function getWeeklyEarned() {
  ensurePointsState();
  const rankingBonus = new Date().getDay() === 0 ? getWeeklyBonus() : 0;
  return pointsState.baseEarned + getDailyPoints() + rankingBonus;
}

function getPointsBalance() {
  return Math.max(0, getWeeklyEarned() - pointsState.spent);
}

function getWeeklyBonus() {
  if (currentWeeklyRank === 1) return 50;
  if (currentWeeklyRank === 2) return 30;
  if (currentWeeklyRank === 3) return 20;
  return 10;
}

function rewardInteraction() {
  ensurePointsState();
  const today = todayKey();
  pointsState.interactions[today] = Math.min(20, (pointsState.interactions[today] || 0) + 2);
  saveStored(storageKeys.points, pointsState);
  if (document.getElementById("view-shop").classList.contains("active")) renderShop();
}

function rewardHealthScore(score) {
  ensurePointsState();
  const today = todayKey();
  const reward = score >= 90 ? 20 : score >= 80 ? 10 : 5;
  if (reward > (pointsState.healthAwards[today] || 0)) {
    pointsState.healthAwards[today] = reward;
    saveStored(storageKeys.points, pointsState);
    if (document.getElementById("view-shop").classList.contains("active")) renderShop();
  }
}

function applyTheme(themeId) {
  if (!shopThemes.some(theme => theme.id === themeId)) return;
  pointsState.selected = themeId;
  document.body.dataset.theme = themeId;
  saveStored(storageKeys.points, pointsState);
  if (document.getElementById("view-shop").classList.contains("active")) renderShop();
}

function renderShop() {
  ensurePointsState();
  const today = todayKey();
  const earned = getWeeklyEarned();
  const balance = getPointsBalance();
  const mealPoints = getMealPoints();
  const interactions = pointsState.interactions[today] || 0;
  const healthAward = pointsState.healthAwards[today] || 0;
  const checkedIn = Boolean(pointsState.checkins[today]);
  const streak = getCheckinStreak();
  animateNumber(document.getElementById("pointsBalance"), balance);
  document.getElementById("pointsEarned").textContent = earned;
  document.getElementById("pointsSpent").textContent = pointsState.spent;
  document.getElementById("dailyCheckin").disabled = checkedIn;
  document.getElementById("checkinLabel").textContent = checkedIn
    ? (streak > 1 ? `今日已签到 · 连续 ${streak} 天` : "今日已签到")
    : (streak > 0 ? `每日签到 +10 · 已连续 ${streak} 天` : "每日签到 +10");
  document.getElementById("pointsProgressCopy").textContent = `今日已上报 ${mealPoints.count} / 3 餐 · 互动 ${interactions} / 20 · 健康奖励 ${healthAward} 分`;
  document.getElementById("weeklyAverageText").textContent = `你的本周健康均分为 ${getWeeklyAverage()}`;
  document.getElementById("weeklyRankText").textContent = `当前第 ${currentWeeklyRank} 名`;
  document.getElementById("weeklyBonus").textContent = `预计 +${getWeeklyBonus()}`;
  document.getElementById("themeGrid").innerHTML = shopThemes.map(theme => {
    const owned = pointsState.owned.includes(theme.id);
    const selected = pointsState.selected === theme.id;
    const affordable = balance >= theme.price;
    const label = selected ? "使用中" : owned ? "应用" : affordable ? "兑换" : "积分不足";
    return `<article class="theme-card${selected ? " selected" : ""}"><div class="theme-preview theme-${theme.id}"></div><div class="theme-card-body"><h4>${theme.name}</h4><p>${theme.description}</p><div class="theme-card-footer"><span class="theme-price">${theme.price ? `${theme.price} 积分` : "免费"}</span><button class="theme-action${selected ? " applied" : ""}" type="button" data-theme-id="${theme.id}" ${!owned && !affordable ? "disabled" : ""}>${label}</button></div></div></article>`;
  }).join("");
}

function handleThemeAction(event) {
  const button = event.target.closest("[data-theme-id]");
  if (!button || button.disabled) return;
  const theme = shopThemes.find(item => item.id === button.dataset.themeId);
  if (!theme) return;
  if (!pointsState.owned.includes(theme.id)) {
    if (getPointsBalance() < theme.price) {
      showToast("本周可用积分不足");
      return;
    }
    pointsState.spent += theme.price;
    pointsState.owned.push(theme.id);
    showToast(`已兑换「${theme.name}」并应用`);
  } else {
    showToast(`已应用「${theme.name}」`);
  }
  applyTheme(theme.id);
}

function getGuessSequence() {
  const hour = new Date().getHours();
  const ids = hour < 11 ? ["vegetable-rice", "lemon-chicken", "mushroom-soup"] : hour < 17 ? ["lemon-chicken", "tomato-beef", "shrimp-pasta"] : ["mushroom-soup", "vegetable-rice", "tomato-beef"];
  return ids.map(id => foods.find(food => food.id === id));
}

function renderInference() {
  const guessPeriod = document.getElementById("guessPeriod");
  if (!guessPeriod) return;
  const analysis = getHealthAnalysis();
  const sequence = getGuessSequence();
  const index = guessIndex % sequence.length;
  const food = sequence[index];
  const match = Math.min(94, Math.max(58, 68 + Math.round(analysis.score / 5) - index * 6 + (profile.goal === "增加蛋白" ? 4 : 0)));
  const period = new Date().getHours() < 11 ? "早餐猜测" : new Date().getHours() < 17 ? "午餐猜测" : "晚餐猜测";
  const average = getWeeklyAverage();
  currentGuessFoodId = food.id;
  document.getElementById("guessPeriod").textContent = `${period} · 根据当前个人健康状况`;
  document.getElementById("guessName").textContent = food.name;
  const guessImage = document.getElementById("guessImage");
  guessImage.src = food.image;
  guessImage.alt = `猜测你吃了${food.name}`;
  guessImage.style.animation = "none";
  void guessImage.offsetWidth;
  guessImage.style.animation = "";
  document.getElementById("guessMatch").textContent = `${match}% 匹配`;
  document.getElementById("guessBar").style.transform = `scaleX(${match / 100})`;
  document.getElementById("guessReason").textContent = `你的目标是「${profile.goal}」，日常${profile.activity}、睡眠 ${profile.sleep} 小时；结合近 7 天健康均分 ${average} 和当前${analysis.ratios.water < 0.8 ? "饮水不足" : "饮水节奏"}，我猜你更可能选择了这道${food.category}的食物。`;
  document.getElementById("guessEvidence").innerHTML = [profile.goal, profile.activity, `${profile.age} 岁`, `近 7 天均分 ${average}`].map(text => `<span>${text}</span>`).join("");
  elements.reportBasis.textContent = "本地规则推理：健康档案 + 近 7 天记录 + 当前营养进度，不调用在线模型";
}

const WELCOME_BLOCK_HTML = `<article class="message agent-message">
  <div class="agent-avatar" aria-hidden="true"><svg class="icon"><use href="#icon-utensils"></use></svg></div>
  <div class="message-content">
    <div class="message-meta"><strong>饭饭</strong><span>结合近 7 天记录与健康档案</span></div>
    <div class="message-bubble"><p>告诉我今天吃了什么，我会记录这一餐，并结合你近期的饮食节奏、活动量和健康目标更新今日报告。</p><p>泛饭 —— 泛揽三餐，饭伴朝夕。<br>广泛记录每一餐，智能生成健康报告，让好好吃饭变成一件简单的事。</p></div>
    <article class="inference-card" id="inferenceCard" aria-live="polite">
      <header class="inference-heading"><div><span class="section-kicker">本地健康画像推理</span><h2>让我猜猜你今天吃了什么</h2></div><span class="report-meal-count" id="mealCount">已记录 0 餐</span></header>
      <div class="inference-body">
        <div class="inference-visual"><img id="guessImage" src="https://images.unsplash.com/photo-1546069901-ba9599a7e63c?auto=format&amp;fit=crop&amp;w=600&amp;q=82" alt="猜测的食物"><span class="inference-match" id="guessMatch">82% 匹配</span></div>
        <div class="inference-copy"><span class="inference-period" id="guessPeriod">结合当前时段与健康档案</span><h3 id="guessName">柠檬鸡胸能量碗</h3><p class="inference-reason" id="guessReason">根据你的健康目标与近期饮食节奏做出的猜测</p><div class="inference-track"><i id="guessBar" style="transform:scaleX(0.82)"></i></div><div class="inference-evidence" id="guessEvidence"></div></div>
      </div>
      <footer class="inference-footer"><span id="reportBasis">本地规则根据健康档案生成，不调用在线模型</span><div class="inference-actions"><button class="button primary-button" id="acceptGuess" type="button"><svg class="icon"><use href="#icon-check"></use></svg>猜对啦，记录这道菜为</button><div class="meal-type-picker" id="mealTypePicker" hidden><button type="button" data-meal-type="早餐">早餐</button><button type="button" data-meal-type="午餐">午餐</button><button type="button" data-meal-type="下午茶">下午茶</button><button type="button" data-meal-type="晚餐">晚餐</button></div><button class="button secondary-button" id="nextGuess" type="button">再猜一次</button></div></footer>
      <div class="inference-secondary-actions"><button id="addMealButton" type="button">告诉饭饭实际吃了什么</button><button id="viewFullReport" type="button">查看完整健康报告</button></div>
    </article>
  </div>
</article>`;

function bindInferenceCard() {
  const next = document.getElementById("nextGuess");
  if (next && !next.dataset.bound) {
    next.dataset.bound = "1";
    next.addEventListener("click", () => {
      guessIndex += 1;
      renderInference();
    });
  }
  const accept = document.getElementById("acceptGuess");
  if (accept && !accept.dataset.bound) {
    accept.dataset.bound = "1";
    accept.addEventListener("click", event => {
      event.stopPropagation();
      const picker = document.getElementById("mealTypePicker");
      if (picker) picker.hidden = !picker.hidden;
    });
  }
  const picker = document.getElementById("mealTypePicker");
  if (picker && !picker.dataset.bound) {
    picker.dataset.bound = "1";
    picker.addEventListener("click", event => {
      const button = event.target.closest("[data-meal-type]");
      if (!button) return;
      picker.hidden = true;
      recordFood(currentGuessFoodId, { type: button.dataset.mealType });
    });
  }
  const add = document.getElementById("addMealButton");
  if (add && !add.dataset.bound) {
    add.dataset.bound = "1";
    add.addEventListener("click", () => elements.chatInput.focus());
  }
  const report = document.getElementById("viewFullReport");
  if (report && !report.dataset.bound) {
    report.dataset.bound = "1";
    report.addEventListener("click", () => switchView("report"));
  }
}

function ensureWelcomeBlock() {
  const conversation = document.getElementById("conversation");
  if (!conversation) return null;
  let welcome = document.getElementById("inferenceCard")?.closest(".message");
  if (welcome && conversation.contains(welcome)) {
    bindInferenceCard();
    return welcome;
  }
  const holder = document.createElement("div");
  holder.innerHTML = WELCOME_BLOCK_HTML.trim();
  welcome = holder.firstElementChild;
  conversation.insertBefore(welcome, conversation.firstChild);
  elements.mealCount = document.getElementById("mealCount");
  elements.reportBasis = document.getElementById("reportBasis");
  bindInferenceCard();
  renderInference();
  renderReport();
  return welcome;
}

function getNutrition() {
  return meals.reduce((total, meal) => ({
    protein: total.protein + meal.protein,
    fiber: total.fiber + meal.fiber,
    veggies: total.veggies + meal.veggies,
    water: total.water + meal.water,
    penalty: total.penalty + meal.penalty
  }), { protein: 0, fiber: 0, veggies: 0, water: 0, penalty: 0 });
}

function getHealthAnalysis() {
  const nutrition = getNutrition();
  const targets = { protein: 90, fiber: 25, veggies: 500, water: profile.waterTarget };
  const ratios = Object.fromEntries(Object.keys(targets).map(key => [key, Math.min(nutrition[key] / targets[key], 1)]));
  const activityBonus = profile.activity === "较高" ? 3 : profile.activity === "适中" ? 2 : 0;
  const score = Math.max(45, Math.min(96, Math.round(68 + ratios.veggies * 8 + ratios.protein * 6 + ratios.fiber * 5 + Math.min(meals.length, 3) + activityBonus - nutrition.penalty)));
  const status = score >= 86 ? "状态很好" : score >= 76 ? "整体均衡" : score >= 66 ? "继续调整" : "需要关注";
  const remainingWater = Math.max(0, profile.waterTarget - nutrition.water);
  const remainingVeggies = Math.max(0, targets.veggies - nutrition.veggies);
  let summary;
  let adjustment;
  if (ratios.water <= Math.min(ratios.fiber, ratios.veggies, ratios.protein)) {
    summary = `目前最需要补的是水分。近 7 天蔬菜摄入已有改善，后续再补充 ${remainingWater} ml 水，并保持蔬菜与全谷物即可。`;
    adjustment = `饮水进度暂时落后，距离今日参考目标还有 ${remainingWater} ml，下午和晚间分次补充即可。`;
  } else if (ratios.veggies < 1) {
    summary = `蛋白质进度较好，蔬菜还可以再丰富一些。后续增加 ${remainingVeggies} g 蔬菜，能让这一天的搭配更完整。`;
    adjustment = `蔬菜还可增加 ${remainingVeggies} g，优先选择方便获得的绿叶菜、番茄或瓜类。`;
  } else {
    summary = "今天的蔬菜与饮水安排较完整，继续保持规律三餐和多样食材，不需要追求单餐完美。";
    adjustment = "晚餐按饥饿程度安排份量，保持蔬菜、蛋白质和全谷物的组合即可。";
  }
  return { nutrition, targets, ratios, score, status, summary, adjustment };
}

function animateNumber(element, target, options = {}) {
  if (!element) return;
  const duration = options.duration || 550;
  const prefix = options.prefix || "";
  const from = element.dataset.animValue === undefined ? 0 : Number(element.dataset.animValue);
  const to = Number(target);
  element.dataset.animValue = String(to);
  if (!Number.isFinite(from) || !Number.isFinite(to) || from === to) {
    element.textContent = `${prefix}${Math.round(to)}`;
    return;
  }
  const start = performance.now();
  const ease = progress => 1 - Math.pow(1 - progress, 3);
  const tick = now => {
    const progress = Math.min(1, (now - start) / duration);
    element.textContent = `${prefix}${Math.round(from + (to - from) * ease(progress))}`;
    if (progress < 1) requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
}

function renderReport() {
  const analysis = getHealthAnalysis();
  const { nutrition, targets, ratios, score, status, adjustment } = analysis;
  elements.mealCount.textContent = `已记录 ${meals.length} 餐`;
  const snapshotAge = Number(profile.age);
  const snapshotHeight = Number(profile.height);
  const snapshotWeight = Number(profile.weight);
  document.getElementById("snapshotAge").textContent = snapshotAge > 0 ? `${snapshotAge} 岁` : "未填写";
  document.getElementById("snapshotBody").textContent =
    snapshotHeight > 0 && snapshotWeight > 0 ? `${snapshotHeight} cm · ${snapshotWeight} kg` : "未填写";
  document.getElementById("profileHeadline").textContent =
    snapshotHeight > 0 ? `${profile.goal}，${profile.activity}状态` : "还没有填写健康档案";
  document.getElementById("profileDescription").textContent =
    snapshotHeight > 0
      ? `报告会结合${profile.activity}活动量、${profile.sleep} 小时睡眠与${profile.goal}目标，观察近期饮食节奏。`
      : "先填写身高、体重和日常活动量，报告才有可靠的背景。";
  document.getElementById("snapshotWater").textContent = `${profile.waterTarget} ml`;
  document.getElementById("snapshotSleep").textContent = `${profile.sleep} 小时`;
  const initialText = document.getElementById("avatarInitialText");
  if (initialText) initialText.textContent = (profile.name || "我").trim().slice(0, 1) || "我";
  if (!meals.length) {
    document.getElementById("detailScoreValue").textContent = "—";
    document.getElementById("detailScoreStatus").textContent = "还没有记录";
    document.getElementById("detailScoreCopy").textContent = "记录今天第一餐后，这里会显示真实评分。";
    document.getElementById("snapshotScore").textContent = "—";
    document.getElementById("detailNutritionList").innerHTML = `<div class="empty-state">今天还没有饮食记录</div>`;
    document.getElementById("reportTags").innerHTML = "";
    document.getElementById("adjustmentInsight").textContent = "还缺少今天的饮食记录。";
    document.getElementById("positiveInsight").textContent = "记录一餐后，饭饭会结合你的档案给出建议。";
    renderTrend(null);
    renderLeaderboard(0);
    return;
  }
  animateNumber(document.getElementById("detailScoreValue"), score);
  document.getElementById("detailScoreStatus").textContent = status;
  document.getElementById("adjustmentInsight").textContent = adjustment;
  animateNumber(document.getElementById("snapshotScore"), score);
  const values = {
    protein: `${nutrition.protein} / ${targets.protein} g`,
    fiber: `${nutrition.fiber} / ${targets.fiber} g`,
    veggies: `${nutrition.veggies} / ${targets.veggies} g`,
    water: `${nutrition.water} / ${targets.water} ml`
  };
  const labels = { protein: "蛋白质", fiber: "膳食纤维", veggies: "蔬菜", water: "饮水" };
  document.getElementById("detailNutritionList").innerHTML = `<div class="detail-nutrition-list">${Object.keys(labels).map(key => `<div class="detail-nutrition-row"><span>${labels[key]}</span><div class="nutrition-track"><i style="transform:scaleX(${Math.round(ratios[key] * 100) / 100})"></i></div><strong>${values[key]}</strong></div>`).join("")}</div>`;
  const tags = [ratios.veggies >= 0.6 ? "蔬菜摄入良好" : "蔬菜待补充", ratios.protein >= 1 ? "蛋白质已达标" : "蛋白质进行中", ratios.water < 0.8 ? "饮水待补充" : "饮水节奏良好"];
  document.getElementById("reportTags").innerHTML = tags.map(tag => `<span>${tag}</span>`).join("");
  renderTrend(score);
  if (!remoteWeekly) updateTodayHistory(score);
  rewardHealthScore(score);
  renderInference();
  renderLeaderboard(score);
  if (document.getElementById("view-shop").classList.contains("active")) renderShop();
}

let trendData = null;
let trendIndex = 6;

function renderTrend(score) {
  const daily = remoteWeekly && Array.isArray(remoteWeekly.daily)
    ? remoteWeekly.daily.slice(-7)
    : [];
  const labels = daily.map(item => String(item.date || "").slice(5));
  const values = daily.map(item => Number(item.score) || 0);
  if (!values.length) {
    trendData = null;
    document.getElementById("trendLine").setAttribute("points", "");
    document.getElementById("trendArea").setAttribute("d", "");
    document.getElementById("trendDots").innerHTML = "";
    document.getElementById("trendAverageValue").textContent = "—";
    document.getElementById("trendSelectedValue").textContent = "—";
    document.getElementById("trendSelectedDate").textContent = "";
    document.querySelectorAll(".chart-label").forEach(label => {
      label.textContent = "";
      label.classList.remove("active");
    });
    return;
  }
  const step = values.length > 1 ? 440 / (values.length - 1) : 0;
  const points = values.map((value, index) => {
    const x = values.length > 1 ? 20 + index * step : 240;
    const y = 260 - value * 2.2;
    return [Number(x.toFixed(1)), Number(Math.max(28, y).toFixed(1))];
  });
  trendData = { labels, values, points };
  trendIndex = values.length - 1;
  const trendLine = document.getElementById("trendLine");
  const trendArea = document.getElementById("trendArea");
  trendLine.setAttribute("points", points.map(point => point.join(",")).join(" "));
  trendArea.setAttribute("d", `M${points[0][0]} 158 L${points.map(point => point.join(" ")).join(" L")} L${points[points.length - 1][0]} 158 Z`);
  trendLine.style.animation = "none";
  trendArea.style.animation = "none";
  void trendLine.getBoundingClientRect();
  trendLine.style.animation = "";
  trendArea.style.animation = "";
  document.getElementById("trendDots").innerHTML = points.map((point, index) => `<circle class="trend-dot${index === trendIndex ? " selected" : ""}" data-index="${index}" cx="${point[0]}" cy="${point[1]}" r="${index === trendIndex ? 6 : 3.5}"></circle>`).join("");
  document.querySelectorAll(".chart-label").forEach((label, index) => {
    const point = points[index];
    if (point) {
      label.setAttribute("x", point[0]);
      label.textContent = labels[index] || "";
    } else {
      label.textContent = "";
    }
    label.classList.toggle("active", index === trendIndex);
  });
  animateNumber(document.getElementById("trendAverageValue"), Math.round(values.reduce((sum, value) => sum + value, 0) / values.length));
  setTrendIndex(trendIndex, false);
}

function setTrendIndex(index, animate = true) {
  if (!trendData) return;
  const clamped = Math.max(0, Math.min(trendData.values.length - 1, index));
  trendIndex = clamped;
  document.querySelectorAll("#trendDots .trend-dot").forEach((dot, position) => {
    const selected = position === clamped;
    dot.classList.toggle("selected", selected);
    dot.setAttribute("r", selected ? "6" : "3.5");
  });
  document.querySelectorAll(".chart-label").forEach((label, position) => label.classList.toggle("active", position === clamped));
  const dateEl = document.getElementById("trendSelectedDate");
  if (dateEl) dateEl.textContent = trendData.labels[clamped];
  rollTrendNumber(trendData.values[clamped], animate);
}

function rollTrendNumber(value, animate = true) {
  const el = document.getElementById("trendSelectedValue");
  if (!el) return;
  const from = Number(el.dataset.value ?? value);
  el.dataset.value = String(value);
  if (!animate || from === value) {
    el.innerHTML = `<span class="roll-new">${value}</span>`;
    el.classList.remove("roll-up", "roll-down");
    return;
  }
  const direction = value > from ? "up" : "down";
  el.innerHTML = `<span class="roll-old">${from}</span><span class="roll-new">${value}</span>`;
  el.classList.remove("roll-up", "roll-down");
  void el.offsetWidth;
  el.classList.add(direction === "up" ? "roll-up" : "roll-down");
  clearTimeout(el._rollTimer);
  el._rollTimer = setTimeout(() => {
    el.innerHTML = `<span class="roll-new">${value}</span>`;
    el.classList.remove("roll-up", "roll-down");
  }, 420);
}

const trendChart = document.getElementById("healthTrendChart");
if (trendChart) {
  let dragging = false;
  const selectNearest = event => {
    if (!trendData) return;
    const rect = trendChart.getBoundingClientRect();
    const scaleX = rect.width / 480;
    const scaleY = rect.height / 200;
    const ctm = trendChart.getScreenCTM?.();
    const toScreen = (x, y) => {
      if (ctm) {
        const point = trendChart.createSVGPoint();
        point.x = x;
        point.y = y;
        const result = point.matrixTransform(ctm);
        return { x: result.x, y: result.y };
      }
      return { x: rect.left + x * scaleX, y: rect.top + y * scaleY };
    };
    let nearest = -1;
    let best = Infinity;
    trendData.points.forEach((point, index) => {
      const screen = toScreen(point[0], point[1]);
      const distance = Math.hypot(screen.x - event.clientX, screen.y - event.clientY);
      if (distance < best) {
        best = distance;
        nearest = index;
      }
    });
    const radius = 22;
    const target = dragging || best <= radius ? nearest : trendData.values.length - 1;
    if (target !== trendIndex) setTrendIndex(target, true);
  };
  trendChart.addEventListener("pointerdown", event => {
    dragging = true;
    trendChart.setPointerCapture?.(event.pointerId);
    selectNearest(event);
  });
  trendChart.addEventListener("pointermove", event => {
    if (event.pointerType === "touch" && !dragging) return;
    selectNearest(event);
  });
  const endDrag = () => {
    if (dragging && trendData) setTrendIndex(trendIndex, true);
    dragging = false;
  };
  trendChart.addEventListener("pointerup", endDrag);
  trendChart.addEventListener("pointercancel", endDrag);
  trendChart.addEventListener("pointerleave", event => {
    if (event.pointerType && event.pointerType !== "mouse") return;
    if (trendData) setTrendIndex(trendData.values.length - 1, true);
  });
  trendChart.style.touchAction = "pan-y";
}

function renderMealRow(meal, index) {
  if (meal.pending) {
    return `<div class="meal-item pending"><time>${escapeHtml(meal.time)}</time><span class="timeline-dot">${icon("check")}</span><div><strong>${escapeHtml(meal.type)}</strong><p>${escapeHtml(meal.text)}</p></div></div>`;
  }
  if (index === editingMealIndex) {
    return `<div class="meal-item completed editing" data-meal-index="${index}"><time>${escapeHtml(meal.time)}</time><span class="timeline-dot">${icon("check")}</span><div class="meal-edit"><select class="meal-edit-type" aria-label="餐次">${MEAL_TYPES.map(type => `<option value="${type}"${type === meal.type ? " selected" : ""}>${type}</option>`).join("")}</select><input class="meal-edit-text" type="text" value="${escapeHtml(meal.text)}" aria-label="记录内容"><div class="meal-edit-actions"><button type="button" class="meal-edit-save" data-meal-action="save">保存</button><button type="button" class="meal-edit-cancel" data-meal-action="cancel">取消</button></div></div></div>`;
  }
  const confirming = index === deletingMealIndex;
  const actions = confirming
    ? `<button type="button" class="meal-action danger" data-meal-action="confirm-delete">删除</button><button type="button" class="meal-action" data-meal-action="cancel">取消</button>`
    : `<button type="button" class="meal-action" data-meal-action="edit" aria-label="修改" title="修改">${icon("pencil")}</button><button type="button" class="meal-action" data-meal-action="delete" aria-label="删除" title="删除">${icon("trash")}</button>`;
  return `<div class="meal-item completed${confirming ? " confirming" : ""}" data-meal-index="${index}"><time>${escapeHtml(meal.time)}</time><span class="timeline-dot">${icon("check")}</span><div><strong>${escapeHtml(meal.type)}</strong><p>${escapeHtml(meal.text)}</p>${confirming ? '<small class="meal-delete-hint">确定删除这条记录？</small>' : ""}</div><div class="meal-actions${confirming ? " is-visible" : ""}">${actions}</div></div>`;
}

function renderMeals() {
  const slots = ["早餐", "午餐", "晚餐"];
  const indexed = meals.map((meal, index) => ({ meal, index }));
  const rows = slots.map(type => {
    const pos = indexed.findIndex(entry => entry.meal.type === type);
    if (pos >= 0) return indexed.splice(pos, 1)[0];
    return { meal: { type, time: type === "晚餐" ? "晚间" : "待记录", text: type === "晚餐" ? "还没有记录晚餐" : "可以随时补充", pending: true }, index: -1 };
  });
  rows.push(...indexed);
  elements.mealTimeline.innerHTML = rows.map(row => renderMealRow(row.meal, row.index)).join("");
}

function deleteMealAt(index) {
  const meal = meals[index];
  if (!meal) return;
  meals.splice(index, 1);
  saveStored(storageKeys.meals, meals);
  editingMealIndex = -1;
  deletingMealIndex = -1;
  renderMeals();
  renderReport();
  showToast(`已删除${meal.type}记录`);
}

function updateMealAt(index, type, text) {
  const meal = meals[index];
  if (!meal) return;
  const nextText = String(text || "").trim();
  if (!nextText) {
    showToast("请填写吃了什么");
    return;
  }
  const nextType = MEAL_TYPES.includes(type) ? type : meal.type;
  meals[index] = { ...meal, type: nextType, text: nextText, ...analyzeMealText(nextText) };
  saveStored(storageKeys.meals, meals);
  editingMealIndex = -1;
  renderMeals();
  renderReport();
  showToast("记录已更新");
}

function applyMealAction(action) {
  if (!action || typeof action !== "object") return;
  const type = String(action.type || "");
  const needle = String(action.text || "").trim();
  let index = -1;
  for (let i = meals.length - 1; i >= 0; i -= 1) {
    if (meals[i].type !== type) continue;
    if (needle && !String(meals[i].text || "").includes(needle)) continue;
    index = i;
    break;
  }
  if (index < 0) {
    showToast(`没有找到${type}记录`);
    return;
  }
  if (action.action === "delete") {
    meals.splice(index, 1);
    showToast(`已删除${type}记录`);
  } else {
    const next = { ...meals[index] };
    if (action.new_type) next.type = action.new_type;
    if (action.new_text) {
      next.text = action.new_text;
      Object.assign(next, analyzeMealText(action.new_text));
    }
    meals[index] = next;
    showToast(`已更新${type}记录`);
  }
  saveStored(storageKeys.meals, meals);
  editingMealIndex = -1;
  deletingMealIndex = -1;
  renderMeals();
  renderReport();
}

elements.mealTimeline?.addEventListener("click", event => {
  const button = event.target.closest("[data-meal-action]");
  const row = event.target.closest("[data-meal-index]");
  if (!button || !row) return;
  const index = Number(row.dataset.mealIndex);
  if (!Number.isInteger(index) || index < 0) return;
  const action = button.dataset.mealAction;
  if (action === "edit") {
    editingMealIndex = index;
    deletingMealIndex = -1;
    renderMeals();
    requestAnimationFrame(() => {
      const input = elements.mealTimeline.querySelector(".meal-item.editing .meal-edit-text");
      input?.focus();
      input?.select?.();
    });
  } else if (action === "delete") {
    deletingMealIndex = index;
    editingMealIndex = -1;
    renderMeals();
  } else if (action === "cancel") {
    editingMealIndex = -1;
    deletingMealIndex = -1;
    renderMeals();
  } else if (action === "confirm-delete") {
    deleteMealAt(index);
  } else if (action === "save") {
    updateMealAt(index, row.querySelector(".meal-edit-type")?.value || "", row.querySelector(".meal-edit-text")?.value || "");
  }
});

elements.mealTimeline?.addEventListener("keydown", event => {
  const row = event.target.closest(".meal-item.editing[data-meal-index]");
  if (!row) return;
  const index = Number(row.dataset.mealIndex);
  if (event.key === "Enter") {
    event.preventDefault();
    updateMealAt(index, row.querySelector(".meal-edit-type")?.value || "", row.querySelector(".meal-edit-text")?.value || "");
  } else if (event.key === "Escape") {
    editingMealIndex = -1;
    renderMeals();
  }
});

function renderLeaderboard(currentScore = getHealthAnalysis().score) {
  const weeklyAverage = getWeeklyAverage();
  const weeklyData = rankings.week.map(item => item.current ? { ...item, score: weeklyAverage } : { ...item }).sort((a, b) => b.score - a.score);
  const championCover = document.querySelector(".champion-cover");
  const championScoreEl = document.querySelector(".champion-score");
  if (!weeklyData.length || remoteFriends.length === 0) {
    if (championCover) championCover.classList.add("is-empty");
    if (championScoreEl) championScoreEl.hidden = true;
    document.getElementById("championAvatar").textContent = "—";
    document.getElementById("championName").textContent = "还没有好友";
    document.getElementById("championScore").textContent = "—";
    document.getElementById("championNote").textContent = "添加好友后显示排行";
    elements.leaderboardList.innerHTML = "";
    document.getElementById("rankingSummary").textContent = "添加好友并开始记录后显示排行";
    return;
  }
  if (championCover) championCover.classList.remove("is-empty");
  if (championScoreEl) championScoreEl.hidden = false;
  currentWeeklyRank = weeklyData.findIndex(item => item.current) + 1;
  const data = activeRanking === "week" ? weeklyData : rankings.today.map(item => item.current ? { ...item, score: currentScore, note: `已记录 ${meals.length} 餐` } : { ...item }).sort((a, b) => b.score - a.score);
  const champion = data[0];
  const championLabel = champion.current ? (profile.name || "我") : champion.name;
  document.getElementById("championAvatar").textContent = championLabel.slice(0, 1);
  document.getElementById("championName").textContent = championLabel;
  animateNumber(document.getElementById("championScore"), champion.score);
  document.getElementById("championNote").textContent = `${activeRanking === "week" ? "本周" : "今日"}第 1 名 · ${champion.note}`;
  const previousCurrent = elements.leaderboardList.querySelector(".leaderboard-row.current-user");
  const previousTop = previousCurrent ? previousCurrent.getBoundingClientRect().top : null;
  elements.leaderboardList.innerHTML = data.map((item, index) => {
    const label = item.current ? (profile.name || "我") : item.name;
    const media = peerMediaFor(item);
    const avatar = (media && (media.avatar || media.cover)) || item.avatar || item.cover ||
      (item.current ? localStorage.getItem("eat-user-avatar") || "" : "");
    const initial = escapeHtml(String(label || "友").slice(0, 1));
    const avatarMarkup = avatar
      ? `<img src="${escapeHtml(avatar)}" alt="" onerror="this.hidden=true;this.nextElementSibling.hidden=false"><span class="friend-initial" hidden>${initial}</span>`
      : initial;
    return `<div class="leaderboard-row${item.current ? " current-user" : ""}"><span class="rank-number">${index + 1}</span><span class="friend-avatar${avatar ? " has-photo" : ""}">${avatarMarkup}</span><div class="friend-copy"><strong>${escapeHtml(label)}</strong><small>${escapeHtml(item.note)}</small></div><span class="friend-score">${item.score}<small>分</small></span></div>`;
  }).join("");
  const nextCurrent = elements.leaderboardList.querySelector(".leaderboard-row.current-user");
  if (previousTop !== null && nextCurrent) {
    const delta = previousTop - nextCurrent.getBoundingClientRect().top;
    if (Math.abs(delta) > 1) {
      nextCurrent.style.transition = "none";
      nextCurrent.style.transform = `translateY(${delta}px)`;
      requestAnimationFrame(() => {
        nextCurrent.style.transition = "transform 460ms cubic-bezier(0.22, 1, 0.36, 1)";
        nextCurrent.style.transform = "translateY(0)";
      });
      nextCurrent.addEventListener("transitionend", () => {
        nextCurrent.style.transition = "";
        nextCurrent.style.transform = "";
      }, { once: true });
    }
  }
  const rank = data.findIndex(item => item.current) + 1;
  document.getElementById("rankingSummary").textContent = activeRanking === "week" ? `本周健康均分 ${weeklyAverage} · 当前第 ${rank} 名 · 周奖励预计 +${getWeeklyBonus()}` : `今日健康分 ${currentScore} · 当前第 ${rank} 名`;
  if (document.getElementById("view-shop").classList.contains("active")) renderShop();
  applyChampionCover(champion);
  syncPeerMedia(data);
}

function updateTodayHistory(score) {
  const today = todayKey();
  const summary = meals.map(meal => meal.text).join("；");
  const existing = history.find(item => item.date === today);
  if (existing) {
    existing.meals = meals.length;
    existing.summary = summary;
    existing.score = score;
  } else {
    history.unshift({ date: today, meals: meals.length, summary, score });
  }
  saveStored(storageKeys.history, history);
  if (document.getElementById("view-history").classList.contains("active")) renderHistory();
}

function renderHistory() {
  const reference = new Date();
  reference.setHours(23, 59, 59, 999);
  const filtered = history.filter(item => {
    if (historyFilter === "all") return true;
    const difference = (reference - new Date(`${item.date}T12:00:00`)) / 86400000;
    return difference >= -1 && difference <= (historyFilter === "week" ? 7 : 30);
  });
  elements.historyList.innerHTML = filtered.length ? filtered.map(item => `<div class="history-row"><time datetime="${item.date}">${item.date.slice(5).replace("-", "/")}</time><span>${item.meals} 餐</span><span class="history-summary" title="${escapeHtml(item.summary)}">${escapeHtml(item.summary)}</span><strong class="history-score">${item.score} 分</strong></div>`).join("") : `<div class="history-empty">这个时间范围还没有饮食记录</div>`;
}

const DEFAULT_SPEND_RANGE = { min: 20, max: 35 };
let menuGroups = null;

function agentBase() {
  return (localStorage.getItem("eat-agent-base") || `http://${location.hostname || "127.0.0.1"}:8787`).replace(/\/$/, "");
}

function spendRange() {
  const min = Number(profile.spend_min);
  const max = Number(profile.spend_max);
  if (Number.isFinite(min) && Number.isFinite(max) && min >= 0 && max >= min) return { min, max };
  return { ...DEFAULT_SPEND_RANGE };
}

function classifyFood(food) {
  const range = spendRange();
  const tolerance = Math.max(2, Math.max(1, range.max - range.min) * 0.25);
  const price = Number(food?.price);
  if (!Number.isFinite(price) || price <= 0) {
    return { group: "other", priceGap: null, reason: "未标注价格" };
  }
  if (price >= range.min && price <= range.max) return { group: "daily", priceGap: 0, reason: "在消费区间内" };
  if (price < range.min && price >= range.min - tolerance) return { group: "daily", priceGap: Math.round((range.min - price) * 10) / 10, reason: "略低于消费区间" };
  if (price > range.max && price <= range.max + tolerance) return { group: "daily", priceGap: Math.round((price - range.max) * 10) / 10, reason: "略高于消费区间" };
  return {
    group: "other",
    priceGap: price < range.min ? Math.round((range.min - price) * 10) / 10 : Math.round((price - range.max) * 10) / 10,
    reason: "偏离消费区间"
  };
}

function decorateFood(food) {
  const local = classifyFood(food);
  const price = Number(food?.price);
  return {
    ...food,
    price: Number.isFinite(price) && price > 0 ? price : null,
    group: food?.group === "daily" || food?.group === "other" ? food.group : local.group,
    price_gap: food?.price_gap ?? local.priceGap,
    price_reason: food?.price_reason || local.reason
  };
}

function foodGroupOf(food) {
  if (food?.group === "daily" || food?.group === "other") return food.group;
  return classifyFood(food).group;
}

function groupLabelOf(food) {
  return foodGroupOf(food) === "daily" ? "日常菜" : "其他菜品";
}

function priceLabelOf(food) {
  const price = Number(food?.price);
  return Number.isFinite(price) && price > 0 ? `¥${price}` : "待定价";
}

function rebuildLocalGroups() {
  const decorated = foods.map(food => decorateFood({ ...food, group: foodGroupOf(food) }));
  foods.splice(0, foods.length, ...decorated);
  menuGroups = {
    daily: decorated.filter(food => food.group === "daily"),
    other: decorated.filter(food => food.group === "other"),
    spend: spendRange(),
    menuPrice: null,
    source: "local"
  };
}

function applyAgentMenu(data) {
  if (!data) return;
  const daily = (data.daily || []).map(decorateFood);
  const other = (data.other || []).map(decorateFood);
  const all = Array.isArray(data.foods) && data.foods.length ? data.foods.map(decorateFood) : [...daily, ...other];
  if (all.length) foods.splice(0, foods.length, ...all);
  const level = data.spend_level || {};
  menuGroups = {
    daily,
    other,
    spend: Number.isFinite(Number(level.min)) && Number.isFinite(Number(level.max)) ? { min: Number(level.min), max: Number(level.max) } : spendRange(),
    menuPrice: data.menu_price && Number(data.menu_price.max) > 0 ? data.menu_price : null,
    source: data.source || "agent"
  };
  renderFoodMenus();
}

async function loadAgentMenu() {
  rebuildLocalGroups();
  renderFoodMenus();
  try {
    const payload = await window.eatTransport.request("/api/menu?mode=work&user_id=u_demo");
    if (payload && payload.ok && payload.data) applyAgentMenu(payload.data);
  } catch {
    // 保持本地消费区间分组
  }
}

function updateMenuHeader() {
  const range = menuGroups?.spend || spendRange();
  const daily = menuGroups?.daily.length || 0;
  const other = menuGroups?.other.length || 0;
  const note = document.querySelector(".food-module-header small");
  if (note) note.textContent = `日常 ${daily} · 其他 ${other} · ¥${range.min}–${range.max}/餐`;
  const kicker = document.querySelector("#expandedMenu .section-kicker");
  if (kicker) kicker.textContent = `全部食物 · 日常 ${daily} / 其他 ${other} · ¥${range.min}–${range.max}/餐`;
}

let foodPage = 0;
const foodPageSize = 2;

function renderSidebarFoodPage(direction = 0) {
  const pageCount = Math.ceil(foods.length / foodPageSize);
  const start = foodPage * foodPageSize;
  const pageFoods = foods.slice(start, start + foodPageSize);
  const list = document.getElementById("sidebarFoodList");
  list.innerHTML = pageFoods.map(food => `<button class="sidebar-food-item" type="button" data-food-id="${food.id}"><img src="${food.image}" alt="${food.name}" loading="lazy"><span class="sidebar-food-copy"><small><span>${food.category}</span><em class="food-group ${foodGroupOf(food)}">${groupLabelOf(food)}</em></small><strong>${food.name}</strong><span class="sidebar-food-meta"><span>${food.calories} 千卡</span><span>${food.protein} g 蛋白</span><span class="food-price">${priceLabelOf(food)}</span></span></span></button>`).join("");
  list.classList.remove("page-forward", "page-back");
  if (direction) {
    void list.offsetWidth;
    list.classList.add(direction > 0 ? "page-forward" : "page-back");
  }
  document.getElementById("foodPageLabel").textContent = `${foodPage + 1} / ${pageCount}`;
  document.getElementById("foodPageDots").innerHTML = Array.from({ length: pageCount }, (_, index) => `<button type="button" data-food-page="${index}" class="${index === foodPage ? "active" : ""}" aria-label="第 ${index + 1} 页"></button>`).join("");
}

function changeFoodPage(direction) {
  const pageCount = Math.ceil(foods.length / foodPageSize);
  foodPage = (foodPage + direction + pageCount) % pageCount;
  renderSidebarFoodPage(direction);
}

let masonryCycle = 0;
let masonryLoading = false;
let masonrySearchMode = false;

function appendMasonryCycle() {
  const columns = [...elements.expandedMenuList.querySelectorAll(".masonry-column")];
  if (!columns.length) return;
  const imageHeights = [112, 148, 128, 168, 136, 156, 120, 176];
  foods.forEach((food, index) => {
    const columnIndex = (index + masonryCycle * 2) % columns.length;
    const imageHeight = imageHeights[(index + masonryCycle + columnIndex) % imageHeights.length];
    const match = getFoodMatch(food);
    const card = document.createElement("button");
    card.type = "button";
    card.className = "masonry-card";
    card.dataset.foodId = food.id;
    card.style.setProperty("--match", "0");
    card.style.animationDelay = `${Math.min(index + masonryCycle * 3, 20) * 26}ms`;
    requestAnimationFrame(() => card.style.setProperty("--match", match));
    card.innerHTML = `<span class="masonry-image-wrap"><img src="${food.image}" alt="${food.name}" style="height:${imageHeight}px" loading="lazy"><span class="masonry-add">${icon("plus")}</span></span><span class="masonry-copy"><small><span>${food.category}</span><em class="food-group ${foodGroupOf(food)}">${groupLabelOf(food)}</em></small><strong>${food.name}</strong><span><span>${food.calories} 千卡</span><span>${food.protein} g 蛋白</span><span class="food-price">${priceLabelOf(food)}</span></span><span class="search-result-match">匹配 ${match}%</span></span>`;
    columns[columnIndex].appendChild(card);
  });
  masonryCycle += 1;
}

function updateFoodFeedProgress() {
  const list = elements.expandedMenuList;
  const cards = list.querySelectorAll(".masonry-card");
  document.getElementById("foodFeedProgress").textContent = `${cards.length} 张卡片`;
  if (masonrySearchMode) return;
  const nearBottom = list.scrollHeight - list.scrollTop - list.clientHeight < 420;
  if (elements.expandedMenu.classList.contains("open") && nearBottom && !masonryLoading) {
    masonryLoading = true;
    appendMasonryCycle();
    document.getElementById("foodFeedProgress").textContent = `${masonryCycle * foods.length} 张卡片`;
    requestAnimationFrame(() => { masonryLoading = false; });
  }
}

function renderMasonryFeed() {
  masonrySearchMode = false;
  masonryCycle = 0;
  masonryLoading = false;
  elements.expandedMenuList.innerHTML = Array.from({ length: 5 }, (_, index) => `<div class="masonry-column" data-column="${index + 1}"></div>`).join("");
  for (let cycle = 0; cycle < 4; cycle += 1) appendMasonryCycle();
  updateFoodFeedProgress();
}

function renderMasonryResults(list, query) {
  masonrySearchMode = true;
  masonryLoading = false;
  if (!list.length) {
    elements.expandedMenuList.innerHTML = `<div class="masonry-empty">没有找到「${escapeHtml(query)}」，换个词试试</div>`;
    document.getElementById("foodFeedProgress").textContent = "0 张卡片";
    return;
  }
  elements.expandedMenuList.innerHTML = Array.from({ length: 5 }, (_, index) => `<div class="masonry-column" data-column="${index + 1}"></div>`).join("");
  const columns = [...elements.expandedMenuList.querySelectorAll(".masonry-column")];
  const imageHeights = [150, 190, 160, 210, 170];
  list.forEach((item, index) => {
    const food = item.food;
    const card = document.createElement("button");
    card.type = "button";
    card.className = "masonry-card";
    card.dataset.foodId = food.id;
    card.style.setProperty("--match", "0");
    card.style.animationDelay = `${Math.min(index, 12) * 26}ms`;
    requestAnimationFrame(() => card.style.setProperty("--match", item.match));
    card.innerHTML = `<span class="masonry-image-wrap"><img src="${food.image}" alt="${escapeHtml(food.name)}" style="height:${imageHeights[index % imageHeights.length]}px" loading="lazy"><span class="masonry-add">${icon("plus")}</span></span><span class="masonry-copy"><small><span>${escapeHtml(food.category)}</span><em class="food-group ${foodGroupOf(food)}">${groupLabelOf(food)}</em></small><strong>${escapeHtml(food.name)}</strong><span><span>${food.calories} 千卡</span><span>${food.protein} g 蛋白</span><span class="food-price">${priceLabelOf(food)}</span></span><span class="search-result-match">匹配 ${item.match}%</span></span>`;
    columns[index % columns.length].appendChild(card);
  });
  document.getElementById("foodFeedProgress").textContent = `${list.length} 张卡片`;
}

function renderFoodMenus() {
  updateMenuHeader();
  renderSidebarFoodPage();
  renderMasonryFeed();
}

const defaultChampionCover = "https://images.unsplash.com/photo-1500530855697-b586d89ba3ee?auto=format&fit=crop&w=800&q=82";

function getFoodMatch(food) {
  const analysis = getHealthAnalysis();
  const vegGap = Math.max(0, 1 - analysis.ratios.veggies);
  const proteinGap = Math.max(0, 1 - analysis.ratios.protein);
  const fiberGap = Math.max(0, 1 - analysis.ratios.fiber);
  let score = 54;
  score += vegGap * Math.min(food.veggies / 500, 1) * 24;
  score += proteinGap * Math.min(food.protein / 90, 1) * 16;
  score += fiberGap * Math.min(food.fiber / 25, 1) * 10;
  score += food.calories <= 480 ? 8 : food.calories <= 580 ? 3 : -6;
  if (food.nature === "indulge") score -= 14;
  if (profile.goal === "增加蛋白" && food.protein >= 30) score += 6;
  if (profile.goal === "控制体重" && food.calories > 520) score -= 6;
  if (profile.goal === "减少添加糖" && food.nature === "indulge") score -= 5;
  if (profile.goal === "均衡饮食" && food.veggies >= 250) score += 4;
  return Math.max(20, Math.min(98, Math.round(score)));
}

function searchFoods(query) {
  const tokens = query.toLowerCase().split(/\s+/).filter(Boolean);
  if (!tokens.length) return [];
  return foods
    .map(food => {
      const hay = `${food.name} ${food.category} ${food.description} ${food.keywords || ""}`.toLowerCase();
      const hit = tokens.filter(token => hay.includes(token)).length;
      return { food, hit, match: getFoodMatch(food) };
    })
    .filter(item => item.hit > 0)
    .sort((a, b) => b.hit - a.hit || b.match - a.match);
}

function renderFoodSearch(query) {
  const box = document.getElementById("foodSearchResults");
  if (!box) return;
  const q = query.trim();
  if (!q) {
    box.classList.remove("open");
    box.innerHTML = "";
    if (elements.expandedMenu.classList.contains("open") && masonrySearchMode) renderMasonryFeed();
    return;
  }
  const list = searchFoods(q);
  if (elements.expandedMenu.classList.contains("open")) renderMasonryResults(list, q);
  if (!list.length) {
    box.classList.add("open");
    box.innerHTML = `<p class="food-search-empty">没有找到「${escapeHtml(q)}」，换个词试试</p>`;
    return;
  }
  const groupNames = { recommend: "推荐菜", indulge: "放纵菜" };
  box.innerHTML = ["recommend", "indulge"].map(nature => {
    const items = list.filter(item => item.food.nature === nature);
    if (!items.length) return "";
    return `<div class="food-search-group"><h4>${groupNames[nature]}</h4>${items.map(item => `<button class="search-result" type="button" data-food-id="${item.food.id}" style="--match:${item.match}"><img src="${item.food.image}" alt="${escapeHtml(item.food.name)}" loading="lazy"><span class="search-result-copy"><small><span>${escapeHtml(item.food.category)}</span><em class="food-group ${foodGroupOf(item.food)}">${groupLabelOf(item.food)}</em></small><strong>${escapeHtml(item.food.name)}</strong><span>${item.food.calories} 千卡 · 蛋白 ${item.food.protein} g · <em class="food-price">${priceLabelOf(item.food)}</em></span></span><span class="search-result-match">匹配 ${item.match}%</span></button>`).join("")}</div>`;
  }).join("");
  box.classList.add("open");
  box.querySelectorAll(".search-result").forEach(card => {
    const target = card.style.getPropertyValue("--match");
    card.style.setProperty("--match", "0");
    requestAnimationFrame(() => card.style.setProperty("--match", target));
  });
}

function clearFoodSearch() {
  const input = document.getElementById("foodSearchInput");
  if (input) input.value = "";
  renderFoodSearch("");
}

let randomHistory = [];

function pickLocalRandom(excludeId) {
  const pool = excludeId ? foods.filter(food => food.id !== excludeId) : foods.slice();
  if (!pool.length) return foods[0];
  const daily = pool.filter(food => foodGroupOf(food) === "daily");
  const other = pool.filter(food => foodGroupOf(food) === "other");
  const preferDaily = Math.random() < 0.7;
  let target = preferDaily ? daily : other;
  if (!target.length) target = preferDaily ? other : daily;
  if (!target.length) target = pool;
  const fresh = target.filter(food => !randomHistory.includes(food.id));
  const candidates = fresh.length ? fresh : target;
  const food = candidates[Math.floor(Math.random() * candidates.length)];
  randomHistory.push(food.id);
  if (randomHistory.length > 6) randomHistory.shift();
  return food;
}

async function pickRandomFood(excludeId) {
  try {
    const payload = await window.eatTransport.request("/api/menu/random", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ user_id: "u_demo", mode: "work", seed: Math.floor(Math.random() * 100000) })
    });
    const remote = payload && payload.ok ? payload.data?.food : null;
    if (remote && remote.id !== excludeId) {
      const local = foods.find(food => food.id === remote.id);
      const merged = decorateFood({ ...(local || {}), ...remote });
      randomHistory.push(merged.id);
      if (randomHistory.length > 6) randomHistory.shift();
      return merged;
    }
  } catch {
    // 后端不可用时走本地 70/30
  }
  return pickLocalRandom(excludeId);
}

function renderRandomPicker(food) {
  randomFoodId = food.id;
  const match = getFoodMatch(food);
  const pickerCard = document.querySelector(".random-picker-card");
  pickerCard.style.setProperty("--match", "0");
  requestAnimationFrame(() => pickerCard.style.setProperty("--match", match));
  document.getElementById("randomImage").src = food.image;
  document.getElementById("randomImage").alt = food.name;
  document.getElementById("randomCategory").textContent = `随机一道 · ${food.category}`;
  document.getElementById("randomName").textContent = food.name;
  document.getElementById("randomDesc").textContent = food.description;
  document.getElementById("randomCalories").textContent = `${food.calories} 千卡`;
  document.getElementById("randomProtein").textContent = `蛋白 ${food.protein} g`;
  const priceNode = document.getElementById("randomPrice");
  if (priceNode) priceNode.textContent = priceLabelOf(food);
  const groupNode = document.getElementById("randomGroup");
  if (groupNode) {
    groupNode.textContent = groupLabelOf(food);
    groupNode.className = `food-group ${foodGroupOf(food)}`;
  }
  document.getElementById("randomMatch").textContent = `匹配 ${match}%`;
}

async function openRandomPicker(excludeId) {
  randomReturnView = activeView;
  const picker = document.getElementById("randomPicker");
  document.getElementById("randomCategory").textContent = "随机一道 · 正在挑选";
  picker.classList.add("open");
  picker.setAttribute("aria-hidden", "false");
  const food = await pickRandomFood(excludeId);
  if (food) renderRandomPicker(food);
}

function closeRandomPicker() {
  const picker = document.getElementById("randomPicker");
  picker.classList.remove("open");
  picker.setAttribute("aria-hidden", "true");
}

function rerollRandom() {
  openRandomPicker(randomFoodId);
}

function confirmRandom() {
  const id = randomFoodId;
  const returnView = randomReturnView;
  closeRandomPicker();
  if (!id) return;
  recordFood(id, { stay: true });
  switchView(returnView);
}

const peerMedia = new Map();
const mediaInFlight = new Set();

function mediaKeyFor(item) {
  return String(item.id || (item.current ? currentUserKey : "") || "");
}

function peerMediaFor(item) {
  const key = mediaKeyFor(item);
  return key ? peerMedia.get(key) : null;
}

function applyOwnMedia(data) {
  if (!currentUserKey || !data || data.ok === false) return;
  peerMedia.set(currentUserKey, {
    cover: String(data.cover || ""),
    avatar: String(data.avatar || ""),
    cover_ver: Number(data.cover_ver || 0),
    avatar_ver: Number(data.avatar_ver || 0)
  });
}

async function syncPeerMedia(items) {
  if (!window.eatTransport || !currentUserKey || !Array.isArray(items)) return;
  const targets = [];
  for (const item of items) {
    const key = mediaKeyFor(item);
    if (!key || mediaInFlight.has(key)) continue;
    const cached = peerMedia.get(key);
    const hasVersion = Boolean(item.cover_ver || item.avatar_ver);
    if (cached && !hasVersion) continue;
    const cachedVersion = cached ? `${cached.cover_ver || 0}:${cached.avatar_ver || 0}` : "";
    const expectedVersion = `${item.cover_ver || 0}:${item.avatar_ver || 0}`;
    if (cachedVersion === expectedVersion) continue;
    targets.push(key);
  }
  if (!targets.length) return;
  const results = await Promise.all(targets.map(async key => {
    mediaInFlight.add(key);
    try {
      const res = await window.eatTransport.request(`/api/profile/cover?user_id=${encodeURIComponent(key)}`);
      if (res && res.ok !== false) {
        peerMedia.set(key, {
          cover: String(res.cover || ""),
          avatar: String(res.avatar || ""),
          cover_ver: Number(res.cover_ver || 0),
          avatar_ver: Number(res.avatar_ver || 0)
        });
        return true;
      }
      return false;
    } catch {
      return false;
    } finally {
      mediaInFlight.delete(key);
    }
  }));
  if (results.some(Boolean)) {
    renderLeaderboard();
    renderFriends();
    renderFriendRanking();
  }
}

function applyChampionCover(champion) {
  const coverImg = document.querySelector(".champion-cover img");
  if (!coverImg || !champion) return;
  const media = peerMediaFor(champion);
  const ownAvatar = localStorage.getItem("eat-user-avatar") || "";
  const src = (media && (media.cover || media.avatar)) || champion.cover ||
    (champion.current ? championCoverSrc : "") ||
    champion.avatar || (champion.current ? ownAvatar : "");
  if (src) {
    coverImg.src = src;
    coverImg.alt = `${champion.name || "好友"}的排行榜封面`;
  } else {
    coverImg.src = defaultChampionCover;
    coverImg.alt = "排行榜第一名的好友封面";
  }
}

const MEDIA_MAX_BYTES = 32 * 1024;

function readCoverFile(file, type = "image/jpeg", maxSide = 480, limit = MEDIA_MAX_BYTES) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => {
      const image = new Image();
      image.onload = () => {
        let side = Math.min(maxSide, Math.max(image.width, image.height));
        let quality = 0.82;
        let dataUrl = "";
        for (let attempt = 0; attempt < 12; attempt += 1) {
          const scale = Math.min(1, side / Math.max(image.width, image.height));
          const canvas = document.createElement("canvas");
          canvas.width = Math.max(1, Math.round(image.width * scale));
          canvas.height = Math.max(1, Math.round(image.height * scale));
          canvas.getContext("2d").drawImage(image, 0, 0, canvas.width, canvas.height);
          dataUrl = canvas.toDataURL(type, quality);
          if (dataUrl.length <= limit) break;
          side = Math.round(side * 0.75);
          quality = Math.max(0.55, quality - 0.08);
          if (side <= 48) break;
        }
        if (dataUrl.length > limit) {
          reject(new Error("image_too_large"));
          return;
        }
        resolve(dataUrl);
      };
      image.onerror = reject;
      image.src = reader.result;
    };
    reader.onerror = reject;
    reader.readAsDataURL(file);
  });
}

function syncCoverPreview(src) {
  const wrap = document.getElementById("coverPreviewWrap");
  const preview = document.getElementById("coverPreview");
  if (src) {
    preview.src = src;
    preview.style.display = "block";
    wrap.classList.add("has-image");
  } else {
    preview.removeAttribute("src");
    preview.style.display = "none";
    wrap.classList.remove("has-image");
  }
}

function openCoverDialog() {
  coverDraftSrc = championCoverSrc;
  syncCoverPreview(coverDraftSrc);
  document.getElementById("coverFile").value = "";
  document.getElementById("coverDialog").showModal();
}

function saveCover() {
  if (!coverDraftSrc) {
    showToast("请先选择一张封面图片");
    return;
  }
  const draft = coverDraftSrc;
  championCoverSrc = draft;
  saveStored(storageKeys.cover, { src: draft });
  if (window.eatTransport && currentUserKey) {
    window.eatTransport.request("/api/profile/cover", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ user_id: currentUserKey, cover: draft })
    }).then(res => {
      if (!res || res.ok === false) {
        showToast(res && res.message === "cover_too_large" ? "图片太大，请换一张" : "封面已存到本地，服务端同步失败");
        return;
      }
      applyOwnMedia(res);
      if (res.cover) {
        championCoverSrc = res.cover;
        saveStored(storageKeys.cover, { src: res.cover });
      }
      renderLeaderboard();
    }).catch(() => {});
  }
  renderLeaderboard();
  document.getElementById("coverDialog").close();
  showToast("冠军封面已保存到服务端");
}

function resetCover() {
  coverDraftSrc = "";
  championCoverSrc = "";
  syncCoverPreview("");
  try { localStorage.removeItem(storageKeys.cover); } catch {}
  if (window.eatTransport && currentUserKey) {
    window.eatTransport.request("/api/profile/cover", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ user_id: currentUserKey, cover: "" })
    }).then(res => {
      if (res && res.ok !== false) applyOwnMedia(res);
      renderLeaderboard();
    }).catch(() => {});
  }
  renderLeaderboard();
  showToast("已恢复默认冠军封面");
}

function mealTypeFromTime(hour) {
  if (hour >= 5 && hour < 10) return "早餐";
  if (hour >= 10 && hour < 14) return "午餐";
  if (hour >= 14 && hour < 17) return "下午茶";
  if (hour >= 17 && hour < 22) return "晚餐";
  return "加餐";
}

function recordFood(foodId, options = {}) {
  const food = foods.find(item => item.id === foodId);
  if (!food) return;
  const type = options.type || mealTypeFromTime(new Date().getHours());
  const meal = recordMeal(food.name, { protein: food.protein, fiber: food.fiber, veggies: food.veggies, water: food.water, penalty: 0 }, type);
  if (!options.stay) {
    switchView("today");
    appendUserMessage(`吃了${food.name}`);
    appendAgentReply(`已从食物菜单加入${meal.type}记录。${getHealthAnalysis().adjustment}`);
    elements.conversation.scrollTo({ top: elements.conversation.scrollHeight, behavior: "smooth" });
  }
  showToast(`已记录「${food.name}」，健康报告已更新`);
}

function openUserInfo() {
  document.getElementById("nameInput").value = profile.name;
  document.getElementById("heightInput").value = profile.height;
  document.getElementById("weightInput").value = profile.weight;
  document.getElementById("ageInput").value = profile.age;
  const spendMin = document.getElementById("spendMinInput");
  const spendMax = document.getElementById("spendMaxInput");
  if (spendMin) spendMin.value = profile.spend_min ?? 20;
  if (spendMax) spendMax.value = profile.spend_max ?? 35;
  updatePersonalPreview();
  elements.userInfoDialog.showModal();
}

function updatePersonalPreview() {
  const name = document.getElementById("nameInput").value.trim() || "我";
  const height = Number(document.getElementById("heightInput").value);
  const weight = Number(document.getElementById("weightInput").value);
  const bmi = height >= 120 && weight > 0 ? (weight / ((height / 100) ** 2)).toFixed(1) : "—";
  document.getElementById("personalAvatarPreview").textContent = name.slice(0, 1);
  document.getElementById("personalPreviewName").textContent = name;
  document.getElementById("bmiValue").textContent = bmi;
}

async function saveSpendRange() {
  const min = Number(document.getElementById("spendMinInput").value);
  const max = Number(document.getElementById("spendMaxInput").value);
  if (!Number.isFinite(min) || !Number.isFinite(max) || min < 0 || max < min) {
    showToast("请填写有效区间，最高不小于最低");
    return;
  }
  const stored = JSON.parse(localStorage.getItem("eat-health-profile-v2") || "{}");
  stored.spend_min = min;
  stored.spend_max = max;
  localStorage.setItem("eat-health-profile-v2", JSON.stringify(stored));
  profile.spend_min = min;
  profile.spend_max = max;
  try {
    const payload = await window.eatTransport.request("/api/profile/spend", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ user_id: "u_demo", mode: "work", spend_min: min, spend_max: max })
    });
    if (!payload || payload.ok === false) throw new Error("failed");
    showToast("每餐消费区间已保存");
  } catch {
    showToast("已在本地保存，连接 Agent 后生效");
  }
  loadAgentMenu();
}

function syncProfileFromAgent(patch) {
  if (!patch || typeof patch !== "object") return;
  profile = { ...profile, ...patch };
  if (elements.userInfoDialog?.open) {
    const fields = { spendMinInput: "spend_min", spendMaxInput: "spend_max", heightInput: "height", weightInput: "weight", ageInput: "age" };
    Object.entries(fields).forEach(([id, key]) => {
      const node = document.getElementById(id);
      if (node && profile[key] != null) node.value = profile[key];
    });
    updatePersonalPreview();
  }
  loadAgentMenu();
}

async function syncProfileToServer(fields) {
  if (!currentUserKey || !window.eatTransport) return null;
  try {
    const result = await window.eatTransport.request("/api/profile", {
      method: "PATCH",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ user_id: currentUserKey, ...fields })
    });
    if (result && result.profile) {
      applyRemoteProfile(result.profile);
      syncProfileForm();
      renderReport();
    }
    return result;
  } catch {
    return null;
  }
}

function savePersonalInfo(event) {
  event.preventDefault();
  const form = document.getElementById("personalForm");
  if (!form.reportValidity()) return;
  profile = {
    ...profile,
    name: document.getElementById("nameInput").value.trim() || "我",
    height: Number(document.getElementById("heightInput").value),
    weight: Number(document.getElementById("weightInput").value),
    age: Number(document.getElementById("ageInput").value)
  };
  draftProfile = { ...profile };
  saveStored(storageKeys.profile, profile);
  applyProfileName();
  syncProfileToServer({
    name: profile.name,
    height: profile.height,
    weight: profile.weight,
    age: profile.age
  });
  renderReport();
  elements.userInfoDialog.close();
  showToast("个人信息已保存，健康档案已同步");
}

function switchView(view) {
  if (!viewTitles[view]) return;
  if (view === "profile" && activeView !== "profile") previousView = activeView;
  closeExpandedMenu();

  document.querySelectorAll(".view").forEach(section => section.classList.toggle("active", section.id === `view-${view}`));
  document.querySelectorAll(".nav-item").forEach(button => {
    const selected = button.dataset.view === view;
    button.classList.toggle("active", selected);
    if (selected) button.setAttribute("aria-current", "page");
    else button.removeAttribute("aria-current");
  });
  elements.pageTitle.textContent = viewTitles[view];
  if (view === "history") renderHistory();
  if (view === "profile") syncProfileForm();
  if (view === "report") renderReport();
  if (view === "friends") {
    renderFriends();
    renderFriendRanking();
    refreshSocial();
    if (!socialTimer) socialTimer = setInterval(refreshSocial, 30000);
  } else if (socialTimer) {
    clearInterval(socialTimer);
    socialTimer = null;
  }
  if (view === "shop") renderShop();
  activeView = view;
}

function userAvatarMarkup() {
  const src = localStorage.getItem("eat-user-avatar") || "";
  return src ? `<div class="user-avatar" aria-hidden="true"><img src="${escapeHtml(src)}" alt=""></div>` : `<div class="user-avatar" aria-hidden="true">我</div>`;
}

function appendUserMessage(text) {
  const article = document.createElement("article");
  article.className = "message user-message";
  const bubble = document.createElement("div");
  bubble.className = "message-bubble";
  const paragraph = document.createElement("p");
  paragraph.textContent = text;
  bubble.appendChild(paragraph);
  article.appendChild(bubble);
  article.insertAdjacentHTML("beforeend", userAvatarMarkup());
  elements.conversation.insertBefore(article, document.getElementById("quickPrompts"));
}

function appendTyping() {
  const article = document.createElement("article");
  article.className = "message agent-message typing-message";
  article.id = "typingMessage";
  article.innerHTML = `<div class="agent-avatar">${icon("utensils")}</div><div class="message-content"><div class="message-meta"><strong>饭饭</strong><span>正在更新饮食分析</span></div><div class="message-bubble typing-bubble"><i></i><i></i><i></i></div></div>`;
  elements.conversation.insertBefore(article, document.getElementById("quickPrompts"));
}

function appendAgentReply(text) {
  const article = document.createElement("article");
  article.className = "message agent-message typing-message";
  const avatar = document.createElement("div");
  avatar.className = "agent-avatar";
  avatar.innerHTML = icon("utensils");
  const content = document.createElement("div");
  content.className = "message-content";
  const meta = document.createElement("div");
  meta.className = "message-meta";
  meta.innerHTML = "<strong>饭饭</strong><span>刚刚</span>";
  const bubble = document.createElement("div");
  bubble.className = "message-bubble";
  const paragraph = document.createElement("p");
  paragraph.textContent = text;
  bubble.appendChild(paragraph);
  content.append(meta, bubble);
  article.append(avatar, content);
  elements.conversation.insertBefore(article, document.getElementById("quickPrompts"));
}

function appendPhotoMessage(src) {
  const article = document.createElement("article");
  article.className = "message user-message";
  article.innerHTML = `<div class="message-bubble photo-bubble"><img src="${src}" alt="上传的饭菜照片"></div>${userAvatarMarkup()}`;
  elements.conversation.insertBefore(article, document.getElementById("quickPrompts"));
  elements.conversation.scrollTo({ top: elements.conversation.scrollHeight, behavior: "smooth" });
}

function analyzeMealText(text) {
  const result = { protein: 8, fiber: 2, veggies: 80, water: 200, penalty: 0 };
  if (/鸡|蛋|鱼|虾|牛|豆腐|豆浆|牛奶|酸奶/.test(text)) result.protein += 16;
  if (/沙拉|青菜|蔬菜|西兰花|番茄|黄瓜|菠菜|菜心|油麦菜/.test(text)) { result.veggies += 180; result.fiber += 5; }
  if (/燕麦|糙米|杂粮|全麦|玉米|红薯/.test(text)) { result.fiber += 4; result.protein += 4; }
  if (/苹果|香蕉|橙|蓝莓|水果|猕猴桃/.test(text)) result.fiber += 3;
  if (/汤|牛奶|豆浆|水|茶|咖啡/.test(text)) result.water += 250;
  if (/奶茶|可乐|汽水|甜饮/.test(text)) { result.penalty += 4; result.water = 100; }
  if (/炸|薯条|炸鸡|烧烤|肥肉/.test(text)) result.penalty += 3;
  return result;
}

function determineMealType(text) {
  const explicit = ["早餐", "午餐", "下午茶", "晚餐", "加餐"].find(type => text.includes(type)) || (/下午茶|点心|零食|奶茶|咖啡/.test(text) ? "下午茶" : null);
  if (explicit) return explicit;
  return mealOrder.find(type => !meals.some(meal => meal.type === type)) || "加餐";
}

function recordMeal(text, nutritionOverride = {}, forcedType = "") {
  const type = forcedType || determineMealType(text);
  const now = new Date();
  const time = `${String(now.getHours()).padStart(2, "0")}:${String(now.getMinutes()).padStart(2, "0")}`;
  const meal = { type, time, text, ...analyzeMealText(text), ...nutritionOverride };
  meals.push(meal);
  saveStored(storageKeys.meals, meals);
  renderMeals();
  renderReport();
  rewardInteraction();
  return meal;
}

function sendPrompt(rawText) {
  if (window.eatAgent && window.eatAgent.intercept(rawText)) {
    elements.chatInput.value = "";
    elements.chatInput.style.height = "auto";
    return;
  }
  const text = rawText.trim();
  if (!text) {
    elements.chatInput.focus();
    return;
  }
  clearTimeout(replyTimer);
  document.getElementById("typingMessage")?.remove();
  appendUserMessage(text);
  appendTyping();
  elements.chatInput.value = "";
  elements.chatInput.style.height = "auto";
  elements.conversation.scrollTo({ top: elements.conversation.scrollHeight, behavior: "smooth" });
  replyTimer = setTimeout(() => {
    document.getElementById("typingMessage")?.remove();
    if (/报告|分析|总结/.test(text) && !/吃|喝/.test(text)) {
      rewardInteraction();
      const analysis = getHealthAnalysis();
      appendAgentReply(`今日综合评分是 ${analysis.score} 分，${analysis.status}。${analysis.summary}`);
    } else {
      const meal = recordMeal(text);
      const analysis = getHealthAnalysis();
      const specialReply = /奶茶|甜饮|可乐/.test(text) ? "已记录这杯饮品。后续正常吃饭、分次补水即可，不需要补偿性节食。" : `已加入${meal.type}记录。${analysis.adjustment}`;
      appendAgentReply(specialReply);
    }
    elements.conversation.scrollTo({ top: elements.conversation.scrollHeight, behavior: "smooth" });
  }, 720);
}

function syncProfileForm() {
  draftProfile = { ...profile };
  document.querySelectorAll("[data-option-group]").forEach(group => {
    const key = group.dataset.optionGroup;
    group.querySelectorAll("button").forEach(button => button.classList.toggle("selected", button.dataset.value === String(draftProfile[key])));
  });
  elements.waterRange.value = profile.waterTarget;
  elements.waterOutput.textContent = `${profile.waterTarget} ml`;
  elements.sleepRange.value = profile.sleep;
  elements.sleepOutput.textContent = `${profile.sleep} 小时`;
  elements.healthNote.value = profile.note;
}

function setSidebarCollapsed(collapsed) {
  elements.appShell.classList.toggle("left-collapsed", collapsed);
  const label = collapsed ? "展开左侧栏" : "收起左侧栏";
  elements.sidebarToggle.dataset.tooltip = label;
  elements.sidebarToggle.setAttribute("aria-label", label);

}

function setDetailsCollapsed(collapsed) {
  elements.workspace.classList.toggle("right-collapsed", collapsed);
  const label = collapsed ? "展开右侧栏" : "收起右侧栏";
  elements.detailsCollapseButton.dataset.tooltip = label;
  elements.detailsCollapseButton.setAttribute("aria-label", label);
}

function openExpandedMenu() {
  const query = document.getElementById("foodSearchInput").value.trim();
  if (query) renderMasonryResults(searchFoods(query), query);
  else renderMasonryFeed();
  elements.expandedMenuList.scrollTop = 0;
  elements.expandedMenu.classList.remove("open");
  void elements.expandedMenu.offsetWidth;
  elements.expandedMenu.classList.add("open");
  elements.expandedMenu.setAttribute("aria-hidden", "false");
  elements.expandedMenuList.scrollTop = 0;
  setTimeout(updateFoodFeedProgress, 60);
}

function closeExpandedMenu() {
  elements.expandedMenu.classList.remove("open");
  elements.expandedMenu.setAttribute("aria-hidden", "true");
}

function positionDropZone() {
  const rect = elements.mainContent.getBoundingClientRect();
  Object.assign(elements.menuDropZone.style, { left: `${rect.left}px`, top: `${rect.top}px`, width: `${rect.width}px`, height: `${rect.height}px` });
}

function pointerOverCenter(event) {
  const rect = elements.mainContent.getBoundingClientRect();
  return event.clientX >= rect.left && event.clientX <= rect.right && event.clientY >= rect.top && event.clientY <= rect.bottom;
}

function beginMenuDrag(event) {
  const rect = elements.foodModule.getBoundingClientRect();
  const ghost = document.createElement("div");
  ghost.className = "menu-drag-ghost";
  ghost.innerHTML = `${icon("utensils")}<strong>食物菜单</strong>${icon("grip")}`;
  document.body.appendChild(ghost);
  menuDrag.ghost = ghost;
  menuDrag.swipe = false;
  menuDrag.offsetX = menuDrag.startX - rect.left;
  menuDrag.offsetY = menuDrag.startY - rect.top;
  try { elements.foodModule.setPointerCapture(event.pointerId); } catch {}
  elements.foodModule.classList.add("is-drag-source");
  document.body.classList.add("is-dragging-menu");
  positionDropZone();
  elements.menuDropZone.classList.add("visible");
  moveMenuDrag(event);
}

function moveMenuDrag(event) {
  if (!menuDrag?.ghost) return;
  const x = event.clientX - menuDrag.offsetX;
  const y = event.clientY - menuDrag.offsetY;
  menuDrag.ghost.style.transform = `translate3d(${x}px, ${y}px, 0)`;
  menuDrag.overCenter = pointerOverCenter(event);
  elements.menuDropZone.classList.toggle("is-over", menuDrag.overCenter);
}

function finishMenuDrag(event) {
  if (!menuDrag) return;
  const drag = menuDrag;
  const wasDragging = Boolean(drag.ghost);
  const shouldOpen = wasDragging && pointerOverCenter(event) && event.type !== "pointercancel";
  const shouldTurnPage = !wasDragging && drag.swipe && event.type !== "pointercancel" && Math.abs(event.clientX - drag.startX) > 36;
  if (drag.ghost) {
    drag.ghost.style.opacity = "0";
    drag.ghost.style.transition = "opacity 160ms var(--ease), transform 200ms var(--ease)";
    const ghost = drag.ghost;
    setTimeout(() => ghost.remove(), 180);
  }
  menuDrag = null;
  elements.foodModule.classList.remove("is-drag-source");
  document.body.classList.remove("is-dragging-menu");
  elements.menuDropZone.classList.remove("visible", "is-over");
  if (wasDragging || shouldTurnPage) {
    suppressMenuClick = true;
    setTimeout(() => { suppressMenuClick = false; }, 240);
  }
  if (shouldTurnPage) changeFoodPage(event.clientX < drag.startX ? 1 : -1);
  if (shouldOpen) setTimeout(openExpandedMenu, 100);
}

function clearLocalData() {
  profile = { ...defaultProfile };
  meals = structuredClone(defaultMeals);
  history = structuredClone(seedHistory);
  pointsState = { ...defaultPointsState, week: getWeekKey() };
  guessIndex = 0;
  Object.values(storageKeys).forEach(key => {
    try { localStorage.removeItem(key); } catch { return; }
  });
  elements.motionToggle.checked = true;
  setMotion(true);
  renderMeals();
  renderReport();
  renderHistory();
  syncProfileForm();
  applyTheme(pointsState.selected);
  if (document.getElementById("view-shop").classList.contains("active")) renderShop();
  elements.settingsDialog.close();
  showToast("本地饮食记录与健康档案已恢复为演示数据");
}

function setMotion(enabled) {
  document.body.classList.toggle("motion-off", !enabled);
  elements.motionToggle.checked = enabled;
  saveStored(storageKeys.motion, enabled);
  if (enabled) startConstellation();
  else stopConstellation();
}

function initDates() {
  const now = new Date();
  document.getElementById("todayDateLabel").textContent = `${now.getFullYear()}年${now.getMonth() + 1}月${now.getDate()}日 · ${weekdayLabels[now.getDay()]}`;
  document.getElementById("dateNumber").textContent = now.getDate();
  document.getElementById("dateMonth").textContent = `${now.getMonth() + 1}月`;
  document.getElementById("reportDateHeading").textContent = `${now.getMonth() + 1} 月 ${now.getDate()} 日 · 综合饮食、活动量与健康目标生成`;
}

function downloadReport() {
  const analysis = getHealthAnalysis();
  const content = [`泛饭 · 今日健康报告`, `日期：${todayKey()}`, `综合评分：${analysis.score} / 100（${analysis.status}）`, `已记录：${meals.length} 餐`, "", "饮食记录", ...meals.map(meal => `${meal.time} ${meal.type}：${meal.text}`), "", "营养进度", `蛋白质：${analysis.nutrition.protein} / ${analysis.targets.protein} g`, `膳食纤维：${analysis.nutrition.fiber} / ${analysis.targets.fiber} g`, `蔬菜：${analysis.nutrition.veggies} / ${analysis.targets.veggies} g`, `饮水：${analysis.nutrition.water} / ${analysis.targets.water} ml`, "", "综合分析", analysis.summary, "", "本报告仅用于日常饮食记录和生活方式参考，不用于诊断或治疗。"].join("\n");
  const url = URL.createObjectURL(new Blob([content], { type: "text/plain;charset=utf-8" }));
  const link = document.createElement("a");
  link.href = url;
  link.download = `今日健康报告-${todayKey()}.txt`;
  link.click();
  URL.revokeObjectURL(url);
  showToast("健康报告摘要已生成");
}

const canvas = document.getElementById("constellationCanvas");
const context = canvas.getContext("2d");
let points = [];
let canvasWidth = 0;
let canvasHeight = 0;
let animationFrame = 0;
let lastFrame = 0;
let animationTime = 0;

function resizeConstellation() {
  const ratio = Math.min(window.devicePixelRatio || 1, 2);
  canvasWidth = window.innerWidth;
  canvasHeight = window.innerHeight;
  canvas.width = Math.round(canvasWidth * ratio);
  canvas.height = Math.round(canvasHeight * ratio);
  canvas.style.width = `${canvasWidth}px`;
  canvas.style.height = `${canvasHeight}px`;
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  points = [];
  const spacing = canvasWidth < 700 ? 112 : 144;
  let row = 0;
  for (let y = -spacing; y < canvasHeight + spacing; y += spacing * 0.86) {
    let column = 0;
    for (let x = -spacing; x < canvasWidth + spacing; x += spacing) {
      points.push({ x: x + ((row * 37 + column * 19) % 43) - 21 + (row % 2) * spacing * 0.5, y: y + ((row * 23 + column * 31) % 37) - 18, phase: (row * 0.71 + column * 0.43) % 6.28, size: (row + column) % 5 === 0 ? 1.5 : 1 });
      column += 1;
    }
    row += 1;
  }
  drawConstellation();
}

function drawConstellation() {
  context.clearRect(0, 0, canvasWidth, canvasHeight);
  const positioned = points.map(point => ({ ...point, px: point.x + Math.sin(animationTime * 0.00012 + point.phase) * 5, py: point.y + Math.cos(animationTime * 0.0001 + point.phase) * 4 }));
  context.lineWidth = 0.7;
  for (let first = 0; first < positioned.length; first += 1) {
    for (let second = first + 1; second < positioned.length; second += 1) {
      const a = positioned[first];
      const b = positioned[second];
      const distance = Math.hypot(a.px - b.px, a.py - b.py);
      if (distance < 154) {
        context.strokeStyle = `rgba(63, 83, 73, ${(1 - distance / 154) * 0.12})`;
        context.beginPath();
        context.moveTo(a.px, a.py);
        context.lineTo(b.px, b.py);
        context.stroke();
      }
    }
  }
  positioned.forEach(point => {
    context.fillStyle = point.size > 1 ? "rgba(65, 88, 76, 0.23)" : "rgba(65, 88, 76, 0.16)";
    context.beginPath();
    context.arc(point.px, point.py, point.size, 0, Math.PI * 2);
    context.fill();
  });
}

function animateConstellation(timestamp) {
  if (document.body.classList.contains("motion-off") || document.hidden) return;
  if (timestamp - lastFrame >= 48) {
    animationTime = timestamp;
    drawConstellation();
    lastFrame = timestamp;
  }
  animationFrame = requestAnimationFrame(animateConstellation);
}

function startConstellation() {
  cancelAnimationFrame(animationFrame);
  if (window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
    drawConstellation();
    return;
  }
  animationFrame = requestAnimationFrame(animateConstellation);
}

function stopConstellation() {
  cancelAnimationFrame(animationFrame);
  drawConstellation();
}

document.querySelectorAll("[data-view]").forEach(button => button.addEventListener("click", () => switchView(button.dataset.view)));
document.querySelectorAll("[data-prompt]").forEach(button => button.addEventListener("click", () => {
  const label = button.textContent.trim();
  if (label.startsWith("记录")) {
    const period = label.replace("记录", "").trim();
    elements.chatInput.value = `${period}为：`;
    elements.chatInput.focus();
    elements.chatInput.setSelectionRange(elements.chatInput.value.length, elements.chatInput.value.length);
    elements.chatInput.dispatchEvent(new Event("input", { bubbles: true }));
    return;
  }
  sendPrompt(button.dataset.prompt);
}));
document.querySelectorAll("[data-option-group]").forEach(group => {
  group.addEventListener("click", event => {
    const button = event.target.closest("button[data-value]");
    if (!button) return;
    draftProfile[group.dataset.optionGroup] = button.dataset.value;
    group.querySelectorAll("button").forEach(option => option.classList.toggle("selected", option === button));
  });
});

document.getElementById("photoButton").addEventListener("click", () => document.getElementById("photoInput").click());
document.getElementById("photoInput").addEventListener("change", event => {
  const file = event.target.files?.[0];
  if (!file) return;
  readCoverFile(file).then(src => {
    appendPhotoMessage(src);
    showToast("照片已上传，视觉识别将在接入视觉模型后启用");
  }).catch(() => showToast("图片读取失败，请换一张试试"));
  event.target.value = "";
});

document.getElementById("chatForm").addEventListener("submit", event => {
  event.preventDefault();
  sendPrompt(elements.chatInput.value);
});

elements.chatInput.addEventListener("keydown", event => {
  if (event.key === "Enter" && !event.shiftKey && !event.isComposing) {
    event.preventDefault();
    sendPrompt(elements.chatInput.value);
  }
});

elements.chatInput.addEventListener("input", () => {
  elements.chatInput.style.height = "auto";
  elements.chatInput.style.height = `${Math.min(elements.chatInput.scrollHeight, 220)}px`;
});

bindInferenceCard();
document.getElementById("profileShortcut").addEventListener("click", () => switchView("profile"));
document.getElementById("downloadReport").addEventListener("click", downloadReport);
document.getElementById("dailyCheckin").addEventListener("click", () => {
  ensurePointsState();
  const today = todayKey();
  if (pointsState.checkins[today]) return;
  pointsState.checkins[today] = today;
  saveStored(storageKeys.points, pointsState);
  renderShop();
  showToast("签到成功，本周积分 +10");
});
document.getElementById("themeGrid").addEventListener("click", handleThemeAction);
document.addEventListener("click", event => {
  const picker = document.getElementById("mealTypePicker");
  if (picker && !event.target.closest(".inference-actions")) picker.hidden = true;
});
elements.sidebarToggle.addEventListener("click", () => setSidebarCollapsed(!elements.appShell.classList.contains("left-collapsed")));
elements.detailsCollapseButton.addEventListener("click", () => setDetailsCollapsed(!elements.workspace.classList.contains("right-collapsed")));
document.getElementById("detailsRailToggle").addEventListener("click", () => setDetailsCollapsed(false));
document.getElementById("closeExpandedMenu").addEventListener("click", closeExpandedMenu);
const handleFoodSelection = event => {
  if (suppressMenuClick) {
    event.preventDefault();
    event.stopPropagation();
    return;
  }
  const button = event.target.closest("[data-food-id]");
  if (button) recordFood(button.dataset.foodId);
};
document.getElementById("sidebarFoodList").addEventListener("click", handleFoodSelection);
elements.expandedMenuList.addEventListener("click", handleFoodSelection);
document.getElementById("foodPrev").addEventListener("click", () => changeFoodPage(-1));
document.getElementById("foodNext").addEventListener("click", () => changeFoodPage(1));
document.getElementById("foodPageDots").addEventListener("click", event => {
  const button = event.target.closest("[data-food-page]");
  if (!button) return;
  const nextPage = Number(button.dataset.foodPage);
  const direction = nextPage > foodPage ? 1 : -1;
  foodPage = nextPage;
  renderSidebarFoodPage(direction);
});
elements.sidebarFoodViewport.addEventListener("keydown", event => {
  if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return;
  event.preventDefault();
  changeFoodPage(event.key === "ArrowRight" ? 1 : -1);
});
elements.expandedMenuList.addEventListener("scroll", updateFoodFeedProgress);

document.getElementById("userAvatarButton").addEventListener("click", openUserInfo);
document.getElementById("editBodyInfo").addEventListener("click", openUserInfo);
document.getElementById("closeUserInfo").addEventListener("click", () => elements.userInfoDialog.close());
document.getElementById("cancelPersonal").addEventListener("click", () => elements.userInfoDialog.close());
document.getElementById("personalForm").addEventListener("submit", savePersonalInfo);
["nameInput", "heightInput", "weightInput"].forEach(id => document.getElementById(id).addEventListener("input", updatePersonalPreview));

const userAvatarKey = "eat-user-avatar";
function applyUserAvatar(src) {
  const value = src !== undefined ? src : (localStorage.getItem(userAvatarKey) || "");
  const initial = document.getElementById("avatarInitial");
  const image = document.getElementById("avatarImage");
  if (initial && image) {
    if (value) {
      image.src = value;
      image.hidden = false;
      initial.classList.add("has-photo");
    } else {
      image.removeAttribute("src");
      image.hidden = true;
      initial.classList.remove("has-photo");
    }
  }
  const preview = document.getElementById("personalAvatarPreview");
  if (preview) {
    if (value) {
      preview.classList.add("has-photo");
      preview.style.backgroundImage = `url("${value}")`;
    } else {
      preview.classList.remove("has-photo");
      preview.style.backgroundImage = "";
    }
  }
  document.querySelectorAll("#conversation .user-avatar").forEach(node => {
    node.innerHTML = value ? `<img src="${value}" alt="">` : "我";
  });
  renderLeaderboard();
}
document.getElementById("avatarUploadButton").addEventListener("click", () => document.getElementById("avatarUploadInput").click());
document.getElementById("avatarUploadInput").addEventListener("change", event => {
  const file = event.target.files?.[0];
  if (!file) return;
  readCoverFile(file, "image/jpeg", 160, 24 * 1024).then(async src => {
    if (window.eatTransport && currentUserKey) {
      try {
        const res = await window.eatTransport.request("/api/profile/cover", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ user_id: currentUserKey, avatar: src })
        });
        if (res && res.ok !== false) {
          applyOwnMedia(res);
          const saved = res.avatar || src;
          localStorage.setItem(userAvatarKey, saved);
          applyUserAvatar(saved);
          renderLeaderboard();
          showToast("头像已同步到服务端");
          return;
        }
        if (res && res.message === "avatar_too_large") {
          showToast("图片太大，请换一张");
          return;
        }
      } catch {}
    }
    localStorage.setItem(userAvatarKey, src);
    applyUserAvatar(src);
    renderLeaderboard();
    showToast("头像已保存到本地");
  }).catch(() => showToast("头像读取失败，请换一张试试"));
  event.target.value = "";
});
applyUserAvatar();
document.getElementById("spendSaveButton").addEventListener("click", saveSpendRange);
elements.userInfoDialog.addEventListener("click", event => {
  if (event.target === elements.userInfoDialog) {
    const rect = elements.userInfoDialog.getBoundingClientRect();
    if (event.clientX < rect.left || event.clientX > rect.right || event.clientY < rect.top || event.clientY > rect.bottom) elements.userInfoDialog.close();
  }
});

elements.foodModule.addEventListener("pointerdown", event => {
  if (event.button !== 0 || event.target.closest(".food-pagination") || event.target.closest(".food-search")) return;
  menuDrag = { startX: event.clientX, startY: event.clientY, ghost: null, swipe: false, startedOnFood: Boolean(event.target.closest("#sidebarFoodViewport")) };
});

elements.foodModule.addEventListener("pointermove", event => {
  if (!menuDrag) return;
  const dx = event.clientX - menuDrag.startX;
  const dy = event.clientY - menuDrag.startY;
  if (!menuDrag.ghost) {
    if (pointerOverCenter(event) || (!menuDrag.startedOnFood && Math.hypot(dx, dy) > 9) || (menuDrag.startedOnFood && !menuDrag.swipe && Math.abs(dy) > 12 && Math.abs(dy) > Math.abs(dx))) {
      beginMenuDrag(event);
    } else if (menuDrag.startedOnFood && !menuDrag.swipe && Math.abs(dx) > 12 && Math.abs(dx) > Math.abs(dy) * 1.2) {
      menuDrag.swipe = true;
      try { elements.foodModule.setPointerCapture(event.pointerId); } catch {}
    }
  }
  if (menuDrag.ghost) {
    event.preventDefault();
    moveMenuDrag(event);
  }
});

elements.foodModule.addEventListener("pointerup", event => {
  if (elements.foodModule.hasPointerCapture(event.pointerId)) elements.foodModule.releasePointerCapture(event.pointerId);
  finishMenuDrag(event);
});

elements.foodModule.addEventListener("pointercancel", event => finishMenuDrag(event));

document.addEventListener("keydown", event => {
  if (event.key === "Escape") closeExpandedMenu();
});

document.getElementById("rankingTabs").addEventListener("click", event => {
  const button = event.target.closest("button[data-ranking]");
  if (!button) return;
  activeRanking = button.dataset.ranking;
  document.querySelectorAll("#rankingTabs button").forEach(tab => tab.classList.toggle("active", tab === button));
  renderLeaderboard();
});

document.getElementById("historyFilters").addEventListener("click", event => {
  const button = event.target.closest("button[data-filter]");
  if (!button) return;
  historyFilter = button.dataset.filter;
  document.querySelectorAll("#historyFilters button").forEach(filter => filter.classList.toggle("active", filter === button));
  renderHistory();
});

elements.waterRange.addEventListener("input", () => {
  draftProfile.waterTarget = Number(elements.waterRange.value);
  elements.waterOutput.textContent = `${draftProfile.waterTarget} ml`;
});

elements.sleepRange.addEventListener("input", () => {
  draftProfile.sleep = Number(elements.sleepRange.value);
  elements.sleepOutput.textContent = `${draftProfile.sleep} 小时`;
});

elements.healthNote.addEventListener("input", () => { draftProfile.note = elements.healthNote.value; });

document.getElementById("healthForm").addEventListener("submit", event => {
  event.preventDefault();
  profile = { ...draftProfile, note: elements.healthNote.value.trim() };
  saveStored(storageKeys.profile, profile);
  syncProfileToServer({
    goal: profile.goal,
    activity: profile.activity,
    water_target: profile.waterTarget,
    sleep: profile.sleep,
    note: profile.note
  });
  renderReport();
  const now = new Date();
  document.getElementById("profileSaved").textContent = `上次保存：今天 ${String(now.getHours()).padStart(2, "0")}:${String(now.getMinutes()).padStart(2, "0")}`;
  showToast("健康档案已保存，报告已同步更新");
});

document.getElementById("profileBack").addEventListener("click", () => switchView("today"));
document.querySelectorAll("[data-home-return]").forEach(button => button.addEventListener("click", () => switchView("today")));
document.getElementById("foodSearchInput").addEventListener("input", event => {
  clearTimeout(searchTimer);
  const query = event.target.value;
  searchTimer = setTimeout(() => renderFoodSearch(query), 150);
});
document.getElementById("foodSearchResults").addEventListener("click", event => {
  const button = event.target.closest("[data-food-id]");
  if (!button) return;
  recordFood(button.dataset.foodId);
  clearFoodSearch();
});
document.getElementById("randomPickButton").addEventListener("click", () => openRandomPicker());
document.getElementById("rerollRandom").addEventListener("click", rerollRandom);
document.getElementById("confirmRandom").addEventListener("click", confirmRandom);
document.getElementById("coverEditButton").addEventListener("click", openCoverDialog);
document.getElementById("closeCoverDialog").addEventListener("click", () => document.getElementById("coverDialog").close());
document.getElementById("coverCancel").addEventListener("click", () => document.getElementById("coverDialog").close());
document.getElementById("coverReset").addEventListener("click", resetCover);
document.getElementById("coverSave").addEventListener("click", saveCover);
document.getElementById("coverFile").addEventListener("change", event => {
  const file = event.target.files?.[0];
  if (!file) return;
  readCoverFile(file).then(src => {
    coverDraftSrc = src;
    syncCoverPreview(src);
  }).catch(() => showToast("图片读取失败，请换一张试试"));
});

document.getElementById("settingsButton").addEventListener("click", () => elements.settingsDialog.showModal());
elements.motionToggle.addEventListener("change", () => setMotion(elements.motionToggle.checked));
document.getElementById("clearDataButton").addEventListener("click", clearLocalData);
elements.settingsDialog.addEventListener("click", event => {
  if (event.target === elements.settingsDialog) {
    const rect = elements.settingsDialog.getBoundingClientRect();
    if (event.clientX < rect.left || event.clientX > rect.right || event.clientY < rect.top || event.clientY > rect.bottom) elements.settingsDialog.close();
  }
});

window.addEventListener("resize", () => {
  resizeConstellation();
  if (menuDrag?.ghost) positionDropZone();
});

document.addEventListener("visibilitychange", () => {
  if (document.hidden) stopConstellation();
  else if (!document.body.classList.contains("motion-off")) startConstellation();
});

initServiceLogin();
ensurePointsState();
applyTheme(pointsState.selected || "aurora");
initDates();
loadAgentMenu();
renderMeals();
renderReport();
renderHistory();
syncProfileForm();
resizeConstellation();
const savedMotion = loadStored(storageKeys.motion, !window.matchMedia("(prefers-reduced-motion: reduce)").matches);
setMotion(savedMotion !== false);
if (window.innerWidth <= 1040) setDetailsCollapsed(true);
if (window.innerWidth <= 820) setSidebarCollapsed(true);
window.eatLocal = {
  sendPrompt,
  recordMeal,
  applyMealAction,
  syncProfileFromAgent,
  getHealthAnalysis,
  renderInference,
  maybeOnboarding,
  ensureWelcomeBlock,
  showToast
};
function initServiceLogin() {
  const overlay = document.getElementById("serviceLogin");
  const form = document.getElementById("serviceLoginForm");
  const appShell = document.getElementById("appShell");
  const splash = document.getElementById("bootSplash");
  if (!overlay || !form) return;
  const hostInput = document.getElementById("serviceHost");
  const portInput = document.getElementById("servicePort");
  const loginInput = document.getElementById("serviceLoginId");
  const passwordInput = document.getElementById("servicePassword");
  const captchaInput = document.getElementById("serviceCaptchaCode");
  const captchaImage = document.getElementById("serviceCaptchaRefresh");
  const registerInput = document.getElementById("serviceRegister");
  const errorBox = document.getElementById("serviceLoginError");
  const button = document.getElementById("serviceLoginButton");
  const tabLogin = document.getElementById("authTabLogin");
  const tabRegister = document.getElementById("authTabRegister");
  const recovery = document.getElementById("forgotPasswordPanel");
  let captchaId = "";
  let authMode = "login";

const pageHost = location.hostname && location.hostname !== "localhost" && location.hostname !== "127.0.0.1"
    ? location.hostname
    : "";
  const storedHost = localStorage.getItem("eat-server-host") || "";
  hostInput.value = pageHost || (storedHost && storedHost !== "10.101.17.32" ? storedHost : "10.112.151.15");
  loginInput.value = localStorage.getItem("eat-server-login") || "";

  const showError = message => {
    errorBox.hidden = !message;
    errorBox.textContent = message || "";
  };

  const loginErrorText = raw => {
    const text = String(raw || "");
    if (text.includes("captcha_invalid") || text.includes("captcha_required")) {
      return "验证码错误或已过期，请重新输入";
    }
    if (text.includes("invalid_credentials")) return "账号或密码错误";
    if (text.includes("login_id_exists")) return "账号已存在，请切换到登录";
    if (text.includes("too_many") || text.includes("locked")) return "登录失败次数过多，请稍后再试";
    if (text.includes("connect_failed") || text.includes("timeout")) {
      return "无法连接服务端，请检查地址、端口和 WiFi";
    }
    return text.replace(/^login_failed:/, "") || "登录失败";
  };

  const setMode = mode => {
    authMode = mode;
    registerInput.checked = mode === "register";
    tabLogin.classList.toggle("active", mode === "login");
    tabRegister.classList.toggle("active", mode === "register");
    button.textContent = mode === "register" ? "注册并进入" : "进入饭饭";
    recovery.hidden = true;
    form.hidden = false;
  };

  const setAuthenticated = authenticated => {
    overlay.hidden = authenticated;
    if (appShell) appShell.hidden = !authenticated;
    document.body.classList.toggle("is-authenticated", authenticated);
  };

  async function refreshCaptcha() {
    captchaId = "";
    if (captchaInput) captchaInput.value = "";
    if (!window.eatTransport || !captchaImage) return;
    captchaImage.textContent = "加载中…";
    try {
      const result = await window.eatTransport.captcha({
        host: hostInput.value.trim(),
        port: Number(portInput.value) || 9443
      });
      if (!result || result.ok === false || !result.svg) {
        throw new Error((result && result.message) || "验证码加载失败");
      }
      captchaId = result.captcha_id || "";
      captchaImage.innerHTML = result.svg;
    } catch (error) {
      captchaImage.textContent = "点击刷新";
      showError(error && error.message ? error.message : "验证码加载失败");
    }
  }

  const boot = async () => {
    setTimeout(() => splash?.classList.add("is-done"), 900);
    setTimeout(() => { if (splash) splash.hidden = true; }, 1320);
    if (!window.eatTransport) {
      setAuthenticated(false);
      refreshCaptcha();
      return;
    }
    try {
      const status = await window.eatTransport.request("/api/service/status");
      const storedKey = localStorage.getItem("eat-agent-user") || "";
      if (status && status.mode === "tcp" && (storedKey || status.user_key)) {
        setAuthenticated(true);
        await enterApp(storedKey || status.user_key);
        return;
      }
    } catch {
      // Agent 尚未启动时保留登录界面
    }
    setAuthenticated(false);
    refreshCaptcha();
  };

  tabLogin?.addEventListener("click", () => setMode("login"));
  tabRegister?.addEventListener("click", () => setMode("register"));
  captchaImage?.addEventListener("click", refreshCaptcha);
  hostInput?.addEventListener("change", refreshCaptcha);
  portInput?.addEventListener("change", refreshCaptcha);
  document.getElementById("forgotPasswordButton")?.addEventListener("click", () => {
    form.hidden = true;
    recovery.hidden = false;
  });
  document.getElementById("closeForgotPassword")?.addEventListener("click", () => {
    recovery.hidden = true;
    form.hidden = false;
  });
  document.getElementById("quitAuthButton")?.addEventListener("click", () => {
    if (window.eatTransport) window.eatTransport.quit();
    else window.close();
  });
  async function logoutService() {
    if (!window.confirm("确定退出登录吗？")) return;
    try { document.getElementById("settingsDialog")?.close(); } catch {}
    try { document.getElementById("userInfoDialog")?.close(); } catch {}
    try { await window.eatTransport.logout(); } catch {}
    localStorage.removeItem("eat-server-login");
    localStorage.removeItem("eat-agent-session");
    localStorage.removeItem("eat-agent-user");
    peerMedia.clear();
    mediaInFlight.clear();
    ["eat-health-profile-v2", "eat-health-meals-v2", "eat-health-history-v2",
      "eat-health-points-v1", "eat-user-avatar", "eat-champion-cover-v1"].forEach(key => {
      try { localStorage.removeItem(key); } catch {}
    });
    window.location.reload();
  }
  document.getElementById("logoutButton")?.addEventListener("click", logoutService);
  document.getElementById("personalLogoutButton")?.addEventListener("click", logoutService);

  form.addEventListener("submit", async event => {
    event.preventDefault();
    showError("");
    if (!captchaId) {
      showError("请先获取验证码");
      refreshCaptcha();
      return;
    }
    button.disabled = true;
    button.textContent = "正在连接…";
    try {
      const result = await window.eatTransport.login({
        host: hostInput.value.trim(),
        port: Number(portInput.value) || 9443,
        login_id: loginInput.value.trim(),
        password: passwordInput.value,
        captcha_id: captchaId,
        captcha_code: captchaInput ? captchaInput.value.trim() : "",
        register: authMode === "register"
      });
      if (!result || result.ok === false) {
        throw new Error((result && result.message) || "登录失败");
      }
      localStorage.setItem("eat-server-host", hostInput.value.trim());
      localStorage.setItem("eat-server-login", loginInput.value.trim());
      ["eat-health-profile-v2", "eat-health-meals-v2", "eat-health-history-v2",
        "eat-health-points-v1", "eat-user-avatar", "eat-champion-cover-v1",
        "eat-agent-session"].forEach(key => { try { localStorage.removeItem(key); } catch {} });
      localStorage.setItem("eat-agent-user", (result && result.user_key) || "");
      setAuthenticated(true);
      window.location.reload();
    } catch (error) {
      showError(loginErrorText(error && error.message ? error.message : ""));
      refreshCaptcha();
    } finally {
      button.disabled = false;
      button.textContent = authMode === "register" ? "注册并进入" : "进入饭饭";
    }
  });

  setMode("login");
  boot();
}

async function enterApp(userKey) {
  currentUserKey = userKey || "";
  if (window.eatAgent && window.eatAgent.state) {
    window.eatAgent.state.userId = currentUserKey;
  }
  if (currentUserKey) localStorage.setItem("eat-agent-user", currentUserKey);
  const myKey = document.getElementById("myUserKey");
  if (myKey) myKey.textContent = currentUserKey || "—";
  clearMockContent();
  await loadRealData(currentUserKey);
  loadAgentMenu();
  maybeOnboarding(currentUserKey);
}

function clearMockContent() {
  const welcome = document.getElementById("inferenceCard")?.closest(".message") || ensureWelcomeBlock();
  document.querySelectorAll("#conversation .message").forEach(node => {
    if (node !== welcome) node.remove();
  });
  if (welcome) welcome.style.display = "";
  meals = loadStored(storageKeys.meals, []);
  const seededMeals = meals.length === defaultMeals.length && meals.every((meal, index) => meal.text === defaultMeals[index].text);
  if (seededMeals && currentUserKey && currentUserKey !== "u_demo") meals = [];
  history = loadStored(storageKeys.history, []);
  remoteWeekly = null;
  remoteFriends = [];
  remoteRanking = [];
  rankings.week = [];
  rankings.today = [];
  profile = { ...defaultProfile, ...loadStored(storageKeys.profile, {}) };
  draftProfile = { ...profile };
  applyProfileName();

  renderMeals();
  syncProfileForm();
  renderReport();
  renderLeaderboard(0);
  renderShop();
  renderFriends();
  renderFriendRanking();
}

async function loadRealData(userKey) {
  const api = window.eatTransport;
  if (!api || !userKey) return;
  const query = encodeURIComponent(userKey);
  const read = async path => {
    try {
      const result = await api.request(path);
      if (!result || result.ok === false) return null;
      return result;
    } catch {
      return null;
    }
  };
  const [profileData, mealsData, weeklyData, friendsData, rankingData, coverData] = await Promise.all([
    read(`/api/profile?user_id=${query}`),
    read(`/api/meals?user_id=${query}&days=7`),
    read(`/api/stats/weekly?user_id=${query}`),
    read(`/api/friends?user_id=${query}`),
    read(`/api/ranking?user_id=${query}&scope=week`),
    read(`/api/profile/cover?user_id=${query}`)
  ]);
  if (coverData && coverData.ok !== false) {
    peerMedia.set(userKey, {
      cover: String(coverData.cover || ""),
      avatar: String(coverData.avatar || ""),
      cover_ver: Number(coverData.cover_ver || 0),
      avatar_ver: Number(coverData.avatar_ver || 0)
    });
  }
  if (coverData && coverData.cover) {
    championCoverSrc = coverData.cover;
    saveStored(storageKeys.cover, { src: championCoverSrc });
  }
  const avatar = (coverData && coverData.avatar) || "";
  localStorage.setItem("eat-user-avatar", avatar);
  applyUserAvatar(avatar);
  applyRemoteProfile(profileData);
  applyRemoteMeals(mealsData);
  applyRemoteWeekly(weeklyData);
  applyRemoteFriends(friendsData);
  applyRemoteRanking(rankingData);
  syncProfileForm();
  renderMeals();
  renderReport();
  renderFriends();
  renderFriendRanking();
  renderLeaderboard();
}

function applyProfileName() {
  const name = String(profile.name || "我").trim() || "我";
  const nameInput = document.getElementById("nameInput");
  if (nameInput) nameInput.value = name;
  const preview = document.getElementById("personalPreviewName");
  if (preview) preview.textContent = name;
  const initial = document.getElementById("avatarInitialText");
  if (initial) initial.textContent = name.slice(0, 1);
  renderLeaderboard();
}

function applyRemoteProfile(data) {
  if (!data) return;
  const stored = loadStored(storageKeys.profile, {});
  profile = {
    ...profile,
    name: stored.name || data.name || "",
    height: Number(stored.height) > 0 ? Number(stored.height) : (Number(data.height) > 0 ? Number(data.height) : ""),
    weight: Number(stored.weight) > 0 ? Number(stored.weight) : (Number(data.weight) > 0 ? Number(data.weight) : ""),
    age: Number(stored.age) > 0 ? Number(stored.age) : (Number(data.age) > 0 ? Number(data.age) : ""),
    goal: stored.goal || data.goal || "均衡饮食",
    activity: stored.activity || data.activity || "久坐",
    waterTarget: Number(stored.waterTarget) > 0 ? Number(stored.waterTarget) : (Number(data.water_target) > 0 ? Number(data.water_target) : 1800),
    sleep: Number.isFinite(parseFloat(stored.sleep)) ? parseFloat(stored.sleep) : (parseFloat(data.sleep) || 7),
    note: stored.note || data.note || "",
    spend_min: Number(stored.spend_min) >= 0 ? Number(stored.spend_min) : (Number(data.spend_min) >= 0 ? Number(data.spend_min) : 20),
    spend_max: Number(stored.spend_max) > 0 ? Number(stored.spend_max) : (Number(data.spend_max) > 0 ? Number(data.spend_max) : 35)
  };
  if (data.user_id) currentUserKey = data.user_id;
  draftProfile = { ...profile };
  saveStored(storageKeys.profile, profile);
  applyProfileName();
}

function applyRemoteMeals(data) {
  if (!data || !Array.isArray(data.meals)) return;
  const today = todayKey();
  const remoteRows = data.meals
    .filter(meal => !meal.date || meal.date === today)
    .map(meal => ({ type: meal.type || "加餐",
      time: meal.time || "",
      text: meal.text || (Array.isArray(meal.items)
        ? meal.items.join("、") : ""),
      protein: Number(meal.protein) || 0,
      fiber: Number(meal.fiber) || 0,
      veggies: Number(meal.veggies) || 0,
      water: Number(meal.water) || 0,
      penalty: Number(meal.penalty) || 0 }));
  if (!remoteRows.length) return;
  const keyOf = meal => `${meal.type}|${meal.text}`;
  const merged = new Map();
  meals.forEach(meal => merged.set(keyOf(meal), meal));
  remoteRows.forEach(meal => {
    const prev = merged.get(keyOf(meal));
    if (prev && !meal.time) meal.time = prev.time;
    merged.set(keyOf(meal), meal);
  });
  meals = [...merged.values()];
  saveStored(storageKeys.meals, meals);
  renderMeals();
  renderReport();
}

function applyRemoteWeekly(data) {
  remoteWeekly = data || null;
  const daily = data && Array.isArray(data.daily) ? data.daily : [];
  history = daily.map(item => ({
    date: item.date,
    meals: Number(item.meals) || 0,
    summary: "",
    score: Number(item.score) || 0
  }));
}

function applyRemoteFriends(data) {
  remoteFriends = data && Array.isArray(data.friends) ? data.friends : [];
}

function applyRemoteRanking(data) {
  const rows = data && Array.isArray(data.ranking) ? data.ranking : [];
  remoteRanking = rows;
  rankings.week = rows.map(row => {
    const current = row.user_id === currentUserKey;
    return {
      id: row.user_id || "",
      name: current ? (profile.name || "我") : (row.name || row.user_id),
      score: Number(row.score) || 0,
      note: current ? "我" : `第 ${row.rank} 名`,
      current,
      cover: row.cover || "",
      avatar: row.avatar || "",
      cover_ver: Number(row.cover_ver || 0),
      avatar_ver: Number(row.avatar_ver || 0)
    };
  });
  rankings.today = rankings.week.map(item => ({ ...item }));
}

async function refreshSocial() {
  if (!currentUserKey || !window.eatTransport) return;
  const query = encodeURIComponent(currentUserKey);
  try {
    const friendsData = await window.eatTransport.request(`/api/friends?user_id=${query}`);
    applyRemoteFriends(friendsData);
  } catch {}
  try {
    const rankingData = await window.eatTransport.request(`/api/ranking?user_id=${query}&scope=week`);
    applyRemoteRanking(rankingData);
  } catch {}
  renderFriends();
  renderFriendRanking();
  renderLeaderboard();
}

function maybeOnboarding(userKey) {
  if (!userKey) return;
  const key = `eat-onboarded:${userKey}`;
  const conversation = document.getElementById("conversation");
  if (!conversation) return;
  let welcome = document.getElementById("inferenceCard")?.closest(".message");
  if (!welcome) welcome = ensureWelcomeBlock();
  if (!welcome) return;
  document.querySelector(".onboarding-message")?.remove();
  if (localStorage.getItem(key) === "1") {
    welcome.style.display = "";
    return;
  }
  welcome.style.display = "none";
  conversation.insertAdjacentHTML("afterbegin", `
    <article class="message agent-message onboarding-message">
      <div class="agent-avatar" aria-hidden="true"><svg class="icon"><use href="#icon-utensils"></use></svg></div>
      <div class="message-content">
        <div class="message-meta"><strong>饭饭</strong><span>第一次见面</span></div>
        <div class="message-bubble">
          <p>我是饭饭，负责记录你每天吃的，也帮你看看营养够不够。</p>
          <p>开始前想认识你一下：身高、体重、年龄、日常活动量和需要留意的身体状况。填好之后，菜单和报告才会贴合你。</p>
          <p>你也可以直接对我说，比如「我 170，60 公斤，久坐，想控制体重」，我来帮你改。</p>
          <p>记录和完成度会积累积分；好友排行榜只显示彼此确认过的好友，不会公开给其他人。</p>
          <div class="onboarding-actions">
            <button class="button primary-button" id="onboardingProfile" type="button">带我去填资料</button>
            <button class="button secondary-button" id="onboardingTell" type="button">直接告诉饭饭</button>
          </div>
        </div>
      </div>
    </article>
  `);
  const complete = () => {
    localStorage.setItem(key, "1");
    document.querySelector(".onboarding-message")?.remove();
    welcome.style.display = "";
    renderInference();
  };
  document.getElementById("onboardingProfile")?.addEventListener("click", () => {
    complete();
    document.getElementById("editBodyInfo")?.click();
  });
  document.getElementById("onboardingTell")?.addEventListener("click", () => {
    complete();
    document.getElementById("chatInput")?.focus();
  });
}

function resetPointsForUser() {
  pointsState = {
    ...defaultPointsState,
    week: getWeekKey(),
    baseEarned: 0,
    spent: 0,
    owned: ["aurora"],
    selected: "aurora"
  };
  saveStored(storageKeys.points, pointsState);
  renderShop();
}

function avatarCellFor(key, name) {
  const media = key ? peerMedia.get(key) : null;
  const src = media && (media.avatar || media.cover);
  const initial = escapeHtml(String(name || "友").slice(0, 1));
  if (!src) return `<span class="friend-avatar">${initial}</span>`;
  return `<span class="friend-avatar has-photo"><img src="${escapeHtml(src)}" alt="" onerror="this.hidden=true;this.nextElementSibling.hidden=false"><span class="friend-initial" hidden>${initial}</span></span>`;
}

function renderFriends() {
  const list = document.getElementById("friendList");
  const empty = document.getElementById("friendsEmpty");
  const count = document.getElementById("friendCount");
  if (!list) return;
  if (count) count.textContent = `${remoteFriends.length} 位好友`;
  if (empty) empty.hidden = remoteFriends.length > 0;
  list.innerHTML = remoteFriends.map(friend => {
    const name = friend.name || friend.id || friend.user_key || "好友";
    const id = friend.id || friend.user_key || "";
    return `<div class="friend-row" data-peer="${escapeHtml(id)}">
      ${avatarCellFor(id, name)}
      <div class="friend-copy"><strong>${escapeHtml(name)}</strong><small>${escapeHtml(id)}</small></div>
      <div class="friend-actions">
        <button type="button" data-friend-action="remove">移除</button>
        <button type="button" data-friend-action="block">拉黑</button>
      </div>
    </div>`;
  }).join("");
  syncPeerMedia(remoteFriends.map(friend => ({
    id: friend.id || friend.user_key || "",
    cover_ver: friend.cover_ver,
    avatar_ver: friend.avatar_ver
  })));
}

function renderFriendRanking() {
  const list = document.getElementById("friendsRanking");
  const empty = document.getElementById("friendsRankingEmpty");
  if (!list) return;
  if (empty) empty.hidden = remoteRanking.length > 0;
  list.innerHTML = remoteRanking.map(row => {
    const name = row.user_id === currentUserKey ? "你" : (row.name || row.user_id);
    return `<li class="friend-rank-row${row.user_id === currentUserKey ? " is-me" : ""}">
      <span class="rank-number">${row.rank}</span>
      ${avatarCellFor(row.user_id, name)}
      <span class="friend-copy"><strong>${escapeHtml(name)}</strong><small>${row.user_id === currentUserKey ? "我" : "好友"}</small></span>
      <strong class="friend-score">${Number(row.score) || 0}<small>分</small></strong>
    </li>`;
  }).join("");
  syncPeerMedia(remoteRanking.map(row => ({
    id: row.user_id,
    cover_ver: row.cover_ver,
    avatar_ver: row.avatar_ver
  })));
}

async function refreshFriendData() {
  if (!currentUserKey || !window.eatTransport) return;
  const query = encodeURIComponent(currentUserKey);
  try {
    const friends = await window.eatTransport.request(`/api/friends?user_id=${query}`);
    applyRemoteFriends(friends);
    renderFriends();
    const ranking = await window.eatTransport.request(`/api/ranking?user_id=${query}&scope=week`);
    applyRemoteRanking(ranking);
    renderFriendRanking();
    renderLeaderboard();
  } catch (error) {
    showToast("好友数据暂时不可用");
  }
}

async function friendAction(peer, action) {
  if (!peer || !currentUserKey) return;
  const path = action === "request" ? "/api/friend/request"
    : action === "accept" ? "/api/friend/accept"
    : action === "remove" ? "/api/friend/remove"
    : "/api/friend/block";
  try {
    const result = await window.eatTransport.request(path, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ user_id: currentUserKey, peer_key: peer })
    });
    if (!result || result.ok === false) {
      throw new Error((result && result.message) || "操作失败");
    }
    showToast(action === "request" ? "好友申请已发送" : action === "remove" ? "已移除好友" : action === "block" ? "已拉黑" : "已接受申请");
    await refreshFriendData();
  } catch (error) {
    showToast(error && error.message ? error.message : "好友操作失败");
  }
}

document.getElementById("friendAddForm")?.addEventListener("submit", event => {
  event.preventDefault();
  const input = document.getElementById("friendKeyInput");
  const peer = input ? input.value.trim() : "";
  if (!peer) return;
  friendAction(peer, "request");
  input.value = "";
});
document.getElementById("friendAcceptForm")?.addEventListener("submit", event => {
  event.preventDefault();
  const input = document.getElementById("friendAcceptKey");
  const peer = input ? input.value.trim() : "";
  if (!peer) return;
  friendAction(peer, "accept");
  input.value = "";
});
document.getElementById("friendList")?.addEventListener("click", event => {
  const button = event.target.closest("[data-friend-action]");
  const row = event.target.closest("[data-peer]");
  if (!button || !row) return;
  friendAction(row.dataset.peer, button.dataset.friendAction);
});
document.getElementById("refreshFriends")?.addEventListener("click", refreshFriendData);
