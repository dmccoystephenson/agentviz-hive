"use strict";
/* hive.js: the agent feed as a beehive comb.

   Polls /api/agents every 2 s, keys everything by id, lays sessions out on
   the comb and eases between snapshots; each frame it writes three row
   arrays into the scene (scene/main.cpp documents their layout).

   Layout: the comb is a hex grid (axial q, r; pointy-top, size 1). Each
   session owns a 19-cell "flower" (a radius-2 hexagon); flowers tile the
   plane on the lattice a*(5,-2) + b*(2,3). The lead bee sits in the centre
   cell and its subagents on the ring around it. A place (repo) is the set of
   flowers of its sessions, tinted by the place's seed; a session keeps its
   flower (and a subagent its cell) for as long as it is in the feed. */

const SQRT3 = Math.sqrt(3);
const CELL_STRIDE = 7, BEE_STRIDE = 16, TRAIL_STRIDE = 6;
const STATE_CODE = {offline: 0, dim: 1, idle: 2, busy: 3, waiting: 4};
const ACTION_CODE = {none: 0, edit: 1, run: 2, read: 3, web: 4, delegate: 5, think: 6, other: 7};
const DIRS = [[1, 0], [1, -1], [0, -1], [-1, 0], [-1, 1], [0, 1]];
// Follower cells around the lead: the six neighbours, then two of ring 2.
const FOLLOWER_CELLS = [[1, 0], [0, 1], [-1, 1], [-1, 0], [0, -1], [1, -1], [1, 1], [-1, -1]];

function axialToWorld(q, r) { return [SQRT3 * (q + r / 2), 1.5 * r]; }
function hexDist(q, r) { return (Math.abs(q) + Math.abs(r) + Math.abs(q + r)) / 2; }

// Flower slots on the super-lattice, in spiral order from the centre.
const SLOTS = (() => {
  const out = [[0, 0]];
  for (let ring = 1; ring <= 6; ring++) {
    let a = -ring, b = ring;  // start at direction 4 * ring
    for (let side = 0; side < 6; side++) {
      for (let k = 0; k < ring; k++) {
        out.push([a, b]);
        a += DIRS[side][0]; b += DIRS[side][1];
      }
    }
  }
  return out.map(([a, b]) => ({a, b, q: 5 * a + 2 * b, r: -2 * a + 3 * b}));
})();
const slotDist = (i, j) => hexDist(SLOTS[i].a - SLOTS[j].a, SLOTS[i].b - SLOTS[j].b);

function hsl(h, s, l) {  // -> linear-ish rgb 0..1
  const f = (n) => {
    const k = (n + h * 12) % 12, a = s * Math.min(l, 1 - l);
    return l - a * Math.max(-1, Math.min(k - 3, 9 - k, 1));
  };
  return [f(0), f(8), f(4)].map((c) => Math.pow(c, 2.0));
}
function cssColor(rgb) {
  return "rgb(" + rgb.map((c) => Math.round(Math.pow(c, 1 / 2.0) * 255)).join(",") + ")";
}

class Hive {
  constructor(mod) {
    this.mod = mod;
    this.sessions = new Map();   // id -> {id, data, slot, x, z, tx, tz, alpha, talpha, followerSlots: Map}
    this.followers = new Map();  // sessionId/followerId -> {key, lead, data, x, z, tx, tz, alpha, talpha}
    this.places = new Map();     // id -> place doc
    this.slotOwner = new Map();  // slot index -> session id
    this.placeHue = new Map();   // place id -> hue 0..1
    this.combRadius = 7;
    this.meadow = [16, 0];
    this.selectedKey = null;
    this.first = true;
    this.ptr = {};
    this.doc = null;
  }

  /* ------------------------------------------------------------ layout */
  _allocSlot(s) {
    const used = (i) => this.slotOwner.has(i);
    const placeSlots = [];
    const otherSlots = [];
    for (const [i, id] of this.slotOwner) {
      const o = this.sessions.get(id);
      if (!o) continue;
      (o.data.place === s.place ? placeSlots : otherSlots).push(i);
    }
    let best = -1;
    if (placeSlots.length) {  // next to the rest of its place
      let bd = 1e9;
      for (let i = 0; i < SLOTS.length; i++) {
        if (used(i)) continue;
        const d = Math.min(...placeSlots.map((j) => slotDist(i, j)));
        if (d < bd) { bd = d; best = i; }
      }
    } else {  // a fresh region, not touching another place's flowers
      for (let i = 0; i < SLOTS.length && best < 0; i++) {
        if (used(i)) continue;
        if (otherSlots.every((j) => slotDist(i, j) > 1)) best = i;
      }
      for (let i = 0; i < SLOTS.length && best < 0; i++) if (!used(i)) best = i;
    }
    return best;
  }

