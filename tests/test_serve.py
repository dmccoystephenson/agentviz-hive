"""Serve the fixture and check the page, its assets and the feed."""
import json
import os
import sys
import threading
import unittest
import urllib.request
from http.server import ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

from vendor.agentfeed.serve import make_handler  # noqa: E402

WEB = os.path.join(ROOT, "web")


class ServeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.httpd = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(WEB, True))
        cls.base = "http://127.0.0.1:%d" % cls.httpd.server_address[1]
        threading.Thread(target=cls.httpd.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()

    def get(self, path):
        with urllib.request.urlopen(self.base + path, timeout=10) as r:
            return r.status, r.headers.get("Content-Type", ""), r.read()

    def test_page_and_assets(self):
        status, ctype, body = self.get("/")
        self.assertEqual(status, 200)
        self.assertIn("text/html", ctype)
        page = body.decode()
        for asset in ("strata-loader.js", "strata-kit.js", "hive.js"):
            self.assertIn(asset, page)
        self.assertIn("hive-engine.wasm", page)
        for asset in ("strata-loader.js", "strata-kit.js", "hive.js", "hive-engine.js", "hive-engine.wasm"):
            status, ctype, body = self.get("/" + asset)
            self.assertEqual(status, 200, asset)
            self.assertGreater(len(body), 100, asset)
        self.assertEqual(self.get("/hive-engine.wasm")[1], "application/wasm")

    def test_feed_parses(self):
        for path in ("/api/agents", "/api/agents?fixture=1"):
            status, ctype, body = self.get(path)
            self.assertEqual(status, 200)
            doc = json.loads(body)
            self.assertEqual(doc["version"], 1)
            self.assertTrue(doc["fixture"])
            states = {s["state"] for s in doc["sessions"]}
            self.assertTrue({"busy", "idle", "waiting", "offline"} <= states)
            places = {p["id"] for p in doc["places"]}
            for s in doc["sessions"]:
                self.assertIn(s["place"], places)
                for f in s["followers"]:
                    self.assertIn(f["state"], ("active", "resting"))

    def test_scene_exports_match_page(self):
        with open(os.path.join(WEB, "hive.js")) as f:
            js = f.read()
        with open(os.path.join(ROOT, "scene", "kit.conf")) as f:
            conf = f.read()
        for name in ("_hv_set_comb", "_hv_set_cells", "_hv_set_bees", "_hv_set_trails"):
            self.assertIn(name, js)
            self.assertIn(name, conf)


if __name__ == "__main__":
    unittest.main()
