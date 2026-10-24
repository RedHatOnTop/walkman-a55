"""Drive the probe and the resource overlay in the host harness.

Run inside the hosttest container (see run.sh) from the build directory,
which holds ./hosttest, overlay.rcc and overlay2.rcc. Starts the harness
without and with an overlay, swaps overlays in the running harness, talks
to 127.0.0.1:7777 and exits non-zero if any check failed.
"""
import os
import re
import socket
import subprocess
import sys
import time

END = b"\n.\n"


class Session:
    def __init__(self, port=7777):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=10)
        self.buf = b""
        self.banner = self._reply()

    def _reply(self):
        while END not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("probe closed the connection")
            self.buf += chunk
        reply, _, self.buf = self.buf.partition(END)
        return reply.decode()

    def cmd(self, line):
        self.sock.sendall(line.encode() + b"\n")
        return self._reply()

    def pipeline(self, *lines):
        """Sends every line at once, then reads the replies in order."""
        self.sock.sendall(b"".join(l.encode() + b"\n" for l in lines))
        return [self._reply() for _ in lines]


failures = 0


def check(name, cond, detail=""):
    global failures
    print(("PASS " if cond else "FAIL ") + name + ("" if cond else f"\n---\n{detail}\n---"))
    if not cond:
        failures += 1


# HOSTTEST_CMD runs the harness some other way, such as under qemu-arm on
# the device's Qt libraries (run-arm.sh).
COMMAND = os.environ.get("HOSTTEST_CMD", "./hosttest").split()


def start(overlay, probe=True):
    env = dict(os.environ, WMMOD_OVERLAY=overlay, WMMOD_HEALTHY_MS="1500")
    if probe:
        env["WMPROBE"] = "1"
    app = subprocess.Popen(COMMAND, env=env, stderr=subprocess.PIPE, text=True)
    deadline = time.time() + 60
    while True:
        try:
            s = Session()
            break
        except OSError:
            if time.time() > deadline or app.poll() is not None:
                app.kill()
                print("probe never came up:", app.poll(), app.stderr.read())
                sys.exit(1)
            time.sleep(0.3)
    time.sleep(1.0)  # let the scene finish loading
    return app, s


def stop(app):
    alive = app.poll() is None
    app.terminate()
    _, err = app.communicate(timeout=5)
    return alive, err


def item_id(tree, name):
    m = re.search(rf'#(\d+) \S+ "{name}"', tree)
    return m.group(1) if m else "0"


def probe_commands():
    app, s = start("/nonexistent/overlay.rcc")

    check("banner", "wmprobe ready" in s.banner, s.banner)
    r = s.cmd("help"); check("help", "tree" in r and "ctx NAME" in r and "overlay" in r, r)
    r = s.cmd("windows"); check("windows", "QQuickView" in r and "visible=1" in r, r)
    r = s.cmd("tree")
    check("tree items", all(n in r for n in ("titleText", "volumeText", "hiddenRect")), r)
    check("tree hidden flag", re.search(r'"hiddenRect".* hidden', r) is not None, r)
    r = s.cmd("tree 2"); check("tree depth limit", "titleText" not in r and '"root"' in r, r)

    r = s.cmd("objects FakeViewModel")
    m = re.search(r'#(\d+) FakeViewModel "playerVM"', r)
    check("objects finds view model", m is not None, r)
    vm = m.group(1) if m else "0"

    r = s.cmd("ctx playerVM"); check("ctx", f"#{vm} FakeViewModel" in r, r)
    r = s.cmd("ctx nosuchname"); check("ctx missing", "<invalid>" in r, r)

    r = s.cmd(f"props {vm}")
    check("props values", "prop int volume = 10" in r and "prop QString title = Track One (ro)" in r, r)
    check("props methods", "addVolume(int)" in r and "describe(QString,bool)" in r and "signal void volumeChanged()" in r, r)

    r = s.cmd(f"get {vm} volume"); check("get", r.strip() == "10", r)
    r = s.cmd(f"json {vm} layouts")
    check("json map", '"MusicPlayer": "qrc:/sid_0201_player/MusicPlayerDefault.qml"' in r and '"nested"' in r, r)
    r = s.cmd(f"json {vm} layouts nested k"); check("json descend", r.split() == ["[", "1,", "2", "]"], r)
    r = s.cmd(f"json {vm} layouts missing"); check("json missing key", r.strip() == "<invalid>", r)
    r = s.cmd(f"json {vm} volume"); check("json scalar", r.strip() == "10", r)
    r = s.cmd(f"set {vm} volume 42"); check("set", r.splitlines() == ["ok", "42"], r)
    r = s.cmd(f"call {vm} addVolume 5"); check("call int return", "ok addVolume(int)" in r and "returned 47" in r, r)
    r = s.cmd(f"call {vm} describe hi true"); check("call two args", "returned hi:47:loud" in r, r)
    r = s.cmd(f"call {vm} reset"); check("call void", "ok reset()" in r, r)
    r = s.cmd(f"get {vm} volume"); check("call took effect", r.strip() == "0", r)
    r = s.cmd(f"call {vm} addVolume notanumber"); check("call bad arg", "cannot convert" in r, r)
    r = s.cmd(f"call {vm} nosuch"); check("call missing method", "no method nosuch" in r, r)

    r = s.cmd("props 99999"); check("stale id", "no live object" in r, r)
    r = s.cmd("set"); check("set without args", "no live object" in r, r)
    r = s.cmd("call 1"); check("call without method", "no method" in r, r)
    r = s.cmd("bogus"); check("unknown command", "unknown command" in r, r)

    r = s.cmd("grab " + os.path.abspath("probe-grab.png"))
    ok = os.path.exists("probe-grab.png") and open("probe-grab.png", "rb").read(8) == b"\x89PNG\r\n\x1a\n"
    check("grab", "saved 800x480" in r and ok, r)
    r = s.cmd("hide 0"); check("hide", "visible=0" in r, r)
    r = s.cmd("show 0"); check("show", "visible=1" in r, r)
    r = s.cmd("env"); check("env", "pid=" in r and "PATH=" in r, r)

    r = s.cmd("objects"); check("objects unfiltered", "QGuiApplication" in r and "engine #" in r, r)

    s2 = Session(); r = s2.cmd("windows"); check("second client", "QQuickView" in r, r)

    r = s.cmd("reload"); check("reload usage", "usage: reload PATH" in r, r)

    r = s.cmd("overlay"); check("overlay absent", "no overlay at /nonexistent/overlay.rcc" in r, r)
    tree = s.cmd("tree")
    r = s.cmd(f"props {item_id(tree, 'badge')}")
    check("stock badge", "prop QString origin = stock" in r, r)

    r = s.cmd("reload " + os.path.abspath("overlay.rcc"))
    check("reload with no overlay in effect", "no overlay was in effect" in r and "refreshed 1: /Body.qml" in r, r)

    alive, err = stop(app)
    check("app still running", alive, err)


