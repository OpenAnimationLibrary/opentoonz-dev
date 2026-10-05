"""Opt-in inventory tests; fixtures are not full glTF conformance fixtures."""

import contextlib
import copy
import hashlib
import importlib.util
import io
import json
import struct
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "glb_bridge_audit.py"
SPEC = importlib.util.spec_from_file_location("glb_bridge_audit", SCRIPT)
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)
PROFILE = json.loads(audit.PROFILE.read_text(encoding="utf-8"))


def glb(document, binary=None, raw=None):
    raw = json.dumps(document).encode("utf-8") if raw is None else raw
    raw += b" " * (-len(raw) % 4)
    chunks = struct.pack("<I4s", len(raw), b"JSON") + raw
    if binary is not None:
        binary += b"\0" * (-len(binary) % 4)
        chunks += struct.pack("<I4s", len(binary), b"BIN\0") + binary
    return struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks


def sample():
    return {"asset": {"version": "2.0"}, "scene": 0,
            "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
            "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "material": 0}]}],
            "materials": [{"pbrMetallicRoughness": {"baseColorFactor": [0, 0, 0, 1]}}]}


class BridgeAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "model space \u017d.glb"

    def read(self, contents):
        self.path.write_bytes(contents)
        return audit.read_glb(self.path)

    def report(self, document):
        return audit.inventory(self.read(glb(document)), PROFILE)

    def test_baseline_contract(self):
        self.assertEqual(PROFILE["schemaVersion"], 1)
        self.assertEqual(len(PROFILE["baseline"]["commit"]), 40)
        self.assertEqual(PROFILE["features"]["animation"]["retention"], "count-only")
        self.assertEqual(PROFILE["features"]["skinning"]["rendering"], "not-evaluated")

    def test_material_assignments_not_merged_by_equal_color(self):
        doc = sample()
        doc["materials"].append(copy.deepcopy(doc["materials"][0]))
        doc["meshes"][0]["primitives"].append({"attributes": {"POSITION": 0}, "material": 1})
        result = self.report(doc)
        self.assertEqual(result["primitiveMaterialAssignments"], {"0": 1, "1": 1})
        self.assertIn("base_color_rgb", result["features"])
        self.assertEqual(result["knownLoadBlockers"], [])
        self.assertEqual(result["validation"], "not-performed")

    def test_default_material_and_non_triangle_primitives(self):
        doc = sample()
        del doc["meshes"][0]["primitives"][0]["material"]
        doc["meshes"][0]["primitives"][0]["mode"] = 1
        result = self.report(doc)
        self.assertEqual(result["primitiveMaterialAssignments"], {"default": 1})
        self.assertEqual(result["features"]["points_lines"]["rendering"], "skipped")
        self.assertTrue(any("No triangle" in n for n in result["notices"]))

    def test_animation_and_skin_are_not_claimed_as_playback(self):
        doc = sample()
        doc["skins"] = [{"joints": [0]}]
        doc["animations"] = [{"name": "Run", "samplers": [
            {"input": 1, "output": 2, "interpolation": kind}
            for kind in ["LINEAR", "STEP", "CUBICSPLINE"]],
            "channels": [{"sampler": 0, "target": {"node": 0, "path": "rotation"}}]}]
        result = self.report(doc)
        self.assertEqual(result["features"]["animation"]["retention"], "count-only")
        self.assertEqual(result["animations"][0]["targetPaths"], {"rotation": 1})
        self.assertEqual(len(result["animations"][0]["interpolations"]), 3)
        self.assertEqual(result["binaryAccessors"], "not-decoded")

    def test_appearance_inventory(self):
        doc = sample()
        p = doc["meshes"][0]["primitives"][0]
        p["attributes"].update({"NORMAL": 1, "TANGENT": 2, "COLOR_0": 3})
        p["targets"] = [{"POSITION": 4}]
        doc["materials"][0].update({"alphaMode": "BLEND", "emissiveFactor": [1, 0, 0]})
        doc["textures"] = [{"source": 0}]
        doc["images"] = [{"bufferView": 0, "mimeType": "image/png"}]
        result = self.report(doc)
        for feature in ["normals", "tangents", "vertex_colors", "morph_targets",
                        "material_alpha", "emission", "textures"]:
            self.assertIn(feature, result["features"])
            self.assertEqual(result["features"][feature]["rendering"], "not-evaluated")

    def test_required_extension_vs_optional_fallback(self):
        doc = sample()
        doc["extensionsUsed"] = ["KHR_animation_pointer", "KHR_mesh_quantization", "VENDOR_unknown"]
        doc["extensionsRequired"] = ["KHR_animation_pointer"]
        result = self.report(doc)
        self.assertEqual(result["knownLoadBlockers"], ["Unsupported required extension: KHR_animation_pointer"])
        self.assertTrue(any("VENDOR_unknown" in n for n in result["notices"]))
        doc["extensionsRequired"] = []
        self.assertEqual(self.report(doc)["knownLoadBlockers"], [])

    def test_uris_are_reported_without_following_them(self):
        doc = sample()
        doc["buffers"] = [{"uri": "https://invalid.example/private.bin"}]
        doc["images"] = [{"uri": "data:image/png;base64,AAAA"}]
        result = self.report(doc)
        self.assertEqual(len(result["knownLoadBlockers"]), 2)
        self.assertNotIn("private.bin", json.dumps(result))

    def test_camera_lights_metadata_and_animation_only(self):
        doc = {"asset": {"version": "2.0", "extras": {"amOtBridge": {"assetId": "fixture"}}},
               "cameras": [{"type": "perspective"}],
               "extensions": {"KHR_lights_punctual": {"lights": [{"type": "point"}]}}}
        result = self.report(doc)
        for feature in ["cameras", "lights", "bridge_metadata"]:
            self.assertEqual(result["features"][feature]["retention"], "not-retained")
        self.assertTrue(result["notices"])
        self.assertEqual(result["counts"]["meshes"], 0)

    def test_json_only_and_binary_chunks_are_not_decoded(self):
        doc = sample()
        self.assertEqual(self.read(glb(doc)), doc)
        self.assertEqual(self.read(glb(doc, binary=b"opaque-invalid-accessor-data")), doc)

    def test_malformed_headers_chunks_and_json(self):
        valid = glb(sample())
        invalid = [b"", b"NOT!" + valid[4:], valid[:-1],
                   valid[:16] + b"BIN\0" + valid[20:],
                   glb(None), glb({}, raw=b'{"asset":{"version":"2.0"},"x":NaN}'),
                   glb({}, raw=b'{"asset":{"version":"2.0"},"x":1e999}'),
                   glb({}, raw=b'{"asset":{"version":"2.0","version":"2.0"}}')]
        for value in invalid:
            with self.subTest(value=value[:20]), self.assertRaises(ValueError):
                self.read(value)

    def test_resource_budgets(self):
        raw = b'[' * 150 + b'0' + b']' * 150
        with self.assertRaises(audit.AuditError):
            self.read(glb({}, raw=raw))
        old_limit = audit.MAX_JSON_BYTES
        try:
            audit.MAX_JSON_BYTES = 4
            with self.assertRaises(audit.AuditError):
                self.read(glb(sample()))
        finally:
            audit.MAX_JSON_BYTES = old_limit
        contents = glb(sample())
        contents += struct.pack("<I4s", 0, b"TEST") * audit.MAX_CHUNKS
        contents = contents[:8] + struct.pack("<I", len(contents)) + contents[12:]
        with self.assertRaises(audit.AuditError):
            self.read(contents)

    def test_structural_mistakes_reported(self):
        invalid = []
        for key, value in [("meshes", [7]), ("materials", None), ("animations", "bad"),
                           ("extensionsUsed", [12]), ("extensionsRequired", ["UNDECLARED"])]:
            doc = sample()
            doc[key] = value
            invalid.append(doc)
        for mode in [True, -1, 7, "4"]:
            doc = sample()
            doc["meshes"][0]["primitives"][0]["mode"] = mode
            invalid.append(doc)
        for doc in invalid:
            with self.subTest(doc=doc), self.assertRaises(audit.AuditError):
                self.report(doc)

    def test_source_preservation_determinism_and_exit_codes(self):
        self.path.write_bytes(glb(sample()))
        before = hashlib.sha256(self.path.read_bytes()).digest()
        outputs = []
        for _ in range(2):
            with contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(audit.main([str(self.path)]), 0)
            outputs.append(output.getvalue())
        self.assertEqual(outputs[0], outputs[1])
        self.assertEqual(before, hashlib.sha256(self.path.read_bytes()).digest())
        doc = sample()
        doc["extensionsUsed"] = doc["extensionsRequired"] = ["VENDOR_unknown"]
        self.path.write_bytes(glb(doc))
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(audit.main([str(self.path)]), 3)
            self.assertEqual(audit.main([str(self.path) + "missing"]), 1)


if __name__ == "__main__":
    unittest.main()
