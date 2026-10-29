#!/usr/bin/env bash
# Calibration fills the screen while capturing and can return to the page.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
node --input-type=module -e '
import fs from "fs";
const root = process.argv[1];
const byId = new Map();

function make(tag) {
  const el = {
    tagName: tag,
    children: [],
    parentElement: null,
    hidden: false,
    value: "",
    textContent: "",
    className: "",
    clientWidth: 640,
    clientHeight: 360,
    videoWidth: 0,
    videoHeight: 0,
    listeners: {},
    classList: null,
  };
  el.classList = {
    add(name) { el._classes.add(name); },
    remove(name) { el._classes.delete(name); },
    contains(name) { return el._classes.has(name); },
    toggle(name, force) {
      const on = force === undefined ? el._classes.has(name) == false : force;
      if (on) el._classes.add(name);
      else el._classes.delete(name);
    },
  };
  el._classes = new Set();
  let id = "";
  Object.defineProperty(el, "id", {
    get() { return id; },
    set(value) {
      if (id !== "") byId.delete(id);
      id = value;
      byId.set(id, el);
    },
  });
  el.setAttribute = (name, value) => { el[name] = value; };
  el.appendChild = (child) => {
    child.parentElement = el;
    el.children.push(child);
    return child;
  };
  el.remove = () => {
    if (el.parentElement == null) return;
    el.parentElement.children = el.parentElement.children.filter((child) => child !== el);
    el.parentElement = null;
  };
  el.addEventListener = (type, fn) => {
    el.listeners[type] = el.listeners[type] || [];
    el.listeners[type].push(fn);
  };
  el.style = new Proxy({}, { set() { return true; } });
  el.focus = () => {};
  if (tag === "canvas") {
    let width = 0;
    let height = 0;
    el.strokes = [];
    Object.defineProperty(el, "width", {
      get() { return width; },
      set(value) {
        width = value;
        el.strokes = [];
      },
    });
    Object.defineProperty(el, "height", {
      get() { return height; },
      set(value) {
        height = value;
        el.strokes = [];
      },
    });
    el.getContext = () => ({
      clearRect() {},
      save() {},
      restore() {},
      strokeRect(x, y, w, h) { el.strokes.push({ x: x, y: y, w: w, h: h }); },
      setLineDash() {},
      strokeStyle: "",
      lineWidth: 1,
    });
  }
  el.click = () => Promise.all((el.listeners.click || []).map((fn) => fn({})));
  el.querySelector = (selector) => findId(el, selector.slice(1));
  el.getBoundingClientRect = () => ({ left: 0, top: 0, width: 640, height: 360 });
  if (tag !== "canvas") {
    el.getContext = () => ({
      clearRect() {},
      save() {},
      restore() {},
      strokeRect() {},
      setLineDash() {},
      strokeStyle: "",
      lineWidth: 1,
    });
  }
  Object.defineProperty(el, "innerHTML", {
    set(html) {
      const tags = html.matchAll(/<([a-z0-9]+)([^>]*)>/gi);
      for (const tag of tags) {
        const id = /id="([^"]+)"/.exec(tag[2]);
        if (id == null) continue;
        const child = make(tag[1]);
        child.id = id[1];
        const value = /value="([^"]*)"/.exec(tag[2]);
        if (value != null) child.value = value[1];
        child.hidden = /(?:\s|^)hidden(?:\s|>|$)/.test(tag[2]);
        el.appendChild(child);
      }
    },
  });
  return el;
}

function findId(el, id) {
  if (el.id === id) return el;
  for (const child of el.children) {
    const found = findId(child, id);
    if (found != null) return found;
  }
  return null;
}

const body = make("body");
const stage = make("div");
const video = make("video");
video.id = "camera-video";
stage.appendChild(video);
body.appendChild(stage);
const button = make("button");
button.id = "camera-calibrate";
body.appendChild(button);
const mount = make("div");
mount.id = "calibration-mount";
body.appendChild(mount);

