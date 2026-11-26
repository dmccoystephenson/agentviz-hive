// Headless smoke test on the demo feed (vendor/agentviz-kit/tests/harness.cjs):
// every fixture agent is on the comb; the "needs you" chip cycles both waiting
// sessions; a tap on a waiting bee opens its panel, which says why it waits;
// the stalled session and the stalled subagent say so; a subagent on its own
// branch shows it; feed strings never become HTML; the empty state and the
// unreachable banner show; the frame is not flat; no console errors. On a
// phone (touch, 390 x 844) the top bar stays one line, no world label sits
// under it, and a tap on a bee low on the screen opens the bottom sheet
// without covering that bee.
// Expected counts come from the feed document, not from numbers here.
//   npm ci && npx playwright install chromium && npm run smoke
const path = require("path");
const h = require("../vendor/agentviz-kit/tests/harness.cjs");

const REPO = path.resolve(__dirname, "..");
const PORT = Number(process.env.HIVE_TEST_PORT || 9116);
const READY = "window.__page && window.__page.polls > 0 && window.__page.model.sessions.size > 0";

function check(cond, message) { if (!cond) throw new Error(message); }

async function checkScene(page) {
  const info = await page.evaluate(() => {
    const m = window.__page.model, doc = window.__page.doc;
    const live = doc.sessions.filter((s) => s.state !== "offline");
    const byPlace = {};
    for (const s of live) byPlace[s.place] = (byPlace[s.place] || 0) + 1;
    return {sessions: m.sessions.size, followers: m.followers.size, shared: m.sharedPlaces.slice().sort(),
      want: {sessions: doc.sessions.length, followers: doc.sessions.reduce((n, s) => n + s.followers.length, 0),
        shared: Object.keys(byPlace).filter((p) => byPlace[p] > 1).sort()},
      waiting: doc.sessions.filter((s) => s.state === "waiting").map((s) => s.name)};
  });
  console.log("scene:", JSON.stringify(info));
  check(info.sessions === info.want.sessions && info.followers === info.want.followers,
    "the comb does not hold every fixture agent");
  check(info.shared.join() === info.want.shared.join(), "scent trails do not match the shared places");
  check(info.waiting.length === 2, "the fixture should have two sessions waiting");
  return info;
}

const panelTitle = (page) => page.evaluate(() => {
  const panel = document.querySelector(".av-panel");
  return panel.hidden ? null : panel.querySelector("h2").textContent.trim();
});
const panelText = (page) => page.evaluate(() => document.querySelector(".av-panel").textContent);

async function checkWaitingChip(page, names) {
  const chip = page.locator(".av-wait");
  check(await chip.isVisible(), "the needs-you chip is hidden");
  check((await chip.locator("b").textContent()) === String(names.length), "the needs-you chip miscounts");
  const seen = [];
  for (let i = 0; i < names.length; i++) { await chip.click(); seen.push(await panelTitle(page)); }
  console.log("needs-you chip cycles:", seen.join(" -> "));
  check(seen.slice().sort().join() === names.slice().sort().join(), "the chip did not cycle both waiting sessions");
  check(await page.locator(".av-panel .av-needs").isVisible(), "the panel does not say why it waits");
}

async function checkTapWaiting(page) {
  await page.evaluate(() => { window.__page.select(null); window.__page.fit(true); window.__page.kit.drive(1); });
  const waiting = await page.evaluate(() => window.__page.doc.sessions.find((s) => s.waitingReason === "permission"));
  const hit = (await page.evaluate(() => window.__strataKit.targets())).find((t) => t.id === waiting.id);
  check(hit, "no tap target for the waiting session");
  await page.mouse.click(hit.x, hit.y);
  await page.waitForTimeout(300);
  const title = await panelTitle(page);
  const reason = await page.evaluate(() => document.querySelector(".av-panel .av-needs").textContent);
  console.log("tap on the waiting bee:", title, "/", reason);
  check(title === waiting.name && reason === "needs permission", "the tap did not open the waiting panel with its reason");
}

