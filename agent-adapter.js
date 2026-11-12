(() => {
  const BASE = (localStorage.getItem("eat-agent-base") || "http://127.0.0.1:8787").replace(/\/$/, "");
  const STORE = {
    mode: "eat-agent-mode",
    model: "eat-agent-model",
    session: "eat-agent-session",
    userId: "eat-agent-user"
  };
  const state = {
    ready: false,
    fallback: false,
    sending: false,
    mode: localStorage.getItem(STORE.mode) || "qa",
    modelId: localStorage.getItem(STORE.model) || "",
    sessionId: localStorage.getItem(STORE.session) || `web_${Date.now().toString(36)}`,
    userId: localStorage.getItem(STORE.userId) || "u_demo",
    models: [],
    meta: null,
    pending: null,
    lastUser: null
  };
  let toastTimer;

  const $ = (selector, root = document) => root.querySelector(selector);

  function escapeHtml(value) {
    return String(value).replace(/[&<>'"]/g, character => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "'": "&#39;", '"': "&quot;" }[character]));
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
        <button type="button" data-mode="qa">问答</button>
        <button type="button" data-mode="work">工作</button>
      </div>
      <label class="agent-model"><span>模型</span><select id="agentModelSelect" aria-label="选择模型"></select></label>
      <button type="button" class="agent-chip" id="agentSkillButton">Skill 市场</button>
      <button type="button" class="agent-chip" id="agentNewSession">新对话</button>
      <span class="agent-conn"><i></i><span id="agentConnText">连接中</span></span>
    `;
    composer.parentElement.insertBefore(toolbar, composer);
    toolbar.querySelectorAll(".agent-mode button").forEach(button => {
      button.classList.toggle("active", button.dataset.mode === state.mode);
      button.addEventListener("click", () => {
        state.mode = button.dataset.mode;
        localStorage.setItem(STORE.mode, state.mode);
        toolbar.querySelectorAll(".agent-mode button").forEach(item => item.classList.toggle("active", item === button));
        toast(state.mode === "work" ? "工作模式：可读实时菜单、写入记录、管理技能" : "问答模式：只读对话");
      });
    });
    $("#agentSkillButton", toolbar)?.addEventListener("click", openMarket);
    $("#agentNewSession", toolbar)?.addEventListener("click", startNewSession);
    const select = $("#agentModelSelect", toolbar);
    select?.addEventListener("change", () => {
      state.modelId = select.value;
      localStorage.setItem(STORE.model, state.modelId);
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
    marketDialog.className = "agent-dialog";
    marketDialog.innerHTML = `
      <header><div><span class="section-kicker">能力扩展</span><h2>Skill 市场</h2></div><button class="icon-button" id="agentSkillClose" type="button" aria-label="关闭">×</button></header>
      <div id="agentSkillList" class="agent-market-list"></div>
      <footer><span id="agentSkillHint" class="agent-muted">安装与分享需要工作模式</span></footer>
    `;
    document.body.append(confirmDialog, marketDialog);
    $("#agentSkillClose")?.addEventListener("click", () => marketDialog.close());
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
      const select = $("#agentModelSelect");
      if (!select) return;
      select.innerHTML = state.models.map(model => {
        const suffix = model.available ? "" : "（未配置 Key）";
        return `<option value="${escapeHtml(model.id)}" ${model.available ? "" : "disabled"}>${escapeHtml(model.name)}${suffix}</option>`;
      }).join("");
      const saved = state.models.find(model => model.id === state.modelId && model.available);
      const preferred = saved || state.models.find(model => model.id === data.default) || state.models.find(model => model.available);
      if (preferred) {
        state.modelId = preferred.id;
        select.value = preferred.id;
        localStorage.setItem(STORE.model, state.modelId);
      }
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
    } catch {
      state.ready = false;
      updateConnection("本地模式", "offline");
    }
  }

  function appendUser(text) {
    const conversation = document.getElementById("conversation");
    if (!conversation) return null;
    const article = document.createElement("article");
    article.className = "message user-message";
    const bubble = document.createElement("div");
    bubble.className = "message-bubble";
    const paragraph = document.createElement("p");
    paragraph.textContent = text;
    bubble.appendChild(paragraph);
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
    const article = options.article || createAgentMessage();
    if (!article) return;
    const bubble = $(".message-bubble", article);
    const status = $(".agent-status", article);
    const paragraph = document.createElement("p");
    bubble.innerHTML = "";
    bubble.appendChild(paragraph);
    status.textContent = "正在思考";
    state.sending = true;
    const citations = [];
    let userArticle = null;
    if (options.appendUser !== false) userArticle = appendUser(text);

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
          confirmed_tools: options.confirmedTools || [],
          client_context: clientContext()
        })
      });
      if (!response.ok || !response.body) throw new Error(`HTTP ${response.status}`);

      await consume(response.body, (event, data) => {
        if (event === "meta") {
          const model = data.model && data.model !== "auto" ? data.model : state.modelId || "本地规则";
          status.textContent = `${data.mode_name || "问答"} · ${model}`;
        } else if (event === "skill") {
          status.textContent = `技能：${data.display_name || data.id}`;
        } else if (event === "tool_call") {
          status.textContent = TOOL_LABELS[data.name] || `正在执行 ${data.name}`;
        } else if (event === "tool_result") {
          if (data.need_confirm) status.textContent = "等待你的确认";
          else status.textContent = data.ok ? "已完成查询" : "查询失败，继续整理";
        } else if (event === "token") {
          paragraph.textContent += data.text || "";
          const conversation = document.getElementById("conversation");
          conversation?.scrollTo({ top: conversation.scrollHeight, behavior: "smooth" });
        } else if (event === "citation") {
          citations.push(data);
        } else if (event === "notice") {
          toast(data.message || "已切换本地模型");
        } else if (event === "confirm_required") {
          state.pending = {
            text,
            tools: data.confirmed_tools || [],
            article
          };
          const dialog = $("#agentConfirmDialog");
          const textNode = $("#agentConfirmText");
          const toolsNode = $("#agentConfirmTools");
          if (textNode) textNode.textContent = "这项操作会修改你的饮食记录或本地技能，确认后才会真正执行。";
          if (toolsNode) toolsNode.innerHTML = (data.confirmed_tools || []).map(tool => `<span>${escapeHtml(tool)}</span>`).join("");
          if (dialog && !dialog.open) dialog.showModal();
          status.textContent = "等待你的确认";
        } else if (event === "error") {
          status.textContent = "出错了";
          paragraph.textContent = data.message || "Agent 返回了错误";
        } else if (event === "done") {
          article.classList.remove("typing-message");
          if (!citations.length) status.textContent = "刚刚";
        }
      });

      if (!paragraph.textContent.trim()) {
        paragraph.textContent = "我这轮没有取到足够的信息。你可以告诉我具体餐次，我再查一次。";
      }
      renderCitations(article, citations);
      article.classList.remove("typing-message");
    } catch (error) {
      removeNode(article);
      removeNode(userArticle);
      state.ready = false;
      updateConnection("本地模式", "offline");
      toast("Agent 不可用，已切回本地分析");
      window.eatAgent.fallback = true;
      window.eatLocal?.sendPrompt?.(text);
    } finally {
      state.sending = false;
    }
  }

  function startNewSession() {
    state.sessionId = `web_${Date.now().toString(36)}`;
    localStorage.setItem(STORE.session, state.sessionId);
    document.querySelectorAll("#conversation .message").forEach(node => node.remove());
    toast("已开始新对话");
  }

  async function openMarket() {
    const dialog = $("#agentSkillDialog");
    const list = $("#agentSkillList");
    const hint = $("#agentSkillHint");
    if (!dialog || !list) return;
    if (hint) hint.textContent = state.mode === "work" ? "工作模式：可以安装或分享技能" : "安装与分享需要先切换到工作模式";
    dialog.showModal();
    list.innerHTML = `<p class="agent-muted">加载中…</p>`;
    try {
      const data = await api("/api/skills/market");
      const skills = data.skills || [];
      if (!skills.length) {
        list.innerHTML = `<p class="agent-muted">市场还没有技能。可以在工作模式下导入 SKILL.md，或由其他用户发布。</p>`;
        return;
      }
      list.innerHTML = skills.map(skill => `
        <article class="agent-market-item">
          <div class="agent-market-copy"><strong>${escapeHtml(skill.display_name || skill.name)}</strong><small>${escapeHtml(skill.description || "")}</small></div>
          <span class="agent-market-meta">v${escapeHtml(skill.version || "1.0.0")} · ${Number(skill.downloads || 0)} 次安装</span>
          <div class="agent-market-actions">
            <button type="button" class="agent-chip" data-install="${escapeHtml(skill.id)}">安装</button>
            <button type="button" class="agent-chip" data-share="${escapeHtml(skill.id)}">分享</button>
          </div>
        </article>
      `).join("");
      list.querySelectorAll("[data-install]").forEach(button => {
        button.addEventListener("click", () => installSkill(button.dataset.install));
      });
      list.querySelectorAll("[data-share]").forEach(button => {
        button.addEventListener("click", () => shareSkill(button.dataset.share));
      });
    } catch (error) {
      list.innerHTML = `<p class="agent-muted">市场加载失败：${escapeHtml(error.message)}</p>`;
    }
  }

  async function installSkill(id) {
    if (state.mode !== "work") {
      toast("请先切换到工作模式");
      return;
    }
    try {
      const data = await api("/api/skills/market/install", {
        method: "POST",
        body: JSON.stringify({ id, user_id: state.userId, mode: "work" })
      });
      toast(data.message || "安装成功");
    } catch (error) {
      toast(`安装失败：${error.message}`);
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
      if (!value) return false;
      if (state.sending) {
        toast("正在回答，请稍等");
        return true;
      }
      send(value);
      return true;
    },
    fallback: false,
    state
  };

  mountToolbar();
  mountDialogs();
  loadModels();
  loadMeta();
})();