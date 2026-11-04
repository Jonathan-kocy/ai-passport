#!/usr/bin/env python3
"""Font coverage contract for the Flappy Bird UI.

The game is English-only and uses LVGL's built-in Montserrat fonts, which cover
printable ASCII (0x20-0x7E) plus LVGL symbol glyphs. This check fails when:
  * a string literal in the game sources contains a character outside that
    range (it would render as a missing-glyph box), or
  * the UI references a Montserrat size that sdkconfig.defaults does not enable.
It proves source-level coverage only; on-device rendering is a separate check.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
UI_SOURCES = sorted((ROOT / "main").glob("flappy_*.[ch]"))
STRING_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
FONT_RE = re.compile(r"lv_font_montserrat_(\d+)")
SYMBOL_RE = re.compile(r"LV_SYMBOL_[A-Z0-9_]+")
ALLOWED_SYMBOLS = {
    "LV_SYMBOL_BATTERY_EMPTY", "LV_SYMBOL_BATTERY_1", "LV_SYMBOL_BATTERY_2",
    "LV_SYMBOL_BATTERY_3", "LV_SYMBOL_BATTERY_FULL",
}


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


class FlappyFontCoverageTest(unittest.TestCase):
    def test_sources_exist(self) -> None:
        names = {p.name for p in UI_SOURCES}
        self.assertIn("flappy_ui.c", names)
        self.assertIn("flappy_text.h", names)

    def test_string_literals_are_printable_ascii(self) -> None:
        for path in UI_SOURCES:
            code = strip_comments(path.read_text(encoding="utf-8"))
            for literal in STRING_RE.findall(code):
                for ch in literal:
                    self.assertTrue(
                        0x20 <= ord(ch) <= 0x7E,
                        f"{path.name}: non-ASCII {ch!r} in \"{literal}\"",
                    )

    def test_used_font_sizes_are_enabled(self) -> None:
        config = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
        ui = strip_comments((ROOT / "main" / "flappy_ui.c").read_text(encoding="utf-8"))
        sizes = set(FONT_RE.findall(ui))
        self.assertTrue(sizes, "UI uses no Montserrat font")
        for size in sorted(sizes, key=int):
            self.assertRegex(
                config, rf"(?m)^CONFIG_LV_FONT_MONTSERRAT_{size}=y$",
                f"lv_font_montserrat_{size} is used but not enabled",
            )

    def test_only_known_symbols(self) -> None:
        for path in UI_SOURCES:
            code = strip_comments(path.read_text(encoding="utf-8"))
            for symbol in SYMBOL_RE.findall(code):
                self.assertIn(symbol, ALLOWED_SYMBOLS, f"{path.name}: {symbol}")


if __name__ == "__main__":
    unittest.main()
