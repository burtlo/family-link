#!/usr/bin/env python3
"""Two twins: recorded open/away is what the friend sees on the next heartbeat."""

from __future__ import annotations

import argparse
import sys

import httpx

from demos.server._shared.registry import load_devices


def fail(reason: str) -> int:
    print(f"FAIL {reason}")
    return 1


def auth(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


def heartbeat(client: httpx.Client, base: str, token: str, available: bool) -> dict:
    r = client.post(
        f"{base}/v1/heartbeat",
        headers=auth(token),
        json={"available": available},
    )
    if r.status_code != 200:
        raise SystemExit(fail(f"heartbeat {r.status_code} {r.text}"))
    body = r.json()
    if body.get("ok") is not True:
        raise SystemExit(fail(f"heartbeat not ok: {body}"))
    if "self" not in body or "peer" not in body:
        raise SystemExit(fail(f"heartbeat missing self/peer: {body}"))
    return body


def expect_person(node: dict, *, device_id: str, name: str, available: bool | None) -> None:
    if node.get("id") != device_id:
        raise SystemExit(fail(f"expected id {device_id}, got {node}"))
    if node.get("name") != name:
        raise SystemExit(fail(f"expected name {name!r}, got {node}"))
    if node.get("available") is not available:
        raise SystemExit(fail(f"expected available={available}, got {node}"))
    if available is None:
        if node.get("online") is not False:
            raise SystemExit(fail(f"unknown peer should be offline: {node}"))
        return
    if node.get("online") is not True:
        raise SystemExit(fail(f"known peer should be online: {node}"))
    if node.get("updated_at") is None:
        raise SystemExit(fail(f"known peer missing updated_at: {node}"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    base = args.base_url.rstrip("/")
    devices = load_devices()
    mazi = devices["box-a"]
    arlo = devices["box-b"]

    with httpx.Client(timeout=5.0) as client:
        # 1. Mazi heartbeats away before Arlo has spoken — peer unknown.
        first = heartbeat(client, base, mazi.token, False)
        expect_person(
            first["self"], device_id=mazi.id, name=mazi.name, available=False
        )
        expect_person(first["peer"], device_id=arlo.id, name=arlo.name, available=None)

        # 2. Arlo heartbeats open. Response must already include Mazi's away.
        arlo_open = heartbeat(client, base, arlo.token, True)
        expect_person(
            arlo_open["self"], device_id=arlo.id, name=arlo.name, available=True
        )
        expect_person(
            arlo_open["peer"], device_id=mazi.id, name=mazi.name, available=False
        )

        # 3. Mazi's next heartbeat (still away) now sees Arlo open.
        mazi_sees = heartbeat(client, base, mazi.token, False)
        expect_person(
            mazi_sees["peer"], device_id=arlo.id, name=arlo.name, available=True
        )

        # 4. Mazi changes to open. That new status is recorded.
        mazi_open = heartbeat(client, base, mazi.token, True)
        expect_person(
            mazi_open["self"], device_id=mazi.id, name=mazi.name, available=True
        )

        # 5. Arlo's following heartbeat contains Mazi's updated open state.
        arlo_sees = heartbeat(client, base, arlo.token, True)
        expect_person(
            arlo_sees["peer"], device_id=mazi.id, name=mazi.name, available=True
        )

        # 6. Mazi goes away again; Arlo learns it on the next beat.
        heartbeat(client, base, mazi.token, False)
        arlo_away = heartbeat(client, base, arlo.token, True)
        expect_person(
            arlo_away["peer"], device_id=mazi.id, name=mazi.name, available=False
        )

    print(f"-- PASS h20_presence {mazi.name}↔{arlo.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
