#!/usr/bin/env python3
"""Copy web/volt2.js into volt2.hpp, between the <volt2-js> markers.

    python3 tools/embed_js.py          # update volt2.hpp
    python3 tools/embed_js.py --check  # fail if volt2.hpp is out of date

The script is split into raw string literals of at most ~12 KB, because MSVC
rejects single string literals longer than about 16 KB. It must be ASCII, since
MSVC reads source files in the local code page unless /utf-8 is given.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'web' / 'volt2.js'
HEADER = ROOT / 'volt2.hpp'
BEGIN, END = '// <volt2-js>\n', '// </volt2-js>\n'
DELIMITER = 'VOLT2JS'
CHUNK = 12000


def render(script):
    if not script.isascii():
        raise SystemExit('web/volt2.js must be ASCII so MSVC reads it correctly')
    if ')' + DELIMITER + '"' in script:
        raise SystemExit('web/volt2.js contains the raw string delimiter')
    parts, current = [], ''
    for line in script.splitlines(keepends=True):
        if current and len(current) + len(line) > CHUNK:
            parts.append(current)
            current = ''
        current += line
    if current:
        parts.append(current)
    body = ''.join(f'R"{DELIMITER}({part}){DELIMITER}",\n' for part in parts)
    return f'{BEGIN}inline const char* const kClientJsParts[] = {{\n{body}}};\n{END}'


def main():
    header = HEADER.read_text(encoding='utf-8')
    start, stop = header.index(BEGIN), header.index(END) + len(END)
    updated = header[:start] + render(SOURCE.read_text(encoding='utf-8')) + header[stop:]
    if '--check' in sys.argv:
        if updated != header:
            raise SystemExit('volt2.hpp is out of date: run python3 tools/embed_js.py')
        print('volt2.hpp embeds the current web/volt2.js')
    elif updated != header:
        HEADER.write_text(updated, encoding='utf-8')
        print('volt2.hpp updated')
    else:
        print('volt2.hpp already up to date')


if __name__ == '__main__':
    main()
