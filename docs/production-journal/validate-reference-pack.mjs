import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import {fileURLToPath} from "node:url";

const root = path.dirname(fileURLToPath(import.meta.url));
const manifestPath = path.join(root, "reference-manifest.json");
const payloadPath = path.join(root, "tiddlers", "reference-tiddlers.json");
const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
const payloadBytes = fs.readFileSync(payloadPath);
const references = JSON.parse(payloadBytes.toString("utf8"));

const failures = [];
const requiredManifestFields = [
  "schema-version",
  "edition",
  "revision",
  "published",
  "reference-count",
  "payload-path",
  "payload-sha256",
];

for (const field of requiredManifestFields) {
  if (manifest[field] === undefined || manifest[field] === "") {
    failures.push(`Manifest field is missing: ${field}`);
  }
}

if (manifest["schema-version"] !== 1) failures.push("Unsupported schema version");
if (manifest.edition !== "ot-dev-production-journal-reference") {
  failures.push("Unexpected edition identifier");
}
if (!Array.isArray(references) || references.length === 0) {
  failures.push("Reference payload must be a non-empty array");
}
if (manifest["reference-count"] !== references.length) {
  failures.push("Manifest reference count does not match the payload");
}

const checksum = crypto.createHash("sha256").update(payloadBytes).digest("hex");
if (manifest["payload-sha256"] !== checksum) {
  failures.push("Reference payload checksum does not match the manifest");
}

const canonicalIds = new Set();
for (const reference of references) {
  if (!reference.title || reference.title.startsWith("$:/")) {
    failures.push(`Unsafe or missing reference title: ${reference.title || "<empty>"}`);
  }
  if (!reference["canonical-id"]) {
    failures.push(`Missing canonical-id: ${reference.title || "<untitled>"}`);
  } else if (canonicalIds.has(reference["canonical-id"])) {
    failures.push(`Duplicate canonical-id: ${reference["canonical-id"]}`);
  } else {
    canonicalIds.add(reference["canonical-id"]);
  }
  if (reference["journal-role"] !== "online-reference") {
    failures.push(`Unexpected journal-role: ${reference.title || "<untitled>"}`);
  }
  if (reference["source-revision"] !== manifest.revision) {
    failures.push(`Revision mismatch: ${reference.title || "<untitled>"}`);
  }
}

if (failures.length) {
  console.error(failures.join("\n"));
  process.exit(1);
}

console.log(
  `Validated ${references.length} references at revision ${manifest.revision} (${checksum.slice(0, 12)}…)`,
);
