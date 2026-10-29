# C/C++ code formatting

Status: normative for active repository C/C++ sources  
Does not apply to: `poc-v1/` (read-only archive), vendored trees (`managed_components/`, `external/`)

## Tooling

- **[clang-format](https://clang.llvm.org/docs/ClangFormat.html)** with the repo-root [`.clang-format`](../../.clang-format) file.
- Base style: **LLVM**, with **4-space** indentation and **Linux** brace rules (function bodies start on the next line).
- **[EditorConfig](https://editorconfig.org/)** ([`.editorconfig`](../../.editorconfig)) keeps basic indent width aligned in editors.

Include order is **not** re-sorted (`SortIncludes: false`) so ESP-IDF and conditional LVGL headers stay stable.

## Commands

After `make install` (venv includes `clang-format`):

```bash
make format        # clang-format in scope + LF normalization for tracked text
make format.check  # exit non-zero if either step would change files
make eol.fix       # LF only (also run by make format)
make eol.check     # LF check only (also run by make format.check)
```

Scope: `client/` and `server/` C/C++ sources, excluding `poc-v1/`, `managed_components/`, and `build/`.

## Line endings

Tracked **text** files use **LF** only. Exceptions:

- `*.bat` and `*.cmd` keep **CRLF** (Windows launchers).
- Binary assets (for example `*.png`, `*.wav`) are not rewritten.

Git enforces this via [`.gitattributes`](../../.gitattributes). **`make format`** runs LF normalization after clang-format; use **`make eol.fix`** / **`make eol.check`** when you only need line endings.

On Windows, IDE and Agent file writes may still produce CRLF in the working tree even though `.editorconfig` specifies LF. Agents should follow [`.cursor/rules/line-endings.mdc`](../../.cursor/rules/line-endings.mdc) and run **`make eol.fix`** after substantive text edits.

HTTP and serial fixtures may still contain `\r\n` **inside string or byte literals**; that is wire format, not file line endings.

## Editor

Cursor / VS Code: use **Format Document** on `.c` / `.cpp` files; clangd and the Microsoft C/C++ extension both read `.clang-format` when `"C_Cpp.clang_format_style": "file"` is set (see [`.vscode/settings.json`](../../.vscode/settings.json)).
