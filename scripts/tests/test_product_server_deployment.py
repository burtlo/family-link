"""Startup loading and restart semantics for the product server deployment."""
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from scripts.tests.test_lab_p1_host import DESK, _Client, _running_server, server


class ProductServerDeploymentTests(unittest.TestCase):
    def setUp(self):
        self.previous = (server.ACCOUNTS, server.ROSTER, server.DEPLOYMENT_PATH)
        self.temp = tempfile.TemporaryDirectory(prefix="product-server-deployment-")

    def tearDown(self):
        server.ACCOUNTS, server.ROSTER, server.DEPLOYMENT_PATH = self.previous
        self.temp.cleanup()

    def _copy_default(self) -> Path:
        path = Path(self.temp.name) / "deployment.yaml"
        path.write_text(
            server.DEFAULT_DEPLOYMENT_PATH.read_text(encoding="utf-8"),
            encoding="utf-8",
        )
        return path

    def test_default_yaml_fixture_exists_and_constructor_loads_it(self):
        self.assertTrue(server.DEFAULT_DEPLOYMENT_PATH.is_file())
        self.assertIn("deployment:", server.DEFAULT_DEPLOYMENT_PATH.read_text(encoding="utf-8"))

        httpd = server.create_server("127.0.0.1", 0, "deployment-default")
        try:
            self.assertEqual(server.DEPLOYMENT_PATH, server.DEFAULT_DEPLOYMENT_PATH.resolve())
        finally:
            httpd.server_close()

        self.assertEqual(len(server.ACCOUNTS["users"]), 4)
        self.assertEqual(len(server.ROSTER["users"]), 4)
        self.assertEqual(server.ACCOUNTS["desk_token"], DESK)
        self.assertEqual(server.ACCOUNTS["endpoint_id"], "endpoint-kitchen")
        self.assertEqual(server.ACCOUNTS["hangout_id"], "hangout-kitchen")
        self.assertEqual(server.ROSTER["hangout_id"], server.ACCOUNTS["hangout_id"])
        self.assertEqual(
            [row["user_id"] for row in server.ROSTER["users"]],
            ["user-maya", "user-jordan", "user-riley", "user-casey"],
        )
        for row in server.ROSTER["users"]:
            self.assertIn("display_name", row)
            self.assertIn("avatar_id", row)
            self.assertEqual(row["card_background_id"], "card-bg-01")

    def test_editing_loaded_yaml_does_not_change_running_server_until_restart(self):
        path = self._copy_default()
        token = DESK

        with _running_server("deployment-static", deployment_path=path) as base:
            client = _Client(base)
            status, before = client.json_request("GET", "/v2/hangout", bearer=token)
            self.assertEqual(status, 200)
            self.assertEqual(before["users"][0]["display_name"], "Maya")

            path.write_text(
                path.read_text(encoding="utf-8").replace("name: Maya", "name: Morgan"),
                encoding="utf-8",
            )
            status, during = client.json_request("GET", "/v2/hangout", bearer=token)
            self.assertEqual(status, 200)
            self.assertEqual(during["users"][0]["display_name"], "Maya")

        with _running_server("deployment-static-restart", deployment_path=path) as base:
            client = _Client(base)
            status, after = client.json_request("GET", "/v2/hangout", bearer=token)
            self.assertEqual(status, 200)
            self.assertEqual(after["users"][0]["display_name"], "Morgan")

    def test_invalid_yaml_and_schema_are_rejected_without_partial_rebind(self):
        path = self._copy_default()
        original_accounts = server.ACCOUNTS
        original_roster = server.ROSTER

        path.write_text("deployment: [", encoding="utf-8")
        with self.assertRaises(server.DeploymentLoadError):
            server.load_server_deployment(path)
        self.assertIs(server.ACCOUNTS, original_accounts)
        self.assertIs(server.ROSTER, original_roster)

        path.write_text(
            self._copy_default().read_text(encoding="utf-8").replace(
                "deployment:\n", "unsupported: true\ndeployment:\n", 1
            ),
            encoding="utf-8",
        )
        with self.assertRaisesRegex(server.DeploymentLoadError, "schema"):
            server.load_server_deployment(path)
        self.assertIs(server.ACCOUNTS, original_accounts)
        self.assertIs(server.ROSTER, original_roster)

    def test_missing_display_name_and_non_pilot_roster_are_rejected(self):
        path = self._copy_default()
        text = path.read_text(encoding="utf-8").replace(
            "    name: Casey\n", "    pin: \"1357\"\n", 1
        )
        path.write_text(text, encoding="utf-8")
        with self.assertRaisesRegex(server.DeploymentLoadError, "display name"):
            server.load_server_deployment(path)

        path.write_text(
            server.DEFAULT_DEPLOYMENT_PATH.read_text(encoding="utf-8").replace(
                "  - id: user-casey\n", "", 1
            ),
            encoding="utf-8",
        )
        with self.assertRaisesRegex(server.DeploymentLoadError, "exactly four"):
            server.load_server_deployment(path)


if __name__ == "__main__":
    unittest.main()
