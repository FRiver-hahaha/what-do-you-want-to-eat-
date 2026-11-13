from __future__ import annotations

import threading
import time
import uuid
from contextlib import contextmanager
from dataclasses import dataclass, field
from queue import Full, Queue
from typing import Any, Iterator

from agent.core.runtime import AgentEvent, AgentRuntime


@dataclass
class Trigger:
    thread_id: str
    user_id: str
    source: str = "web"
    payload: dict[str, Any] = field(default_factory=dict)
    id: str = field(default_factory=lambda: f"trg_{uuid.uuid4().hex[:10]}")


@dataclass
class WorkerState:
    id: str
    thread_id: str
    status: str = "idle"
    last_event_at: float = field(default_factory=time.time)
    events: int = 0
    lock: threading.Lock = field(default_factory=threading.Lock)


@dataclass
class ThreadState:
    id: str
    worker_id: str
    subscribers: set[str] = field(default_factory=set)


class EventBus:
    def __init__(self, maxsize: int = 200) -> None:
        self._queues: dict[str, Queue] = {}
        self._lock = threading.Lock()
        self._maxsize = maxsize

    def subscribe(self, thread_id: str) -> Queue:
        with self._lock:
            return self._queues.setdefault(thread_id, Queue(maxsize=self._maxsize))

    def publish(self, thread_id: str, event: AgentEvent) -> None:
        queue = self.subscribe(thread_id)
        try:
            queue.put_nowait(event)
        except Full:
            pass

    def drain(self, thread_id: str, limit: int = 20) -> list[AgentEvent]:
        queue = self.subscribe(thread_id)
        events = []
        for _ in range(limit):
            try:
                events.append(queue.get_nowait())
            except Exception:
                break
        return events


class Router:
    def __init__(self) -> None:
        self.threads: dict[str, ThreadState] = {}
        self.workers: dict[str, WorkerState] = {}
        self.lock = threading.Lock()

    def route(self, trigger: Trigger) -> tuple[WorkerState, bool]:
        with self.lock:
            thread = self.threads.get(trigger.thread_id)
            if thread is not None:
                worker = self.workers.get(thread.worker_id)
                if worker is not None:
                    worker.last_event_at = time.time()
                    return worker, False
            worker = WorkerState(id=f"wk_{uuid.uuid4().hex[:8]}", thread_id=trigger.thread_id)
            self.workers[worker.id] = worker
            self.threads[trigger.thread_id] = ThreadState(
                id=trigger.thread_id,
                worker_id=worker.id,
                subscribers={worker.id},
            )
            return worker, True

    def snapshot(self) -> list[WorkerState]:
        with self.lock:
            return list(self.workers.values())

    def status(self) -> dict[str, Any]:
        with self.lock:
            return {
                "threads": len(self.threads),
                "workers": [
                    {
                        "id": worker.id,
                        "thread_id": worker.thread_id,
                        "status": worker.status,
                        "events": worker.events,
                        "idle_seconds": round(time.time() - worker.last_event_at, 1),
                    }
                    for worker in self.workers.values()
                ],
            }


class WorkerPool:
    def __init__(self, max_workers: int = 4) -> None:
        self._semaphore = threading.BoundedSemaphore(max_workers)
        self.max_workers = max_workers

    @contextmanager
    def acquire(self) -> Iterator[None]:
        self._semaphore.acquire()
        try:
            yield
        finally:
            self._semaphore.release()


class Watchdog(threading.Thread):
    def __init__(self, router: Router, bus: EventBus, timeout: float = 120.0, interval: float = 5.0) -> None:
        super().__init__(name="agent-watchdog", daemon=True)
        self.router = router
        self.bus = bus
        self.timeout = timeout
        self.interval = interval
        self._stop_event = threading.Event()
        self.last_scan = time.time()
        self.stalled: list[dict[str, Any]] = []

    def run(self) -> None:
        while not self._stop_event.wait(self.interval):
            now = time.time()
            self.last_scan = now
            for worker in self.router.snapshot():
                if worker.status == "running" and now - worker.last_event_at > self.timeout:
                    worker.status = "stalled"
                    item = {"worker_id": worker.id, "thread_id": worker.thread_id, "idle_seconds": round(now - worker.last_event_at, 1)}
                    self.stalled.append(item)
                    self.stalled = self.stalled[-20:]
                    self.bus.publish(worker.thread_id, AgentEvent("watchdog", {"level": "error", **item}))

    def stop(self) -> None:
        self._stop_event.set()

    def status(self) -> dict[str, Any]:
        return {"alive": self.is_alive(), "last_scan": self.last_scan, "stalled": self.stalled[-5:]}


class SuperAgent:
    def __init__(
        self,
        runtime: AgentRuntime,
        router: Router | None = None,
        bus: EventBus | None = None,
        pool: WorkerPool | None = None,
        watchdog: Watchdog | None = None,
    ) -> None:
        self.runtime = runtime
        self.router = router or Router()
        self.bus = bus or EventBus()
        self.pool = pool or WorkerPool(4)
        self.watchdog = watchdog or Watchdog(self.router, self.bus)
        self.watchdog.start()

    def handle(self, request: dict[str, Any]) -> Iterator[AgentEvent]:
        trigger = Trigger(
            thread_id=str(request.get("session_id") or f"thread_{uuid.uuid4().hex[:10]}"),
            user_id=str(request.get("user_id") or "u_demo"),
            source=str(request.get("source") or "web"),
            payload=request,
        )
        worker, created = self.router.route(trigger)
        worker.status = "running"
        worker.last_event_at = time.time()
        worker.events += 1
        yield AgentEvent(
            "worker_start",
            {
                "worker_id": worker.id,
                "thread_id": worker.thread_id,
                "created": created,
                "trigger_id": trigger.id,
            },
        )
        status = "ok"
        try:
            with self.pool.acquire():
                with worker.lock:
                    for event in self.runtime.handle(request):
                        worker.last_event_at = time.time()
                        self.bus.publish(worker.thread_id, event)
                        yield event
        except GeneratorExit:
            status = "cancelled"
            raise
        except Exception as exc:
            status = "error"
            yield AgentEvent("error", {"code": "super_agent_error", "message": str(exc)[:300]})
        finally:
            worker.status = "idle" if status == "ok" else status
            self.bus.publish(
                worker.thread_id,
                AgentEvent("worker_done", {"worker_id": worker.id, "thread_id": worker.thread_id, "status": worker.status}),
            )
        if status != "cancelled":
            yield AgentEvent(
                "worker_done",
                {"worker_id": worker.id, "thread_id": worker.thread_id, "status": worker.status},
            )

    def status(self) -> dict[str, Any]:
        return {
            "mode": "single-agent-dual-layer",
            "router": self.router.status(),
            "pool": {"max_workers": self.pool.max_workers},
            "watchdog": self.watchdog.status(),
            "event_bus": {"threads": len(self.bus._queues)},
        }

    def close(self) -> None:
        self.watchdog.stop()