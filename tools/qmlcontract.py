"""List, for each QML file of the app, what the C++ side depends on, i.e.
what a replacement file must keep. Everything else in the file is free.

    qmlcontract.py QRC_DIR APP_BINARY MOC_JSON [FILE_SUBSTR]

QRC_DIR is qrcdump.py output, MOC_JSON is mocdump.py --json output.
For the root object of each file:
  props    root properties that some moc class also has; FrameWork.qml's
           Binding writes ViewModel values into the view by property name
  signals  root signals the binary connects to by name (SIGNAL "2name(")
  calls    root functions the binary connects or invokes by name ("1name(")
  objects  objectName values anywhere in the file that the binary contains,
           which it can find with findChild()
Names are matched against the whole binary, not per screen, so this is an
over-approximation: a name listed here may still be unused by this screen.
"""
import json
import os
import re
import sys


def strip(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def root_body(text):
    """The text inside the first top-level object's braces, with nested
    objects removed, so only root-level declarations remain."""
    text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
    start = text.find("{")
    if start < 0:
        return "", ""
    root_type = text[:start].split()[-1] if text[:start].split() else ""
    depth, out = 0, []
    for ch in text[start + 1:]:
        if ch == "{":
            depth += 1
        elif ch == "}":
            if depth == 0:
                break
            depth -= 1
        elif depth == 0:
            out.append(ch)
    return root_type, "".join(out)


def main(qrc_dir, binary, moc_json, only=""):
    blob = open(binary, "rb").read()
    cstrings = set(re.findall(rb"[\x20-\x7e]{2,}", blob))
    vm_props = {p["name"] for c in json.load(open(moc_json)) for p in c["properties"]}

    def connected(prefix, name):
        return re.search(re.escape(prefix + name.encode()) + rb"\(", blob) is not None

    def in_binary(name):
        return name.encode() in cstrings or name.encode("utf-16-le") in blob

    for dirpath, _, files in sorted(os.walk(qrc_dir)):
        for f in sorted(files):
            if not f.endswith(".qml"):
                continue
            path = os.path.join(dirpath, f)
            rel = os.path.relpath(path, qrc_dir)
            if only not in rel:
                continue
            text = strip(open(path, encoding="utf-8").read())
            root_type, body = root_body(text)
            props = re.findall(r"\b(?:readonly\s+)?property\s+[\w<>.]+\s+(\w+)", body)
            signals = re.findall(r"\bsignal\s+(\w+)", body)
            funcs = re.findall(r"\bfunction\s+(\w+)", body)
            names = set(re.findall(r'objectName\s*[:=]\s*"([^"]+)"', strip(open(path, encoding="utf-8").read())))
            contract = {
                "props": [p for p in props if p in vm_props],
                "signals": [s for s in signals if connected(b"2", s)],
                "calls": [fn for fn in funcs if connected(b"1", fn)],
                "objects": sorted(n for n in names if in_binary(n)),
            }
            if not any(contract.values()):
                continue
            print(f"{rel} ({root_type})")
            for k, v in contract.items():
                if v:
                    print(f"  {k:8} {' '.join(v)}")


if __name__ == "__main__":
    main(*sys.argv[1:])
