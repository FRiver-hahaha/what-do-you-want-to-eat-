from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any

AGENT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = AGENT_DIR.parent
CONFIG_DIR = AGENT_DIR / "config"


def _load_dotenv() -> None:
    if os.environ.get("EAT_DISABLE_DOTENV") == "1":
        return
    path = Path(os.environ.get("EAT_ENV_FILE", str(PROJECT_ROOT / ".env")))
    try:
        lines = path.read_text(encoding="utf-8-sig").splitlines()
    except OSError:
        return
    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if stripped.startswith("export "):
            stripped = stripped[7:].strip()
        if "=" not in stripped:
            continue
        key, value = stripped.split("=", 1)
        key = key.strip()
        value = value.strip().strip('"').strip("'")
        if key and key not in os.environ:
            os.environ[key] = value


_load_dotenv()

DATA_DIR = Path(os.environ.get("EAT_AGENT_DATA_DIR", str(PROJECT_ROOT / "data" / "agent")))
SKILLS_USER_DIR = DATA_DIR / "skills"
RAG_DIR = DATA_DIR / "rag"
LOG_DIR = DATA_DIR / "logs"
APP_DB = Path(os.environ.get("EAT_AGENT_DB", str(DATA_DIR / "agent.db")))
RAG_DB = Path(os.environ.get("EAT_AGENT_RAG_DB", str(RAG_DIR / "knowledge.db")))

SERVER_BASE_URL = os.environ.get("EAT_SERVER_BASE_URL", "").rstrip("/")
SERVER_TOKEN = os.environ.get("EAT_SERVER_TOKEN", "")
SERVICE_TIMEOUT = float(os.environ.get("EAT_SERVICE_TIMEOUT", "5"))

DEFAULT_USER_ID = os.environ.get("EAT_DEFAULT_USER_ID", "u_demo")
DEFAULT_SESSION_ID = os.environ.get("EAT_DEFAULT_SESSION_ID", "s_demo")

MCP_DISABLED = os.environ.get("EAT_MCP_DISABLE", "0") == "1"
MCP_START_TIMEOUT = float(os.environ.get("EAT_MCP_START_TIMEOUT", "12"))

MAX_TOOL_STEPS = int(os.environ.get("EAT_MAX_TOOL_STEPS", "6"))
MAX_CONTEXT_MESSAGES = int(os.environ.get("EAT_MAX_CONTEXT_MESSAGES", "18"))
MAX_CONTEXT_CHARS = int(os.environ.get("EAT_MAX_CONTEXT_CHARS", "1000000"))
MAX_TOOL_RESULT_CHARS = int(os.environ.get("EAT_MAX_TOOL_RESULT_CHARS", "6000"))
RAG_TOP_K = int(os.environ.get("EAT_RAG_TOP_K", "4"))

DEFAULT_MODEL_ID = "rule-fallback"


def _load(name: str, default: dict[str, Any]) -> dict[str, Any]:
    path = CONFIG_DIR / name
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return default
    return data if isinstance(data, dict) else default


def load_models_config() -> dict[str, Any]:
    return _load(
        "models.json",
        {
            "default": DEFAULT_MODEL_ID,
            "models": [
                {
                    "id": DEFAULT_MODEL_ID,
                    "name": "本地规则引擎",
                    "provider": "rule",
                    "caps": ["chat", "tools"],
                    "context": 8000,
                    "description": "离线兜底模型，无网络时保证可用",
                }
            ],
            "routing": {"intent": DEFAULT_MODEL_ID, "chat": DEFAULT_MODEL_ID, "summary": DEFAULT_MODEL_ID},
        },
    )


def load_mcp_config() -> dict[str, Any]:
    return _load("mcp_servers.json", {"version": 1, "servers": {}})


def load_modes_config() -> dict[str, Any]:
    return _load(
        "modes.json",
        {
            "qa": {
                "id": "qa",
                "name": "问答模式",
                "description": "只读对话",
                "tool_categories": ["read"],
                "allow_local_tools": False,
                "allow_skill_install": False,
                "allow_write": False,
            },
            "work": {
                "id": "work",
                "name": "工作模式",
                "description": "可执行本地任务",
                "tool_categories": ["read", "write", "local"],
                "allow_local_tools": True,
                "allow_skill_install": True,
                "allow_write": True,
            },
        },
    )


MODELS_CONFIG = load_models_config()
MCP_CONFIG = load_mcp_config()
MODES_CONFIG = load_modes_config()


def ensure_dirs() -> None:
    for path in (DATA_DIR, SKILLS_USER_DIR, RAG_DIR, LOG_DIR):
        path.mkdir(parents=True, exist_ok=True)


def resolve_placeholders(value: str) -> str:
    return (
        value.replace("{python}", os.environ.get("EAT_PYTHON", os.sys.executable))
        .replace("{project_root}", str(PROJECT_ROOT))
        .replace("{data_dir}", str(DATA_DIR))
    )


def available_models() -> list[dict[str, Any]]:
    models = MODELS_CONFIG.get("models", [])
    result: list[dict[str, Any]] = []
    for model in models:
        if model.get("hidden"):
            continue
        item = dict(model)
        provider = item.get("provider", "")
        env_name = item.get("api_key_env")
        if provider == "rule":
            item["available"] = True
        elif provider == "openai-compatible" and not env_name:
            item["available"] = True
        else:
            item["available"] = bool(os.environ.get(env_name or "", ""))
        item.pop("api_key_env", None)
        result.append(item)
    return result


def default_model_id() -> str:
    configured = MODELS_CONFIG.get("default")
    if configured:
        for model in available_models():
            if model["id"] == configured and model.get("available"):
                return configured
    for model in available_models():
        if model.get("available"):
            return str(model["id"])
    return DEFAULT_MODEL_ID