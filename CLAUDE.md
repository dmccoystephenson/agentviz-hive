# CLAUDE.md

agentviz-hive: Claude Code sessions drawn as bees on a honeycomb (Strata web kit).
Part of the agentviz family: https://github.com/dmccoystephenson/agentviz

- Data comes only from `/api/agents` (vendor/agentfeed; schema in the agentviz repo's docs/FEED.md).
- Shared pieces come from the agentviz kit (docs/KIT.md there): `vendor/agentviz-kit/` and the
  `web/agentviz-*` copies. Never edit `vendor/` or `web/agentviz-*`; re-vendor from an agentviz
  checkout with `tools/vendor-kit.sh <this repo>`. `tests/test_serve.py` checks the PINs.
- Page: `web/hive-model.js` lays out patches and cells on the comb and eases them;
  `web/hive-frame.js` packs each frame in the agentviz wire format; `web/page.js` boots the feed
  client, HUD, labels, picking and camera; `web/index.html` holds the theme and the legend.
- Scene: `scene/main.cpp` decodes the wire (`agentviz.h`) and calls `comb.h` (meadow, frame, comb
  mesh, patches, flowers), `bee.h` (poses, body), `cell.h` (cell states, waiting sign, stalled,
  selection) and `props.h` (action props, tool-call pulse, scent trails). Build:
  `EMSDK=/path/to/emsdk vendor/agentviz-kit/scene/build-scene.sh scene/kit.conf`
  (emsdk 6.0.11, Strata 0.10.0). The built `web/hive-engine.*` are committed.
- Run: `python3 -m vendor.agentfeed serve --web web --port 8846 [--fixture]`.
- Tests: `python3 -m unittest discover -s tests` (port 9118, `HIVE_UNIT_PORT`); browser tests after
  `npm ci` and `npx playwright install chromium`: `npm run smoke`, `npm run screenshots` (Pillow
  from requirements-dev.txt for the GIF), port 9116 (`HIVE_TEST_PORT`).
- Screenshots come from the FIXTURE feed only, never live sessions.
