const storageKeys = {
  profile: "eat-health-profile-v2",
  meals: "eat-health-meals-v2",
  history: "eat-health-history-v2",
  motion: "eat-agent-motion"
};

const defaultProfile = {
  goal: "均衡饮食",
  activity: "久坐",
  waterTarget: 1800,
  sleep: 7,
  note: "下午容易口渴，晚餐不宜过饱"
};

const defaultMeals = [
  { type: "早餐", time: "08:20", text: "燕麦酸奶、蓝莓、水煮蛋", protein: 22, fiber: 6, veggies: 0, water: 300, penalty: 0 },
  { type: "午餐", time: "12:18", text: "柠檬鸡胸能量碗、无糖茶", protein: 38, fiber: 9, veggies: 320, water: 650, penalty: 0 }
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

const viewTitles = {
  today: "今天吃了什么",
  report: "健康报告",
  profile: "健康档案",
  history: "历史记录"
};

const monthLabels = ["一月", "二月", "三月", "四月", "五月", "六月", "七月", "八月", "九月", "十月", "十一月", "十二月"];
const weekdayLabels = ["周日", "周一", "周二", "周三", "周四", "周五", "周六"];
const mealOrder = ["早餐", "午餐", "晚餐", "加餐"];

let profile = loadStored(storageKeys.profile, defaultProfile);
let meals = loadStored(storageKeys.meals, defaultMeals);
let history = loadStored(storageKeys.history, seedHistory);
let draftProfile = { ...profile };
let historyFilter = "week";
let activeRanking = "week";
let toastTimer;
let replyTimer;
let menuDrag = null;
let suppressMenuClick = false;

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
  menuLauncher: document.getElementById("menuLauncher"),
  sidebarMenuPopover: document.getElementById("sidebarMenuPopover"),
  expandedMenu: document.getElementById("expandedMenu"),
  expandedMenuList: document.getElementById("expandedMenuList"),
  menuDropZone: document.getElementById("menuDropZone"),
  sidebarToggle: document.getElementById("sidebarToggle"),
  detailsToggle: document.getElementById("detailsToggle"),
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

function renderReport() {
  const analysis = getHealthAnalysis();
  const { nutrition, targets, ratios, score, status, summary, adjustment } = analysis;
  elements.mealCount.textContent = `已记录 ${meals.length} 餐`;
  elements.scoreValue.textContent = score;
  elements.scoreCircle.style.strokeDashoffset = `${251.33 * (1 - score / 100)}`;
  elements.scoreStatus.textContent = status;
  const delta = score - 78;
  elements.scoreDelta.textContent = `较近 7 日均值 ${delta >= 0 ? "+" : ""}${delta}`;
  elements.reportSummary.textContent = summary;
  elements.reportBasis.textContent = `依据：今日 ${meals.length} 餐、近 7 天记录、${profile.activity}与${profile.goal}目标`;
  document.getElementById("detailScoreValue").textContent = score;
  document.getElementById("detailScoreStatus").textContent = status;
  document.getElementById("adjustmentInsight").textContent = adjustment;
  document.getElementById("snapshotScore").textContent = score;
  document.getElementById("profileHeadline").textContent = `${profile.goal}，${profile.activity}状态`;
  document.getElementById("profileDescription").textContent = `报告会结合${profile.activity}活动量、${profile.sleep} 小时睡眠与${profile.goal}目标，观察近期饮食节奏。`;
  document.getElementById("snapshotWater").textContent = `${profile.waterTarget} ml`;
  document.getElementById("snapshotSleep").textContent = `${profile.sleep} 小时`;
  const values = {
    protein: `${nutrition.protein} / ${targets.protein} g`,
    fiber: `${nutrition.fiber} / ${targets.fiber} g`,
    veggies: `${nutrition.veggies} / ${targets.veggies} g`,
    water: `${nutrition.water} / ${targets.water} ml`
  };
  Object.keys(values).forEach(key => {
    document.getElementById(`${key}Value`).textContent = values[key];
    document.getElementById(`${key}Bar`).style.width = `${Math.round(ratios[key] * 100)}%`;
  });
  const labels = { protein: "蛋白质", fiber: "膳食纤维", veggies: "蔬菜", water: "饮水" };
  document.getElementById("detailNutritionList").innerHTML = `<div class="detail-nutrition-list">${Object.keys(labels).map(key => `<div class="detail-nutrition-row"><span>${labels[key]}</span><div class="nutrition-track"><i style="width:${Math.round(ratios[key] * 100)}%"></i></div><strong>${values[key]}</strong></div>`).join("")}</div>`;
  const tags = [ratios.veggies >= 0.6 ? "蔬菜摄入良好" : "蔬菜待补充", ratios.protein >= 1 ? "蛋白质已达标" : "蛋白质进行中", ratios.water < 0.8 ? "饮水待补充" : "饮水节奏良好"];
  document.getElementById("reportTags").innerHTML = tags.map(tag => `<span>${tag}</span>`).join("");
  renderTrend(score);
  updateTodayHistory(score);
  renderLeaderboard(score);
}

function renderTrend(score) {
  const values = [74, 80, 77, 83, 79, 86, score];
  const points = values.map((value, index) => {
    const x = 20 + index * 73.3;
    const y = 158 - (value - 60) * 3.2;
    return [Number(x.toFixed(1)), Number(Math.max(28, y).toFixed(1))];
  });
  document.getElementById("trendLine").setAttribute("points", points.map(point => point.join(",")).join(" "));
  document.getElementById("trendArea").setAttribute("d", `M${points[0][0]} 158 L${points.map(point => point.join(" ")).join(" L")} L${points[points.length - 1][0]} 158 Z`);
  document.getElementById("trendDots").innerHTML = points.map((point, index) => `<circle class="trend-dot${index === points.length - 1 ? " current" : ""}" cx="${point[0]}" cy="${point[1]}" r="${index === points.length - 1 ? 5 : 3.5}"></circle>`).join("");
  document.getElementById("trendAverage").textContent = `均值 ${Math.round(values.reduce((sum, value) => sum + value, 0) / values.length)}`;
}

function renderMeals() {
  const slots = ["早餐", "午餐", "晚餐"];
  const remaining = [...meals];
  const rows = slots.map(type => {
    const index = remaining.findIndex(meal => meal.type === type);
    const meal = index >= 0 ? remaining.splice(index, 1)[0] : null;
    return meal || { type, time: type === "晚餐" ? "晚间" : "待记录", text: type === "晚餐" ? "还没有记录晚餐" : "可以随时补充", pending: true };
  });
  rows.push(...remaining);
  elements.mealTimeline.innerHTML = rows.map(meal => `<div class="meal-item ${meal.pending ? "pending" : "completed"}"><time>${escapeHtml(meal.time)}</time><span class="timeline-dot">${icon("check")}</span><div><strong>${escapeHtml(meal.type)}</strong><p>${escapeHtml(meal.text)}</p></div></div>`).join("");
}

function renderLeaderboard(currentScore = getHealthAnalysis().score) {
  const data = rankings[activeRanking].map(item => item.current ? { ...item, score: currentScore, note: activeRanking === "today" ? `已记录 ${meals.length} 餐` : item.note } : { ...item });
  data.sort((a, b) => b.score - a.score);
  elements.leaderboardList.innerHTML = data.map((item, index) => `<div class="leaderboard-row${item.current ? " current-user" : ""}"><span class="rank-number">${index + 1}</span><span class="friend-avatar">${escapeHtml(item.name === "你" ? "我" : item.name.slice(0, 1))}</span><div class="friend-copy"><strong>${escapeHtml(item.name)}</strong><small>${escapeHtml(item.note)}</small></div><span class="friend-score">${item.score}<small>分</small></span></div>`).join("");
  const rank = data.findIndex(item => item.current) + 1;
  document.getElementById("rankingSummary").textContent = activeRanking === "week" ? `你本周连续记录 5 天，当前第 ${rank} 名` : `你今日已记录 ${meals.length} 餐，当前第 ${rank} 名`;
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

function switchView(view) {
  if (!viewTitles[view]) return;
  closeExpandedMenu();
  closeSidebarMenu();
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
  const explicit = mealOrder.find(type => text.includes(type)) || (/下午茶|零食|奶茶|咖啡/.test(text) ? "加餐" : null);
  if (explicit && !meals.some(meal => meal.type === explicit)) return explicit;
  return mealOrder.find(type => !meals.some(meal => meal.type === type)) || "加餐";
}

function recordMeal(text) {
  const type = determineMealType(text);
  const now = new Date();
  const time = `${String(now.getHours()).padStart(2, "0")}:${String(now.getMinutes()).padStart(2, "0")}`;
  const meal = { type, time, text, ...analyzeMealText(text) };
  meals.push(meal);
  saveStored(storageKeys.meals, meals);
  renderMeals();
  renderReport();
  return meal;
}

function sendPrompt(rawText) {
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
  if (collapsed) closeSidebarMenu();
}

function setDetailsCollapsed(collapsed) {
  elements.workspace.classList.toggle("right-collapsed", collapsed);
  const label = collapsed ? "展开右侧栏" : "收起右侧栏";
  elements.detailsToggle.dataset.tooltip = label;
  elements.detailsToggle.setAttribute("aria-label", label);
}

function closeSidebarMenu() {
  elements.sidebarMenuPopover.classList.remove("open");
  elements.menuLauncher.setAttribute("aria-expanded", "false");
}

function toggleSidebarMenu() {
  if (elements.appShell.classList.contains("left-collapsed")) setSidebarCollapsed(false);
  const open = !elements.sidebarMenuPopover.classList.contains("open");
  elements.sidebarMenuPopover.classList.toggle("open", open);
  elements.menuLauncher.setAttribute("aria-expanded", String(open));
}

function openExpandedMenu() {
  closeSidebarMenu();
  elements.expandedMenuList.scrollTop = 0;
  elements.expandedMenu.classList.remove("open");
  void elements.expandedMenu.offsetWidth;
  elements.expandedMenu.classList.add("open");
  elements.expandedMenu.setAttribute("aria-hidden", "false");
  elements.expandedMenuList.scrollTop = 0;
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
  const rect = elements.menuLauncher.getBoundingClientRect();
  const ghost = document.createElement("div");
  ghost.className = "menu-drag-ghost";
  ghost.innerHTML = `${icon("menu")}<strong>快捷菜单</strong>${icon("grip")}`;
  document.body.appendChild(ghost);
  menuDrag.ghost = ghost;
  menuDrag.offsetX = event.clientX - rect.left;
  menuDrag.offsetY = event.clientY - rect.top;
  elements.menuLauncher.classList.add("is-drag-source");
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
  const wasDragging = Boolean(menuDrag.ghost);
  const shouldOpen = wasDragging && pointerOverCenter(event);
  if (menuDrag.ghost) {
    menuDrag.ghost.style.opacity = "0";
    menuDrag.ghost.style.transition = "opacity 160ms var(--ease), transform 200ms var(--ease)";
    const ghost = menuDrag.ghost;
    setTimeout(() => ghost.remove(), 180);
  }
  menuDrag = null;
  elements.menuLauncher.classList.remove("is-drag-source");
  document.body.classList.remove("is-dragging-menu");
  elements.menuDropZone.classList.remove("visible", "is-over");
  if (wasDragging) {
    suppressMenuClick = true;
    setTimeout(() => { suppressMenuClick = false; }, 240);
  }
  if (shouldOpen) setTimeout(openExpandedMenu, 100);
}

function clearLocalData() {
  profile = { ...defaultProfile };
  meals = structuredClone(defaultMeals);
  history = structuredClone(seedHistory);
  Object.values(storageKeys).forEach(key => {
    try { localStorage.removeItem(key); } catch { return; }
  });
  elements.motionToggle.checked = true;
  setMotion(true);
  renderMeals();
  renderReport();
  renderHistory();
  syncProfileForm();
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
  document.getElementById("dateMonth").textContent = monthLabels[now.getMonth()];
  document.getElementById("reportDateHeading").textContent = `${now.getMonth() + 1} 月 ${now.getDate()} 日 · 综合饮食、活动量与健康目标生成`;
}

function downloadReport() {
  const analysis = getHealthAnalysis();
  const content = [`今天你吃了吗 · 今日健康报告`, `日期：${todayKey()}`, `综合评分：${analysis.score} / 100（${analysis.status}）`, `已记录：${meals.length} 餐`, "", "饮食记录", ...meals.map(meal => `${meal.time} ${meal.type}：${meal.text}`), "", "营养进度", `蛋白质：${analysis.nutrition.protein} / ${analysis.targets.protein} g`, `膳食纤维：${analysis.nutrition.fiber} / ${analysis.targets.fiber} g`, `蔬菜：${analysis.nutrition.veggies} / ${analysis.targets.veggies} g`, `饮水：${analysis.nutrition.water} / ${analysis.targets.water} ml`, "", "综合分析", analysis.summary, "", "本报告仅用于日常饮食记录和生活方式参考，不用于诊断或治疗。"].join("\n");
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
document.querySelectorAll("[data-prompt]").forEach(button => button.addEventListener("click", () => sendPrompt(button.dataset.prompt)));
document.querySelectorAll("[data-option-group]").forEach(group => {
  group.addEventListener("click", event => {
    const button = event.target.closest("button[data-value]");
    if (!button) return;
    draftProfile[group.dataset.optionGroup] = button.dataset.value;
    group.querySelectorAll("button").forEach(option => option.classList.toggle("selected", option === button));
  });
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
  elements.chatInput.style.height = `${Math.min(elements.chatInput.scrollHeight, 112)}px`;
});

document.getElementById("viewFullReport").addEventListener("click", () => switchView("report"));
document.getElementById("addMealButton").addEventListener("click", () => elements.chatInput.focus());
document.getElementById("profileShortcut").addEventListener("click", () => switchView("profile"));
document.getElementById("downloadReport").addEventListener("click", downloadReport);
elements.sidebarToggle.addEventListener("click", () => setSidebarCollapsed(!elements.appShell.classList.contains("left-collapsed")));
elements.detailsToggle.addEventListener("click", () => setDetailsCollapsed(!elements.workspace.classList.contains("right-collapsed")));
document.getElementById("detailsRailToggle").addEventListener("click", () => setDetailsCollapsed(false));
elements.menuLauncher.addEventListener("click", () => { if (!suppressMenuClick) toggleSidebarMenu(); });
document.getElementById("closeExpandedMenu").addEventListener("click", closeExpandedMenu);

elements.menuLauncher.addEventListener("pointerdown", event => {
  if (event.button !== 0) return;
  menuDrag = { startX: event.clientX, startY: event.clientY, ghost: null, overCenter: false };
  try { elements.menuLauncher.setPointerCapture(event.pointerId); } catch { return; }
});

elements.menuLauncher.addEventListener("pointermove", event => {
  if (!menuDrag) return;
  if (!menuDrag.ghost && Math.hypot(event.clientX - menuDrag.startX, event.clientY - menuDrag.startY) > 7) beginMenuDrag(event);
  if (menuDrag.ghost) {
    event.preventDefault();
    moveMenuDrag(event);
  }
});

elements.menuLauncher.addEventListener("pointerup", event => {
  if (elements.menuLauncher.hasPointerCapture(event.pointerId)) elements.menuLauncher.releasePointerCapture(event.pointerId);
  finishMenuDrag(event);
});

elements.menuLauncher.addEventListener("pointercancel", event => finishMenuDrag(event));

document.addEventListener("pointerdown", event => {
  if (!event.target.closest(".menu-anchor")) closeSidebarMenu();
});

document.addEventListener("keydown", event => {
  if (event.key === "Escape") {
    closeExpandedMenu();
    closeSidebarMenu();
  }
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
  renderReport();
  const now = new Date();
  document.getElementById("profileSaved").textContent = `上次保存：今天 ${String(now.getHours()).padStart(2, "0")}:${String(now.getMinutes()).padStart(2, "0")}`;
  showToast("健康档案已保存，报告已同步更新");
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

initDates();
renderMeals();
renderReport();
renderHistory();
syncProfileForm();
resizeConstellation();
const savedMotion = loadStored(storageKeys.motion, !window.matchMedia("(prefers-reduced-motion: reduce)").matches);
setMotion(savedMotion !== false);
if (window.innerWidth <= 1040) setDetailsCollapsed(true);
if (window.innerWidth <= 820) setSidebarCollapsed(true);