def count(path):
    return open(path).read().strip() if os.path.exists(path) else None


def set_count(path, n):
    with open(path, "w") as f:
        f.write(f"{n}\n")


def overlay():
    set_count("badboots", 0)
    app, s = start(os.path.abspath("overlay.rcc"))
    check("bad start counted", count("badboots") == "1", count("badboots"))

    r = s.cmd("overlay")
    check("overlay status", "3 files, 2 replaced, 1 added, 0 not served" in r and "1 moved behind" in r, r)
    tree = s.cmd("tree")
    r = s.cmd(f"props {item_id(tree, 'badge')}")
    check("badge replaced", "prop QString origin = overlay" in r, r)
    check("app file used from overlay QML", '"pill"' in tree, tree)
    check("nested file replaced", '"overlayDot"' in tree and '"stockDot"' not in tree, tree)
    check("overlay-only file", '"extra"' in tree, tree)
    check("stock parts untouched", '"titleText"' in tree and '"hiddenRect"' in tree, tree)
    r = s.cmd("grab " + os.path.abspath("probe-grab-overlay.png"))
    check("grab with overlay", "saved 800x480" in r, r)

    time.sleep(2)
    check("bad start count reset when healthy", count("badboots") == "0", count("badboots"))
    r = s.cmd("overlay"); check("reset reported", "badboots reset" in r, r)

    alive, err = stop(app)
    check("app still running with overlay", alive, err)
    check("no QML warnings", "qrc:/" not in err, err)


def image_width(s, tree, name):
    m = re.search(r"implicitWidth = (\S+)", s.cmd(f"props {item_id(tree, name)}"))
    return m.group(1) if m else None


def reload():
    set_count("badboots", 0)
    app, s = start(os.path.abspath("overlay.rcc"))
    try:
        reload_checks(app, s)
    except ConnectionError as e:
        _, err = stop(app)
        check("app survives the reload checks", False, f"{e}\n{err[-3000:]}")
        return
    alive, err = stop(app)
    check("app still running after reloads", alive, err)
    check("no QML warnings across reloads", "qrc:/" not in err, err)


