"""Source scans for docs/standards/device-presentation-invariants.md.

Each scan takes source text (or a name → text map) so negative fixtures can be
fed in directly. Scans assert invariants, never the presence of a helper.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEVICE_MAIN = ROOT / "client/device/main"

LOGIN = "product_login_bridge.cpp"
ROUTER = "product_stage_router.cpp"
APP_MAIN = "app_main.c"

VISIBILITY_SETTERS = (
    "product_login_present",
    "product_inbox_set_ui_visible",
    "product_volume_set_visible",
    "product_send_present",
    "product_record_set_visible",
)

TICK_PHASES = (
    "product_hardware_buttons_poll",
    "flush_wifi_schedule",
    "product_login_http_service",
    "note_sign_in_edge",
    "product_inbox_service",
    "product_send_service",
    "product_record_service",
    "product_stage_present",
)

Z_ORDER_CALLS = ("lv_obj_move_foreground", "lv_obj_move_background", "lv_obj_swap")

RENDER_FUNCTIONS = ("render_login_copy", "render_pin_screen")
RENDER_FORBIDDEN = (
    "product_stage_set",
    "product_stage_",
    "product_inbox_",
    "product_send_",
    "product_volume_",
    "product_record_",
    "note_sign_in_edge",
    "wake_login_display",
    "blank_login_display",
    "lv_indev_reset",
    "bsp_display_",
    "lv_obj_align",
    "lv_obj_center",
    "lv_obj_move_",
)

MUTE_FUNCTIONS = ("product_login_on_mute_level", "enter_muted", "leave_muted")
MUTE_FORBIDDEN = ("render_", "product_stage_present", "lv_indev_reset", "lv_refr_now")


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def _match_closer(text: str, open_index: int) -> int:
    depth = 0
    for i in range(open_index, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return -1


def function_body(text: str, name: str) -> str:
    """Body of the first definition of `name` (a call followed by `{`)."""
    for match in re.finditer(rf"\b{re.escape(name)}\s*\([^;{{)]*\)\s*\{{", text):
        brace = text.find("{", match.start())
        end = _match_closer(text, brace)
        if end > brace:
            return text[brace + 1 : end]
    return ""


def _calls(text: str, name: str) -> list[int]:
    """Offsets of calls to `name`, excluding its definition and declarations."""
    found = []
    for match in re.finditer(rf"\b{re.escape(name)}\s*\(", text):
        line_start = text.rfind("\n", 0, match.start()) + 1
        prefix = text[line_start : match.start()]
        typed = re.fullmatch(r'\s*(extern\s+"C"\s+)?((static|inline|const)\s+)*([\w:]+)[\s*]+', prefix)
        if typed and typed.group(4) not in ("return", "else", "case", "do"):
            continue
        found.append(match.start())
    return found


def load_sources(base: Path | None = None) -> dict[str, str]:
    root = base if base is not None else DEVICE_MAIN
    return {
        path.name: strip_comments(path.read_text(encoding="utf-8"))
        for path in sorted(root.iterdir())
        if path.suffix in (".c", ".cpp")
    }


def scan_single_timer(sources: dict[str, str]) -> list[str]:
    """PRES-TICK: exactly one lv_timer, the login presentation tick."""
    sites = [
        (name, match.group(1))
        for name, text in sources.items()
        for match in re.finditer(r"\blv_timer_create\s*\(\s*(\w*)", text)
    ]
    if len(sites) != 1:
        where = ", ".join(sorted({name for name, _ in sites})) or "none"
        return [f"PRES-TICK: expected exactly one lv_timer_create, found {len(sites)} ({where})"]
    name, callback = sites[0]
    if name != LOGIN or callback != "on_login_timer":
        return [f"PRES-TICK: the only lv_timer must be on_login_timer in {LOGIN} (found {callback} in {name})"]
    return []


def scan_tick_phase_order(login: str) -> list[str]:
    """PRES-ORDER and PRES-NORETURN."""
    body = function_body(login, "on_login_timer")
    if not body:
        return ["PRES-ORDER: on_login_timer missing"]
    errors: list[str] = []
    if re.search(r"\breturn\b", body):
        errors.append("PRES-NORETURN: on_login_timer must not return early")
    last_name, last_at = "", -1
    for phase in TICK_PHASES:
        calls = _calls(body, phase)
        if not calls:
            errors.append(f"PRES-ORDER: on_login_timer must call {phase}")
            continue
        if calls[0] < last_at:
            errors.append(f"PRES-ORDER: {phase} must run after {last_name}")
        last_name, last_at = phase, calls[0]
    present = _calls(body, "product_stage_present")
    if present and any(m.start() > present[-1] for m in re.finditer(r"\b(render_\w+|product_\w+_service)\s*\(", body)):
        errors.append("PRES-ORDER: nothing may render or service after product_stage_present")
    signed = body.find("if (s_session.signed_in() != 0)")
    other = body.find("} else {", signed)
    if signed < 0 or other < 0:
        errors.append("PRES-ORDER: inbox service must be gated on signed_in with a signed-out branch")
    else:
        if "product_inbox_service" not in body[signed:other]:
            errors.append("PRES-ORDER: product_inbox_service runs only while signed in")
        if "product_inbox_discard_results" not in body[other:]:
            errors.append("PRES-ORDER: signed-out ticks must call product_inbox_discard_results")
    return errors


def scan_single_visibility_owner(sources: dict[str, str]) -> list[str]:
    """PRES-OWNER: only product_stage_present calls module visibility setters."""
    errors: list[str] = []
    for name, text in sources.items():
        for setter in VISIBILITY_SETTERS:
            calls = _calls(text, setter)
            if not calls:
                continue
            if name != ROUTER:
                errors.append(f"PRES-OWNER: {name} calls {setter}; only {ROUTER} may")
                continue
            present = function_body(text, "product_stage_present")
            if len(_calls(present, setter)) != len(calls):
                errors.append(f"PRES-OWNER: {setter} called outside product_stage_present")
    return errors


def scan_no_z_order(sources: dict[str, str]) -> list[str]:
    """PRES-HIDDEN: hiding gates input; no sibling reordering."""
    return [
        f"PRES-HIDDEN: {name} calls {call}"
        for name, text in sources.items()
        for call in Z_ORDER_CALLS
        if _calls(text, call)
    ]


def scan_single_pointer_reset(sources: dict[str, str]) -> list[str]:
    """PRES-POINTER: one lv_indev_reset, inside wake_login_display."""
    total = sum(len(_calls(text, "lv_indev_reset")) for text in sources.values())
    wake = function_body(sources.get(LOGIN, ""), "wake_login_display")
    if total != 1 or len(_calls(wake, "lv_indev_reset")) != 1:
        return [f"PRES-POINTER: expected one lv_indev_reset inside wake_login_display, found {total} in total"]
    return []


def scan_display_power(sources: dict[str, str]) -> list[str]:
    """PRES-POWER: brightness/backlight only at boot and blank/wake; never BSP sleep."""
    errors: list[str] = []
    allowed = {
        LOGIN: ("wake_login_display", "blank_login_display"),
        APP_MAIN: ("app_main", "boot_backlight_after_display"),
        "product_app_run.c": ("product_app_run",),
        "product_network.c": ("join_configured_wifi",),
    }
    for name, text in sources.items():
        for call in ("bsp_display_exit_sleep", "bsp_display_enter_sleep"):
            if _calls(text, call):
                errors.append(f"PRES-POWER: {name} calls {call} (breaks BOX-3 touch INT)")
        for call in ("bsp_display_brightness_set", "bsp_display_backlight_off", "bsp_display_backlight_on"):
            count = len(_calls(text, call))
            if count == 0:
                continue
            inside = sum(len(_calls(function_body(text, fn), call)) for fn in allowed.get(name, ()))
            if inside != count:
                errors.append(f"PRES-POWER: {name} calls {call} outside the boot/blank/wake owners")
    return errors


def scan_mute_reconcile(login: str, hw: str) -> list[str]:
    """PRES-MUTE: level sampled every poll; reconcile never renders."""
    errors: list[str] = []
    poll = function_body(hw, "product_hardware_buttons_poll")
    if "product_login_on_mute_level(product_hardware_buttons_mute_latched())" not in poll:
        errors.append("PRES-MUTE: the button poll must pass the latch level every tick")
    for fn in MUTE_FUNCTIONS:
        body = function_body(login, fn)
        if not body:
            errors.append(f"PRES-MUTE: {fn} missing")
            continue
        for forbidden in MUTE_FORBIDDEN:
            if re.search(rf"\b{re.escape(forbidden)}\w*\s*\(", body):
                errors.append(f"PRES-MUTE: {fn} must not call {forbidden}")
    level = function_body(login, "product_login_on_mute_level")
    if "enter_muted(" not in level or "leave_muted(" not in level:
        errors.append("PRES-MUTE: product_login_on_mute_level dispatches to enter_muted / leave_muted")
    for fn in ("enter_muted", "leave_muted"):
        if "product_login_http_on_mute()" not in function_body(login, fn):
            errors.append(f"PRES-MUTE: {fn} must reset the login HTTP roster fence")
    return errors


def scan_render_is_pure(login: str) -> list[str]:
    """PRES-RENDER and PRES-EDGE."""
    errors: list[str] = []
    for fn in RENDER_FUNCTIONS:
        body = function_body(login, fn)
        if not body:
            errors.append(f"PRES-RENDER: {fn} missing")
            continue
        for forbidden in RENDER_FORBIDDEN:
            if re.search(rf"\b{re.escape(forbidden)}\w*\s*\(", body):
                errors.append(f"PRES-RENDER: {fn} must not call {forbidden}")
    tick = function_body(login, "on_login_timer")
    if not _calls(tick, "note_sign_in_edge"):
        errors.append("PRES-EDGE: sign-in side effects run from note_sign_in_edge in the tick")
    return errors


def scan_presentation_invariants(base: Path | None = None) -> list[str]:
    sources = load_sources(base)
    login = sources.get(LOGIN, "")
    hw = sources.get("product_hardware_buttons.c", "")
    errors: list[str] = []
    errors.extend(scan_single_timer(sources))
    errors.extend(scan_tick_phase_order(login))
    errors.extend(scan_single_visibility_owner(sources))
    errors.extend(scan_no_z_order(sources))
    errors.extend(scan_single_pointer_reset(sources))
    errors.extend(scan_display_power(sources))
    errors.extend(scan_mute_reconcile(login, hw))
    errors.extend(scan_render_is_pure(login))
    return errors
