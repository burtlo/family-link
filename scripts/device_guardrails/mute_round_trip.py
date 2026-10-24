"""Structural guardrails for signed-in and login-desk mute round-trip (EXP-LOGIN)."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEVICE_MAIN = ROOT / "client/device/main"


def _read(name: str) -> str:
    return (DEVICE_MAIN / name).read_text(encoding="utf-8")


def _match_closer(text: str, open_index: int) -> int:
    depth = 0
    i = open_index
    while i < len(text):
        ch = text[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def _function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        return ""
    brace = text.find("{", start)
    if brace < 0:
        return ""
    end = _match_closer(text, brace)
    if end < 0:
        return ""
    return text[brace + 1 : end]


def scan_hardware_poll_uses_level_not_press_edge(hw: str) -> list[str]:
    """Mute is a physical latch; poll must sample level every tick."""
    body = _function_body(hw, "void product_hardware_buttons_poll(void)")
    if not body:
        return ["product_hardware_buttons.c: product_hardware_buttons_poll missing"]
    errors: list[str] = []
    if "product_login_on_mute_level(product_hardware_buttons_mute_latched())" not in body:
        errors.append(
            "product_hardware_buttons.c: poll must call "
            "product_login_on_mute_level(product_hardware_buttons_mute_latched())"
        )
    # Press edge must not be the only mute path (regression: edge-only mute).
    if re.search(
        r"if\s*\(\s*atomic_exchange_explicit\s*\(\s*&s_mute_pressed",
        body,
    ) and not re.search(
        r"atomic_exchange_explicit\s*\(\s*&s_mute_pressed[\s\S]*?"
        r"product_login_on_mute_level\s*\(\s*product_hardware_buttons_mute_latched\s*\(\s*\)\s*\)",
        body,
    ):
        errors.append(
            "product_hardware_buttons.c: mute press edge must not replace latch level sampling"
        )
    return errors


def scan_login_timer_mute_path(login: str) -> list[str]:
    """LVGL login timer must keep polling the latch and must not bail out before HTTP."""
    body = _function_body(login, "void on_login_timer(lv_timer_t *timer)")
    if not body:
        return ["product_login_bridge.cpp: on_login_timer missing"]
    errors: list[str] = []
    poll_at = body.find("product_hardware_buttons_poll()")
    http_at = body.find("product_login_http_service")
    if poll_at < 0:
        errors.append("product_login_bridge.cpp: on_login_timer must poll hardware buttons")
    elif http_at >= 0 and poll_at > http_at:
        errors.append(
            "product_login_bridge.cpp: product_hardware_buttons_poll must run before "
            "product_login_http_service"
        )
    # Regression from desk: early return while muted skipped HTTP and stranded login after unmute.
    bad_idle = (
        "if (s_backlight_dark && s_session.stage() == fl_product_login::kStageMuted)"
        in body
        and re.search(
            r"if\s*\(\s*s_backlight_dark\s*&&\s*s_session\.stage\(\)\s*==\s*"
            r"fl_product_login::kStageMuted\s*\)\s*\{[^}]*return\s*;",
            body,
        )
    )
    if bad_idle:
        errors.append(
            "product_login_bridge.cpp: on_login_timer must not return early while muted "
            "(keeps login HTTP and unmute repaint alive)"
        )
    if body.count("return;") > 0:
        errors.append(
            "product_login_bridge.cpp: on_login_timer must not early-return "
            "(mute latch is sampled here every tick)"
        )
    if "const bool muted_blank" not in body:
        errors.append("product_login_bridge.cpp: on_login_timer must define muted_blank")
    render_gate = body.find("if (!muted_blank)")
    if render_gate < 0:
        errors.append(
            "product_login_bridge.cpp: muted_blank must gate render_login_copy only, not poll"
        )
    elif poll_at >= 0 and render_gate < poll_at:
        errors.append(
            "product_login_bridge.cpp: muted_blank must not run before product_hardware_buttons_poll"
        )
    if "product_inbox_service(now_ms)" in body and "if (s_session.signed_in() != 0)" not in body:
        errors.append(
            "product_login_bridge.cpp: product_inbox_service must run only while signed in"
        )
    signed_at = body.find("if (s_session.signed_in() != 0)")
    else_at = body.find("} else {", signed_at)
    if signed_at >= 0 and else_at > signed_at:
        signed_branch = body[signed_at:else_at]
        if "product_inbox_discard_results()" in signed_branch:
            errors.append(
                "product_login_bridge.cpp: product_inbox_discard_results belongs in signed-out "
                "branch only"
            )
    if "product_inbox_discard_results()" not in body:
        errors.append(
            "product_login_bridge.cpp: signed-out ticks must call product_inbox_discard_results"
        )
    return errors


def scan_login_on_mute_level(login: str) -> list[str]:
    body = _function_body(login, "void product_login_on_mute_level(int latched)")
    if not body:
        return ["product_login_bridge.cpp: product_login_on_mute_level missing"]
    errors: list[str] = []
    if "blank_login_display()" not in body or "wake_login_display()" not in body:
        errors.append(
            "product_login_bridge.cpp: mute level must use blank_login_display / wake_login_display"
        )
    if "bsp_display_backlight_on()" in body:
        errors.append(
            "product_login_bridge.cpp: unmute must wake via wake_login_display, not backlight_on only"
        )
    unlatched = body.split("return;", 1)[-1] if "return;" in body else ""
    if "render_login_copy();" not in body:
        errors.append("product_login_bridge.cpp: product_login_on_mute_level must repaint login")
    elif "Steady open-latch UI is driven by on_login_timer" not in body:
        errors.append(
            "product_login_bridge.cpp: unlatched mute repaint must be edge-only "
            "(document timer owns steady-state login UI)"
        )
    unlatched_render = unlatched.rfind("render_login_copy();")
    if unlatched_render >= 0:
        before_render = unlatched[max(0, unlatched_render - 512) : unlatched_render]
        if "if (changed)" not in before_render:
            errors.append(
                "product_login_bridge.cpp: open-latch render_login_copy must run only when changed"
            )
    if "clear_roster_cards();" not in unlatched:
        errors.append(
            "product_login_bridge.cpp: unmute must clear_roster_cards before roster refetch"
        )
    if "product_login_http_on_mute()" not in body:
        errors.append("product_login_bridge.cpp: mute/unmute must reset login HTTP roster fence")
    return errors


def scan_inbox_on_mute(inbox: str) -> list[str]:
    body = _function_body(inbox, "void product_inbox_on_mute(void)")
    if not body:
        return ["product_inbox_bridge.cpp: product_inbox_on_mute missing"]
    errors: list[str] = []
    for forbidden in ("product_inbox_http_worker_stop", "vTaskDelete"):
        if forbidden in body:
            errors.append(f"product_inbox_bridge.cpp: product_inbox_on_mute must not call {forbidden}")
    if "lv_timer_pause(s_timer)" not in body:
        errors.append("product_inbox_bridge.cpp: product_inbox_on_mute must pause inbox LVGL timer")
    return errors


def scan_inbox_attach_pauses_timer(inbox: str) -> list[str]:
    body = _function_body(inbox, "extern \"C\" bool product_inbox_attach")
    if not body:
        return ["product_inbox_bridge.cpp: product_inbox_attach missing"]
    if "lv_timer_create(on_inbox_timer" not in body:
        return ["product_inbox_bridge.cpp: product_inbox_attach must create inbox LVGL timer"]
    create_at = body.rfind("lv_timer_create(on_inbox_timer")
    pause_at = body.rfind("lv_timer_pause(s_timer)")
    if pause_at < 0 or pause_at < create_at:
        return [
            "product_inbox_bridge.cpp: pause inbox LVGL timer after create until sign-in "
            "(login roster touch)"
        ]
    return []


def scan_inbox_signed_in_resume(inbox: str) -> list[str]:
    body = _function_body(inbox, "void product_inbox_on_signed_in(void)")
    if not body:
        return ["product_inbox_bridge.cpp: product_inbox_on_signed_in missing"]
    if "lv_timer_resume(s_timer)" not in body:
        return ["product_inbox_bridge.cpp: product_inbox_on_signed_in must resume inbox LVGL timer"]
    return []


def scan_login_roster_z_order(login: str) -> list[str]:
    errors: list[str] = []
    show = _function_body(login, "static void show_roster_cards(void)")
    if not show:
        return ["product_login_bridge.cpp: show_roster_cards missing"]
    fast_start = show.find("if (roster_cards_current())")
    slow_start = show.find("clear_roster_cards();")
    if fast_start < 0 or slow_start < 0:
        errors.append("product_login_bridge.cpp: show_roster_cards fast-path block missing")
    else:
        fast_body = show[fast_start:slow_start]
        if "login_desk_raise_interactive()" not in fast_body:
            errors.append(
                "product_login_bridge.cpp: roster fast-path must call login_desk_raise_interactive"
            )
    if "lv_obj_move_foreground(s_roster_row)" not in show:
        errors.append("product_login_bridge.cpp: show_roster_cards must move roster to foreground")
    if "login_desk_raise_interactive()" not in show:
        errors.append("product_login_bridge.cpp: show_roster_cards must park chrome under roster")
    if "login_desk_raise_interactive" not in login:
        errors.append("product_login_bridge.cpp: login_desk_raise_interactive missing")
    timer = _function_body(login, "void on_login_timer(lv_timer_t *timer)")
    if "login_desk_raise_interactive();" in timer:
        errors.append(
            "product_login_bridge.cpp: on_login_timer must not raise every tick (fights touch)"
        )
    pin = _function_body(login, "static void render_pin_screen(void)")
    if pin and "login_desk_raise_interactive()" not in pin:
        errors.append("product_login_bridge.cpp: render_pin_screen must raise desk on entry")
    if "on_roster_card_pressed" not in login:
        errors.append("product_login_bridge.cpp: roster cards must handle LV_EVENT_PRESSED")
    return errors


def scan_overlay_park_under_login(login: str, inbox: str, volume: str) -> list[str]:
    errors: list[str] = []
    if "product_inbox_push_under_login" not in login:
        errors.append("product_login_bridge.cpp: must park inbox under login on signed-out render")
    if "product_volume_push_under_login" not in login:
        errors.append("product_login_bridge.cpp: must park volume under login on signed-out render")
    inbox_push = _function_body(inbox, "void product_inbox_push_under_login(void)")
    if "lv_obj_move_background(s_lower_root)" not in inbox_push:
        errors.append("product_inbox_bridge.cpp: push_under_login must move lower root to background")
    if "LV_OBJ_FLAG_CLICKABLE" not in inbox_push:
        errors.append("product_inbox_bridge.cpp: push_under_login must clear CLICKABLE on inbox roots")
    if "void product_volume_push_under_login(void)" not in volume:
        errors.append("product_volume_bridge.cpp: product_volume_push_under_login missing")
    return errors


def scan_mute_round_trip(root: Path | None = None) -> list[str]:
    base = root if root is not None else DEVICE_MAIN
    login = (base / "product_login_bridge.cpp").read_text(encoding="utf-8")
    inbox = (base / "product_inbox_bridge.cpp").read_text(encoding="utf-8")
    volume = (base / "product_volume_bridge.cpp").read_text(encoding="utf-8")
    hw = (base / "product_hardware_buttons.c").read_text(encoding="utf-8")
    errors: list[str] = []
    errors.extend(scan_hardware_poll_uses_level_not_press_edge(hw))
    errors.extend(scan_login_timer_mute_path(login))
    errors.extend(scan_login_on_mute_level(login))
    errors.extend(scan_inbox_on_mute(inbox))
    errors.extend(scan_inbox_attach_pauses_timer(inbox))
    errors.extend(scan_inbox_signed_in_resume(inbox))
    errors.extend(scan_login_roster_z_order(login))
    errors.extend(scan_overlay_park_under_login(login, inbox, volume))
    return errors
