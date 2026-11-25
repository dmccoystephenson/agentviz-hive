// Regenerates every README image in docs/media/ from the DEMO feed (never live):
//   npm run screenshots [-- name ...]
// Stills are drawn with ?drive=1 (the kit draws one frame on demand at an
// exact scene time), so they repeat. The GIF steps scene time frame by frame
// and is encoded with Pillow (vendor/agentviz-kit/tests/make_gif.py).
const fs = require("fs");
const os = require("os");
const path = require("path");
const h = require("../vendor/agentviz-kit/tests/harness.cjs");
const screens = require("../vendor/agentviz-kit/tests/screens.cjs");

const REPO = path.resolve(__dirname, "..");
const MEDIA = path.join(REPO, "docs/media");
const PORT = Number(process.env.HIVE_TEST_PORT || 9116);
const READY = "window.__page && window.__page.polls > 0 && window.__page.model.sessions.size > 0";
const only = new Set(process.argv.slice(2));
const want = (name) => only.size === 0 || only.has(name);
const DESKTOP = {viewport: {width: 1600, height: 900}};
const PHONE = {viewport: {width: 390, height: 844}, dpr: 3, mobile: true};
const Y = -1.35;

// Camera helpers run in the page: the whole comb, one bee, or between two.
const HELPERS = `
window.__shots = {
  t: 0,
  cam(c) { Object.assign(window.__page.kit.cam, {ty: 0}, c); },
  at(id, c) { const o = window.__page.model.find(id).o; this.cam(Object.assign({tx: o.tx + (c.dx || 0), tz: o.tz + (c.dz || 0)}, c)); },
  between(a, b, c) { const m = window.__page.model, p = m.find(a).o, q = m.find(b).o;
    this.cam(Object.assign({tx: (p.tx + q.tx) / 2, tz: (p.tz + q.tz) / 2}, c)); },
  // A second of easing up to t, so positions settle and labels place.
  draw() { const k = window.__page.kit; for (const dt of [1.0, 0.3, 0]) k.drive(this.t - dt); },
};`;

async function openPage(browser, server, options) {
  const opened = await h.open(browser, `${server.base}/?fixture=1&drive=1`, Object.assign({ready: READY}, options));
  await h.stillAnimations(opened.page);
  await opened.page.evaluate(() => window.__page.stopPolling());
  await opened.page.evaluate(HELPERS);
  return opened;
}

// setup is a JS string run in the page; t is the scene time drawn.
async function shoot(page, name, t, setup, select = null, legend = false) {
  if (!want(name)) return;
  await page.evaluate(({t, setup, select, legend}) => {
    window.__shots.t = t;
    window.__page.select(select, false);
    document.getElementById("legend").hidden = !legend;
    (0, eval)(setup);
  }, {t, setup, select, legend});
  await screens.still(page, path.join(MEDIA, name + ".png"), () => window.__shots.draw());
}

const FIT = "window.__page.fit(true)";
const DESKTOP_SHOTS = [
  ["hero", 12.3, FIT],
  ["angle-low", 14.1, `${FIT}; window.__shots.cam({yaw: ${Y + 0.9}, pitch: 0.42, dist: window.__page.kit.cam.dist * 0.85})`],
  ["angle-top", 15.7, `${FIT}; window.__shots.cam({yaw: ${Y - 0.4}, pitch: 1.4, dist: window.__page.kit.cam.dist * 1.05})`],
  ["lead-subagents", 21.4, `window.__shots.at("demo-lead-b", {yaw: ${Y + 0.3}, pitch: 0.75, dist: 9.5})`],
  ["lead-delegate", 22.2, `window.__shots.at("demo-lead-a", {yaw: ${Y - 0.3}, pitch: 0.8, dist: 9})`],
  ["waiting", 30.15, `window.__shots.at("demo-waiting", {ty: 1.2, yaw: ${Y + 0.2}, pitch: 0.5, dist: 13})`],
  ["question", 31.4, `window.__shots.at("demo-asker", {ty: 1.8, yaw: ${Y - 0.25}, pitch: 0.35, dist: 10})`],
  ["idle-offline", 33, `window.__shots.between("demo-idle", "demo-offline-2", {yaw: ${Y}, pitch: 0.85, dist: 13})`],
  ["shared-repo", 41, `window.__shots.between("demo-solo", "demo-thinker", {yaw: ${Y - 0.2}, pitch: 0.8, dist: 14})`],
  ["stalled", 44.3, `window.__shots.at("demo-stalled", {yaw: ${Y + 0.4}, pitch: 0.7, dist: 9})`],
];
const SELECTED_SHOTS = [
  ["resting-subagent", 51, `window.__shots.at("demo-lead-a", {yaw: ${Y + 0.9}, pitch: 0.85, dist: 8})`, "demo-lead-a/demo-a5"],
  ["panel-desktop", 61, `window.__shots.at("demo-lead-b", {yaw: ${Y}, pitch: 0.9, dist: 18, dx: 4})`, "demo-lead-b"],
];

async function desktopStills(browser, server) {
  const {ctx, page, errors} = await openPage(browser, server, DESKTOP);
  for (const [name, t, setup] of DESKTOP_SHOTS) await shoot(page, name, t, setup);
  for (const [name, t, setup, select] of SELECTED_SHOTS) await shoot(page, name, t, setup, select);
  await shoot(page, "legend", 64, FIT, null, true);
  h.assertNoErrors(errors);
  await ctx.close();
}

async function phoneStills(browser, server) {
  if (!want("phone-sheet") && !want("phone-waiting")) return;
  const {ctx, page, errors} = await openPage(browser, server, PHONE);
  await shoot(page, "phone-sheet", 71, `window.__shots.at("demo-lead-a", {yaw: ${Y}, pitch: 1.0, dist: 17, dz: -4})`, "demo-lead-a");
  await shoot(page, "phone-waiting", 75, FIT);
  h.assertNoErrors(errors);
  await ctx.close();
}

// The GIF: Otto's and Wren's workers at work, five seconds at 10 frames a second.
async function hiveGif(browser, server) {
  if (!want("hive")) return;
  const {ctx, page, errors} = await openPage(browser, server, {viewport: {width: 800, height: 450}});
  await page.evaluate((y) => window.__shots.between("demo-lead-a", "demo-lead-b", {yaw: y, pitch: 0.85, dist: 17}), Y);
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "hive-gif-"));
  const FPS = 10;
  await screens.frames(page, tmp, FPS * 5, (i) => window.__page.kit.drive(100 + i / 10));
  screens.gif(tmp, path.join(MEDIA, "hive.gif"), FPS, 640);
  fs.rmSync(tmp, {recursive: true, force: true});
  h.assertNoErrors(errors);
  await ctx.close();
}

h.run("screenshots", {repo: REPO, port: PORT}, async ({server, browser}) => {
  fs.mkdirSync(MEDIA, {recursive: true});
  await desktopStills(browser, server);
  await phoneStills(browser, server);
  await hiveGif(browser, server);
});
