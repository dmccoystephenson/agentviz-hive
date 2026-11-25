// Regenerates every README image in docs/media/ from the FIXTURE feed:
//   python3 -m vendor.agentfeed serve --web web --port 8946 --fixture &
//   NODE_PATH=<dir with playwright> node tests/screenshots.cjs [http://127.0.0.1:8946]
// Needs ffmpeg for hive.gif. Runs the page under ?drive=1, so each frame is
// drawn at an exact scene time and the shots repeat.
const fs = require("fs");
const os = require("os");
const path = require("path");
const {execFileSync} = require("child_process");
const {chromium} = require("playwright");

const base = process.argv[2] || "http://127.0.0.1:8946";
const media = path.join(__dirname, "..", "docs", "media");
fs.mkdirSync(media, {recursive: true});
const ARGS = ["--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"];

// Frames -> GIF: ffmpeg when it is on PATH, else Pillow.
function makeGif(dir, fps, out) {
  try {
    execFileSync("ffmpeg", ["-y", "-loglevel", "error", "-framerate", String(fps), "-i", path.join(dir, "f%03d.png"),
      "-vf", "scale=640:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=bayer:bayer_scale=4", out]);
    return;
  } catch (e) {
    if (e.code !== "ENOENT") throw e;
  }
  const py = [
    "import sys, glob",
    "from PIL import Image",
    "d, fps, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]",
    "fr = [Image.open(f).convert('RGB').resize((640, 360), Image.LANCZOS) for f in sorted(glob.glob(d + '/f*.png'))]",
    "pal = fr[0].quantize(colors=96, method=Image.Quantize.MEDIANCUT)",
    "q = [f.quantize(palette=pal, dither=Image.Dither.FLOYDSTEINBERG) for f in fr]",
    "q[0].save(out, save_all=True, append_images=q[1:], duration=int(1000 / fps), loop=0, optimize=True)",
  ].join("\n");
  execFileSync("python3", ["-c", py, dir, String(fps), out]);
}

async function open(browser, ctxOpts, quality = 2) {
  const ctx = await browser.newContext(ctxOpts);
  const page = await ctx.newPage();
  const errors = [];
  page.on("pageerror", (e) => errors.push(String(e)));
  page.on("console", (m) => { if (m.type() === "error") errors.push(m.text()); });
  await page.goto(base + "/?fixture=1&drive=1&quality=" + quality, {waitUntil: "commit"});
  await page.waitForFunction(() => window.__hiveReady && window.__hiveKit, null, {timeout: 0, polling: 500});
  page._errors = errors;
  return page;
}

// Point the camera, select (or not) and draw scene time t, then shoot.
async function shot(page, file, o) {
  await page.evaluate((o) => {
    const kit = window.__hiveKit, hive = window.__hive;
    const legend = document.getElementById("legend");
    legend.hidden = !o.legend;
    window.__hiveSelect(o.select || null);
    let tx, tz;
    if (o.between) {
      const [a, b] = o.between.map((id) => hive.sessions.get(id));
      tx = (a.tx + b.tx) / 2; tz = (a.tz + b.tz) / 2;
    } else if (o.at) {
      const s = hive.sessions.get(o.at);
      tx = s.tx + (o.dx || 0); tz = s.tz + (o.dz || 0);
    } else {
      [tx, tz] = hive.centre();
    }
    Object.assign(kit.cam, {tx, tz, ty: 0, yaw: o.yaw, pitch: o.pitch, dist: o.dist});
    // A few frames up to t so easing settles and labels place.
    for (const dt of [1.0, 0.3, 0]) kit.drive(o.t - dt);
  }, o);
  // Labels change between shown and hidden only after a short real-time
  // hold: wait it out, then draw t again.
  await page.waitForTimeout(450);
  await page.evaluate((t) => window.__hiveKit.drive(t), o.t);
  await page.waitForTimeout(100);
  await page.screenshot({path: path.join(media, file), timeout: 0});
  console.log("wrote", file);
}