async function checkStalled(page) {
  const who = await page.evaluate(() => {
    const doc = window.__page.doc, lead = doc.sessions.find((s) => s.stalled);
    const owner = doc.sessions.find((s) => s.followers.some((f) => f.stalled));
    return {lead: lead.id, follower: owner.id + "/" + owner.followers.find((f) => f.stalled).id,
      label: owner.followers.find((f) => f.stalled).label};
  });
  await page.evaluate((id) => window.__page.select(id, false), who.lead);
  check((await panelText(page)).includes("stalled"), "the stalled session's panel does not say stalled");
  await page.evaluate((id) => window.__page.select(id, false), who.follower);
  check((await panelTitle(page)) === who.label && (await panelText(page)).includes("stalled"),
    "the stalled subagent's panel does not say stalled");
  const flag = await page.evaluate((id) => {
    const packed = AgentvizWire.create();
    window.HiveFrame.write(packed, window.__page.model, window.__page.doc, () => 0, () => 0);
    const data = packed.finish(), W = AgentvizWire, want = W.id24(id);
    const start = W.HEADER + data[2] * W.ZONE_STRIDE;
    for (let i = 0; i < data[3]; i++) if (data[start + i * W.AGENT_STRIDE] === want) return data[start + i * W.AGENT_STRIDE + 6];
    return null;
  }, who.follower);
  check(flag === 1, "the stalled subagent is not stalled on the wire");
  console.log("stalled:", who.lead, "and", who.follower);
}

async function checkBranch(page) {
  const f = await page.evaluate(() => {
    for (const s of window.__page.doc.sessions)
      for (const x of s.followers) if (x.branch && x.branch !== s.branch) return {id: s.id + "/" + x.id, branch: x.branch};
    return null;
  });
  check(f, "the fixture has no subagent on its own branch");
  await page.evaluate((id) => window.__page.select(id, false), f.id);
  const rows = await page.evaluate(() => [...document.querySelectorAll(".av-panel dd")].map((d) => d.textContent));
  check(rows.includes(f.branch), "the subagent panel does not show its own branch");
}

async function checkTextOnly(page) {
  const result = await page.evaluate(() => {
    const doc = JSON.parse(JSON.stringify(window.__page.doc));
    const lead = doc.sessions.find((s) => s.followers.length > 0);
    lead.followers[0].label = "<img src=x id=pwned>";
    window.__page.feed(doc);
    window.__page.select(lead.id, false);
    window.__page.select(lead.id + "/" + lead.followers[0].id, false);
    window.__page.kit.drive(2);
    return {img: !!document.getElementById("pwned"),
      label: [...document.querySelectorAll(".av-label")].some((l) => l.textContent === "<img src=x id=pwned>"),
      panel: document.querySelector(".av-panel h2").textContent.trim() === "<img src=x id=pwned>"};
  });
  check(!result.img && result.label && result.panel, "a feed label was parsed as HTML: " + JSON.stringify(result));
}

async function checkEmptyState(page) {
  const shown = await page.evaluate(() => {
    const doc = JSON.parse(JSON.stringify(window.__page.doc));
    const sessions = doc.sessions;
    window.__page.feed(Object.assign(doc, {sessions: []}));
    const visible = !document.querySelector(".av-empty").hidden;
    window.__page.feed(Object.assign(doc, {sessions}));
    return visible && document.querySelector(".av-empty").hidden;
  });
  check(shown, "no empty-state hint with zero sessions");
}

async function checkUnreachable(page) {
  await page.route("**/api/agents*", (route) => route.fulfill({status: 503, body: "down"}));
  await page.evaluate(async () => { await window.__page.pollOnce(); await window.__page.pollOnce(); });
  const banner = await page.evaluate(() => !document.querySelector(".av-banner").hidden && document.querySelector(".av-banner").textContent);
  console.log("banner:", banner);
  check(banner && banner.includes("HTTP 503"), "no unreachable banner after failed polls");
}