let state = {
  state: "CALIBRATION_CAPTURING",
  message: "Show the board",
  accepted: 1,
  target_count: 20,
  can_finish: false,
  stereo: true,
  next_target: { x: 0.35, y: 0.35, width: 0.3, height: 0.3 },
};
const document = {
  body,
  getElementById(id) { return byId.get(id) || null; },
  createElement: make,
};
const window = {
  listeners: {},
  addEventListener(type, fn) {
    window.listeners[type] = window.listeners[type] || [];
    window.listeners[type].push(fn);
  },
  setInterval(fn) {
    window.tick = fn;
    return 1;
  },
  clearInterval() {},
};
globalThis.document = document;
globalThis.window = window;
globalThis.fetch = async (url, options) => {
  const method = options && options.method ? options.method : "GET";
  if (url === "/api/calibration" && method === "POST") {
    return { ok: true, status: 202, async json() { return {}; } };
  }
  if (url === "/api/calibration" && method === "DELETE") {
    state = { state: "CALIBRATION_IDLE", accepted: 0, target_count: 20 };
    return { ok: true, status: 200, async json() { return state; } };
  }
  if (url === "/api/calibration") {
    return { ok: true, status: 200, async json() { return state; } };
  }
  return { ok: true, status: 200, async json() { return {}; } };
};

const source = fs.readFileSync(root + "/web/calibration.js", "utf8");
new Function(source)();

function fail(name) {
  console.log("FAIL " + name);
  process.exit(1);
}

button.click();
const form = document.getElementById("calibration-form");
await form.listeners.submit[0]({ preventDefault() {} });
if (body.classList.contains("calibration-large") == false
    || stage.classList.contains("calibration-stage") == false) {
  fail("starting calibration fills the screen");
}
const panel = document.getElementById("calibration-panel");
const exitLarge = document.getElementById("calibration-exit");
if (panel.hidden || exitLarge.hidden) {
  fail("the large view keeps the calibration controls");
}
const overlay = stage.children.find((child) => child.tagName === "canvas");
const mid = video.getBoundingClientRect().width / 2;
const guides = overlay == null ? [] : overlay.strokes;
if (guides.length !== 2
    || guides[0].x + guides[0].w > mid
    || guides[1].x < mid) {
  fail("the guide stays inside both stereo halves");
}
const css = fs.readFileSync(root + "/web/calibration.css", "utf8");
const stageRule = /body\.calibration-large \.calibration-stage[\s\S]*?\{([^}]+)\}/.exec(css);
const panelRule = /body\.calibration-large \.calibration-panel \{([^}]+)\}/.exec(css);
const stageZ = stageRule == null ? 0 : Number((/z-index:\s*(\d+)/.exec(stageRule[1]) || [])[1] || 0);
const panelZ = panelRule == null ? 0 : Number((/z-index:\s*(\d+)/.exec(panelRule[1]) || [])[1] || 0);
if (stageZ <= 1000 || panelZ <= stageZ) {
  fail("the large picture stays above the map");
}
exitLarge.click();
if (body.classList.contains("calibration-large") || panel.hidden == false && exitLarge.hidden == false) {
  fail("exit large view returns to the page and keeps the session");
}
if (panel.hidden || exitLarge.hidden == false) {
  fail("exit large view returns to the page and keeps the session");
}
await window.tick();
if (body.classList.contains("calibration-large")) {
  fail("a dismissed large view stays dismissed while capturing");
}
state = { state: "CALIBRATION_FAILED", message: "Camera frames stopped", accepted: 1, target_count: 20 };
await window.tick();
state = { state: "CALIBRATION_CAPTURING", message: "Show the board", accepted: 1, target_count: 20, can_finish: false };
await window.tick();
if (body.classList.contains("calibration-large") == false) {
  fail("a new capture returns to the large view");
}
window.listeners.keydown[0]({ key: "Escape" });
if (body.classList.contains("calibration-large") || panel.hidden) {
  fail("escape leaves the large view without ending calibration");
}
await document.getElementById("calibration-cancel").click();
if (body.classList.contains("calibration-large") || panel.hidden == false) {
  fail("cancel leaves the large view and closes calibration");
}
console.log("PASS calibration fills the screen and can return to the page");
' "$ROOT" || exit 1

