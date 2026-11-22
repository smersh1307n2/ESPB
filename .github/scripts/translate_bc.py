#!/usr/bin/env python3
"""Translate one LLVM bitcode file through the ESPB REST API with quota failover."""
import base64
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "bytecode_groups"))
from translator_keys import collect_translator_keys

if len(sys.argv) != 3:
    raise SystemExit("usage: translate_bc.py input.bc output.espb")

source, destination = sys.argv[1:]
with open(source, "rb") as f:
    payload = {
        "fileName": os.path.basename(source),
        "bcFileContent": base64.b64encode(f.read()).decode("ascii"),
        "hintsFileContent": None,
        "idxFileContent": None,
        "customFileContent": None,
        "options": {
            "enableRegisterPatching": True,
            "enablePeephole": True,
            "enableLlvmOptimizations": True,
            "enableCallResultOptimizations": True,
            "enableAllocaOpt": True,
            "enableCompaction": True
        }
    }

# Combine aggregate and numbered secrets. This ensures newly added numbered
# slots are not hidden when an older aggregate secret is also configured.
keys = collect_translator_keys(os.environ)
if not keys:
    key_names = ("ESPB_API_KEY",) + tuple(
        f"ESPB_API_KEY_{index}" for index in range(1, 10)
    )
    keys = [os.environ.get(name, "").strip() for name in key_names]
    keys = [key for key in keys if key]
if not keys:
    raise SystemExit(
        "No ESPB API keys configured. Set ESPB_TRANSLATOR_KEYS or "
        "ESPB_API_KEY through ESPB_API_KEY_9."
    )

url = os.environ.get("ESPB_API_URL", "http://espb.runasp.net/api/v1/translate")
body = json.dumps(payload).encode("utf-8")
quota_markers = ("quota", "remainingrequests", "daily limit", "request limit", "translation limit", "rate limit")

# Log secret variable names only; never print key material.
for slot, key in enumerate(keys, 1):
    names = ("ESPB_API_KEY",) + tuple("ESPB_API_KEY_" + str(i) for i in range(1, 12))
    sources = [name for name in names if key and os.environ.get(name, "").strip() == key]
    print("Translator attempt slot=%d; configured_from=%s" % (slot, ",".join(sources) or "aggregate/unknown"))
    req = urllib.request.Request(url, data=body, headers={
        "Authorization": "Bearer " + key,
        "Content-Type": "application/json"
    }, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=300) as response:
            raw = response.read().decode("utf-8")
            result = json.loads(raw)
    except urllib.error.HTTPError as exc:
        raw = exc.read().decode("utf-8", errors="replace")
        if exc.code == 429 or any(marker in raw.lower() for marker in quota_markers):
            print(f"Key slot {slot}: HTTP {exc.code}; quota-like response={raw[:500]!r}; trying next key.")
            continue
        raise SystemExit(f"Translation HTTP {exc.code}: {raw[:2000]}")
    except (urllib.error.URLError, TimeoutError) as exc:
        raise SystemExit(f"Translation transport error: {exc}")

    if result.get("status") == "success":
        encoded = result.get("espbFileContent")
        if not encoded:
            raise SystemExit("API reported success without espbFileContent.")
        with open(destination, "wb") as f:
            f.write(base64.b64decode(encoded, validate=True))
        print(f"Translation succeeded using key slot {slot}; output: {destination}")
        break

    error = str(result.get("errorMessage", result))
    if any(marker in json.dumps(result).lower() for marker in quota_markers):
        print(f"Key slot {slot}: quota exhausted; trying next key.")
        continue
    raise SystemExit("Translation failed: " + error[:2000])
else:
    raise SystemExit("All configured API keys are quota-exhausted.")
