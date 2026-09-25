"""Copy the plugin's shaders into demo/plugin.js, by script.

    python3 demo/tools/splice_shaders.py

The page's claim is that it runs the plugin's OWN shaders, so `demo/plugin.js`
carries `source/Shaders.cpp`'s four string constants as template literals, and
`check_shaders.py` fails `tools/verify.sh` if a character of any of them
differs. Hand-copying a 100-line raw string is exactly how a character
differs, so this does the copy: it replaces everything between the
`// @@shaders` markers in plugin.js with the current C++ text, tabs and all.

A backtick, a backslash or a `${` cannot be carried by a template literal
unchanged, and is refused here rather than escaped. The four shaders hold none.

Run it after any change to Shaders.cpp, then commit plugin.js with the C++. It
never touches anything outside the markers.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

SNIPPETS = [
    ("VERTEX", "kVertexShader"),
    ("COPY", "kCopyShader"),
    ("MEANS", "kMeansShader"),
    ("BOARD", "kBoardShader"),
]

BEGIN = "// @@shaders-begin -- written by demo/tools/splice_shaders.py, do not edit\n"
END = "// @@shaders-end\n"


def from_cpp(source, symbol):
    match = re.search(r'const char\* const ' + symbol + r' = R"\((.*?)\)";', source, re.S)
    return None if match is None else match.group(1)


def main():
    with open(os.path.join(REPO, "source", "Shaders.cpp")) as handle:
        cpp = handle.read()
    js_path = os.path.join(REPO, "demo", "plugin.js")
    with open(js_path) as handle:
        js = handle.read()

    block = ""
    for name, symbol in SNIPPETS:
        body = from_cpp(cpp, symbol)
        if body is None:
            raise SystemExit(f"{symbol} not found in source/Shaders.cpp")
        for bad in ("${", "`", "\\"):
            if bad in body:
                raise SystemExit(f"{symbol} contains {bad!r}, which a template literal cannot carry unchanged")
        block += f"\nconst {name} = `{body}`;\n"
    block += "\n"

    start = js.find(BEGIN)
    stop = js.find(END)
    if start < 0 or stop < 0 or stop < start:
        raise SystemExit("the @@shaders markers are not in demo/plugin.js")
    js = js[: start + len(BEGIN)] + block + js[stop:]
    with open(js_path, "w") as handle:
        handle.write(js)
    print(f"spliced {len(SNIPPETS)} shaders into demo/plugin.js")
    return 0


if __name__ == "__main__":
    sys.exit(main())
