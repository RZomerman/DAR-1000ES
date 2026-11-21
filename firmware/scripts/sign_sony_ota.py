"""Package a split ESP app image for POST /api/ota/update."""

import argparse
import hashlib
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("role", choices=("controller", "audio"))
    parser.add_argument("image", type=Path)
    parser.add_argument("key", type=Path, help="Local P-256 private key PEM")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    image = args.image.read_bytes()
    role = f"DAR-1000ES OTA v1:{args.role}".encode("ascii") + b"\0"
    if not image.startswith(b"\xe9") or len(image) > 0x300000 or role not in image:
        parser.error("Expected an OTA-enabled app image for the selected role.")
    digest = hashlib.sha256(image).digest()
    signature = subprocess.run(
        ["openssl", "dgst", "-sha256", "-sign", str(args.key)],
        input=role + digest, stdout=subprocess.PIPE, check=True,
    ).stdout
    if not 1 <= len(signature) <= 72:
        parser.error("Expected a P-256 ECDSA signature.")
    with args.output.open("xb") as output:
        output.write(b"SOTA" + digest + bytes([len(signature)]) + signature + image)


if __name__ == "__main__":
    main()
