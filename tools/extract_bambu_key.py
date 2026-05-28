#!/usr/bin/env python3
"""
extract_bambu_key.py — Extract the RSA signing key from Bambu Connect.

This tool extracts the private key and X.509 certificate that Bambu Connect
uses to sign MQTT print commands.  Once extracted, place the files in your
slicer's plugin config directory to enable printing without Developer Mode:

  <config_dir>/bambu_connect_private.pem   ← private key (PKCS8 PEM)
  <config_dir>/bambu_connect_cert.pem      ← X.509 certificate (PEM)

Usage
-----
  # 1. Install Node.js and @electron/asar
  npm install -g @electron/asar

  # 2. Run this script pointing at your Bambu Connect installation
  python3 extract_bambu_key.py --app /path/to/BambuConnect/resources/app.asar

  # Or point at an already-extracted directory
  python3 extract_bambu_key.py --src /path/to/extracted/src

Platform paths
--------------
  Windows : C:\\Program Files\\Bambu Lab\\Bambu Connect\\resources\\app.asar
  macOS   : /Applications/BambuConnect.app/Contents/Resources/app.asar
  Linux   : /opt/bambu-connect/resources/app.asar   (or flatpak equivalent)

Legal note
----------
This script extracts material from your own legally-obtained copy of Bambu
Connect for the purpose of interoperability with your own printer.
The extracted key material is NOT redistributed by this project.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


# ── RC4 decryption (used in older obfuscated builds) ─────────────────────────

def rc4_decrypt(data: bytes, key: bytes) -> bytes:
    S = list(range(256))
    j = 0
    for i in range(256):
        j = (j + S[i] + key[i % len(key)]) % 256
        S[i], S[j] = S[j], S[i]
    i = j = 0
    out = bytearray()
    for byte in data:
        i = (i + 1) % 256
        j = (j + S[i]) % 256
        S[i], S[j] = S[j], S[i]
        out.append(byte ^ S[(S[i] + S[j]) % 256])
    return bytes(out)


# ── PEM extraction helpers ────────────────────────────────────────────────────

PEM_PRIVATE_RE = re.compile(
    r'(-----BEGIN (?:RSA |EC )?PRIVATE KEY-----.*?-----END (?:RSA |EC )?PRIVATE KEY-----)',
    re.DOTALL
)
PEM_CERT_RE = re.compile(
    r'(-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----)',
    re.DOTALL
)


def find_pems_in_text(text: str):
    """Return (private_key_pem, cert_pem) or (None, None) if not found."""
    keys  = PEM_PRIVATE_RE.findall(text)
    certs = PEM_CERT_RE.findall(text)
    return (keys[0] if keys else None, certs[0] if certs else None)


def extract_from_js(js_text: str):
    """
    Try several strategies to pull the PEM material out of main.js.

    Strategy A — plaintext PEM literals (v1.1.x unobfuscated).
    Strategy B — RC4-encrypted strings decoded inline.
    Strategy C — JSON-embedded escaped PEM.
    """
    # Strategy A: plain PEM blocks in the source
    key, cert = find_pems_in_text(js_text)
    if key and cert:
        print("[+] Strategy A: found plaintext PEM blocks")
        return key, cert

    # Strategy B: look for the ure() RC4 pattern
    # Pattern: ure("HEX_CIPHERTEXT","HEX_KEY")
    ure_re = re.compile(r'ure\(["\']([0-9a-fA-F]+)["\'],\s*["\']([0-9a-fA-F]+)["\']\)')
    for m in ure_re.finditer(js_text):
        try:
            ct  = bytes.fromhex(m.group(1))
            key_bytes = bytes.fromhex(m.group(2))
            plain = rc4_decrypt(ct, key_bytes)
            decoded = plain.decode('utf-8', errors='replace')
            k, c = find_pems_in_text(decoded)
            if k or c:
                print("[+] Strategy B: RC4 decryption succeeded")
                key  = k or key
                cert = c or cert
        except Exception:
            pass
    if key and cert:
        return key, cert

    # Strategy C: escaped JSON strings containing PEM
    json_str_re = re.compile(r'"(-----BEGIN[^"\\]*(?:\\.[^"\\]*)*-----END[^"]+-----)"')
    for m in json_str_re.finditer(js_text):
        raw = m.group(1).replace('\\n', '\n').replace('\\\\', '\\')
        k, c = find_pems_in_text(raw)
        key  = k or key
        cert = c or cert
    if key and cert:
        print("[+] Strategy C: found JSON-escaped PEM")
        return key, cert

    return key, cert


# ── Main ──────────────────────────────────────────────────────────────────────

def extract_asar(asar_path: Path, dest: Path):
    """Use npx @electron/asar to extract app.asar → dest/"""
    if shutil.which('asar'):
        cmd = ['asar', 'extract', str(asar_path), str(dest)]
    else:
        cmd = ['npx', '--yes', '@electron/asar', 'extract', str(asar_path), str(dest)]
    print(f"[*] Extracting {asar_path} …")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"[!] asar extraction failed:\n{result.stderr}")
        sys.exit(1)
    print(f"[+] Extracted to {dest}")


def find_main_js(src_dir: Path):
    """Find main.js (or main.js.enc/bundle) inside extracted src."""
    candidates = [
        src_dir / 'main.js',
        src_dir / 'out' / 'main.js',
        src_dir / 'dist' / 'main.js',
        src_dir / 'app' / 'main.js',
    ]
    for p in candidates:
        if p.exists():
            return p
    # broader search
    for p in src_dir.rglob('main.js'):
        return p
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--app',  type=Path, help='Path to Bambu Connect app.asar')
    group.add_argument('--src',  type=Path, help='Path to already-extracted src directory')
    parser.add_argument('--out', type=Path, default=Path('.'),
                        help='Output directory (default: current dir)')
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    tmpdir = None

    try:
        if args.app:
            tmpdir = tempfile.mkdtemp(prefix='bambu_extract_')
            src_dir = Path(tmpdir)
            extract_asar(args.app, src_dir)
        else:
            src_dir = args.src

        main_js = find_main_js(src_dir)
        if not main_js:
            print("[!] Could not find main.js in extracted sources.")
            print("    Files found:")
            for p in sorted(src_dir.rglob('*.js'))[:20]:
                print(f"      {p}")
            sys.exit(1)

        print(f"[*] Reading {main_js} …")
        js_text = main_js.read_text(encoding='utf-8', errors='replace')

        private_key, cert = extract_from_js(js_text)

        if not private_key:
            print("[!] Private key not found.")
            print("    This version of Bambu Connect may use v8 bytecode obfuscation.")
            print("    Try an older version (v1.1.3 is known-good) or use memory dumping.")
            sys.exit(1)

        if not cert:
            print("[!] Certificate not found alongside the private key.")
            print("    The key was extracted but cert_id will be unknown.")

        out_key  = args.out / 'bambu_connect_private.pem'
        out_cert = args.out / 'bambu_connect_cert.pem'

        out_key.write_text(private_key)
        print(f"[+] Private key  → {out_key}")

        if cert:
            out_cert.write_text(cert)
            print(f"[+] Certificate  → {out_cert}")

        print()
        print("Copy these files to your slicer's plugin config directory:")
        print("  Linux  : ~/.config/BambuStudio/plugins/")
        print("  macOS  : ~/Library/Application Support/BambuStudio/plugins/")
        print("  Windows: %APPDATA%\\BambuStudio\\plugins\\")
        print()
        print("Restart the slicer — non-Developer-Mode printing will be enabled.")

    finally:
        if tmpdir:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == '__main__':
    main()
