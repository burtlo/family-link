import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from device_ui_catalog import DEFAULT_CATALOG, DEFAULT_OUT, load_catalog, pack, validate_catalog


class DeviceUiCatalogTests(unittest.TestCase):
    def test_repo_catalog_validates(self):
        packed = load_catalog(DEFAULT_CATALOG)
        validate_catalog(packed)
        ids = [scene["id"] for scene in packed["scenes"]]
        self.assertEqual(packed["document"], "family-link.device-ui-catalog/v1")
        self.assertGreaterEqual(len(ids), 37)
        self.assertIn("CAT-21", ids)
        self.assertIn("CAT-37", ids)
        volume = next(scene for scene in packed["scenes"] if scene["id"] == "CAT-37")
        self.assertEqual(volume["slots"]["lower"]["panel"], "PANEL-BODY-VOLUME-SLIDER")
        sketch = next(scene for scene in packed["scenes"] if scene["id"] == "CAT-22")
        self.assertTrue(sketch["slots"]["lower"]["props"]["messages"][1].get("sketch"))
        self.assertIn("CAT-01", ids)
        self.assertIn("CAT-14", ids)
        self.assertIn("CAT-15", ids)
        self.assertIn("CAT-17", ids)
        self.assertIn("CAT-20", ids)
        bust = next(scene for scene in packed["scenes"] if scene["id"] == "CAT-20")
        self.assertEqual(bust["slots"]["lower"]["props"]["card_style"], "bust")
        manifest_path = DEFAULT_CATALOG.parent.parent / "avatars" / "avatars.json"
        if manifest_path.is_file():
            import json

            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            users = bust["slots"]["lower"]["props"]["users"]
            self.assertEqual(len(users), len(manifest.get("portraits", {})))
            for user in users:
                self.assertIn(user.get("portrait"), manifest["portraits"])
        sketch = next(scene for scene in packed["scenes"] if scene["id"] == "CAT-17")
        self.assertEqual(sketch["layout"], "LAYOUT-SPLIT-PANE-FOCUSED-SIDE")
        self.assertEqual(sketch["slots"]["focused"]["panel"], "PANEL-FOCUSED-DRAWING")
        self.assertEqual(sketch["slots"]["side"]["panel"], "PANEL-SIDE-COLOR-PALETTE")
        self.assertEqual(packed["tokens"]["focused_side"]["split_x"], 240)
        self.assertEqual(packed["tokens"]["theme"]["fl-color-sketch-canvas"], "#000000")
        self.assertEqual(packed["tokens"]["split_pane"]["split_y"], 80)
        self.assertEqual(packed["tokens"]["theme"]["fl-color-surface"], "#101418")
        self.assertEqual(packed["tokens"]["theme"]["fl-color-surface-upper"], "#101418")
        self.assertEqual(packed["tokens"]["theme"]["fl-color-surface-lower"], "#242C34")
        cooldown = next(scene for scene in packed["scenes"] if scene["id"] == "CAT-14")
        self.assertEqual(cooldown["layout"], "LAYOUT-FULL-CANVAS")
        roster = next(scene for scene in packed["scenes"] if scene["id"] == "CAT-01")
        self.assertEqual(roster["slots"]["upper"]["panel"], "PANEL-HEADER-TITLE-LEFT")
        self.assertEqual(roster["slots"]["lower"]["panel"], "PANEL-BODY-ROSTER-CAROUSEL")

    def test_pack_writes_json(self):
        packed = load_catalog(DEFAULT_CATALOG)
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / "catalog.json"
            written = pack(DEFAULT_CATALOG, out)
            data = json.loads(out.read_text(encoding="utf-8"))
            self.assertEqual(written["scenes"][0]["id"], data["scenes"][0]["id"])
            self.assertEqual(data["theme"], "THEME-DARK")

    def test_committed_pack_matches_yaml(self):
        packed = load_catalog(DEFAULT_CATALOG)
        self.assertTrue(DEFAULT_OUT.is_file(), "run: python scripts/device_ui_catalog.py pack")
        committed = json.loads(DEFAULT_OUT.read_text(encoding="utf-8"))
        self.assertEqual([scene["id"] for scene in packed["scenes"]],
                         [scene["id"] for scene in committed["scenes"]])
        self.assertEqual(packed["tokens"]["split_pane"]["split_y"],
                         committed["tokens"]["split_pane"]["split_y"])


if __name__ == "__main__":
    unittest.main()
