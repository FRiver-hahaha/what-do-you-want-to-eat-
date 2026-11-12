from __future__ import annotations

import argparse
import os

from agent.config import ensure_dirs
from agent.data.db import init_db
from agent.server import run_server


def main() -> None:
    parser = argparse.ArgumentParser(description="今天你吃了吗 Agent 服务")
    parser.add_argument("--host", default=os.environ.get("EAT_AGENT_HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("EAT_AGENT_PORT", "8787")))
    args = parser.parse_args()

    ensure_dirs()
    init_db()
    run_server(args.host, args.port)


if __name__ == "__main__":
    main()