async function checkPhone(browser, server) {
  const {ctx, page, errors} = await h.open(browser, server.base + "/?fixture=1&drive=1",
    {ready: READY, viewport: {width: 390, height: 844}, dpr: 2, mobile: true});
  await h.stillAnimations(page);
  const draw = async (t) => { for (const ms of [250, 250]) { await page.evaluate((x) => window.__page.kit.drive(x), t); await page.waitForTimeout(ms); } };
  await page.evaluate(() => window.__page.stopPolling());
  await draw(1);
  const bar = await page.evaluate(() => {
    const b = document.querySelector(".av-bar").getBoundingClientRect();
    const kids = [...document.querySelectorAll(".av-bar > *")].filter((e) => e.offsetWidth > 0)
      .map((e) => e.getBoundingClientRect());
    const covered = [...document.querySelectorAll(".av-label:not(.sk-label-off)")].filter((e) => e.style.display !== "none")
      .filter((e) => { const r = e.getBoundingClientRect(); return r.top < b.bottom && r.bottom > b.top; }).map((e) => e.textContent);
    return {height: Math.round(b.height), rows: new Set(kids.map((r) => Math.round(r.top + r.height / 2))).size,
      right: Math.max(...kids.map((r) => r.right)), covered};
  });
  console.log("phone bar:", JSON.stringify(bar));
  check(bar.rows === 1 && bar.right <= 390, "the phone top bar wraps or overflows");
  check(bar.covered.length === 0, "labels sit under the phone top bar: " + bar.covered.join(", "));
  // The lowest bee on screen: the sheet would cover it unless the view makes room.
  const low = await page.evaluate(() => window.__strataKit.targets().filter((t) => t.y < innerHeight - 40)
    .sort((a, b) => b.y - a.y)[0]);
  check(low && low.y > 844 * 0.45, "no bee low enough on the phone screen to test the sheet");
  await page.touchscreen.tap(low.x, low.y);
  await draw(1.2);
  await page.waitForTimeout(800);
  await draw(1.3);
  const after = await page.evaluate(() => {
    const id = window.__strataKit.lastTapId, panel = document.querySelector(".av-panel");
    const t = window.__strataKit.targets().find((x) => x.id === id);
    return {id, open: !panel.hidden, top: Math.round(panel.getBoundingClientRect().top), y: t && Math.round(t.y)};
  });
  console.log("phone tap:", JSON.stringify(after));
  check(after.open && after.y != null && after.y < after.top - 20, "the phone sheet covers the bee just tapped");
  h.assertNoErrors(errors);
  await ctx.close();
}

h.run("smoke", {repo: REPO, port: PORT}, async ({server, browser}) => {
  const {page, errors} = await h.open(browser, server.base + "/?fixture=1&drive=1&quality=1", {ready: READY});
  await h.stillAnimations(page);
  await page.evaluate(() => window.__page.stopPolling());
  const info = await checkScene(page);
  await checkWaitingChip(page, info.waiting);
  await checkTapWaiting(page);
  await checkStalled(page);
  await checkBranch(page);
  await checkTextOnly(page);
  await checkEmptyState(page);
  await page.evaluate(() => { window.__page.select(null); window.__page.fit(true); });
  await page.mouse.move(2, 400);  // no hover ring in the saved frame
  const png = await require("../vendor/agentviz-kit/tests/screens.cjs").still(page,
    path.join(REPO, "docs/media/screenshot.png"), () => window.__page.kit.drive(12));
  console.log("distinct colours:", await h.assertNotFlat(page, png));
  h.assertNoErrors(errors);
  await checkPhone(browser, server);
  await checkUnreachable(page);  // last: the failed fetches log console errors of their own
});
