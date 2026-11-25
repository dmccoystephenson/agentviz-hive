/* hive-frame.js: packs the hive for the scene, once a frame, in the agentviz
   wire format (vendor/agentviz-kit, docs/KIT.md). scene/comb.h, bee.h,
   cell.h and props.h read what is written here.

     scene floats  comb radius (cells), flower field x, z
     zones         kind 0: one session's patch; centre = the rect's middle;
                   extras: tint r, g, b, strength
     agents        x, z = the bee's cell; extras: lead x, lead z, the session's
                   colour r, g, b, active (a worker working for a live lead)
     links         kind 0: scent trail between live sessions in one place

   Classic script: window.HiveFrame. Needs HiveModel. */
(function () {
  "use strict";
  const {bandOf} = window.HiveModel;
  const ZONE = {patch: 0};
  const LINK = {scent: 0};
  const PATCH_HALF = 2;  // a patch is a radius-2 hexagon of cells
  const TINT = 0.85, OFFLINE_TINT = 0.45;

  function patch(wire, model, o) {
    const s = o.data, rgb = model.placeColour(s.place);
    wire.zone({id: "patch:" + o.id, kind: ZONE.patch, seed: s.seed || 0, appear: o.appear,
      x0: o.x - PATCH_HALF, z0: o.z - PATCH_HALF, x1: o.x + PATCH_HALF, z1: o.z + PATCH_HALF,
      extra: [rgb[0], rgb[1], rgb[2], TINT * (s.state === "offline" ? OFFLINE_TINT : 1)]});
  }

  function bee(wire, model, b, highlight, pulse) {
    const lead = model.sessions.get(b.session.id);
    const band = bandOf(b.session);
    const active = b.lead || (b.follower.state === "active" && b.session.state !== "offline");
    wire.agent({session: b.session, follower: b.follower, x: b.o.x, y: 0, z: b.o.z,
      appear: b.lead ? b.o.appear : b.o.appear * lead.appear, highlight, pulse,
      extra: [lead.x, lead.z, band[0], band[1], band[2], active ? 1 : 0]});
  }

  // One trail along each place's live sessions, in order.
  function scentTrails(wire, model) {
    for (const list of model.liveByPlace().values()) {
      for (let i = 0; i + 1 < list.length; i++) {
        const a = list[i], b = list[i + 1];
        wire.link({kind: LINK.scent, from: a.id, to: b.id, x0: a.x, z0: a.z, x1: b.x, z1: b.z,
          seed: (a.data.seed || 0) * 3.1, alpha: Math.min(a.appear, b.appear)});
      }
    }
  }

  // highlightOf(id) -> 0 none, 1 selected, 2 hovered; pulseOf(id) -> 0..1.
  function write(wire, model, doc, highlightOf, pulseOf) {
    wire.begin({doc, scene: [model.combRadius, model.meadow[0], model.meadow[1]]});
    for (const o of model.sessions.values()) patch(wire, model, o);
    for (const b of model.bees(true)) bee(wire, model, b, highlightOf(b.id), pulseOf(b.id));
    scentTrails(wire, model);
    return wire;
  }

  window.HiveFrame = {write, ZONE, LINK};
})();
