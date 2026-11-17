/*
 * Transport adapter for the Eat App.
 *
 * The page never talks to a port directly: it goes through one interface so the
 * same DOM/CSS/JS can run in two environments:
 *   - browser: fetch + SSE against the local Agent HTTP server (development).
 *   - WebView2: postMessage to the native shell, which relays frames to the
 *     local Agent over a named pipe. No TCP port is exposed by the App.
 */
(function () {
  const webview = window.chrome && window.chrome.webview ? window.chrome.webview : null;
  const configured = localStorage.getItem("eat-agent-base") ||
    `${location.hostname || "127.0.0.1"}:8787`;
  const baseUrl = /^https?:\/\//.test(configured) ? configured : `http://${configured}`;

  let sequence = 1;
  const pending = new Map();

  function resolvePending(id) {
    const entry = pending.get(id);
    if (entry) pending.delete(id);
    return entry;
  }

  if (webview) {
    webview.addEventListener("message", (event) => {
      const message = event.data || {};
      if (message.type === "agent.event") {
        const entry = pending.get(message.id);
        if (entry) entry.onEvent(message.event, message.data);
      } else if (message.type === "agent.done") {
        const entry = resolvePending(message.id);
        if (entry) entry.resolve({ ok: true });
      } else if (message.type === "agent.error") {
        const entry = resolvePending(message.id);
        if (entry) entry.reject(new Error(message.message || "agent_error"));
      } else if (message.type === "service.response") {
        const entry = resolvePending(message.id);
        if (entry) entry.resolve(message.data);
      } else if (message.type === "service.login.response") {
        const entry = resolvePending(message.id);
        if (entry) entry.resolve(message);
      }
    });
  }

  async function consumeSse(stream, onEvent) {
    const reader = stream.getReader();
    const decoder = new TextDecoder();
    let buffer = "";
    for (;;) {
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

  async function browserStream(payload, onEvent) {
    const response = await fetch(`${baseUrl}/api/chat/stream`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    if (!response.ok || !response.body) throw new Error(`HTTP ${response.status}`);
    await consumeSse(response.body, onEvent);
  }

  function appStream(payload, onEvent) {
    const id = `c${sequence++}`;
    return new Promise((resolve, reject) => {
      pending.set(id, { onEvent, resolve, reject });
      webview.postMessage({ type: "agent.chat", id, payload });
    });
  }

  function appRequest(path, init) {
    const id = `r${sequence++}`;
    return new Promise((resolve, reject) => {
      pending.set(id, {
        onEvent: () => {},
        resolve,
        reject
      });
      webview.postMessage({ type: "service.request", id, path, init: init || {} });
    });
  }

  function appLogin(payload) {
    const id = `l${sequence++}`;
    return new Promise((resolve, reject) => {
      pending.set(id, { onEvent: () => {}, resolve, reject });
      webview.postMessage({ type: "service.login", id, ...payload });
    });
  }

  async function browserLogin(payload) {
    const response = await fetch(`${baseUrl}/api/service/login`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  window.eatTransport = {
    mode: webview ? "app" : "browser",
    base: baseUrl,
    chatStream(payload, onEvent) {
      return webview ? appStream(payload, onEvent) : browserStream(payload, onEvent);
    },
    login(payload) {
      return webview ? appLogin(payload) : browserLogin(payload);
    },
    async request(path, init) {
      if (webview) return appRequest(path, init);
      const response = await fetch(`${baseUrl}${path}`, init || {});
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      return response.json();
    },
    status() {
      return { mode: this.mode, online: navigator.onLine, base: baseUrl };
    }
  };
})();