  setDoc(doc) {
    this.doc = doc;
    this.places = new Map((doc.places || []).map((p) => [p.id, p]));
    // Each place keeps a hue for the page's life: its seed, pushed apart from
    // the hues already taken so neighbouring regions read as different.
    for (const p of doc.places || []) {
      if (this.placeHue.has(p.id)) continue;
      let h = p.seed || 0;
      for (let tries = 0; tries < 12; tries++) {
        const clash = [...this.placeHue.values()].some((o) => Math.min(Math.abs(o - h), 1 - Math.abs(o - h)) < 0.09);
        if (!clash) break;
        h = (h + 0.618034) % 1;
      }
      this.placeHue.set(p.id, h);
    }
    const seen = new Set(), seenF = new Set();
    // Busy and waiting sessions first, so they take the central flowers.
    const order = [...(doc.sessions || [])].sort((a, b) =>
      (STATE_CODE[b.state] >= 3) - (STATE_CODE[a.state] >= 3));
    for (const s of order) {
      seen.add(s.id);
      let o = this.sessions.get(s.id);
      if (!o) {
        o = {id: s.id, data: s, slot: -1, alpha: this.first ? 1 : 0, talpha: 1, followerSlots: new Map()};
        this.sessions.set(s.id, o);
      }
      o.data = s;
      o.talpha = 1;
      if (o.slot < 0) {
        o.slot = this._allocSlot(s);
        if (o.slot >= 0) this.slotOwner.set(o.slot, s.id);
      }
      const sl = SLOTS[Math.max(o.slot, 0)];
      o.q = sl.q; o.r = sl.r;
      [o.tx, o.tz] = axialToWorld(sl.q, sl.r);
      if (o.x == null) { o.x = o.tx; o.z = o.tz; }
      // Followers keep their cell while they stay in the feed.
      const fIds = new Set((s.followers || []).map((f) => f.id));
      for (const [fid, k] of o.followerSlots) if (!fIds.has(fid)) o.followerSlots.delete(fid);
      for (const f of (s.followers || []).slice(0, FOLLOWER_CELLS.length)) {
        if (!o.followerSlots.has(f.id)) {
          const taken = new Set(o.followerSlots.values());
          let k = 0;
          while (taken.has(k)) k++;
          o.followerSlots.set(f.id, k);
        }
        const key = s.id + "/" + f.id;
        seenF.add(key);
        const [dq, dr] = FOLLOWER_CELLS[o.followerSlots.get(f.id)];
        let fo = this.followers.get(key);
        if (!fo) {
          fo = {key, alpha: this.first ? 1 : 0};
          this.followers.set(key, fo);
        }
        fo.lead = s.id; fo.data = f; fo.talpha = 1;
        [fo.tx, fo.tz] = axialToWorld(sl.q + dq, sl.r + dr);
        if (fo.x == null) { fo.x = fo.tx; fo.z = fo.tz; }
      }
    }
    for (const [id, o] of this.sessions) if (!seen.has(id)) o.talpha = 0;
    for (const [k, f] of this.followers) if (!seenF.has(k)) f.talpha = 0;
    // The comb grows to hold every flower, plus a margin.
    let R = 6;
    for (const o of this.sessions.values()) R = Math.max(R, hexDist(o.q, o.r) + 3);
    this.combRadius = R;
    this.meadow = [SQRT3 * (R + 0.5) + 6, 0];
    this.first = false;
  }

  /* ------------------------------------------------------------ frame */
  _rows(name, stride, n) {
    const p = this.ptr[name] || {ptr: 0, cap: 0};
    if (n * stride > p.cap) {
      if (p.ptr) this.mod._free(p.ptr);
      p.cap = Math.max(64, n * stride * 2);
      p.ptr = this.mod._malloc(p.cap * 4);
      this.ptr[name] = p;
    }
    return p;
  }

