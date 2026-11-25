// Headless smoke test: load the fixture page, wait for the kit to draw, check
// there are no console errors and the frame is not one flat colour, tap a
// session and see the panel open, and save docs/media/screenshot.png.
//
//   python3 -m vendor.agentfeed serve --web web --port 8946 --fixture &
//   NODE_PATH=<dir with playwright> node tests/smoke.cjs [http://127.0.0.1:8946]
const path = require("path");
const {chromium} = require("playwright");

const base = process.argv[2] || "http://127.0.0.1:8946";
const out = path.join(__dirname, "..", "docs", "media", "screenshot.png");

(async () => {
  const browser = await chromium.launch({args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"]});
  const page = await browser.newPage({viewport: {width: 1400, height: 860}});
  const errors = [];
  page.on("console", (m) => { if (m.type() === "error") errors.push(m.text()); });
  page.on("pageerror", (e) => errors.push(String(e)));
  await page.goto(base + "/?fixture=1&quality=1", {waitUntil: "commit"});
  await page.waitForFunction(() => window.__hiveReady && window.__strataKit && window.__strataKit.stats().frames > 5,
    null, {timeout: 120000, polling: 500});
  await page.waitForTimeout(2500);
  const info = await page.evaluate(() => ({
    sessions: window.__hive.sessions.size,
    followers: window.__hive.followers.size,
    shared: window.__hive.sharedPlaces,
    targets: window.__strataKit.targets(),
  }));
  if (info.sessions !== 8) throw new Error("expected 8 sessions, got " + info.sessions);
  if (info.followers !== 11) throw new Error("expected 11 followers, got " + info.followers);
  if (!info.shared.includes("engine")) throw new Error("expected a scent trail in 'engine'");
  // The frame is not one flat colour.
  const flat = await page.evaluate(() => {
    const c = document.getElementById("view");
    const g = c.getContext("webgl2");
    const px = new Uint8Array(4 * 64);
    const seen = new Set();
    for (let i = 0; i < 64; i++) {
      g.readPixels(Math.floor(c.width * ((i % 8) + 0.5) / 8), Math.floor(c.height * (Math.floor(i / 8) + 0.5) / 8), 1, 1,
        g.RGBA, g.UNSIGNED_BYTE, px.subarray(i * 4, i * 4 + 4));
      seen.add(px.slice(i * 4, i * 4 + 3).join(","));
    }
    return seen.size;
  });
  if (flat < 4) throw new Error("the frame looks flat (" + flat + " colours)");
  // Tap the waiting session: the panel opens with its name.
  const t = info.targets.find((x) => x.id === "demo-waiting");
  if (!t) throw new Error("no tap target for the waiting session");
  await page.mouse.click(t.x, t.y);
  await page.waitForTimeout(400);
  const sheet = await page.evaluate(() => {
    const s = document.getElementById("sheet");
    return s.hidden ? null : s.textContent;
  });
  if (!sheet || !sheet.includes("Thistle")) throw new Error("tapping the waiting bee did not open its panel: " + sheet);
  // Pause the frame loop (SwiftShader frames are slow enough to starve the
  // screenshot) and draw one frame by hand, with Thistle still selected.
  await page.evaluate(() => { const k = window.__hiveKit; k.running = false; k.drive(performance.now() / 1000); });
  await page.waitForTimeout(300);
  await page.screenshot({path: out, timeout: 120000});
  if (errors.length) throw new Error("console errors:\n" + errors.join("\n"));
  console.log("smoke ok:", JSON.stringify({sessions: info.sessions, followers: info.followers, colours: flat}), "->", out);
  await browser.close();
})().catch((e) => { console.error("smoke FAILED:", e.message); process.exit(1); });