(async () => {
  // HIVE_SHOTS=stills,gif,phone (default all) regenerates only some of them.
  const want = new Set((process.env.HIVE_SHOTS || "stills,gif,phone").split(","));
  const browser = await chromium.launch({args: ARGS});
  const Y = -1.35;
  if (want.has("stills")) {
  const page = await open(browser, {viewport: {width: 1600, height: 900}});
  await shot(page, "hero.png", {yaw: Y, pitch: 0.95, dist: 34, t: 12.3});
  await shot(page, "angle-low.png", {yaw: Y + 0.9, pitch: 0.42, dist: 30, t: 14.1});
  await shot(page, "angle-top.png", {yaw: Y - 0.4, pitch: 1.4, dist: 36, t: 15.7});
  await shot(page, "lead-subagents.png", {at: "demo-lead-b", yaw: Y + 0.3, pitch: 0.75, dist: 9, t: 21.4});
  await shot(page, "lead-delegate.png", {at: "demo-lead-a", yaw: Y - 0.3, pitch: 0.8, dist: 9, t: 22.2});
  await shot(page, "waiting.png", {at: "demo-waiting", yaw: Y + 0.2, pitch: 0.6, dist: 11, t: 30.15});
  await shot(page, "idle-offline.png", {between: ["demo-idle", "demo-offline-2"], yaw: Y, pitch: 0.85, dist: 13, t: 33});
  await shot(page, "shared-repo.png", {at: "demo-solo", yaw: Y - 0.2, pitch: 0.8, dist: 13, dx: 1, dz: -1.5, t: 41});
  await shot(page, "resting-subagent.png", {at: "demo-lead-a", select: "demo-lead-a/demo-a5", yaw: Y + 0.9, pitch: 0.85, dist: 8, t: 51});
  await shot(page, "panel-desktop.png", {at: "demo-lead-b", select: "demo-lead-b", yaw: Y, pitch: 0.9, dist: 18, t: 61});
  await shot(page, "legend.png", {yaw: Y, pitch: 0.95, dist: 34, t: 64, legend: true});

  if (page._errors.length) throw new Error("console errors:\n" + page._errors.join("\n"));
  await page.context().close();
  }
  if (want.has("gif")) {
  // A short loop for the GIF on a smaller page (SwiftShader is slow): Otto's
  // and Wren's workers at work.
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "hive-gif-"));
  const gif = await open(browser, {viewport: {width: 800, height: 450}}, 1);
  await gif.evaluate(() => {
    const kit = window.__hiveKit, hive = window.__hive;
    const a = hive.sessions.get("demo-lead-a"), b = hive.sessions.get("demo-lead-b");
    Object.assign(kit.cam, {tx: (a.tx + b.tx) / 2, tz: (a.tz + b.tz) / 2, yaw: -1.35, pitch: 0.85, dist: 17});
  });
  const fps = 10, secs = 5;
  for (let i = 0; i < fps * secs; i++) {
    await gif.evaluate((t) => window.__hiveKit.drive(t), 100 + i / fps);
    await gif.screenshot({path: path.join(tmp, `f${String(i).padStart(3, "0")}.png`), timeout: 0});
  }
  makeGif(tmp, fps, path.join(media, "hive.gif"));
  fs.rmSync(tmp, {recursive: true, force: true});
  console.log("wrote hive.gif");
  if (gif._errors.length) throw new Error("console errors:\n" + gif._errors.join("\n"));
  await gif.context().close();
  }
  if (want.has("phone")) {
  // Phone portrait, DPR 3, bottom sheet open on a lead.
  const phone = await open(browser, {viewport: {width: 390, height: 844}, deviceScaleFactor: 3, isMobile: true, hasTouch: true}, 1);
  await shot(phone, "phone-sheet.png", {at: "demo-lead-a", dx: 2.5, select: "demo-lead-a", yaw: Y, pitch: 1.0, dist: 17, t: 71});
  await shot(phone, "phone-waiting.png", {at: "demo-waiting", yaw: Y, pitch: 0.95, dist: 22, t: 75});
  if (phone._errors.length) throw new Error("console errors:\n" + phone._errors.join("\n"));
  }
  await browser.close();
})().catch((e) => { console.error("screenshots FAILED:", e.message); process.exit(1); });
