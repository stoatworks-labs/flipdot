"""The demo's shaders must be the plugin's shaders, character for character.

    python3 demo/tools/check_shaders.py

Called from `tools/verify.sh`. Exit code 1 means the two copies have drifted.
Galvo's shape, by way of splitflap's, for a plugin whose shaders are four
whole programs rather than snippets to be joined.

------------------------------------------------------------------- why

`source/Shaders.cpp` holds four GLSL string constants -- the vertex shader,
the copy pass, the means pass and the board -- and `demo/plugin.js` holds the
same four as template literals. That is two copies of the same text, and two
copies drift -- quietly, because a demo that renders a *plausible* sign looks
exactly like a demo that renders the right one. The whole claim of these
pages is that they run the plugin's own shader rather than something
reimplemented to look similar, so the claim needs something enforcing it.

Nothing else can. `fdtest` drives the real plugin class through a real FFGL
sequence and has no idea this page exists; `tools/mutate.sh` mutates the C++
copies and never looks at the JS one.

------------------------------------------------------------------- what it does

Pulls each `R"( ... )"` body out of the C++ and each matching backtick literal
out of `plugin.js`, and compares them exactly -- no whitespace normalisation,
no comment stripping. A comment updated on one side and not the other is
exactly the drift worth catching, because comments in this repo carry the
reasoning that justifies the code.

A template literal cannot carry a backtick, a backslash or a `${` unchanged.
None of the four shaders contains any of them today; `splice_shaders.py`
refuses to copy one that does, and this refuses a JS copy holding a backslash,
so nothing can be hidden behind an escape.

`demo/tools/splice_shaders.py` writes the block from the C++; run it rather
than editing plugin.js by hand.

------------------------------------------------------------------- what it cannot

Nothing here checks the *ported* half: the sign, the driver, the disc profile,
the dither, the control laws and the frame sequence in `demo/sign.js`. That is
`demo/tools/check_port.sh`'s job, against the plugin's own C++, on its cases.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

# JS constant, C++ symbol. Every string constant in Shaders.cpp, in its order.
SNIPPETS = [
    ("VERTEX", "kVertexShader"),
    ("COPY", "kCopyShader"),
    ("MEANS", "kMeansShader"),
    ("BOARD", "kBoardShader"),
]


def from_cpp(source, symbol):
    match = re.search(r'const char\* const ' + symbol + r' = R"\((.*?)\)";', source, re.S)
    return None if match is None else match.group(1)


def from_js(source, name):
    match = re.search(r'^const ' + name + r' = `(.*?)`;$', source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = body.find("\\")
    if stray >= 0:
        return None, f"backslash at line {body[:stray].count(chr(10)) + 1}"
    return body, None


def main():
    with open(os.path.join(REPO, "source", "Shaders.cpp")) as handle:
        cpp = handle.read()
    with open(os.path.join(REPO, "demo", "plugin.js")) as handle:
        js = handle.read()

    problems = 0
    for name, symbol in SNIPPETS:
        cpp_text = from_cpp(cpp, symbol)
        js_text, complaint = from_js(js, name)

        if cpp_text is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp")
            problems += 1
            continue
        if "${" in cpp_text or "`" in cpp_text or "\\" in cpp_text:
            print(f"FAIL  {symbol} contains a backtick, a backslash or ${{, which a template literal cannot carry unchanged")
            problems += 1
            continue
        if complaint is not None:
            print(f"FAIL  {name} in demo/plugin.js has a {complaint}")
            problems += 1
            continue
        if js_text is None:
            print(f"FAIL  {name} not found in demo/plugin.js")
            problems += 1
            continue
        if cpp_text == js_text:
            print(f"ok    {name:<8} matches {symbol} ({len(cpp_text)} chars)")
            continue

        problems += 1
        print(f"FAIL  {name} has drifted from {symbol} in source/Shaders.cpp")
        cpp_lines = cpp_text.splitlines()
        js_lines = js_text.splitlines()
        for i in range(max(len(cpp_lines), len(js_lines))):
            a = cpp_lines[i] if i < len(cpp_lines) else "<missing>"
            b = js_lines[i] if i < len(js_lines) else "<missing>"
            if a != b:
                print(f"        first difference at line {i + 1}")
                print(f"          C++: {a}")
                print(f"          js : {b}")
                break

    # The constants in Shaders.cpp that this table does not know are drift too.
    known = {symbol for _, symbol in SNIPPETS}
    for symbol in re.findall(r'const char\* const (k\w+) = R"\(', cpp):
        if symbol not in known:
            print(f"FAIL  {symbol} is in source/Shaders.cpp but not in this check's table")
            problems += 1

    print()
    if problems:
        print(f"{problems} problem(s) -- run demo/tools/splice_shaders.py, do not edit plugin.js by hand")
        return 1
    print(f"all {len(SNIPPETS)} shaders are identical to the plugin's")
    return 0


if __name__ == "__main__":
    sys.exit(main())
