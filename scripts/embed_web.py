#!/usr/bin/env python3
"""Embed the PS5 web interface in a C header."""

from pathlib import Path
import sys


def c_string(line: str) -> str:
    escaped = (
        line.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\t", "\\t")
        .replace("\r", "\\r")
        .replace("\n", "\\n")
    )
    return f'"{escaped}"'


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: embed_web.py INPUT.html OUTPUT.h", file=sys.stderr)
        return 2

    source = Path(sys.argv[1])
    output = Path(sys.argv[2])
    html = source.read_text(encoding="utf-8")
    chunks = html.splitlines(keepends=True)

    rendered = [
        "#ifndef PS5TORRENT_WEB_CONTENT_H\n",
        "#define PS5TORRENT_WEB_CONTENT_H\n\n",
        "/* Generated from src/web/index.html by scripts/embed_web.py. */\n",
        "static const char web_index_html[] =\n",
    ]
    rendered.extend(f"{c_string(chunk)}\n" for chunk in chunks)
    rendered.extend([";\n\n", "#endif /* PS5TORRENT_WEB_CONTENT_H */\n"])
    output.write_text("".join(rendered), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
