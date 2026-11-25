/* hive-model.js: the agent feed laid out on a honeycomb, eased between snapshots.

   Layout: the comb is a hex grid (axial q, r; pointy-top, cell size 1). Each
   session owns a 19-cell patch (a radius-2 hexagon); patches tile the plane on
   the lattice a*(5,-2) + b*(2,3). The lead bee sits in the centre cell and its
   subagents on the cells around it. A place (repo) is the set of patches of
   its sessions, tinted by the place's hue; a session keeps its patch (and a
   subagent its cell) for as long as it is in the feed.

   Classic script: window.HiveModel. Needs AgentvizFeed (easing, followerKey). */
(function () {
  "use strict";
  const {followerKey} = window.AgentvizFeed;
  const SQRT3 = Math.sqrt(3);
  const DIRS = [[1, 0], [1, -1], [0, -1], [-1, 0], [-1, 1], [0, 1]];
  // Subagent cells around the lead: the six neighbours, then two of ring 2.
  const FOLLOWER_CELLS = [[1, 0], [0, 1], [-1, 1], [-1, 0], [0, -1], [1, -1], [1, 1], [-1, -1]];
  const MOVE_RATE = 3.5, FADE_RATE = 1.5;
  const HUE_GAP = 0.09, GOLDEN = 0.618034;

  const axialToWorld = (q, r) => [SQRT3 * (q + r / 2), 1.5 * r];
  const hexDist = (q, r) => (Math.abs(q) + Math.abs(r) + Math.abs(q + r)) / 2;

  // Patch slots on the super-lattice, in spiral order from the centre.
  const SLOTS = (() => {
    const out = [[0, 0]];
    for (let ring = 1; ring <= 6; ring++) {
      let a = -ring, b = ring;
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

  // HSL -> rgb 0..1, squared (the engine's colours are linear-ish).
  function hsl(h, s, l) {
    const f = (n) => {
      const k = (n + h * 12) % 12, a = s * Math.min(l, 1 - l);
      return l - a * Math.max(-1, Math.min(k - 3, 9 - k, 1));
    };
    return [f(0), f(8), f(4)].map((c) => c * c);
  }
  const cssColor = (rgb) => "rgb(" + rgb.map((c) => Math.round(Math.sqrt(c) * 255)).join(",") + ")";
  const bandOf = (session) => hsl(session.seed || 0, 0.75, 0.55);
  const isLive = (state) => state === "busy" || state === "waiting";

  // Position and fade-in, eased towards their targets each frame.
  function easeBody(o, dt) {
    const k = dt > 0 ? 1 - Math.exp(-dt * MOVE_RATE) : 1;
    const step = dt > 0 ? Math.min(1, dt * FADE_RATE) : 1;
    o.x += (o.tx - o.x) * k; o.z += (o.tz - o.z) * k;
    o.appear += Math.sign(o.target - o.appear) * Math.min(Math.abs(o.target - o.appear), step);
  }

  class HiveModel {
    constructor() {
      this.sessions = new Map();   // id -> {id, data, slot, q, r, x, z, tx, tz, appear, target, followerCells}
      this.followers = new Map();  // "lead/follower" -> {id, lead, session, data, x, z, tx, tz, appear, target}
      this.slotOwner = new Map();  // slot index -> session id
      this.placeHue = new Map();   // place id -> hue 0..1
      this.combRadius = 6;
      this.meadow = [16, 0];
      this.sharedPlaces = [];
      this.first = true;
    }

    /* ------------------------------------------------------------ snapshots */
    update(doc) {
      this._assignHues(doc.places || []);
      const seen = new Set();
      // Busy and waiting sessions first, so they take the central patches.
      const order = [...(doc.sessions || [])].sort((a, b) => isLive(b.state) - isLive(a.state));
      for (const s of order) this._placeSession(s, seen);
      for (const [id, o] of this.sessions) if (!seen.has(id)) o.target = 0;
      for (const [id, f] of this.followers) if (!seen.has(id)) f.target = 0;
      this._growComb();
      this.sharedPlaces = [...this.liveByPlace().entries()].filter(([, l]) => l.length > 1).map(([id]) => id);
      this.first = false;
    }

    // Each place keeps a hue for the page's life: its seed, pushed apart from
    // the hues already taken so neighbouring regions read as different.
    _assignHues(places) {
      for (const p of places) {
        if (this.placeHue.has(p.id)) continue;
        let hue = p.seed || 0;
        const clashes = (h) => [...this.placeHue.values()].some((o) => Math.min(Math.abs(o - h), 1 - Math.abs(o - h)) < HUE_GAP);
        for (let tries = 0; tries < 12 && clashes(hue); tries++) hue = (hue + GOLDEN) % 1;
        this.placeHue.set(p.id, hue);
      }
    }

    _placeSession(s, seen) {
      seen.add(s.id);
      let o = this.sessions.get(s.id);
      if (!o) {
        o = {id: s.id, slot: -1, appear: this.first ? 1 : 0, followerCells: new Map()};
        this.sessions.set(s.id, o);
      }
      o.data = s;
      o.target = 1;
      if (o.slot < 0) {
        o.slot = this._freeSlot(s);
        if (o.slot >= 0) this.slotOwner.set(o.slot, s.id);
      }
      const slot = SLOTS[Math.max(o.slot, 0)];
      o.q = slot.q; o.r = slot.r;
      [o.tx, o.tz] = axialToWorld(slot.q, slot.r);
      if (o.x == null) { o.x = o.tx; o.z = o.tz; }
      this._placeFollowers(o, seen);
    }

    // Subagents keep their cell while they stay in the feed.
    _placeFollowers(o, seen) {
      const s = o.data, list = (s.followers || []).slice(0, FOLLOWER_CELLS.length);
      const ids = new Set(list.map((f) => f.id));
      for (const fid of o.followerCells.keys()) if (!ids.has(fid)) o.followerCells.delete(fid);
      for (const f of list) {
        if (!o.followerCells.has(f.id)) {
          const taken = new Set(o.followerCells.values());
          let cell = 0;
          while (taken.has(cell)) cell++;
          o.followerCells.set(f.id, cell);
        }
        const id = followerKey(s, f);
        seen.add(id);
        let fo = this.followers.get(id);
        if (!fo) {
          fo = {id, appear: this.first ? 1 : 0};
          this.followers.set(id, fo);
        }
        Object.assign(fo, {lead: s.id, session: s, data: f, target: 1});
        const [dq, dr] = FOLLOWER_CELLS[o.followerCells.get(f.id)];
        [fo.tx, fo.tz] = axialToWorld(o.q + dq, o.r + dr);
        if (fo.x == null) { fo.x = fo.tx; fo.z = fo.tz; }
      }
    }

    // Next to the rest of its place, or a fresh region not touching another place.
    _freeSlot(s) {
      const own = [], others = [];
      for (const [i, id] of this.slotOwner) {
        const o = this.sessions.get(id);
        if (o && o.data) (o.data.place === s.place ? own : others).push(i);
      }
      const free = SLOTS.map((_, i) => i).filter((i) => !this.slotOwner.has(i));
      if (own.length) {
        let best = -1, bestD = Infinity;
        for (const i of free) {
          const d = Math.min(...own.map((j) => slotDist(i, j)));
          if (d < bestD) { bestD = d; best = i; }
        }
        return best;
      }
      const apart = free.find((i) => others.every((j) => slotDist(i, j) > 1));
      return apart != null ? apart : (free.length ? free[0] : -1);
    }

    // The comb grows to hold every patch, plus a margin; the flowers sit beyond it.
    _growComb() {
      let radius = 6;
      for (const o of this.sessions.values()) radius = Math.max(radius, hexDist(o.q, o.r) + 3);
      this.combRadius = radius;
      this.meadow = [SQRT3 * (radius + 0.5) + 6, 0];
    }

    /* ------------------------------------------------------------ frames */
    step(dt) {
      for (const [id, o] of this.sessions) {
        easeBody(o, dt);
        if (o.target === 0 && o.appear <= 0.01) {
          this.sessions.delete(id);
          if (this.slotOwner.get(o.slot) === id) this.slotOwner.delete(o.slot);
        }
      }
      for (const [id, f] of this.followers) {
        easeBody(f, dt);
        if (f.target === 0 && f.appear <= 0.01) this.followers.delete(id);
      }
    }

    // Live (not offline, still in the feed) sessions grouped by place.
    liveByPlace() {
      const byPlace = new Map();
      for (const o of this.sessions.values()) {
        if (o.data.state === "offline" || o.target === 0) continue;
        if (!byPlace.has(o.data.place)) byPlace.set(o.data.place, []);
        byPlace.get(o.data.place).push(o);
      }
      return byPlace;
    }

    placeColour(place) { return hsl(this.placeHue.has(place) ? this.placeHue.get(place) : 0.1, 0.6, 0.55); }

    // Every bee still in the feed (and, with leaving, those fading out):
    // {id, lead, o, session, follower}.
    bees(leaving = false) {
      const out = [], keep = (o) => leaving || o.target > 0;
      for (const o of this.sessions.values())
        if (keep(o)) out.push({id: o.id, lead: true, o, session: o.data, follower: null});
      for (const f of this.followers.values())
        if (keep(f) && this.sessions.has(f.lead)) out.push({id: f.id, lead: false, o: f, session: f.session, follower: f.data});
      return out;
    }

    find(id) { return this.bees().find((b) => b.id === id) || null; }

    // The rectangle holding every session's cell, grown by margin.
    bounds(margin) {
      const b = {x0: Infinity, z0: Infinity, x1: -Infinity, z1: -Infinity};
      for (const o of this.sessions.values()) {
        b.x0 = Math.min(b.x0, o.tx - margin); b.x1 = Math.max(b.x1, o.tx + margin);
        b.z0 = Math.min(b.z0, o.tz - margin); b.z1 = Math.max(b.z1, o.tz + margin);
      }
      return b.x0 < Infinity ? b : {x0: -margin, z0: -margin, x1: margin, z1: margin};
    }
  }

  window.HiveModel = {HiveModel, hsl, cssColor, bandOf, axialToWorld};
})();
