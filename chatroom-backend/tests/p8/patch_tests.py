#!/usr/bin/env python3
"""维护既有 P8 测试脚本中的 health.version 断言，使其接受新阶段版本号。"""

from __future__ import annotations

import io
import os

HERE = os.path.dirname(os.path.abspath(__file__))

PATCHES = [
    (
        os.path.join(HERE, "p8s2_http_test.py"),
        'check("health version p8-s2/p8-s3", health.get("version") in {"p8-s2", "p8-s3"}, health)',
        'check("health version p8-s2/p8-s3/p8-s4", '
        'health.get("version") in {"p8-s2", "p8-s3", "p8-s4"}, health)',
    ),
    (
        os.path.join(HERE, "p8s3_http_test.py"),
        'check("health 200 且 version=p8-s3",\n'
        '          status == 200 and health.get("version") == "p8-s3", health)',
        'check("health 200 且 version=p8-s3/p8-s4",\n'
        '          status == 200 and health.get("version") in {"p8-s3", "p8-s4"}, health)',
    ),
]


def main() -> int:
    for path, old, new in PATCHES:
        text = io.open(path, encoding="utf-8").read()
        if new in text:
            print(f"SKIP  {path} (已是最新)")
            continue
        if old not in text:
            print(f"FAIL  {path} (未找到待替换内容)")
            return 1
        io.open(path, "w", encoding="utf-8", newline="\n").write(text.replace(old, new))
        print(f"PATCH {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())