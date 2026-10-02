#!/usr/bin/env python3
"""Edit WWE '13's flat keyboard bindings without changing unrelated TOML lines."""

from __future__ import annotations

import argparse
import os
import stat
import sys
import tempfile
from pathlib import Path


DEFAULT_FILE = Path(__file__).resolve().parents[1] / "out/linux-play/wwe13.toml"

# Exact ParseVirtualKey names from ReXGlue src/ui/keybinds.cpp:kKeyNames.
KEY_NAMES = frozenset(
    [f"F{i}" for i in range(1, 25)]
    + list("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
    + list("0123456789")
    + [
        "Backtick",
        "Minus",
        "Plus",
        "Comma",
        "Period",
        "Semicolon",
        "Slash",
        "Backslash",
        "LBracket",
        "RBracket",
        "Quote",
        "Escape",
        "Return",
        "Space",
        "Tab",
        "Backspace",
        "Delete",
        "Insert",
        "Home",
        "End",
        "PageUp",
        "PageDown",
        "Left",
        "Right",
        "Up",
        "Down",
        "Shift",
        "Control",
        "Alt",
        *[f"Numpad{i}" for i in range(10)],
        "NumpadEnter",
        "NumpadPlus",
        "NumpadMinus",
        "NumpadStar",
        "NumpadSlash",
        "PrintScreen",
        "Pause",
        "CapsLock",
        "NumLock",
        "ScrollLock",
        "LMB",
        "RMB",
        "MMB",
    ]
)

# These rows track the 24 player-facing inputs/defaults from
# docs/CONTROLS.md and mnk_input_driver.cpp:35-58. The extra empty Guide cvar
# is deliberately not a player-facing pad input.
PAD_INPUTS = [
    ("A", "keybind_a", "Space", ("a", "button_a")),
    ("B", "keybind_b", "Shift", ("b", "button_b")),
    ("X", "keybind_x", "R", ("x", "button_x")),
    ("Y", "keybind_y", "E", ("y", "button_y")),
    ("Left trigger (LT)", "keybind_left_trigger", "Z", ("left_trigger", "lt")),
    ("Right trigger (RT)", "keybind_right_trigger", "Control", ("right_trigger", "rt")),
    ("Left shoulder (LB)", "keybind_left_shoulder", "Q", ("left_shoulder", "lb")),
    ("Right shoulder (RB)", "keybind_right_shoulder", "F", ("right_shoulder", "rb")),
    ("Left stick up", "keybind_lstick_up", "W", ("left_stick_up",)),
    ("Left stick down", "keybind_lstick_down", "S", ("left_stick_down",)),
    ("Left stick left", "keybind_lstick_left", "A", ("left_stick_left",)),
    ("Left stick right", "keybind_lstick_right", "D", ("left_stick_right",)),
    ("Left stick press (L3)", "keybind_lstick_press", "C", ("left_stick_press", "l3")),
    ("Right stick up", "keybind_rstick_up", "I", ("right_stick_up",)),
    ("Right stick down", "keybind_rstick_down", "K", ("right_stick_down",)),
    ("Right stick left", "keybind_rstick_left", "J", ("right_stick_left",)),
    ("Right stick right", "keybind_rstick_right", "L", ("right_stick_right",)),
    (
        "Right stick press (R3)",
        "keybind_rstick_press",
        "M",
        ("right_stick_press", "r3"),
    ),
    ("D-pad up", "keybind_dpad_up", "Up", ("dpad_up",)),
    ("D-pad down", "keybind_dpad_down", "Down", ("dpad_down",)),
    ("D-pad left", "keybind_dpad_left", "Left", ("dpad_left",)),
    ("D-pad right", "keybind_dpad_right", "Right", ("dpad_right",)),
    ("Back", "keybind_back", "Tab", ("back",)),
    ("Start", "keybind_start", "Escape", ("start",)),
]

PAD_BY_CVAR = {cvar: (label, default) for label, cvar, default, _ in PAD_INPUTS}


class KeymapError(Exception):
    pass


def normalize_input(value: str) -> str:
    return "".join(character.lower() for character in value if character.isalnum())


PAD_ALIASES: dict[str, str] = {}
for label, cvar, _default, aliases in PAD_INPUTS:
    for alias in (label, cvar, *aliases):
        PAD_ALIASES[normalize_input(alias)] = cvar


def parse_binding_line(line: str) -> tuple[str, int, int, str | None] | None:
    """Return cvar, replace-start/end, and string value for one flat assignment."""
    cursor = 3 if line.startswith("\ufeff") else 0
    while cursor < len(line) and line[cursor] in " \t":
        cursor += 1
    key_begin = cursor
    while cursor < len(line) and (line[cursor].isalnum() or line[cursor] == "_"):
        cursor += 1
    cvar = line[key_begin:cursor]
    if cvar not in PAD_BY_CVAR:
        return None
    while cursor < len(line) and line[cursor] in " \t":
        cursor += 1
    if cursor == len(line) or line[cursor] != "=":
        return None
    cursor += 1
    while cursor < len(line) and line[cursor] in " \t":
        cursor += 1
    value_begin = cursor
    comment = line.find("#", cursor)
    fallback_end = len(line) if comment < 0 else comment
    while fallback_end > cursor and line[fallback_end - 1] in " \t":
        fallback_end -= 1
    value_end = fallback_end

    if cursor < len(line) and line[cursor] in "\"'":
        quote = line[cursor]
        cursor += 1
        value: list[str] = []
        while cursor < len(line):
            character = line[cursor]
            if character == quote:
                return cvar, value_begin, cursor + 1, "".join(value)
            if character == "\\" and quote == '"':
                cursor += 1
                if cursor >= len(line) or line[cursor] not in ('"', "\\"):
                    return cvar, value_begin, value_end, None
                character = line[cursor]
            value.append(character)
            cursor += 1
        return cvar, value_begin, value_end, None
    return cvar, value_begin, value_end, None


def line_body_and_ending(line: str) -> tuple[str, str]:
    if line.endswith("\r\n"):
        return line[:-2], "\r\n"
    if line.endswith("\n"):
        return line[:-1], "\n"
    if line.endswith("\r"):
        return line[:-1], "\r"
    return line, ""


def is_table_header(line: str) -> bool:
    cursor = 1 if line.startswith("\ufeff") else 0
    while cursor < len(line) and line[cursor] in " \t":
        cursor += 1
    return cursor < len(line) and line[cursor] == "["


def read_config(path: Path) -> tuple[str, dict[str, str], set[str]]:
    if not path.exists():
        return "", {}, set()
    try:
        text = path.read_bytes().decode("utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        raise KeymapError(f"cannot read UTF-8 TOML file {path}: {exc}") from exc
    bindings: dict[str, str] = {}
    present: set[str] = set()
    root_table = True
    for raw_line in text.splitlines(keepends=True):
        body, _ending = line_body_and_ending(raw_line)
        if root_table and is_table_header(body):
            root_table = False
        if not root_table:
            continue
        parsed = parse_binding_line(body)
        if parsed is not None:
            cvar, _begin, _end, value = parsed
            present.add(cvar)
            if value is not None:
                bindings[cvar] = value
    return text, bindings, present


def atomic_write(path: Path, content: bytes, mode: int | None = None) -> None:
    temp_name: str | None = None
    try:
        fd, temp_name = tempfile.mkstemp(prefix=path.name + ".tmp.", dir=path.parent)
        with os.fdopen(fd, "wb") as output:
            output.write(content)
            output.flush()
            os.fsync(output.fileno())
        if mode is not None:
            os.chmod(temp_name, stat.S_IMODE(mode))
        os.replace(temp_name, path)
        temp_name = None
        try:
            directory_fd = os.open(path.parent, os.O_RDONLY)
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
        except OSError:
            pass
    except OSError as exc:
        raise KeymapError(f"cannot atomically write {path}: {exc}") from exc
    finally:
        if temp_name is not None:
            try:
                os.unlink(temp_name)
            except OSError:
                pass


def merge_config(path: Path, updates: dict[str, str], present_before: set[str]) -> None:
    text, _current, _present_now = read_config(path)
    # If the file changed since the command collected its updates, keep its
    # unrelated lines and all current bindings not named in updates.
    lines = text.splitlines(keepends=True)
    newline = "\r\n" if "\r\n" in text else "\n"
    merged: list[str] = []
    found: set[str] = set()
    insertion_position: int | None = None
    root_table = True
    for raw_line in lines:
        body, ending = line_body_and_ending(raw_line)
        if root_table and is_table_header(body):
            root_table = False
            insertion_position = len(merged)
            merged.append(raw_line)
            continue
        if not root_table:
            merged.append(raw_line)
            continue
        parsed = parse_binding_line(body)
        if parsed is None:
            merged.append(raw_line)
            continue
        cvar, begin, end, _value = parsed
        if cvar not in updates:
            merged.append(raw_line)
            continue
        found.add(cvar)
        replacement = f'"{updates[cvar]}"'
        merged.append(body[:begin] + replacement + body[end:] + ending)

    if insertion_position is None:
        insertion_position = len(merged)
    additions: list[str] = []
    for _label, cvar, default, _aliases in PAD_INPUTS:
        if cvar in found:
            continue
        value = updates.get(cvar)
        if value is None or (cvar not in present_before and value == default):
            continue
        if not additions and insertion_position > 0 and not merged[insertion_position - 1].endswith(("\n", "\r")):
            additions.append(newline)
        additions.append(f'{cvar} = "{value}"{newline}')
    merged[insertion_position:insertion_position] = additions

    new_text = "".join(merged)
    if new_text == text:
        return
    existed = path.exists()
    old_bytes = text.encode("utf-8")
    mode = path.stat().st_mode if existed else None
    if existed:
        atomic_write(path.with_name(path.name + ".bak"), old_bytes, mode)
    atomic_write(path, new_text.encode("utf-8"), mode)


def list_bindings(path: Path) -> None:
    _text, values, present = read_config(path)
    print(f"Bindings file: {path} ({'present' if path.exists() else 'missing; defaults in use'})")
    print(f"{'Pad input':30} {'Current key':18} {'Default':12} Source")
    for label, cvar, default, _aliases in PAD_INPUTS:
        value = values.get(cvar, default)
        valid = value in KEY_NAMES
        if cvar in present and cvar not in values:
            shown = "[unreadable TOML value]"
        else:
            shown = value if valid else f"{value} [not accepted]"
        source = "file" if cvar in present else "default"
        print(f"{label:30} {shown:18} {default:12} {source}")


def run(args: argparse.Namespace) -> int:
    path: Path = args.file.expanduser()
    if args.command == "list":
        list_bindings(path)
        return 0

    text, values, present = read_config(path)
    del text
    if args.command == "set":
        pad = PAD_ALIASES.get(normalize_input(args.pad_input))
        if pad is None:
            names = ", ".join(label for label, *_rest in PAD_INPUTS)
            raise KeymapError(f"unknown pad input {args.pad_input!r}; choose one of: {names}")
        if args.key not in KEY_NAMES:
            raise KeymapError(
                f"unsupported key name {args.key!r}; names are case-sensitive and must match "
                "ReXGlue ParseVirtualKey"
            )
        label, default = PAD_BY_CVAR[pad]
        merge_config(path, {pad: args.key}, present)
        print(f"Set {label} ({pad}) to {args.key} in {path}")
        return 0

    if args.command == "reset":
        updates = {
            cvar: default
            for _label, cvar, default, _aliases in PAD_INPUTS
            if cvar in present
        }
        if updates:
            merge_config(path, updates, present)
        print(f"Reset {len(updates)} existing binding(s) to defaults in {path}")
        return 0

    raise KeymapError(f"unsupported command: {args.command}")


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--file",
        type=Path,
        default=DEFAULT_FILE,
        help=f"TOML config path (default: {DEFAULT_FILE})",
    )
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("list", help="show current bindings and defaults")
    set_parser = commands.add_parser("set", help="set one pad input to an accepted key")
    set_parser.add_argument("pad_input", help="pad label, alias, or keybind_* cvar")
    set_parser.add_argument("key", help="exact case-sensitive ParseVirtualKey name")
    commands.add_parser("reset", help="reset existing keybind lines to defaults")
    return parser


def main() -> int:
    args = make_parser().parse_args()
    try:
        return run(args)
    except KeymapError as exc:
        print(f"keymap.py: error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
