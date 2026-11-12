from __future__ import annotations

import re
from typing import Any

INJECTION_MARKERS = (
    "忽略以上",
    "忽略之前",
    "系统提示词",
    "system prompt",
    "开发者模式",
    "developer mode",
    "绕过权限",
    "查询他人",
    "导出全部用户",
    "忽略安全",
)

SECRET_PATTERN = re.compile(r"sk-[A-Za-z0-9_-]{16,}")
PHONE_PATTERN = re.compile(r"(?<!\d)1[3-9]\d{9}(?!\d)")
MAX_IMAGES = 4
MAX_IMAGE_CHARS = 2_500_000


def detect_injection(text: str) -> dict[str, Any]:
    lowered = text.lower()
    markers = [marker for marker in INJECTION_MARKERS if marker.lower() in lowered]
    return {"risk": "high" if len(markers) >= 2 else "low" if markers else "none", "markers": markers}


def sanitize_images(value: Any) -> tuple[list[str], str]:
    if value is None:
        return [], ""
    if not isinstance(value, list):
        return [], "图片参数必须是数组"
    images: list[str] = []
    for item in value[:MAX_IMAGES]:
        if not isinstance(item, str):
            continue
        text = item.strip()
        if text.startswith("data:image/") and len(text) <= MAX_IMAGE_CHARS:
            images.append(text)
        elif text.startswith("https://") or text.startswith("http://"):
            images.append(text)
    if len(value) > MAX_IMAGES:
        return images, f"最多接收 {MAX_IMAGES} 张图片，已忽略多余部分"
    return images, ""


def filter_output(text: str) -> tuple[str, list[str]]:
    findings: list[str] = []
    filtered = text
    if SECRET_PATTERN.search(filtered):
        findings.append("secret")
        filtered = SECRET_PATTERN.sub("[已隐藏密钥]", filtered)
    if PHONE_PATTERN.search(filtered):
        findings.append("phone")
        filtered = PHONE_PATTERN.sub("[已隐藏手机号]", filtered)
    return filtered, findings