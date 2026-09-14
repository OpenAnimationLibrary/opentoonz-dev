#!/usr/bin/env python3
"""Read-only GLB JSON inventory against a pinned OT capability baseline.

Python 3.9+, standard library only. This is documentation/development tooling,
not the production loader or a glTF validator. Binary accessors are NOT decoded;
validity, active-scene visibility and appearance fidelity are NOT established.
"""

import argparse
import json
import math
import struct
import sys
from collections import Counter
from pathlib import Path

PROFILE = Path(__file__).resolve().parents[1] / "glb_bridge_capabilities.json"
MAX_FILE_BYTES = 1024 * 1024 * 1024
MAX_JSON_BYTES = 16 * 1024 * 1024
MAX_JSON_DEPTH = 128
MAX_CHUNKS = 64


class AuditError(ValueError):
    """The bounded inventory could not be read safely."""


def check_depth(raw):
    depth, quoted, escaped = 0, False, False
    for byte in raw:
        if quoted:
            if escaped:
                escaped = False
            elif byte == 92:
                escaped = True
            elif byte == 34:
                quoted = False
        elif byte == 34:
            quoted = True
        elif byte in (91, 123):
            depth += 1
            if depth > MAX_JSON_DEPTH:
                raise AuditError("JSON nesting exceeds the audit limit.")
        elif byte in (93, 125):
            depth -= 1


def no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise AuditError("Duplicate JSON property: " + key)
        result[key] = value
    return result


def finite_float(value):
    number = float(value)
    if not math.isfinite(number):
        raise AuditError("Non-finite JSON number.")
    return number


def invalid_constant(value):
    raise AuditError("Invalid JSON number: " + value)


def read_glb(path):
    """Read the JSON chunk only; skip BIN/unknown chunks without allocating them."""
    with Path(path).open("rb") as stream:
        stream.seek(0, 2)
        size = stream.tell()
        if size < 20 or size > MAX_FILE_BYTES:
            raise AuditError("File size is outside the audit limit.")
        stream.seek(0)
        magic, version, length = struct.unpack("<4sII", stream.read(12))
        if magic != b"glTF" or version != 2 or length != size:
            raise AuditError("Invalid GLB 2 header or declared length.")
        document, kinds, offset = None, [], 12
        while offset < size:
            if len(kinds) >= MAX_CHUNKS:
                raise AuditError("GLB chunk count exceeds the audit limit.")
            if size - offset < 8:
                raise AuditError("Truncated GLB chunk header.")
            stream.seek(offset)
            chunk_size, kind = struct.unpack("<I4s", stream.read(8))
            offset += 8
            if chunk_size % 4 or offset + chunk_size > size:
                raise AuditError("Unaligned or truncated GLB chunk.")
            if not kinds and kind != b"JSON":
                raise AuditError("The first GLB chunk must be JSON.")
            if kind == b"JSON":
                if kinds or chunk_size > MAX_JSON_BYTES:
                    raise AuditError("Repeated or oversized JSON chunk.")
                raw = stream.read(chunk_size)
                check_depth(raw)
                document = json.loads(
                    raw.decode("utf-8"), object_pairs_hook=no_duplicates,
                    parse_float=finite_float, parse_constant=invalid_constant)
            if kind == b"BIN\0" and (kind in kinds or len(kinds) != 1):
                raise AuditError("BIN must be the unique second GLB chunk.")
            kinds.append(kind)
            offset += chunk_size
    if not isinstance(document, dict):
        raise AuditError("GLB JSON must be an object.")
    if obj(document.get("asset"), "asset").get("version") != "2.0":
        raise AuditError("asset.version must be 2.0.")
    return document


def obj(value, label):
    if not isinstance(value, dict):
        raise AuditError(label + " must be an object.")
    return value


def items(container, key):
    value = container.get(key, [])
    if not isinstance(value, list) or any(not isinstance(v, dict) for v in value):
        raise AuditError(key + " must be an array of objects.")
    return value


def strings(container, key):
    value = container.get(key, [])
    if not isinstance(value, list) or any(not isinstance(v, str) for v in value):
        raise AuditError(key + " must be an array of strings.")
    return value