  frame(dt) {
    const k = dt > 0 ? 1 - Math.exp(-dt * 3.5) : 1;
    const fade = dt > 0 ? Math.min(1, dt * 1.5) : 1;
    const ease = (o) => {
      o.x += (o.tx - o.x) * k; o.z += (o.tz - o.z) * k;
      o.alpha += Math.sign(o.talpha - o.alpha) * Math.min(Math.abs(o.talpha - o.alpha), fade);
    };
    for (const [id, o] of this.sessions) {
      ease(o);
      if (o.talpha === 0 && o.alpha <= 0.01) {
        this.sessions.delete(id);
        if (this.slotOwner.get(o.slot) === id) this.slotOwner.delete(o.slot);
      }
    }
    for (const [key, f] of this.followers) {
      ease(f);
      if (f.talpha === 0 && f.alpha <= 0.01) this.followers.delete(key);
    }
    const mod = this.mod;
    mod._hv_set_comb(this.combRadius, this.meadow[0], this.meadow[1]);

    // Region tints: every cell of each session's flower.
    const cells = [];
    for (const o of this.sessions.values()) {
      const col = hsl(this.placeHue.get(o.data.place) ?? 0.1, 0.6, 0.55);
      const a = o.alpha * (o.data.state === "offline" ? 0.45 : 1);
      for (let dq = -2; dq <= 2; dq++)
        for (let dr = Math.max(-2, -dq - 2); dr <= Math.min(2, -dq + 2); dr++) {
          const [x, z] = axialToWorld(dq, dr);
          cells.push(o.x + x, o.z + z, col[0], col[1], col[2], hexDist(dq, dr), a * 0.85);
        }
    }
    let p = this._rows("cells", CELL_STRIDE, cells.length / CELL_STRIDE);
    mod.HEAPF32.set(cells, p.ptr >> 2);
    mod._hv_set_cells(p.ptr, cells.length / CELL_STRIDE);

    const bees = [];
    for (const o of this.sessions.values()) {
      const s = o.data, band = hsl(s.seed || 0, 0.75, 0.55);
      bees.push(o.x, o.z, o.x, o.z, 0, STATE_CODE[s.state] ?? 1, ACTION_CODE[s.action] ?? 0, s.seed || 0,
        this.selectedKey === o.id ? 1 : 0, band[0], band[1], band[2], o.alpha, s.stalled ? 1 : 0, 1, 0);
    }
    for (const f of this.followers.values()) {
      const lead = this.sessions.get(f.lead);
      if (!lead) continue;
      const d = f.data, band = hsl(lead.data.seed || 0, 0.75, 0.55);
      const active = d.state === "active" && lead.data.state !== "offline";
      bees.push(f.x, f.z, lead.x, lead.z, 1, active ? 3 : 2, ACTION_CODE[d.action] ?? 0, d.seed ?? 0.5,
        this.selectedKey === f.key ? 1 : 0, band[0], band[1], band[2], f.alpha * lead.alpha, 0, active ? 1 : 0, 0);
    }
    p = this._rows("bees", BEE_STRIDE, bees.length / BEE_STRIDE);
    mod.HEAPF32.set(bees, p.ptr >> 2);
    mod._hv_set_bees(p.ptr, bees.length / BEE_STRIDE);

    // Scent trails: live sessions that share a place (possible conflict).
    const byPlace = new Map();
    for (const o of this.sessions.values()) {
      if (o.data.state === "offline" || o.talpha === 0) continue;
      if (!byPlace.has(o.data.place)) byPlace.set(o.data.place, []);
      byPlace.get(o.data.place).push(o);
    }
    const trails = [];
    for (const list of byPlace.values())
      for (let i = 0; i + 1 < list.length; i++)
        trails.push(list[i].x, list[i].z, list[i + 1].x, list[i + 1].z,
          Math.min(list[i].alpha, list[i + 1].alpha), (list[i].data.seed || 0) * 3.1);
    this.sharedPlaces = [...byPlace.entries()].filter(([, l]) => l.length > 1).map(([id]) => id);
    p = this._rows("trails", TRAIL_STRIDE, Math.max(1, trails.length / TRAIL_STRIDE));
    mod.HEAPF32.set(trails, p.ptr >> 2);
    mod._hv_set_trails(p.ptr, trails.length / TRAIL_STRIDE);
  }

  /* ------------------------------------------------------------ picking */
  things() {
    const out = [];
    for (const o of this.sessions.values())
      if (o.talpha > 0) out.push({key: o.id, kind: "session", x: o.x, z: o.z, y: 0.9, o});
    for (const f of this.followers.values())
      if (f.talpha > 0) out.push({key: f.key, kind: "follower", x: f.x, z: f.z, y: 0.7, o: f});
    return out;
  }

  pick(x, y, radius, project) {
    let best = null, bd = radius * radius;
    for (const t of this.things()) {
      for (const yy of [0.3, t.y]) {
        const s = project(t.x, yy, t.z);
        if (!s) continue;
        const d = (s.x - x) ** 2 + (s.y - y) ** 2;
        // Leads win ties over the workers around them.
        const w = t.kind === "session" ? d * 0.6 : d;
        if (w < bd) { bd = w; best = t; }
      }
    }
    return best;
  }

  centre() {
    let n = 0, x = 0, z = 0;
    for (const o of this.sessions.values()) { x += o.tx; z += o.tz; n++; }
    return n ? [x / n, z / n] : [0, 0];
  }
}

window.Hive = Hive;
window.HiveColors = {hsl, cssColor};