# The drive page names the picture #drive-video. That id must not keep the
# picture in the page column while calibration is large.
LAYOUT="$(mktemp --suffix=.html)"
python3 - "$ROOT" "$LAYOUT" << 'PY'
import pathlib, sys
root, dest = sys.argv[1:]
css = pathlib.Path(root, "web/calibration.css").read_text()
page = """
  body { margin: 1.5rem; }
  .drive-layout {
    display: grid;
    grid-template-columns: minmax(18rem, 24rem) minmax(16rem, 52rem) 16rem;
    gap: 1rem;
  }
  .drive-video {
    min-height: 10rem;
    max-height: 60vh;
    position: relative;
    display: flex;
    flex-direction: column;
    padding: 1rem;
  }
  .drive-video video {
    width: 100%;
    min-height: 10rem;
    max-height: 52vh;
    object-fit: contain;
  }
  .camera-layout {
    display: grid;
    grid-template-columns: minmax(18rem, 44rem) 15rem;
  }
  .camera-view video {
    display: block;
    width: 100%;
    min-height: 12rem;
    max-height: 52vh;
    object-fit: contain;
  }
"""
html = """<!DOCTYPE html>
<html><head><meta charset="utf-8"><style>%s</style><style>%s</style></head>
<body>
<section style="height:30rem">spacer</section>
<div class="camera-layout"><div class="camera-view" id="dog-stage">
<video id="dog-video" width="1280" height="480"></video>
</div></div>
<div class="drive-layout"><div class="drive-video" id="drive-video">
<video id="camera-video" width="1280" height="480"></video>
<p id="camera-status">status</p>
</div></div>
<script>
document.body.classList.add("calibration-large");
document.getElementById("dog-stage").classList.add("calibration-stage");
document.getElementById("drive-video").classList.add("calibration-stage");

// ================================================================================

function measure(id, videoId) {
  const stage = document.getElementById(id).getBoundingClientRect();
  const video = document.getElementById(videoId).getBoundingClientRect();
  return {
    sw: stage.width, sh: stage.height,
    videoW: video.width, videoH: video.height,
    position: getComputedStyle(document.getElementById(id)).position
  };
}
document.body.insertAdjacentHTML("beforeend", "<pre id=\\"out\\"></pre>");
document.getElementById("out").textContent = JSON.stringify({
  vw: innerWidth, vh: innerHeight,
  drive: measure("drive-video", "camera-video"),
  dog: measure("dog-stage", "dog-video")
});
</script></body></html>
""" % (page, css)
pathlib.Path(dest).write_text(html)
PY
CHROME=""
for candidate in google-chrome google-chrome-stable chromium chromium-browser; do
  if command -v "$candidate" >/dev/null 2>&1; then
    CHROME="$candidate"
    break
  fi
done
if [ -z "$CHROME" ]; then
  echo "FAIL large view layout check needs Chrome"
  rm -f "$LAYOUT"
  exit 1
fi
DUMP="$(mktemp)"
"$CHROME" --headless=new --disable-gpu --no-sandbox \
  --user-data-dir="$(mktemp -d)" --window-size=1400,900 \
  --virtual-time-budget=3000 --dump-dom "file://$LAYOUT" >"$DUMP" 2>/dev/null
python3 - "$DUMP" << 'PY'
import json, pathlib, re, sys
text = pathlib.Path(sys.argv[1]).read_text(errors="replace")
match = re.search(r'<pre id="out">(.*?)</pre>', text)
if match is None:
    print("FAIL large view layout was not measured")
    sys.exit(1)
box = json.loads(match.group(1))

# ================================================================================

def fills(name, item):
    if item["position"] != "fixed" or item["sw"] < box["vw"] * 0.9 or item["sh"] < box["vh"] * 0.9:
        print("FAIL", name, "picture stays in the column", item)
        return False
    if item["videoW"] < box["vw"] * 0.9 or item["videoH"] < box["vh"] * 0.9:
        print("FAIL", name, "picture stays in the column", item)
        return False
    return True

if fills("drive page", box["drive"]) == False or fills("dog page", box["dog"]) == False:
    sys.exit(1)
print("PASS calibration picture fills the viewport")
PY
status=$?
rm -f "$LAYOUT" "$DUMP"
exit $status