def inventory(document, profile):
    """Inventory all stored objects, including unused objects and other scenes."""
    arrays = {key: items(document, key) for key in (
        "nodes", "meshes", "materials", "textures", "images", "skins",
        "animations", "cameras", "scenes", "buffers")}
    primitives = [p for mesh in arrays["meshes"] for p in items(mesh, "primitives")]
    attributes, modes, assignments = set(), Counter(), Counter()
    features, blockers, notices = set(), [], []
    for primitive in primitives:
        mode = primitive.get("mode", 4)
        if type(mode) is not int or not 0 <= mode <= 6:
            raise AuditError("Primitive mode must be an integer from 0 to 6.")
        modes[str(mode)] += 1
        features.add("triangle_surfaces" if mode >= 4 else "points_lines")
        attributes.update(obj(primitive.get("attributes", {}), "attributes"))
        material = primitive.get("material")
        if "material" in primitive:
            if type(material) is not int or not 0 <= material < len(arrays["materials"]):
                raise AuditError("Primitive material index is out of range.")
        assignments[str(material) if material is not None else "default"] += 1
        if items(primitive, "targets"):
            features.add("morph_targets")
    for semantic, feature in (("NORMAL", "normals"), ("TANGENT", "tangents")):
        if semantic in attributes:
            features.add(feature)
    if any(a.startswith("COLOR_") for a in attributes):
        features.add("vertex_colors")
    if arrays["nodes"]:
        features.add("node_transforms")
    for key, feature in (("skins", "skinning"), ("animations", "animation"),
                         ("cameras", "cameras"), ("textures", "textures")):
        if arrays[key]:
            features.add(feature)
    for material in arrays["materials"]:
        features.update(("base_color_rgb", "pbr_factors", "material_sidedness"))
        pbr = obj(material.get("pbrMetallicRoughness", {}), "pbrMetallicRoughness")
        rgba = pbr.get("baseColorFactor", [1, 1, 1, 1])
        if not isinstance(rgba, list) or len(rgba) != 4 or any(
                type(v) not in (int, float) or not math.isfinite(v) for v in rgba):
            raise AuditError("baseColorFactor must have four finite numbers.")
        if rgba[3] != 1 or material.get("alphaMode", "OPAQUE") != "OPAQUE":
            features.add("material_alpha")
        if material.get("emissiveFactor", [0, 0, 0]) != [0, 0, 0] or "emissiveTexture" in material:
            features.add("emission")
    extensions = obj(document.get("extensions", {}), "extensions")
    lights = obj(extensions.get("KHR_lights_punctual", {}), "KHR_lights_punctual")
    if items(lights, "lights"):
        features.add("lights")
    metadata_objects = [document["asset"]] + arrays["nodes"] + arrays["materials"] + arrays["animations"]
    if any(isinstance(o.get("extras"), dict) and "amOtBridge" in o["extras"] for o in metadata_objects):
        features.add("bridge_metadata")
    known = set(profile["loaderExtensions"])
    used, required = strings(document, "extensionsUsed"), strings(document, "extensionsRequired")
    for extension in sorted(set(required) - known):
        blockers.append("Unsupported required extension: " + extension)
    for extension in sorted(set(used) - known - set(required)):
        notices.append("Optional extension ignored by the baseline; core fallback must be independently verified: " + extension)
    if not set(required).issubset(used):
        raise AuditError("extensionsRequired must be a subset of extensionsUsed.")
    for key in ("buffers", "images"):
        for index, entry in enumerate(arrays[key]):
            if "uri" in entry:
                blockers.append("{}[{}] uses a URI; the baseline accepts embedded GLB resources only.".format(key, index))
    if document["asset"].get("minVersion", "2.0") != "2.0":
        blockers.append("asset.minVersion is not supported by the pinned loader.")
    if not any(int(mode) >= 4 for mode in modes):
        notices.append("No triangle primitives declared; the current FX has no surface to render.")
    if "scene" not in document:
        notices.append("No default scene is declared; export an explicit default for predictable selection.")
    clips = []
    for index, animation in enumerate(arrays["animations"]):
        channels, samplers = items(animation, "channels"), items(animation, "samplers")
        paths = [obj(c.get("target"), "animation target").get("path", "unspecified") for c in channels]
        interpolation = [s.get("interpolation", "LINEAR") for s in samplers]
        if any(not isinstance(v, str) for v in paths + interpolation):
            raise AuditError("Animation target paths and interpolation must be strings.")
        clips.append({"index": index, "name": animation.get("name", ""),
                      "channels": len(channels), "samplers": len(samplers),
                      "targetPaths": dict(sorted(Counter(paths).items())),
                      "interpolations": dict(sorted(Counter(interpolation).items()))})
    return {"reportVersion": 1, "audit": "json-inventory-only",
            "validation": "not-performed", "runtimeLoad": "not-attempted",
            "binaryAccessors": "not-decoded", "scope": "all-stored-objects-not-active-scene",
            "profile": profile["profile"], "profileVersion": profile["profileVersion"],
            "baseline": profile["baseline"],
            "counts": {**{k: len(v) for k, v in arrays.items()}, "primitives": len(primitives)},
            "primitiveModes": dict(sorted(modes.items())), "attributes": sorted(attributes),
            "primitiveMaterialAssignments": dict(sorted(assignments.items())),
            "features": {k: profile["features"][k] for k in sorted(features)},
            "animations": clips, "extensionsUsed": used, "extensionsRequired": required,
            "knownLoadBlockers": blockers, "notices": notices,
            "qualification": "No detected blocker does not prove loadability, fidelity, or conformance. Compare colors against the A:M source and run glb_inspect plus the Khronos validator."}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("glb", type=Path)
    args = parser.parse_args(argv)
    try:
        profile = json.loads(PROFILE.read_text(encoding="utf-8"))
        report = inventory(read_glb(args.glb), profile)
    except (OSError, ValueError, TypeError, KeyError, RecursionError, OverflowError, struct.error) as error:
        print(json.dumps({"audit": "failed", "error": str(error)}, ensure_ascii=True))
        return 1
    print(json.dumps(report, indent=2, ensure_ascii=True, allow_nan=False))
    return 3 if report["knownLoadBlockers"] else 0


if __name__ == "__main__":
    sys.exit(main())
