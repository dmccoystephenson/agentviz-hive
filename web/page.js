/* page.js: boots the hive. The feed client, HUD, labels, picking and wire
   come from the agentviz kit (agentviz-*.js); this file wires them to the
   HiveModel and the Strata kit, and owns the camera and the world labels. */
(function () {
  "use strict";
  const {HiveModel, cssColor, bandOf} = window.HiveModel;
  const DEFAULT_YAW = -1.35, DEFAULT_PITCH = 0.95, NEAR_SHIFT = 0.12;
  // A portrait screen is narrow: look down more steeply so the comb fills
  // more of the height, not a strip across the middle.
  const PORTRAIT_PITCH = 1.2;
  const PHONE_WIDTH = 640;  // agentviz-hud.css: below this the card is a bottom sheet
  const REASON = {question: "has a question", permission: "needs permission"};
  const canvas = document.getElementById("view");
  const model = new HiveModel();
  const wire = AgentvizWire.create();
  const labels = AgentvizScreen.labels(document.getElementById("labels"));
  let kit = null, mod = null, feed = null, selectedId = null, hoveredId = null, camAnim = null, fitted = false;
  let sceneTime = 0;

  /* ------------------------------------------------------------ camera */
  // Every session's patch in view (x runs across the screen at the default yaw).
  function fitView() {
    const b = model.bounds(4.5);
    const aspect = canvas.clientWidth / Math.max(1, canvas.clientHeight);
    const portrait = aspect < 1, pitch = portrait ? PORTRAIT_PITCH : DEFAULT_PITCH;
    const span = Math.max((b.x1 - b.x0) / Math.min(aspect, 1.9), (b.z1 - b.z0) * 0.85);
    // Perspective makes the near side loom: aim a little towards the camera.
    // A steep portrait view barely looms, so it aims at the middle instead.
    const toEye = portrait ? 0 : span * NEAR_SHIFT;
    const dist = portrait ? Math.max(16, (b.x1 - b.x0) * 1.12 / aspect + 2) : Math.max(16, span * 1.05 + 4);
    return {yaw: DEFAULT_YAW, pitch, dist,
      tx: (b.x0 + b.x1) / 2 + Math.cos(DEFAULT_YAW) * toEye, ty: 0, tz: (b.z0 + b.z1) / 2 + Math.sin(DEFAULT_YAW) * toEye};
  }
  function fit(instant) { flyTo(fitView(), instant); }
  function flyTo(to, instant) {
    if (!kit) return;
    if (instant || kit.driven) { Object.assign(kit.cam, to); camAnim = null; return; }
    camAnim = {from: Object.assign({}, kit.cam), to, t0: performance.now(), ms: 700};
  }
  function stepCam() {
    if (!camAnim) return;
    const k = Math.min(1, (performance.now() - camAnim.t0) / camAnim.ms), e = AgentvizFeed.smoothstep(k);
    for (const key of Object.keys(camAnim.to)) kit.cam[key] = AgentvizFeed.lerp(camAnim.from[key], camAnim.to[key], e);
    if (k >= 1) camAnim = null;
  }
  function focusOn(bee, dist) {
    flyTo({tx: bee.o.tx, ty: 0, tz: bee.o.tz, dist: Math.min(kit.cam.dist, dist)});
  }
  // On a phone the card is a bottom sheet over the lower half: if it would
  // cover the bee just tapped, slide the view (no zoom, no turn) so the bee
  // sits in the clear part of the screen above it.
  function keepClearOfSheet(bee) {
    if (!kit || canvas.clientWidth > PHONE_WIDTH) return;
    const to = AgentvizScreen.reveal(kit.cam, [bee.o.tx, bee.lead ? 0.9 : 0.7, bee.o.tz], hud.freeRect(canvas));
    if (to) flyTo(to);
  }

  /* ------------------------------------------------------------ labels */
  function sessionLabel(o) {
    const s = o.data, waiting = s.state === "waiting", offline = s.state === "offline";
    const resting = s.state === "idle" || s.state === "dim", stalled = !!s.stalled && !waiting;
    const cls = "lead" + (waiting ? " waiting" : "") + (offline ? " offline" : "") + (resting ? " resting" : "") +
      (stalled ? " stalled" : "") + (o.id === selectedId ? " sel" : "");
    const name = s.name || s.id;
    const text = waiting ? name + " " + (REASON[s.waitingReason] || "needs you") : stalled ? name + " · stalled" : name;
    const el = labels.set("s:" + o.id, {cls, action: s.state === "busy" ? s.action : null, text});
    el.style.setProperty("--band", cssColor(bandOf(s)));
    el.style.opacity = o.appear;
    const rank = waiting || o.id === selectedId ? 0 : (offline ? 3 : (resting ? 2 : 1));
    return {el, q: kit.project(o.x, waiting ? 2.4 : 2.0, o.z), rank};
  }
  function placeLabel(o) {
    const el = labels.set("p:" + o.data.place, {cls: "place", text: o.data.placeLabel || o.data.place});
    return {el, q: kit.project(o.x, 0.5, o.z + 3.4), rank: 2};
  }
  // A subagent is labelled only while selected (the comb would drown in text).
  function followerLabel(f) {
    const d = f.data, el = labels.set("f:" + f.id, {cls: "sel", action: d.state === "active" ? d.action : "none",
      text: d.label || d.id});
    return {el, q: kit.project(f.x, 1.5, f.z), rank: 0};
  }
  function placeLabels() {
    const items = [], placesDone = new Set();
    labels.begin();
    for (const o of model.sessions.values()) {
      items.push(sessionLabel(o));
      if (placesDone.has(o.data.place)) continue;
      placesDone.add(o.data.place);
      items.push(placeLabel(o));
    }
    const f = selectedId && model.followers.get(selectedId);
    if (f) items.push(followerLabel(f));
    labels.end();
    placeClear(items);
  }

  // Keep world labels out from under the HUD (agentviz-hud.js placeLabels):
  // the open card or sheet, the legend and, on a phone, the whole top band.
  // On a desktop the bar's empty middle stays usable: only its chips are
  // kept clear. Strata kit 1.4 takes the area as placeLabels' topInset /
  // avoid; until the vendored kit is 1.4 AgentvizScreen.keepClear does it.
  const KIT_AVOIDS = (() => {
    const [major, minor] = String(StrataKit.version).split(".").map(Number);
    return major > 1 || (major === 1 && minor >= 4);
  })();
  function placeClear(items) {
    if (canvas.clientWidth <= PHONE_WIDTH) { hud.placeLabels(items, {canvas, strataAvoid: KIT_AVOIDS}); return; }
    const area = hud.clearArea(canvas), pad = 4, c = canvas.getBoundingClientRect();
    for (const chip of hud.bar.querySelectorAll(".av-chip:not([hidden])")) {
      const r = chip.getBoundingClientRect();
      if (r.width > 0 && r.height > 0) area.avoid.push({x0: r.left - c.left - pad, y0: r.top - c.top - pad,
        x1: r.right - c.left + pad, y1: r.bottom - c.top + pad});
    }
    area.top = -Infinity;
    if (KIT_AVOIDS) {
      AgentvizScreen.keepClear(items, {W: area.W, H: area.H});
      StrataKit.placeLabels(items, AgentvizScreen.strataAvoid(area));
    } else {
      AgentvizScreen.keepClear(items, area);
      StrataKit.placeLabels(items);
    }
  }

  /* ------------------------------------------------------------ picking + selection */
  // Leads list two points (cell and hovering bee) so they win ties with the workers around them.
  function pick(x, y, radius) {
    const candidates = model.bees().map((b) => ({item: b,
      points: b.lead ? [[b.o.x, 0.3, b.o.z], [b.o.x, 0.9, b.o.z], [b.o.x, 1.4, b.o.z]] : [[b.o.x, 0.7, b.o.z]]}));
    return AgentvizScreen.pick(kit.project, candidates, x, y, radius);
  }
  const targetOf = (b) => (b ? {session: b.session, follower: b.follower} : null);
  const idOf = (target) => (target ? (target.follower ? AgentvizFeed.followerKey(target.session, target.follower) : target.session.id) : null);

  function onSelect(target, how) {
    selectedId = idOf(target);
    const bee = selectedId && model.find(selectedId);
    if (kit) kit.selected = bee || null;
    if (bee && how === "waiting") focusOn(bee, 22);
    else if (bee && how === "list") focusOn(bee, 14);
    else if (bee && how === "tap") keepClearOfSheet(bee);
  }

  const hud = AgentvizHud.create({
    title: "Agent Hive",
    legend: document.getElementById("legend"),
    buttons: [{icon: "home", label: "Recentre the view", onClick: () => fit(false)}],
    onSelect,
    oneRow: true,
    rows: ({session, follower}) => (!follower && model.sharedPlaces.includes(session.place)
      ? [["scent", "shares this repo with another live session"]] : []),
  });

  function accept(doc) {
    model.update(doc);
    hud.update(doc);
    if (!fitted && model.sessions.size) { fitted = true; fit(true); }
  }

  /* ------------------------------------------------------------ boot */
  function frame(t, now, dt) {
    sceneTime = t;
    stepCam();
    model.step(dt);
    const highlight = (id) => (id === selectedId ? 1 : id === hoveredId ? 2 : 0);
    window.HiveFrame.write(wire, model, feed.doc, highlight, feed.pulse).send(mod);
  }

  function screenTargets() {
    const r = canvas.getBoundingClientRect();
    return model.bees().map((b) => {
      const s = kit.project(b.o.x, b.lead ? 0.9 : 0.7, b.o.z);
      return s && {id: b.id, x: s.x + r.left, y: s.y + r.top};
    }).filter(Boolean);
  }

  function createKit() {
    return StrataKit.create({
      module: mod, canvas,
      camera: {yaw: DEFAULT_YAW, pitch: DEFAULT_PITCH, dist: 30, tx: 0, ty: 0, tz: 0},
      limits: {maxDist: 110, minDist: 5, maxPitch: 1.45},
      frame,
      after: placeLabels,
      pick,
      idOf: (b) => b.id,
      onTap: (b) => hud.select(targetOf(b), "tap"),
      onHover(b) { hoveredId = b ? b.id : null; canvas.style.cursor = b ? "pointer" : ""; },
      onSlow: () => StrataLoader.slow(),
      onFatal: (code, msg) => StrataLoader.fallback(code, msg),
      targets: screenTargets,
    });
  }

  async function boot() {
    try {
      mod = await StrataLoader.load({script: "hive-engine.js", factory: "createHive", wasm: "hive-engine.wasm",
        title: "the agent hive"});
    } catch (error) {
      console.error("hive: the engine did not load", error);
      return;  // StrataLoader has already shown its fallback
    }
    try {
      kit = createKit();
    } catch (error) {
      StrataLoader.fallback(error.code === "no-webgl2" ? "no-webgl2" : "engine");
      return;
    }
    // The tool-call pulse runs on scene time when driven (?drive=1), so captures repeat.
    const now = () => (kit.driven ? sceneTime : Date.now() / 1000);
    feed = AgentvizFeed.create({onUpdate: accept, onStatus: hud.setStatus, now});
    window.__page = {kit, model, hud, fit, flyTo,
      select: (id, focus) => hud.select(targetOf(model.find(id)), focus ? "list" : "api"),
      get doc() { return feed.doc; }, get polls() { return feed.polls; },
      stopPolling: () => feed.stop(), pollOnce: () => feed.pollOnce(), feed: (d) => feed.push(d)};
    // A fixed late-afternoon sun, so captures repeat.
    kit.setSun(Date.UTC(2026, 5, 21, 23, 0, 0) / 1000, 40, -105);
    feed.start();
    kit.start();
    if (kit.driven) kit.drive(0);
    StrataLoader.done();
    canvas.focus({preventScroll: true});
  }

  boot();
})();
