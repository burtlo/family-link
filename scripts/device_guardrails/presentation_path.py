"""LVGL login-timer reachability scan (PRES-PATH, STK-PATH).

Workers may block. Only functions reachable from ``on_login_timer`` by a direct
call, or by a function-pointer hook assigned in a reached function, are scanned.
Task entry points passed to ``xTaskCreate`` are not followed.
"""
from __future__ import annotations

import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEVICE_MAIN = REPO / "client/device/main"
_KEYWORDS = {
    "if",
    "for",
    "while",
    "switch",
    "catch",
    "return",
    "sizeof",
    "defined",
}

_FORBIDDEN = (
    (re.compile(r"\besp_http_client_perform\s*\("), "esp_http_client_perform on timer path"),
    (re.compile(r"\bbsp_sdcard_mount\s*\("), "bsp_sdcard_mount on timer path"),
    (
        re.compile(r"\bxQueueReceive\s*\([^;]*\bportMAX_DELAY\b"),
        "unbounded xQueueReceive on timer path",
    ),
    (
        re.compile(r"\bxSemaphoreTake\s*\([^;]*\bportMAX_DELAY\b"),
        "unbounded xSemaphoreTake on timer path",
    ),
)

_DEF = re.compile(
    r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(([^;{}]*)\)\s*(?:const\s*)?\{"
)
_CALL = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
_ASSIGN = re.compile(r"=\s*&?\s*([A-Za-z_][A-Za-z0-9_]*)\b")


def _strip_c_comments(text: str) -> str:
    without_block = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    without_line = re.sub(r"//[^\n]*", "", without_block)
    return without_line.replace('extern "C"', " ")


def _brace_body(source: str, open_index: int) -> str | None:
    depth = 0
    for index in range(open_index, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[open_index : index + 1]
    return None


def _http_result_local_lines(body: str) -> list[str]:
    hits: list[str] = []
    for line in body.splitlines():
        stripped = line.strip()
        if re.search(r"\bHttpResult\s+\w+", stripped) and not re.match(
            r"static\s+HttpResult\s+\w+", stripped
        ):
            hits.append(stripped)
    return hits


def _definitions(text: str) -> list[tuple[str, str]]:
    found: list[tuple[str, str]] = []
    for match in _DEF.finditer(text):
        name = match.group(1)
        if name in _KEYWORDS:
            continue
        body = _brace_body(text, match.end() - 1)
        if body is None:
            continue
        found.append((name, body))
    return found


def _index_sources(files: list[Path]) -> dict[str, list[tuple[str, str]]]:
    index: dict[str, list[tuple[str, str]]] = {}
    for path in files:
        text = _strip_c_comments(path.read_text(encoding="utf-8"))
        for name, body in _definitions(text):
            index.setdefault(name, []).append((path.name, body))
    return index


def _source_files(root: Path) -> list[Path]:
    return sorted(list(root.glob("*.c")) + list(root.glob("*.cpp")))


def _successors(body: str, index: dict[str, list[tuple[str, str]]]) -> set[str]:
    names: set[str] = set()
    for match in _CALL.finditer(body):
        name = match.group(1)
        if name not in _KEYWORDS and name in index:
            names.add(name)
    for match in _ASSIGN.finditer(body):
        name = match.group(1)
        if name in index:
            names.add(name)
    return names


def _scan_index(index: dict[str, list[tuple[str, str]]], roots: tuple[str, ...]) -> list[str]:
    errors: list[str] = []
    seen: set[str] = set()
    queue = [name for name in roots if name in index]
    missing = [name for name in roots if name not in index]
    for name in missing:
        errors.append(f"timer entry not found: {name}")
    while queue:
        name = queue.pop(0)
        if name in seen:
            continue
        seen.add(name)
        for filename, body in index.get(name, []):
            for pattern, message in _FORBIDDEN:
                if pattern.search(body):
                    errors.append(f"{filename}:{name}: {message}")
            if _http_result_local_lines(body):
                errors.append(f"{filename}:{name}: non-static HttpResult on timer path")
            for successor in sorted(_successors(body, index)):
                if successor not in seen:
                    queue.append(successor)
    return errors


def scan_timer_path(source_root: Path | None = None) -> list[str]:
    """Violations reachable from on_login_timer under client/device/main."""
    root = source_root or DEVICE_MAIN
    index = _index_sources(_source_files(root))
    return _scan_index(index, ("on_product_timer",))


def scan_text_for_timer_violation(
    sample: str, function_name: str = "product_record_service"
) -> list[str]:
    """Scan one function body (negative fixtures that name a single callee)."""
    text = _strip_c_comments(sample)
    body = None
    for name, found in _definitions(text):
        if name == function_name:
            body = found
            break
    if body is None:
        return [f"could not parse function {function_name}"]
    errors: list[str] = []
    for pattern, message in _FORBIDDEN:
        if pattern.search(body):
            errors.append(message)
    if _http_result_local_lines(body):
        errors.append("non-static HttpResult on timer path")
    return errors


def scan_snippet(sample: str, entry: str = "on_login_timer") -> list[str]:
    """Scan a multi-function snippet starting at entry, including callees."""
    text = _strip_c_comments(sample)
    index: dict[str, list[tuple[str, str]]] = {}
    for name, body in _definitions(text):
        index.setdefault(name, []).append(("snippet", body))
    return _scan_index(index, (entry,))
