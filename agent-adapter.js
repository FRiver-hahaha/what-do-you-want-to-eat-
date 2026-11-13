(() => {
  const BASE = (localStorage.getItem("eat-agent-base") || "http://127.0.0.1:8787").replace(/\/$/, "");
  const STORE = {
    mode: "eat-agent-mode",
    model: "eat-agent-model",
    tone: "eat-agent-tone",
    access: "eat-agent-access",
    expert: "eat-agent-expert",
    session: "eat-agent-session",
    userId: "eat-agent-user"
  };
  const MAX_IMAGES = 10;
  const state = {
    ready: false,
    fallback: false,
    sending: false,
    mode: localStorage.getItem(STORE.mode) || "qa",
    modelId: localStorage.getItem(STORE.model) || "",
    tone: localStorage.getItem(STORE.tone) || "normal",
    access: localStorage.getItem(STORE.access) || "manual",
    expertId: localStorage.getItem(STORE.expert) || "",
    sessionId: localStorage.getItem(STORE.session) || `web_${Date.now().toString(36)}`,
    userId: localStorage.getItem(STORE.userId) || "u_demo",
    models: [],
    meta: null,
    experts: [],
    pending: null,
    lastUser: null,
    images: []
  };
  let toastTimer;

  const $ = (selector, root = document) => root.querySelector(selector);

  function escapeHtml(value) {
    return String(value).replace(/[&<>'"]/g, character => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "'": "&#39;", '"': "&quot;" }[character]));
  }

  function renderInline(text) {
    return text
      .replace(/\*\*(.+?)\*\*/g, "<strong>$1</strong>")
      .replace(/`([^`]+)`/g, "<code>$1</code>");
  }

  function renderMarkdown(markdown) {
    const lines = escapeHtml(markdown).split("\n");
    let html = "";
    let list = "";
    const closeList = () => {
      if (list) {
        html += `</${list}>`;
        list = "";
      }
    };
    for (const raw of lines) {
      const line = raw.trim();
      if (!line) {
        closeList();
        continue;
      }
      if (line.startsWith("### ")) {
        closeList();
        html += `<h3>${renderInline(line.slice(4))}</h3>`;
      } else if (line.startsWith("## ")) {
        closeList();
        html += `<h2>${renderInline(line.slice(3))}</h2>`;
      } else if (line.startsWith("# ")) {
        closeList();
        html += `<h3>${renderInline(line.slice(2))}</h3>`;
      } else if (/^[-*]\s+/.test(line)) {
        if (list !== "ul") {
          closeList();
          list = "ul";
          html += "<ul>";
        }
        html += `<li>${renderInline(line.replace(/^[-*]\s+/, ""))}</li>`;
      } else if (/^\d+\.\s+/.test(line)) {
        if (list !== "ol") {
          closeList();
          list = "ol";
          html += "<ol>";
        }
        html += `<li>${renderInline(line.replace(/^\d+\.\s+/, ""))}</li>`;
      } else if (line.startsWith("&gt; ")) {
        closeList();
        html += `<blockquote>${renderInline(line.slice(5))}</blockquote>`;
      } else {
        closeList();
        html += `<p>${renderInline(line)}</p>`;
      }
    }
    closeList();
    return html;
  }

  function toast(message) {
    const element = document.getElementById("toast");
    if (!element) return;
    clearTimeout(toastTimer);
    element.querySelector("span").textContent = message;
    element.classList.add("show");
    toastTimer = setTimeout(() => element.classList.remove("show"), 2600);
  }

  function readStored(key, fallback) {
    try {
      return JSON.parse(localStorage.getItem(key)) || fallback;
    } catch {
      return fallback;
    }
  }

  function clientContext() {
    const context = {
      profile: readStored("eat-health-profile-v2", {}),
      meals: readStored("eat-health-meals-v2", [])
    };
    const analysis = window.eatLocal?.getHealthAnalysis?.();
    if (analysis) {
      context.today = { score: analysis.score, status: analysis.status, nutrition: analysis.nutrition };
    }
    return context;
  }

  async function api(path, options = {}) {
    const response = await fetch(BASE + path, {
      headers: { "Content-Type": "application/json" },
      ...options
    });
    if (!response.ok) {
      let detail = "";
      try {
        const payload = await response.json();
        detail = payload.message || payload.error || "";
      } catch {
        detail = "";
      }
      throw new Error(detail || `HTTP ${response.status}`);
    }
    return response.json();
  }

  function updateConnection(text, mode) {
    const toolbar = $("#agentConnText");
    if (toolbar) toolbar.textContent = text;
    const bar = $(".agent-conn");
    if (bar) {
      bar.classList.toggle("online", mode === "online");
      bar.classList.toggle("offline", mode === "offline");
    }
    const agentState = $(".agent-state");
    if (agentState) {
      agentState.innerHTML = `<span class="status-dot"></span>${escapeHtml(text)}`;
    }
  }

  function mountToolbar() {
    const composer = document.getElementById("chatForm");
    if (!composer || document.getElementById("agentToolbar")) return;
    const toolbar = document.createElement("div");
    toolbar.id = "agentToolbar";
    toolbar.className = "agent-toolbar";
    toolbar.innerHTML = `
      <div class="agent-mode" role="group" aria-label="Agent 模式">
        <button type="button" class="agent-btn" data-mode="qa">问答</button>
        <div class="agent-work" id="agentWork">
          <button type="button" class="agent-btn agent-work-main" data-mode="work"><span id="agentWorkLabel">工作 · 手动审批</span></button>
          <button type="button" class="agent-work-caret" id="agentAccessToggle" aria-label="切换工作方式"><svg class="icon"><use href="#icon-chevron-right"></use></svg></button>
          <div class="agent-access-menu" id="agentAccessMenu">
            <button type="button" data-access="manual"><span>手动审批</span><small>重要操作由你确认</small></button>
            <button type="button" data-access="full"><span>完全访问</span><small>写操作直接执行</small></button>
          </div>
        </div>
      </div>
      <button type="button" class="agent-btn" id="agentNewSession">新对话</button>
      <button type="button" class="agent-btn agent-context-chip" id="agentContextButton"><span id="agentContextLabel">上下文 0%</span></button>
      <input type="file" id="agentImageInput" accept="image/*" multiple hidden>
      <span class="agent-conn"><i></i><span id="agentConnText">连接中</span></span>
    `;
    composer.parentElement.insertBefore(toolbar, composer);
    const preview = document.createElement("div");
    preview.id = "agentImagePreview";
    preview.className = "agent-image-preview";
    toolbar.insertAdjacentElement("afterend", preview);
    const expertChips = document.createElement("div");
    expertChips.id = "agentExpertChips";
    expertChips.className = "agent-expert-chips";
    preview.insertAdjacentElement("afterend", expertChips);
    toolbar.querySelectorAll("[data-mode]").forEach(button => {
      button.classList.toggle("active", button.dataset.mode === state.mode);
      button.addEventListener("click", () => applyMode(button.dataset.mode));
    });
    $("#agentAccessToggle", toolbar)?.addEventListener("click", event => {
      event.stopPropagation();
      toggleAccessMenu();
    });
    $("#agentAccessMenu", toolbar)?.querySelectorAll("[data-access]").forEach(button => {
      button.addEventListener("click", () => {
        setAccess(button.dataset.access);
        closeAccessMenu();
      });
    });
    document.addEventListener("click", event => {
      if (!event.target.closest(".agent-work")) closeAccessMenu();
    });
    $("#agentNewSession", toolbar)?.addEventListener("click", startNewSession);
    $("#agentContextButton", toolbar)?.addEventListener("click", () => toggleContextPanel());
    $("#agentImageInput", toolbar)?.addEventListener("change", handleImageFiles);
    document.getElementById("agentMarketEntry")?.addEventListener("click", openMarket);
    document.getElementById("photoButton")?.addEventListener("click", event => {
      if (!state.ready) return;
      event.preventDefault();
      event.stopImmediatePropagation();
      $("#agentImageInput", toolbar)?.click();
    }, true);
    bindComposer();
    updateWorkLabel();
    setInterval(() => { if (state.ready) refreshContextBadge(); }, 8000);
  }

  function updateInputCount() {
    const input = document.getElementById("chatInput");
    const counter = document.getElementById("inputCount");
    if (!input || !counter) return;
    const limit = Number(input.maxLength) > 0 ? Number(input.maxLength) : 2000;
    counter.textContent = `${input.value.length}/${limit}`;
    counter.classList.toggle("near-limit", input.value.length >= limit * 0.9);
  }

  function clearComposer() {
    const input = document.getElementById("chatInput");
    if (input) {
      input.value = "";
      input.style.height = "auto";
    }
    updateInputCount();
  }

  function bindComposer() {
    const input = document.getElementById("chatInput");
    if (!input || input.dataset.agentBound) return;
    input.dataset.agentBound = "1";
    input.addEventListener("input", updateInputCount);
    input.addEventListener("paste", handleImagePaste);
    updateInputCount();
  }

  function handleImageFiles(event) {
    const files = Array.from(event.target.files || []);
    event.target.value = "";
    addImageFiles(files);
  }

  function handleImagePaste(event) {
    const items = Array.from(event.clipboardData?.items || []);
    const files = items
      .filter(item => item.type && item.type.startsWith("image/"))
      .map(item => item.getAsFile())
      .filter(Boolean);
    if (!files.length) return;
    event.preventDefault();
    addImageFiles(files);
  }

  function addImageFiles(files) {
    const images = files.filter(file => file && file.type && file.type.startsWith("image/"));
    if (!images.length) return;
    const room = MAX_IMAGES - state.images.length;
    if (room <= 0) {
      toast(`最多上传 ${MAX_IMAGES} 张图片`);
      return;
    }
    if (images.length > room) toast(`最多上传 ${MAX_IMAGES} 张图片`);
    images.slice(0, room).forEach(file => {
      if (file.size > 2_000_000) {
        toast("单张图片不能超过 2MB");
        return;
      }
      const reader = new FileReader();
      reader.onload = () => {
        state.images.push(String(reader.result));
        renderImagePreview();
      };
      reader.readAsDataURL(file);
    });
  }

  function applyMode(mode) {
    state.mode = mode === "work" ? "work" : "qa";
    localStorage.setItem(STORE.mode, state.mode);
    document.querySelectorAll(".agent-mode [data-mode]").forEach(item => item.classList.toggle("active", item.dataset.mode === state.mode));
    updateWorkLabel();
    refreshContextBadge();
  }

  function updateWorkLabel() {
    const label = document.getElementById("agentWorkLabel");
    if (label) label.textContent = `工作 · ${state.access === "full" ? "完全访问" : "手动审批"}`;
    const menu = document.getElementById("agentAccessMenu");
    menu?.querySelectorAll("[data-access]").forEach(button => button.classList.toggle("active", button.dataset.access === state.access));
  }

  function toggleAccessMenu() {
    const menu = document.getElementById("agentAccessMenu");
    if (menu) menu.classList.toggle("open");
  }

  function closeAccessMenu() {
    const menu = document.getElementById("agentAccessMenu");
    if (menu) menu.classList.remove("open");
  }

  function openAccessSheet(reason = "settings") {
    const dialog = $("#agentAccessDialog");
    if (!dialog) return;
    syncAccessOptions();
    const note = $("#agentAccessNote");
    if (note) {
      note.textContent = reason === "switch"
        ? "工作模式会读取菜单并执行写操作，请选择审批方式"
        : "签到与早、中、晚餐上报始终需要你手动操作";
    }
    if (!dialog.open) dialog.showModal();
  }

  function syncAccessOptions() {
    document.querySelectorAll("#agentAccessDialog [data-access]").forEach(button => {
      button.classList.toggle("active", button.dataset.access === state.access);
    });
  }

  function setAccess(level) {
    state.access = level === "full" ? "full" : "manual";
    localStorage.setItem(STORE.access, state.access);
    updateWorkLabel();
    syncAccessOptions();
    $("#agentAccessDialog")?.close();
    toast(state.access === "full" ? "已切换为完全访问" : "已切换为手动审批");
    refreshContextBadge();
  }

  function contextQuery() {
    const params = new URLSearchParams({
      session_id: state.sessionId,
      user_id: state.userId,
      mode: state.mode,
      access_level: state.access,
      tone: state.tone
    });
    if (state.modelId) params.set("model_id", state.modelId);
    return params.toString();
  }

  async function contextStats() {
    return api(`/api/context?${contextQuery()}`);
  }

  async function refreshContextBadge() {
    const button = document.getElementById("agentContextButton");
    if (!button || !state.ready) return;
    try {
      const stats = await contextStats();
      const label = document.getElementById("agentContextLabel");
      if (label) label.textContent = `上下文 ${stats.percent}%`;
      button.dataset.percent = String(stats.percent);
      button.style.setProperty("--context-progress", String(stats.percent));
    } catch {
      const label = document.getElementById("agentContextLabel");
      if (label) label.textContent = "上下文";
    }
  }

  function toggleContextPanel() {
    const panel = $("#agentContextPanel");
    if (panel?.open) {
      closeContextPanel();
      return;
    }
    panel?.classList.remove("closing");
    openContextPanel();
  }

  function closeContextPanel() {
    const panel = $("#agentContextPanel");
    if (!panel?.open) return;
    panel.classList.add("closing");
    setTimeout(() => {
      panel.classList.remove("closing");
      panel.close();
    }, 280);
  }

  async function openContextPanel() {
    const panel = $("#agentContextPanel");
    if (!panel) return;
    const button = document.getElementById("agentContextButton");
    if (button) {
      const rect = button.getBoundingClientRect();
      const width = Math.min(380, window.innerWidth - 24);
      const left = Math.max(12, Math.min(rect.left, window.innerWidth - width - 12));
      panel.style.width = `${width}px`;
      panel.style.left = `${left}px`;
      panel.style.bottom = `${window.innerHeight - rect.top + 10}px`;
      panel.style.transformOrigin = `${rect.left + rect.width / 2 - left}px 100%`;
    }
    if (!panel.open) panel.show();
    await renderContextPanel();
  }

  async function renderContextPanel() {
    const percentNode = $("#agentContextPercent");
    const usedNode = $("#agentContextUsed");
    const bar = $("#agentContextBar");
    const legend = $("#agentContextLegend");
    if (!bar || !legend) return;
    bar.innerHTML = `<span class="agent-context-loading">正在统计</span>`;
    legend.innerHTML = "";
    try {
      const stats = await contextStats();
      state.contextStats = stats;
      if (percentNode) percentNode.textContent = `${stats.percent}%`;
      if (usedNode) {
        const windowLabel = stats.window >= 1_000_000
          ? `${Math.round(stats.window / 100000) / 10}M`
          : `${Math.round(stats.window / 1000)}K`;
        usedNode.textContent = `已使用 ${Math.round(stats.used / 100) / 10}K / ${windowLabel}`;
      }
      const total = Math.max(1, stats.used);
      bar.innerHTML = stats.segments
        .filter(item => item.tokens > 0)
        .map(item => `<i data-segment="${item.id}" style="flex:${Math.max(1, Math.round(item.tokens / total * 100))} 0 0" title="${escapeHtml(item.label)} ${item.tokens}"></i>`)
        .join("");
      legend.innerHTML = stats.segments.map(item => `
        <li><i data-segment="${item.id}"></i><span>${escapeHtml(item.label)}</span><strong>${item.tokens ? Math.round(item.tokens / total * 1000) / 10 : 0}%</strong></li>
      `).join("");
      const compact = $("#agentContextCompact");
      if (compact) compact.disabled = !stats.compactable;
    } catch (error) {
      bar.innerHTML = `<span class="agent-context-loading">统计失败：${escapeHtml(error.message)}</span>`;
    }
  }

  async function compactContext() {
    const button = $("#agentContextCompact");
    if (button) button.disabled = true;
    try {
      const data = await api("/api/context/compact", {
        method: "POST",
        body: JSON.stringify({
          session_id: state.sessionId,
          user_id: state.userId,
          mode: state.mode,
          access_level: state.access,
          model_id: state.modelId || undefined
        })
      });
      toast(data.saved > 0 ? `已压缩 ${data.saved} tokens` : "当前没有可压缩的早期对话");
      await renderContextPanel();
      refreshContextBadge();
    } catch (error) {
      toast(`压缩失败：${error.message}`);
    } finally {
      if (button) button.disabled = false;
    }
  }

  function selectedExperts() {
    return state.experts.filter(item => state.expertIds.includes(item.id));
  }

  function updateExpertLabel() {
    const button = document.getElementById("agentExpertButton");
    if (!button) return;
    const selected = selectedExperts();
    button.textContent = selected.length === 0
      ? "专家"
      : selected.length === 1
        ? (selected[0].display_name || selected[0].name)
        : `专家 ${selected.length}`;
    button.classList.toggle("has-expert", selected.length > 0);
    updateExpertChips();
  }

  function updateExpertChips() {
    const container = document.getElementById("agentExpertChips");
    if (!container) return;
    const selected = selectedExperts();
    container.classList.toggle("has-chips", selected.length > 0);
    container.innerHTML = selected.map(skill => `
      <span class="agent-expert-chip">
        <i>${escapeHtml((skill.display_name || skill.name || "专").slice(0, 1))}</i>
        <span>${escapeHtml(skill.display_name || skill.name)}</span>
        <button type="button" data-remove-expert="${escapeHtml(skill.id)}" aria-label="移除专家">×</button>
      </span>
    `).join("");
    container.querySelectorAll("[data-remove-expert]").forEach(button => {
      button.addEventListener("click", () => toggleExpert(button.dataset.removeExpert));
    });
  }

  async function loadExperts() {
    if (state.experts.length) return state.experts;
    try {
      const data = await api(`/api/skills?mode=work&user_id=${encodeURIComponent(state.userId)}`);
      state.experts = data.skills || [];
    } catch {
      state.experts = [];
    }
    updateExpertChips();
    return state.experts;
  }

  function renderExpertList(experts) {
    const list = $("#agentExpertList");
    if (!list) return;
    list.innerHTML = `
      <button type="button" class="agent-expert-option ${state.expertIds.length ? "" : "active"}" data-expert=""><span class="agent-expert-mark">自</span><span class="agent-expert-copy"><strong>自动选择</strong><small>按消息匹配最合适的技能</small></span></button>
      ${experts.map(skill => `
        <button type="button" class="agent-expert-option ${state.expertIds.includes(skill.id) ? "active" : ""}" data-expert="${escapeHtml(skill.id)}">
          <span class="agent-expert-mark">${escapeHtml((skill.display_name || skill.name || "专").slice(0, 1))}</span>
          <span class="agent-expert-copy"><strong>${escapeHtml(skill.display_name || skill.name)}</strong><small>${escapeHtml(skill.description || "")}${skill.source === "user" ? " · 已安装" : ""}</small></span>
          <span class="agent-expert-check">${state.expertIds.includes(skill.id) ? "已选" : ""}</span>
        </button>
      `).join("")}
      <p class="agent-expert-hint">可多选组合，点击外部关闭</p>
    `;
    list.querySelectorAll("[data-expert]").forEach(button => {
      button.addEventListener("click", () => toggleExpert(button.dataset.expert));
    });
  }

  async function openExpertMenu() {
    const menu = $("#agentExpertMenu");
    if (!menu) return;
    if (menu.classList.contains("open")) {
      menu.classList.remove("open");
      return;
    }
    const experts = await loadExperts();
    renderExpertList(experts);
    const anchor = document.getElementById("agentExpertButton");
    const rect = anchor?.getBoundingClientRect();
    menu.style.left = `${Math.max(12, rect ? rect.left : 12)}px`;
    menu.style.top = `${rect ? rect.bottom + 8 : 80}px`;
    menu.classList.add("open");
  }

  function toggleExpert(id) {
    if (!id) {
      state.expertIds = [];
    } else if (state.expertIds.includes(id)) {
      state.expertIds = state.expertIds.filter(item => item !== id);
    } else {
      state.expertIds = [...state.expertIds, id];
    }
    localStorage.setItem(STORE.expert, JSON.stringify(state.expertIds));
    updateExpertLabel();
    if ($("#agentExpertMenu")?.classList.contains("open")) renderExpertList(state.experts);
    toast(state.expertIds.length ? `已选择 ${state.expertIds.length} 位专家` : "已切换为自动匹配");
  }

  function renderImagePreview() {
    const preview = $("#agentImagePreview");
    if (!preview) return;
    preview.classList.toggle("has-images", state.images.length > 0);
    preview.innerHTML = state.images.map((source, index) => `
      <div class="agent-image-thumb">
        <img src="${source}" alt="待发送图片">
        <button type="button" data-remove-image="${index}" aria-label="移除图片">×</button>
      </div>
    `).join("");
    preview.querySelectorAll("[data-remove-image]").forEach(button => {
      button.addEventListener("click", () => {
        state.images.splice(Number(button.dataset.removeImage), 1);
        renderImagePreview();
      });
    });
  }

  function mountDialogs() {
    if (document.getElementById("agentConfirmDialog")) return;
    const confirmDialog = document.createElement("dialog");
    confirmDialog.id = "agentConfirmDialog";
    confirmDialog.className = "agent-dialog";
    confirmDialog.innerHTML = `
      <form method="dialog" class="agent-dialog-body">
        <header><div><span class="section-kicker">需要确认</span><h2>执行这项操作？</h2></div></header>
        <p id="agentConfirmText" class="agent-muted"></p>
        <div id="agentConfirmTools" class="agent-confirm-tools"></div>
        <footer>
          <button value="cancel" class="button secondary-button" type="submit">取消</button>
          <button value="confirm" class="button primary-button" type="submit">确认执行</button>
        </footer>
      </form>
    `;
    const marketDialog = document.createElement("dialog");
    marketDialog.id = "agentSkillDialog";
    marketDialog.className = "agent-dialog agent-market-dialog";
    marketDialog.innerHTML = `
      <form method="dialog" class="agent-market-body">
        <header><div><span class="section-kicker">能力扩展</span><h2>Skill 市场</h2></div><button class="icon-button" id="agentSkillClose" type="button" aria-label="关闭">×</button></header>
        <div class="agent-market-toolbar">
          <label class="agent-market-search"><svg class="icon"><use href="#icon-search"></use></svg><input id="agentMarketSearch" type="search" placeholder="搜索名称、作者或标签" aria-label="搜索技能"></label>
          <div class="agent-market-tabs" id="agentMarketTabs" role="group" aria-label="技能分类">
            <button type="button" data-category="" class="active">全部</button>
            <button type="button" data-category="health">健康</button>
            <button type="button" data-category="fitness">健身</button>
            <button type="button" data-category="nutrition">营养</button>
            <button type="button" data-category="planning">规划</button>
          </div>
          <button type="button" class="agent-chip" id="agentMarketImport">导入 SKILL.md</button>
          <input id="agentSkillFile" type="file" accept=".md,text/markdown,text/plain" hidden>
        </div>
        <div id="agentSkillList" class="agent-market-list"></div>
        <section class="agent-market-installed">
          <h3>已安装</h3>
          <div id="agentInstalledList"></div>
        </section>
        <footer><span id="agentSkillHint" class="agent-muted">安装市场技能需要工作模式</span></footer>
      </form>
    `;
    const accessDialog = document.createElement("dialog");
    accessDialog.id = "agentAccessDialog";
    accessDialog.className = "agent-access-sheet";
    accessDialog.innerHTML = `
      <form method="dialog" class="agent-access-body">
        <header><div><span class="section-kicker">工作模式</span><h2>选择访问权限</h2></div><button type="button" id="agentAccessClose" aria-label="关闭">×</button></header>
        <div class="agent-access-options">
          <button type="button" data-access="manual"><span class="agent-access-mark">手</span><span><strong>手动审批</strong><small>重要操作由你确认</small></span></button>
          <button type="button" data-access="full"><span class="agent-access-mark">全</span><span><strong>完全访问</strong><small>写操作直接执行，三餐上报仍需确认</small></span></button>
        </div>
        <p class="agent-access-note" id="agentAccessNote">签到与早、中、晚餐上报始终需要你手动操作</p>
      </form>
    `;
    const contextPanel = document.createElement("dialog");
    contextPanel.id = "agentContextPanel";
    contextPanel.className = "agent-context-panel";
    contextPanel.innerHTML = `
      <form method="dialog" class="agent-context-body">
        <header><div><span class="section-kicker">会话资源</span><h2>上下文使用率</h2></div><button type="button" id="agentContextClose" aria-label="关闭">×</button></header>
        <div class="agent-context-summary"><strong id="agentContextPercent">0%</strong><span id="agentContextUsed">已使用 0 / 64K</span></div>
        <div class="agent-context-bar" id="agentContextBar"></div>
        <ul class="agent-context-legend" id="agentContextLegend"></ul>
        <footer><button type="button" class="button primary-button" id="agentContextCompact">压缩上下文</button><small>保留最近 6 条，压缩早期对话</small></footer>
      </form>
    `;
    const expertMenu = document.createElement("div");
    expertMenu.id = "agentExpertMenu";
    expertMenu.className = "agent-expert-menu";
    expertMenu.innerHTML = `<div class="agent-expert-head">选择专家</div><div id="agentExpertList" class="agent-expert-list"></div>`;
    document.body.append(confirmDialog, marketDialog, accessDialog, contextPanel, expertMenu);
    $("#agentSkillClose")?.addEventListener("click", () => marketDialog.close());
    $("#agentMarketSearch")?.addEventListener("input", () => {
      clearTimeout(state.marketTimer);
      state.marketTimer = setTimeout(() => openMarket(true), 220);
    });
    $("#agentMarketTabs")?.addEventListener("click", event => {
      const button = event.target.closest("button[data-category]");
      if (!button) return;
      $("#agentMarketTabs").querySelectorAll("button").forEach(item => item.classList.toggle("active", item === button));
      state.marketCategory = button.dataset.category || "";
      openMarket(true);
    });
    $("#agentMarketImport")?.addEventListener("click", () => {

      $("#agentSkillFile")?.click();
    });
    $("#agentSkillFile")?.addEventListener("change", importSkillFile);
    accessDialog.querySelectorAll("[data-access]").forEach(button => {
      button.addEventListener("click", () => setAccess(button.dataset.access));
    });
    $("#agentAccessClose")?.addEventListener("click", () => accessDialog.close());
    $("#agentContextClose")?.addEventListener("click", () => closeContextPanel());
    $("#agentContextCompact")?.addEventListener("click", compactContext);
    document.addEventListener("click", event => {
      if (!contextPanel.open) return;
      if (contextPanel.contains(event.target) || event.target.closest("#agentContextButton")) return;
      closeContextPanel();
    });
    document.addEventListener("click", event => {
      const menu = document.getElementById("agentExpertMenu");
      if (!menu?.classList.contains("open")) return;
      if (menu.contains(event.target) || event.target.closest("#agentExpertButton")) return;
      menu.classList.remove("open");
    });
    confirmDialog.addEventListener("close", () => {
      if (confirmDialog.returnValue !== "confirm" || !state.pending) return;
      const pending = state.pending;
      state.pending = null;
      state.sending = false;
      send(pending.text, { confirmedTools: pending.tools, article: pending.article, appendUser: false });
    });
  }

  async function loadModels() {
    try {
      const data = await api("/api/models");
      state.models = data.models || [];
      const saved = state.models.find(model => model.id === state.modelId && model.available);
      const preferred = saved || state.models.find(model => model.id === data.default) || state.models.find(model => model.available);
      if (preferred) {
        state.modelId = preferred.id;
        localStorage.setItem(STORE.model, state.modelId);
      }
      updateModelLabel();
      renderModelMenu();
    } catch {
      toast("模型列表获取失败");
    }
  }

  async function loadMeta() {
    try {
      const meta = await api("/api/agent/meta");
      state.meta = meta;
      state.ready = true;
      const source = meta.service?.mode === "service" ? "服务端" : "mock";
      const mcpOnline = Object.values(meta.mcp?.servers || {}).some(server => server.running);
      updateConnection(`Agent 在线 · ${source}${mcpOnline ? " · MCP" : ""}`, "online");
      updateWorkLabel();
      updateExpertLabel();
      loadExperts().then(() => updateExpertLabel());
      refreshContextBadge();
    } catch {
      state.ready = false;
      updateConnection("本地模式", "offline");
    }
  }

  function appendUser(text, images = []) {
    const conversation = document.getElementById("conversation");
    if (!conversation) return null;
    const article = document.createElement("article");
    article.className = "message user-message";
    const bubble = document.createElement("div");
    bubble.className = "message-bubble";
    if (text) {
      const paragraph = document.createElement("p");
      paragraph.textContent = text;
      bubble.appendChild(paragraph);
    }
    if (images.length) {
      const media = document.createElement("div");
      media.className = "message-images";
      images.forEach(source => {
        const image = document.createElement("img");
        image.src = source;
        image.alt = "用户上传的图片";
        media.appendChild(image);
      });
      bubble.appendChild(media);
    }
    article.appendChild(bubble);
    const anchor = document.getElementById("quickPrompts");
    conversation.insertBefore(article, anchor || null);
    conversation.scrollTo({ top: conversation.scrollHeight, behavior: "smooth" });
    state.lastUser = article;
    return article;
  }

  function createAgentMessage() {
    const conversation = document.getElementById("conversation");
    if (!conversation) return null;
    const article = document.createElement("article");
    article.className = "message agent-message typing-message";
    article.innerHTML = `
      <div class="agent-avatar"><svg class="icon"><use href="#icon-utensils"></use></svg></div>
      <div class="message-content">
        <div class="message-meta"><strong>饭饭</strong><span class="agent-status">正在连接 Agent</span></div>
        <div class="message-bubble"><p></p></div>
      </div>
    `;
    const anchor = document.getElementById("quickPrompts");
    conversation.insertBefore(article, anchor || null);
    conversation.scrollTo({ top: conversation.scrollHeight, behavior: "smooth" });
    return article;
  }

  function removeNode(node) {
    if (node && node.parentElement) node.remove();
  }

  function renderCitations(article, citations) {
    if (!article || !citations.length) return;
    const content = $(".message-content", article);
    if (!content) return;
    const details = document.createElement("details");
    details.className = "agent-citations";
    details.innerHTML = `
      <summary>参考来源（${citations.length}）</summary>
      <div class="agent-citation-list">
        ${citations.map(item => `<p><strong>${escapeHtml(item.title || "知识库")}</strong><span>${escapeHtml(String(item.text || "").slice(0, 180))}</span></p>`).join("")}
      </div>
    `;
    content.appendChild(details);
  }

  async function consume(stream, onEvent) {
    const reader = stream.getReader();
    const decoder = new TextDecoder();
    let buffer = "";
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      buffer += decoder.decode(value, { stream: true });
      let index;
      while ((index = buffer.indexOf("\n\n")) >= 0) {
        const frame = buffer.slice(0, index);
        buffer = buffer.slice(index + 2);
        let event = "message";
        const dataLines = [];
        for (const line of frame.split("\n")) {
          if (line.startsWith("event:")) event = line.slice(6).trim();
          else if (line.startsWith("data:")) dataLines.push(line.slice(5).trim());
        }
        if (!dataLines.length) continue;
        try {
          onEvent(event, JSON.parse(dataLines.join("\n")));
        } catch {
          continue;
        }
      }
    }
  }

  const TOOL_LABELS = {
    get_health_profile: "正在查询健康档案",
    get_recent_meals: "正在读取饮食记录",
    get_nutrition_gap: "正在计算营养缺口",
    search_foods: "正在筛选菜品",
    get_menu: "正在读取实时菜单",
    get_weekly_stats: "正在汇总本周数据",
    get_friend_ranking: "正在读取好友排名",
    record_meal: "正在准备写入饮食记录",
    list_skills: "正在读取技能列表",
    install_skill: "正在安装技能",
    import_skill: "正在导入技能",
    get_agent_status: "正在检查 Agent 状态"
  };

  async function send(text, options = {}) {
    if (state.sending) {
      toast("正在回答，请稍等");
      return;
    }
    const images = options.images || state.images || [];
    let userArticle = null;
    if (options.appendUser !== false) userArticle = appendUser(text, images);
    const article = options.article || createAgentMessage();
    if (!article) return;
    const bubble = $(".message-bubble", article);
    const status = $(".agent-status", article);
    let answerText = "";
    bubble.innerHTML = "";
bubble.classList.add("is-streaming");
    status.textContent = "正在思考";
    state.sending = true;
    state.images = [];
    renderImagePreview();
    clearComposer();
    const citations = [];
    recordHistory("user", text || "请分析图片");

    try {
      const response = await fetch(`${BASE}/api/chat/stream`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          user_id: state.userId,
          session_id: state.sessionId,
          message: text,
          mode: state.mode,
          model_id: state.modelId || undefined,
          tone: state.tone,
          skill_id: state.expertIds[0] || undefined,
          skill_ids: state.expertIds,
          access_level: state.access,
          confirmed_tools: options.confirmedTools || [],
          client_context: clientContext(),
          images
        })
      });
      if (!response.ok || !response.body) throw new Error(`HTTP ${response.status}`);

      await consume(response.body, (event, data) => {
        if (event === "meta") {
          const model = data.model && data.model !== "auto" ? data.model : state.modelId || "本地规则";
          status.textContent = `${data.mode_name || "问答"} · ${model}`;
        } else if (event === "worker_start") {
          status.textContent = "已分派 Worker";
        } else if (event === "plan") {
          status.textContent = `计划：${data.task_type || "处理中"}`;
        } else if (event === "skill") {
          status.textContent = `技能：${data.display_name || data.id}`;
        } else if (event === "tool_call") {
          status.textContent = TOOL_LABELS[data.name] || `正在执行 ${data.name}`;
        } else if (event === "tool_result") {
          if (data.need_confirm) status.textContent = "等待你的确认";
          else status.textContent = data.ok ? "已完成查询" : "查询失败，继续整理";
          if (data.name === "update_health_profile" && data.ok) syncLocalProfile(data.data?.profile);
        } else if (event === "token") {
          answerText += data.text || "";
          bubble.innerHTML = renderMarkdown(answerText);
          const conversation = document.getElementById("conversation");
          conversation?.scrollTo({ top: conversation.scrollHeight, behavior: "smooth" });
        } else if (event === "citation") {
          citations.push(data);
        } else if (event === "notice") {
          toast(data.message || "已切换本地模型");
        } else if (event === "replan") {
          toast(`已重新规划：${data.failed_tool || "工具失败"}`);
        } else if (event === "watchdog") {
          toast(data.message || "后台任务已超时");
        } else if (event === "confirm_required") {
          state.pending = {
            text,
            tools: data.confirmed_tools || [],
            article
          };
          const dialog = $("#agentConfirmDialog");
          const textNode = $("#agentConfirmText");
          const toolsNode = $("#agentConfirmTools");
          if (textNode) textNode.textContent = "会修改你的数据，确认后生效。";
          if (toolsNode) toolsNode.innerHTML = (data.confirmed_tools || []).map(tool => `<span>${escapeHtml(tool)}</span>`).join("");
          if (dialog && !dialog.open) dialog.showModal();
          status.textContent = "等待你的确认";
        } else if (event === "error") {
          status.textContent = "出错了";
          answerText = data.message || "Agent 返回了错误";
          bubble.innerHTML = renderMarkdown(answerText);
        } else if (event === "done") {
          article.classList.remove("typing-message");
          bubble.classList.remove("is-streaming");
          if (!citations.length) status.textContent = "刚刚";
          refreshContextBadge();
        }
      });

      if (!answerText.trim()) {
        answerText = "我这轮没有取到足够的信息。你可以告诉我具体餐次，我再查一次。";
        bubble.innerHTML = renderMarkdown(answerText);
      }
      recordHistory("agent", answerText);
      renderCitations(article, citations);
      article.classList.remove("typing-message");
      bubble.classList.remove("is-streaming");
    } catch (error) {
      removeNode(article);
      removeNode(userArticle);
      state.ready = false;
      updateConnection("本地模式", "offline");
      toast("Agent 不可用，已切回本地分析");
      window.eatAgent.fallback = true;
      if (text) {
        window.eatLocal?.sendPrompt?.(text);
      }
    } finally {
      state.sending = false;
    }
  }

  const HISTORY_KEY = "eat-agent-history";

  function currentModelName() {
    const model = state.models.find(item => item.id === state.modelId);
    return model ? model.name : "模型";
  }

  function updateModelLabel() {
    const label = document.getElementById("profileModelLabel");
    if (label) label.textContent = currentModelName();
  }

  function ensureModelMenu() {
    let menu = document.getElementById("agentModelMenu");
    if (menu) return menu;
    menu = document.createElement("div");
    menu.id = "agentModelMenu";
    menu.className = "agent-model-menu";
    menu.innerHTML = `<div class="agent-model-menu-head">选择模型</div><div id="agentModelList"></div>`;
    document.body.append(menu);
    document.addEventListener("click", event => {
      if (menu.contains(event.target) || event.target.closest("#profileShortcut")) return;
      menu.classList.remove("open");
    });
    return menu;
  }

  function syncLocalProfile(profile) {
    if (!profile || typeof profile !== "object") return;
    const key = "eat-health-profile-v2";
    let next = {};
    try {
      next = JSON.parse(localStorage.getItem(key) || "{}");
    } catch {
      next = {};
    }
    ["height", "weight", "age"].forEach(field => {
      const value = Number(profile[field]);
      if (profile[field] != null && !Number.isNaN(value)) next[field] = value;
    });
    if (profile.water_target != null) next.waterTarget = Number(profile.water_target);
    if (profile.sleep_hours != null) next.sleep = Number(profile.sleep_hours);
    ["name", "goal", "activity", "note", "gender", "diet_style"].forEach(field => {
      if (profile[field] != null) next[field] = String(profile[field]);
    });
    localStorage.setItem(key, JSON.stringify(next));
    toast("档案已更新，刷新页面后档案页同步显示");
  }

  function mountToneSetting() {
    const dialog = document.getElementById("settingsDialog");
    const content = dialog?.querySelector(".dialog-content");
    if (!content || document.getElementById("agentToneRow")) return;
    const options = [
      { id: "normal", label: "正常" },
      { id: "gentle", label: "温和" },
      { id: "efficient", label: "效率" }
    ];
    if (!options.some(item => item.id === state.tone)) {
      state.tone = "normal";
      localStorage.setItem(STORE.tone, state.tone);
    }
    const row = document.createElement("div");
    row.className = "setting-row";
    row.id = "agentToneRow";
    row.innerHTML = `<span><strong>回答语气</strong><small>决定饭饭的表达方式，默认正常</small></span><div class="agent-tone-options" role="group" aria-label="回答语气">${options.map(item => `<button type="button" data-tone="${item.id}">${item.label}</button>`).join("")}</div>`;
    content.prepend(row);
    const buttons = row.querySelectorAll("[data-tone]");
    const sync = () => buttons.forEach(button => button.classList.toggle("active", button.dataset.tone === state.tone));
    buttons.forEach(button => {
      button.addEventListener("click", () => {
        state.tone = button.dataset.tone;
        localStorage.setItem(STORE.tone, state.tone);
        sync();
        toast(`回答语气：${button.textContent}`);
      });
    });
    sync();
  }

  function renderModelMenu() {
    const menu = ensureModelMenu();
    const list = menu.querySelector("#agentModelList");
    if (!list) return;
    list.innerHTML = state.models.length
      ? state.models.map(model => {
        const real = model.real_model || model.model || model.id;
        const detail = [real, model.available ? "" : "未配置 Key"].filter(Boolean).join(" · ");
        return `<button type="button" class="agent-model-option ${model.id === state.modelId ? "active" : ""}" data-model-id="${escapeHtml(model.id)}" ${model.available ? "" : "disabled"}><span class="agent-model-name">${escapeHtml(model.name)}</span><small class="agent-model-meta">${escapeHtml(detail)}</small></button>`;
      }).join("")
      : `<p class="agent-muted">暂无可用模型</p>`;
    list.querySelectorAll("[data-model-id]").forEach(button => {
      button.addEventListener("click", () => {
        if (button.disabled) return;
        state.modelId = button.dataset.modelId;
        localStorage.setItem(STORE.model, state.modelId);
        updateModelLabel();
        renderModelMenu();
        menu.classList.remove("open");
        toast(`已切换模型：${button.textContent}`);
      });
    });
  }

  function openModelMenu() {
    const menu = ensureModelMenu();
    renderModelMenu();
    const button = document.getElementById("profileShortcut");
    if (!button) return;
    const rect = button.getBoundingClientRect();
    menu.style.left = `${Math.max(12, rect.left)}px`;
    menu.style.bottom = `${window.innerHeight - rect.top + 8}px`;
    menu.classList.add("open");
  }

  function formatHistoryTime(timestamp) {
    const date = new Date(timestamp);
    const pad = value => String(value).padStart(2, "0");
    return `${date.getMonth() + 1}/${date.getDate()} ${pad(date.getHours())}:${pad(date.getMinutes())}`;
  }

  function loadHistorySessions() {
    const list = readStored(HISTORY_KEY, []);
    return Array.isArray(list) ? list : [];
  }

  function saveHistorySessions(list) {
    try {
      localStorage.setItem(HISTORY_KEY, JSON.stringify(list.slice(0, 60)));
    } catch {}
  }

  function recordHistory(role, text) {
    const value = String(text || "").trim();
    if (!value) return;
    const list = loadHistorySessions();
    let session = list.find(item => item.id === state.sessionId);
    if (!session) {
      session = { id: state.sessionId, title: role === "user" ? value.slice(0, 24) : "新对话", time: Date.now(), messages: [] };
      list.unshift(session);
    }
    session.messages.push({ role, text: value.slice(0, 4000), time: Date.now() });
    if (role === "user" && (!session.title || session.title === "新对话")) session.title = value.slice(0, 24);
    session.time = Date.now();
    saveHistorySessions(list);
    renderHistoryList(document.getElementById("agentHistorySearch")?.value || "");
  }

  function closeHistoryPanel() {
    const panel = document.getElementById("agentHistoryPanel");
    if (panel) panel.classList.remove("open");
    const entry = document.getElementById("agentHistoryEntry");
    if (entry) entry.style.display = "";
  }

  function ensureHistoryPanel() {
    let panel = document.getElementById("agentHistoryPanel");
    if (panel) return panel;
    panel = document.createElement("aside");
    panel.id = "agentHistoryPanel";
    panel.className = "agent-history";
    panel.innerHTML = `
      <header class="agent-history-head"><div class="agent-history-head-left"><button class="icon-button" id="agentHistoryCollapse" type="button" aria-label="缩回历史对话"><svg class="icon"><use href="#icon-chevron-right"></use></svg></button><div><span class="section-kicker">对话历史</span><h2>历史对话</h2></div></div><button class="icon-button" id="agentHistoryClose" type="button" aria-label="关闭历史对话"><svg class="icon"><use href="#icon-x"></use></svg></button></header>
      <div class="agent-history-search"><svg class="icon"><use href="#icon-search"></use></svg><input id="agentHistorySearch" type="search" placeholder="搜索历史对话" aria-label="搜索历史对话"></div>
      <div class="agent-history-list" id="agentHistoryList"></div>
    `;
    document.body.append(panel);
    panel.querySelector("#agentHistoryCollapse").addEventListener("click", closeHistoryPanel);
    panel.querySelector("#agentHistoryClose").addEventListener("click", closeHistoryPanel);
    panel.querySelector("#agentHistorySearch").addEventListener("input", event => renderHistoryList(event.target.value));
    return panel;
  }

  function renderHistoryList(query = "") {
    const listNode = document.getElementById("agentHistoryList");
    if (!listNode) return;
    const keyword = String(query || "").trim().toLowerCase();
    const items = loadHistorySessions()
      .filter(session => {
        if (!keyword) return true;
        const haystack = [session.title, ...(session.messages || []).map(message => message.text)].join(" ").toLowerCase();
        return haystack.includes(keyword);
      })
      .sort((a, b) => b.time - a.time);
    if (!items.length) {
      listNode.innerHTML = `<p class="agent-muted">${keyword ? "没有匹配的历史对话" : "还没有历史对话"}</p>`;
      return;
    }
    listNode.innerHTML = items.map(session => `<button type="button" class="agent-history-item ${session.id === state.sessionId ? "active" : ""}" data-session-id="${escapeHtml(session.id)}"><strong>${escapeHtml(session.title || "未命名对话")}</strong><time>${formatHistoryTime(session.time)}</time></button>`).join("");
    listNode.querySelectorAll("[data-session-id]").forEach(button => {
      button.addEventListener("click", () => openHistorySession(button.dataset.sessionId));
    });
  }

  function openHistorySession(id) {
    const session = loadHistorySessions().find(item => item.id === id);
    if (!session) return;
    state.sessionId = session.id;
    localStorage.setItem(STORE.session, state.sessionId);
    const conversation = document.getElementById("conversation");
    if (conversation) {
      conversation.querySelectorAll(".message").forEach(node => node.remove());
      const anchor = document.getElementById("quickPrompts");
      session.messages.forEach(message => {
        const article = document.createElement("article");
        if (message.role === "user") {
          article.className = "message user-message";
          article.innerHTML = `<div class="message-bubble"><p>${escapeHtml(message.text)}</p></div>`;
        } else {
          article.className = "message agent-message";
          article.innerHTML = `<div class="agent-avatar"><svg class="icon"><use href="#icon-utensils"></use></svg></div><div class="message-content"><div class="message-meta"><strong>饭饭</strong><span>历史回复</span></div><div class="message-bubble"><p>${escapeHtml(message.text)}</p></div></div>`;
        }
        conversation.insertBefore(article, anchor || null);
      });
      conversation.scrollTo({ top: conversation.scrollHeight });
    }
    closeHistoryPanel();
    toast(`已切换到：${session.title || "历史对话"}`);
  }

  function toggleHistoryPanel() {
    const panel = ensureHistoryPanel();
    const entry = document.getElementById("agentHistoryEntry");
    if (panel.classList.contains("open")) {
      closeHistoryPanel();
      return;
    }
    renderHistoryList(document.getElementById("agentHistorySearch")?.value || "");
    panel.classList.add("open");
    if (entry) entry.style.display = "none";
  }

  function archiveCurrentSession() {
    const conversation = document.getElementById("conversation");
    if (!conversation) return;
    const nodes = [...conversation.querySelectorAll(".message")].slice(1);
    const entries = nodes.map(node => {
      const bubble = node.querySelector(".message-bubble");
      const text = bubble ? bubble.textContent.trim() : "";
      return { role: node.classList.contains("user-message") ? "user" : "agent", text, time: Date.now() };
    }).filter(entry => entry.text && !/正在|连接 Agent/.test(entry.text));
    if (!entries.length) return;
    const list = loadHistorySessions();
    let session = list.find(item => item.id === state.sessionId);
    if (!session) {
      session = { id: state.sessionId, title: entries[0].text.slice(0, 24), time: Date.now(), messages: [] };
      list.unshift(session);
    }
    entries.forEach(entry => {
      if (!session.messages.some(item => item.role === entry.role && item.text === entry.text)) session.messages.push(entry);
    });
    session.time = Date.now();
    saveHistorySessions(list);
  }

  function startNewSession() {
    archiveCurrentSession();
    state.sessionId = `web_${Date.now().toString(36)}`;
    localStorage.setItem(STORE.session, state.sessionId);
    state.images = [];
    renderImagePreview();
    const conversation = document.getElementById("conversation");
    if (conversation) [...conversation.querySelectorAll(".message")].slice(1).forEach(node => node.remove());
    renderHistoryList(document.getElementById("agentHistorySearch")?.value || "");
    toast("已开始新对话");
  }

  function ensureMessageRails() {
    let rails = document.getElementById("messageRails");
    if (rails) return rails;
    rails = document.createElement("div");
    rails.id = "messageRails";
    rails.className = "message-rails";
    const host = document.getElementById("view-today") || document.getElementById("mainContent") || document.body;
    host.append(rails);
    return rails;
  }

  function renderMessageRails() {
    const rails = ensureMessageRails();
    const users = [...document.querySelectorAll("#conversation .user-message")];
    rails.innerHTML = users.map((node, index) => {
      const text = node.querySelector(".message-bubble")?.textContent?.trim() || "";
      return `<button type="button" class="message-rail" data-rail-index="${index}"><span>${escapeHtml(text.slice(0, 80))}</span></button>`;
    }).join("");
    rails.querySelectorAll("[data-rail-index]").forEach(button => {
      button.addEventListener("click", () => {

        users[Number(button.dataset.railIndex)]?.scrollIntoView({ behavior: "smooth", block: "center" });
      });
    });
    applyAgentAvatar();
  }

  function observeMessages() {
    const conversation = document.getElementById("conversation");
    if (!conversation || conversation.dataset.railsObserved) return;
    conversation.dataset.railsObserved = "1";
    new MutationObserver(() => renderMessageRails()).observe(conversation, { childList: true });
  }

  function applyAgentAvatar() {
    let src = "";
    try {
      const saved = JSON.parse(localStorage.getItem("eat-agent-avatar") || "null");
      src = saved?.src || "";
    } catch {}
    if (!src) src = "assets/agent-avatar.png";
    document.querySelectorAll(".agent-avatar").forEach(node => {
      node.classList.add("has-photo");
      node.style.backgroundImage = `url("${src}")`;
    });
  }

  async function openMarket(force = false) {
    const dialog = $("#agentSkillDialog");
    const list = $("#agentSkillList");
    const installedList = $("#agentInstalledList");
    const hint = $("#agentSkillHint");
    if (!dialog || !list) return;
    if (!dialog.open) dialog.showModal();
    if (force || !list.dataset.loaded) {
      list.innerHTML = `<p class="agent-muted">加载中…</p>`;
      if (installedList) installedList.innerHTML = `<p class="agent-muted">加载中…</p>`;
    }
    if (hint) hint.textContent = state.mode === "work" ? "工作模式：可以安装、导入与卸载技能" : "安装、导入与卸载需要先切换到工作模式";
    const search = $("#agentMarketSearch")?.value.trim() || "";
    const category = state.marketCategory || "";
    try {
      const query = new URLSearchParams({ search, category });
      const [marketData, installedData] = await Promise.all([
        api(`/api/skills/market?${query.toString()}`),
        api(`/api/skills/market/installed?user_id=${encodeURIComponent(state.userId)}`)
      ]);
      const skills = marketData.skills || [];
      const installed = installedData.skills || [];
      const installedIds = new Set(installed.map(skill => skill.id));
      list.dataset.loaded = "1";
      if (!skills.length) {
        list.innerHTML = `<p class="agent-muted">没有匹配的技能。可以在工作模式下导入 SKILL.md。</p>`;
      } else {
        list.innerHTML = skills.map(skill => {
          const tags = (skill.tags || []).map(tag => `<span>${escapeHtml(tag)}</span>`).join("");
          return `
            <article class="agent-market-item">
              <header><strong>${escapeHtml(skill.display_name || skill.name)}</strong><span class="agent-market-owner">@${escapeHtml(skill.owner || "community")}</span></header>
              <p>${escapeHtml(skill.description || "")}</p>
              <div class="agent-market-tags">${tags}</div>
              <footer>
                <span class="agent-market-meta">v${escapeHtml(skill.version || "1.0.0")} · ${Number(skill.downloads || 0)} 次安装 · ${Number(skill.rating || 0).toFixed(1)} 分</span>
                <div class="agent-market-actions">
                  ${installedIds.has(skill.id)
                    ? `<span class="agent-market-installed-tag">已安装</span>`
                    : `<button type="button" class="agent-chip" data-install="${escapeHtml(skill.id)}" ${state.mode !== "work" ? "disabled" : ""}>安装</button>`}
                </div>
              </footer>
            </article>
          `;
        }).join("");
      }
      list.querySelectorAll("[data-install]").forEach(button => {
        button.addEventListener("click", () => installSkill(button.dataset.install));
      });
      if (installedList) {
        installedList.innerHTML = installed.length
          ? installed.map(skill => `
              <div class="agent-installed-item">
                <span><strong>${escapeHtml(skill.display_name || skill.name)}</strong><small>${escapeHtml(skill.category || "技能")} · v${escapeHtml(skill.version || "1.0.0")}</small></span>
                <button type="button" class="agent-chip agent-chip-quiet" data-uninstall="${escapeHtml(skill.id)}" ${state.mode !== "work" ? "disabled" : ""}>卸载</button>
              </div>
            `).join("")
          : `<p class="agent-muted">还没有安装市场技能。内置专家默认可用。</p>`;
        installedList.querySelectorAll("[data-uninstall]").forEach(button => {
          button.addEventListener("click", () => uninstallSkill(button.dataset.uninstall));
        });
      }
    } catch (error) {
      list.innerHTML = `<p class="agent-muted">市场加载失败：${escapeHtml(error.message)}</p>`;
    }
  }

  async function installSkill(id) {
    if (state.mode !== "work") {
      toast("安装技能需要工作模式");
      return;
    }
    try {
      const data = await api("/api/skills/market/install", {
        method: "POST",
        body: JSON.stringify({ id, user_id: state.userId, mode: "work" })
      });
      toast(data.message || "安装成功");
      state.experts = [];
      await openMarket(true);
      updateExpertLabel();
    } catch (error) {
      toast(`安装失败：${error.message}`);
    }
  }

  async function uninstallSkill(id) {
    if (state.mode !== "work") {
      toast("卸载技能需要工作模式");
      return;
    }
    try {
      const data = await api(`/api/skills/${encodeURIComponent(id)}`, {
        method: "DELETE",
        body: JSON.stringify({ user_id: state.userId, mode: "work" })
      });
      toast(data.message || "已卸载");
      state.experts = [];
      await openMarket(true);
      updateExpertLabel();
    } catch (error) {
      toast(`卸载失败：${error.message}`);
    }
  }

  async function importSkillFile(event) {
    const file = event.target.files?.[0];
    event.target.value = "";
    if (!file) return;
    if (file.size > 200_000) {
      toast("SKILL.md 不能超过 200KB");
      return;
    }
    try {
      const content = await file.text();
      const data = await api("/api/skills/install", {
        method: "POST",
        body: JSON.stringify({ content, user_id: state.userId })
      });
      toast(data.message || "导入成功");
      state.experts = [];
      await openMarket(true);
      updateExpertLabel();
    } catch (error) {
      toast(`导入失败：${error.message}`);
    }
  }

  async function shareSkill(id) {
    if (state.mode !== "work") {
      toast("请先切换到工作模式");
      return;
    }
    const friend = window.prompt("输入好友的用户 ID，例如 u_lin");
    if (!friend) return;
    try {
      const data = await api("/api/skills/market/share", {
        method: "POST",
        body: JSON.stringify({ id, from_user: state.userId, to_users: [friend.trim()] })
      });
      toast(data.message || "分享成功");
    } catch (error) {
      toast(`分享失败：${error.message}`);
    }
  }

  window.eatAgent = {
    intercept(text) {
      if (!state.ready) return false;
      if (this.fallback) {
        this.fallback = false;
        return false;
      }
      const value = String(text || "").trim();
      if (!value && !state.images.length) return false;
      if (state.sending) {
        toast("正在回答，请稍等");
        return true;
      }
      send(value, { images: state.images });
      return true;
    },
    fallback: false,
    state
  };

  document.getElementById("profileShortcut")?.addEventListener("click", event => {
    event.preventDefault();
    event.stopImmediatePropagation();
    const menu = document.getElementById("agentModelMenu");
    if (menu?.classList.contains("open")) menu.classList.remove("open");
    else openModelMenu();
  }, true);

  document.getElementById("agentHistoryEntry")?.addEventListener("click", event => {
    event.preventDefault();
    toggleHistoryPanel();
  });

  renderMessageRails();
  observeMessages();
  applyAgentAvatar();

  mountToolbar();
  mountDialogs();
  mountToneSetting();
  updateWorkLabel();
  updateExpertLabel();
  loadModels();
  loadMeta();
})();