def reload_checks(app, s):
    tree = s.cmd("tree")
    check("stock body before reload", '"bodyStock"' in tree and '"leafStock"' in tree, tree)
    check("stock body image", image_width(s, tree, "bodyDot") == "4", tree)
    hot = item_id(tree, "hot")

    # The tree is sent along with the reload and must still come after it.
    r, tree = s.pipeline("reload " + os.path.abspath("overlay2.rcc"), "tree")
    check("reload summary", "overlay2.rcc: 9 files, 7 changed" in r, r)
    check("reload lists changed paths",
          all(f"changed {p}\n" in r + "\n" for p in
              ("/Badge.qml", "/Body.qml", "/bodyDot.png", "/dot.png",
               "/parts/Leaf.qml", "/parts/New.qml", "/parts/Pill.qml")), r)
    check("reload names files new to the app", "new /parts/New.qml\n" in r and "a type name may not" in r, r)
    check("reload refreshes the token holder", "refreshed 1: /Body.qml" in r, r)
    check("open meta-objects are not taken for holders", "QQuickPathViewAttached" not in r, r)
    kept = "kept, still in use outside any wmReloadToken: "
    check("reload names what it kept",
          all(kept + p in r for p in ("/Badge.qml (", "/dot.png (1 objects)", "/parts/Pill.qml (")), r)
    check("body replaced", '"bodyV2"' in tree and '"bodyStock"' not in tree, tree)
    check("file only the body uses reloaded", '"leafV2"' in tree and '"leafStock"' not in tree, tree)
    check("reloaded image is the new one", image_width(s, tree, "bodyDot") == "8", tree)
    check("image outside the token keeps the old one", image_width(s, tree, "outsideDot") == "4", tree)
    r = s.cmd(f"props {item_id(tree, 'badge')}")
    check("badge outside the token unchanged", "prop QString origin = overlay\n" in r + "\n", r)
    r = s.cmd(f"get {hot} wmReloadToken"); check("token advanced", r.strip() == "1", r)
    r = s.cmd("overlay"); check("status records the reload", "reloaded " + os.path.abspath("overlay2.rcc") in r, r)

    r, tree = s.pipeline("reload " + os.path.abspath("overlay.rcc"), "tree")
    check("reload back", "7 changed" in r and "refreshed 1: /Body.qml" in r and "\nnew " not in r, r)
    check("body back to the first version", '"bodyStock"' in tree and '"leafStock"' in tree, tree)
    check("image back to the first version", image_width(s, tree, "bodyDot") == "4", tree)
    r = s.cmd(f"get {hot} wmReloadToken"); check("token advanced again", r.strip() == "2", r)

    r = s.cmd("reload /nonexistent/x.rcc"); check("reload missing file", "cannot read /nonexistent/x.rcc" in r, r)
    r = s.cmd("reload " + os.path.abspath(__file__)); check("reload non-rcc", "is not an rcc v1 file" in r, r)
    tree = s.cmd("tree")
    check("failed reloads change nothing", '"bodyStock"' in tree and '"pill"' in tree, tree)

    # The device's app crashed when a live StateButton changed state after a
    # reload, then created a window it had not loaded before.
    live = item_id(tree, "liveBtn")
    s.cmd(f"set {live} state Down")
    r = s.cmd(f"get {live} opacity")
    check("state with a binding applies after reloads", r.strip() == "0.25" and app.poll() is None, r)
    s.cmd(f"set {item_id(tree, 'lateLoader')} active true")
    tree = s.cmd("tree")
    check("window first loaded after reloads", '"lateBtn"' in tree and app.poll() is None, tree)

    # A Loader fetching over HTTP stays loading until this side answers.
    slow = item_id(tree, "slowLoader")
    with socket.create_server(("127.0.0.1", 7790)) as server:
        server.settimeout(30)
        s.cmd(f"set {slow} url http://127.0.0.1:7790/Slow.qml")
        conn, _ = server.accept()
        r = s.cmd("reload " + os.path.abspath("overlay2.rcc"))
        check("no reload while a Loader is loading", "not now, still loading" in r and "changed" not in r, r)
        conn.recv(65536)
        conn.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\n"
                     b'import QtQuick 2.0\nItem { objectName: "slowItem" }\n')
        conn.close()
    for _ in range(50):
        if '"slowItem"' in s.cmd("tree"):
            break
        time.sleep(0.2)
    r = s.cmd("reload " + os.path.abspath("overlay2.rcc"))
    check("reload once the Loader is done", "refreshed 1: /Body.qml" in r, r)


def bad_boots():
    set_count("badboots", 3)
    app, s = start(os.path.abspath("overlay.rcc"))
    r = s.cmd("overlay"); check("overlay skipped after 3 bad starts", "skipped: 3 starts" in r, r)
    tree = s.cmd("tree")
    r = s.cmd(f"props {item_id(tree, 'badge')}")
    check("stock UI after bad starts", "prop QString origin = stock" in r, r)
    check("count left for the user", count("badboots") == "3", count("badboots"))
    stop(app)
    set_count("badboots", 0)


def probe_off():
    env = dict(os.environ, WMMOD_OVERLAY="/nonexistent/overlay.rcc")
    env.pop("WMPROBE", None)
    app = subprocess.Popen(COMMAND, env=env, stderr=subprocess.PIPE, text=True)
    time.sleep(3)
    try:
        socket.create_connection(("127.0.0.1", 7777), timeout=2).close()
        refused = False
    except OSError:
        refused = True
    check("probe off without its flag", refused and app.poll() is None)
    app.terminate()
    _, err = app.communicate(timeout=5)
    check("probe says how to enable it", "create /nonexistent/probe or set WMPROBE=1" in err, err)


def main():
    groups = [probe_commands, overlay, reload, bad_boots, probe_off]
    only = sys.argv[1:]
    for group in groups:
        if not only or group.__name__ in only:
            group()
    print(f"{failures} failed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
