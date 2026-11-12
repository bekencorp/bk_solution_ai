"""Host checks for the minimum Muse integration's frozen contract."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MuseContractTest(unittest.TestCase):
    def test_upstream_is_pinned(self):
        contract = (ROOT / "docs/muse_protocol_contract.md").read_text()
        self.assertIn("812c46f9fe42b69d916613a9dc702cbac5ff7b54", contract)
        self.assertIn("Noise_XX_25519_AESGCM_SHA256", contract)

    def test_ble_uuids_match_pinned_contract(self):
        header = (ROOT / "ap/src/muse/muse_pairing.h").read_text()
        expected = {
            "7fdd3d1c-38ea-46cf-8b46-314ecf5f240c",
            "4d593029-28a2-4a6e-a1f0-3c2d5e8f9b01",
            "d75dc4ca-7b2b-4e9c-8f0a-1d2e3f4a5b6c",
        }
        self.assertEqual(expected, set(re.findall(r'"([0-9a-f-]{36})"', header)))

    def test_register_advertises_only_health(self):
        source = (ROOT / "ap/src/muse/muse_commands.c").read_text()
        advertised = re.findall(
            r'\\"name\\":\\"([a-z0-9_.]+)\\"', source
        )
        self.assertEqual(["device.health"], advertised)
        self.assertIn(r'\"link.register\"', source)
        self.assertIn('"link.invoke"', source)

    def test_real_sdk_token_not_committed(self):
        for path in [
            ROOT / "ap/Kconfig.projbuild",
            ROOT / "ap/config/bk7259_ap/defconfig",
        ]:
            self.assertNotRegex(path.read_text(), r"mgst_[A-Za-z0-9_-]{8,}")

    def test_feature_stays_default_off(self):
        kconfig = (ROOT / "ap/Kconfig.projbuild").read_text()
        block = re.search(
            r"config MUSE_GADGET\n(?P<body>.*?)(?=\n\s*config |\n\s*endmenu)",
            kconfig,
            re.DOTALL,
        )
        self.assertIsNotNone(block)
        self.assertRegex(block.group("body"), r"\bdefault n\b")
        self.assertNotIn(
            "CONFIG_MUSE_GADGET=y",
            (ROOT / "ap/config/bk7259_ap/defconfig").read_text(),
        )

    def test_runtime_sources_are_wired(self):
        cmake = (ROOT / "ap/ap_sources.cmake").read_text()
        for source in [
            "muse_ble_pairing.c",
            "muse_vm_api.c",
            "muse_session.cpp",
            "noise_core/src/ClientSession.cpp",
        ]:
            self.assertIn(source, cmake)

    def test_phase_one_lifecycle_is_wired(self):
        service = (ROOT / "ap/src/muse/muse_service.c").read_text()
        pairing = (ROOT / "ap/src/muse/muse_ble_pairing.c").read_text()
        vm_api = (ROOT / "ap/src/muse/muse_vm_api.c").read_text()
        session = (ROOT / "ap/src/muse/muse_session.cpp").read_text()
        demo = (ROOT / "ap/src/demo/muse_gadget.c").read_text()
        kconfig = (ROOT / "ap/Kconfig.projbuild").read_text()

        self.assertIn("muse_vm_api_refresh_device_token(", service)
        self.assertIn("muse_storage_update_tokens(", service)
        self.assertIn('json_string(root, "ssid")', pairing)
        self.assertIn("restore_wifi_from_storage(", service)
        self.assertIn("muse_service_apply_wifi(", service)
        self.assertIn("config MUSE_GADGET_CA_PATH", kconfig)
        self.assertIn("muse_service_set_ca_cert(", demo)
        self.assertIn("mbedtls_ssl_get_verify_result(", vm_api)
        self.assertIn("mbedtls_ssl_get_verify_result(", session)


if __name__ == "__main__":
    unittest.main()
