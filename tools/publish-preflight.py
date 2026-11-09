#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "requests>=2.31",
#   "packaging>=23.0",
# ]
# ///

"""Pre-flight checks before publishing to the PlatformIO registry.

Checks:
1. Version has no pre-release suffix (e.g. -dev)
2. Local version is greater than the latest published version
3. PLATFORMIO_AUTH_TOKEN is present (when --check-secrets is passed)
"""

import json
import os
import sys
from pathlib import Path

import requests
from packaging.version import Version


def get_pio_latest(owner: str, name: str) -> str | None:
    """Get latest version from PlatformIO registry."""
    url = f"https://api.registry.platformio.org/v3/packages/{owner}/library/{name}"
    resp = requests.get(url, timeout=10)
    if resp.status_code == 404:
        return None
    resp.raise_for_status()
    return resp.json().get("version", {}).get("name")


def main():
    check_secrets = "--check-secrets" in sys.argv

    repo_root = Path(__file__).resolve().parent.parent
    with open(repo_root / "library.json") as f:
        local_version = json.load(f)["version"]

    errors = []
    print(f"Local version: {local_version}")

    # Check no pre-release suffix
    v = Version(local_version)
    if v.pre is not None or v.dev is not None or v.post is not None:
        errors.append(
            f"Version '{local_version}' has a pre-release/dev suffix — "
            "strip it before publishing"
        )

    # Check against PlatformIO registry
    pio_latest = get_pio_latest("inanimate", "Esp32Lua")
    if pio_latest:
        print(f"PlatformIO registry latest: {pio_latest}")
        if Version(local_version) <= Version(pio_latest):
            errors.append(
                f"Local version {local_version} is not greater than "
                f"PlatformIO registry version {pio_latest}"
            )
    else:
        print("PlatformIO registry: no published version found (first publish)")

    # Check secrets
    if check_secrets and not os.environ.get("PLATFORMIO_AUTH_TOKEN"):
        errors.append("PLATFORMIO_AUTH_TOKEN is not set in GitHub repo settings")

    if errors:
        print()
        for err in errors:
            print(f"ERROR: {err}")
        sys.exit(1)

    print("\nAll pre-flight checks passed.")


if __name__ == "__main__":
    main()
