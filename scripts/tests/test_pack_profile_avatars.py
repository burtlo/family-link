import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from pack_profile_avatars import (
    CAT20_SCENE,
    PORTRAIT_CMAKE,
    portrait_c_asset,
    portrait_display_name,
    portrait_id_from_name,
    portrait_lvgl_symbol,
    SCHEMA,
    AVATARS_DOC,
)


class PackProfileAvatarsTests(unittest.TestCase):
    def test_portrait_id_from_name(self):
        self.assertEqual(portrait_id_from_name(Path("arlo.png")), "arlo")
        self.assertEqual(portrait_id_from_name(Path("Mazi-Source.PNG")), "mazi")
        with self.assertRaises(ValueError):
            portrait_id_from_name(Path("!!!.png"))

    def test_portrait_c_symbols(self):
        self.assertEqual(portrait_lvgl_symbol("avatar-an01"), "portrait_avatar_an01_candidate")
        self.assertEqual(portrait_c_asset("avatar-mc12"), "portrait_avatar_mc12.c")

    def test_portrait_display_name(self):
        self.assertEqual(portrait_display_name("avatar-an01"), "AN01")
        self.assertEqual(portrait_display_name("avatar-mc12"), "MC12")

    def test_portrait_cmake_lists_all_packed_sources(self):
        import json

        if not PORTRAIT_CMAKE.is_file():
            self.skipTest("portrait_pack_sources.cmake not generated; run make avatars")
        manifest = json.loads((AVATARS_DOC / "avatars.json").read_text(encoding="utf-8"))
        text = PORTRAIT_CMAKE.read_text(encoding="utf-8")
        for portrait_id in manifest.get("portraits", {}):
            self.assertIn(portrait_c_asset(portrait_id), text)

    def test_pack_lab14_catalog_updates_embedded_json(self):
        import json

        from pack_profile_avatars import LAB14_CATALOG_JSON, pack_lab14_catalog, verify_cat20_catalog

        pack_lab14_catalog()
        manifest = json.loads((AVATARS_DOC / "avatars.json").read_text(encoding="utf-8"))
        verify_cat20_catalog(set(manifest.get("portraits", {})))
        bust = next(
            scene
            for scene in json.loads(LAB14_CATALOG_JSON.read_text(encoding="utf-8"))["scenes"]
            if scene["id"] == "CAT-20"
        )
        self.assertEqual(len(bust["slots"]["lower"]["props"]["users"]), len(manifest["portraits"]))

    def test_cat20_scene_tracks_manifest(self):
        import json

        if not CAT20_SCENE.is_file():
            self.skipTest("CAT-20 scene not generated yet; run make avatars")
        manifest = json.loads((AVATARS_DOC / "avatars.json").read_text(encoding="utf-8"))
        text = CAT20_SCENE.read_text(encoding="utf-8")
        for portrait_id in manifest.get("portraits", {}):
            self.assertIn(f"portrait: {portrait_id}", text)
            self.assertIn(f"id: {portrait_id}", text)

    def test_manifest_schema_file(self):
        path = AVATARS_DOC / "avatars.json"
        self.assertTrue(path.is_file(), "avatars.json should exist")
        import json

        data = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(data["schema"], SCHEMA)
        self.assertIn("palettes", data)
        self.assertIn("portraits", data)


if __name__ == "__main__":
    unittest.main()
