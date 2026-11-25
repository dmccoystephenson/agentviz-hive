# agentviz-hive

Beehive view of Claude Code sessions (agentviz family, port 8846).

- Data: ONLY `/api/agents` from `vendor/agentfeed` (schema: docs/FEED.md in
  dmccoystephenson/agentviz). Never edit `vendor/agentfeed/`; re-vendor with
  `/root/agentviz/tools/vendor-feed.sh .`.
- Scene: `scene/main.cpp` (Strata 0.10.0 web kit). Build with `scene/build.sh`;
  the built `web/hive-engine.js|.wasm` and kit scripts are committed.
- Page: `web/index.html` + `web/hive.js` (layout, easing, picking, panel).
- Run: `python3 -m vendor.agentfeed serve --web web --port 8846 [--fixture]`.
- Tests: `python3 -m unittest discover -s tests`; smoke:
  `NODE_PATH=<dir with playwright> node tests/smoke.cjs`.
- Screenshots come from the FIXTURE feed only. Never commit live session data.
