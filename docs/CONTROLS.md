# Keyboard controls

The keyboard plays as an Xbox 360 controller, so in-game prompts show controller buttons. A controller and the
keyboard can be used together. Every key can be changed on the launcher's **Controls** page.

| Pad input | Keyboard key (release default, WWE 2K-style) | Game built-in (no wwe13.toml) |
| --- | --- | --- |
| A (grapple, menu confirm) | L | Space |
| B (Irish whip / pin, menu back) | K | Shift |
| X (strike) | J | R |
| Y (signature / finisher) | Space | E |
| Left trigger (LT, run / climb) | Shift | Z |
| Right trigger (RT, reversal) | 1 | Control |
| Left shoulder (LB, pick up / ring in-out) | U | Q |
| Right shoulder (RB, limb targeting) | Control | F |
| Left stick up / down / left / right | W / S / A / D | W / S / A / D |
| Left stick press (L3) | C | C |
| Right stick up / down / left / right | T / G / F / H | I / K / J / L |
| Right stick press (R3) | V | M |
| D-pad up / down / left / right (taunts) | Up / Down / Left / Right | same |
| Back | Tab | Tab |
| Start | Escape | Escape |

Release defaults live in the shipped `wwe13.toml` (tools/package-release.sh) and in the
launcher (launcher2/core/controls.cpp); the launcher's Controls page edits them and adds any missing row on start.
The game's own built-in keys (ReXGlue mnk_input_driver.cpp) apply only when no wwe13.toml is present.

## Changing keys by hand

The keys are `keybind_*` settings in `wwe13.toml` next to the game executable (Windows: next to `wwe13.exe`; Linux:
in `Linux-x86_64/`). Use the flat names as top-level keys and ReXGlue's key names (`Control`, not `Ctrl`):

```toml
keybind_a = "L"
keybind_right_trigger = "1"
keybind_rstick_up = "T"
```

The mouse is not used for gameplay. `python3 tools/keymap.py list` / `set A G` edits the file from a terminal.
