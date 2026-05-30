#!/usr/bin/env python3
"""
extract_bambu_key.py — Extract the RSA signing key from Bambu Connect.

This tool extracts the private key and X.509 certificate that Bambu Connect
uses to sign MQTT print commands.  Once extracted, place the files in your
slicer's plugin config directory to enable printing without Developer Mode:

  <config_dir>/bambu_connect_private.pem   ← private key (PKCS8 PEM)
  <config_dir>/bambu_connect_cert.pem      ← X.509 certificate (PEM) [global fallback]
  <config_dir>/certs/<serial>.pem          ← device-specific certificate (preferred)

Usage
-----
  # 1. Install Node.js and @electron/asar
  npm install -g @electron/asar

  # 2. Run this script pointing at your Bambu Connect installation
  python3 extract_bambu_key.py --app /path/to/BambuConnect/resources/app.asar

  # Or point at an already-extracted directory
  python3 extract_bambu_key.py --src /path/to/extracted/src

  # Also search for device-specific certs in your slicer config:
  python3 extract_bambu_key.py --app /path/to/app.asar --find-device-certs

Platform paths
--------------
  Windows : C:\\Program Files\\Bambu Lab\\Bambu Connect\\resources\\app.asar
  macOS   : /Applications/BambuConnect.app/Contents/Resources/app.asar
  Linux   : /opt/bambu-connect/resources/app.asar   (or flatpak equivalent)

Device-specific certificates
-----------------------------
Non-Developer-Mode firmware requires a cert whose CN matches the printer serial.
The plugin looks for:
  1. <config_dir>/certs/<serial>.pem   (device-specific — preferred)
  2. <config_dir>/bambu_connect_cert.pem (global Bambu Connect cert — fallback)

Use --find-device-certs to scan BambuStudio/OrcaSlicer config dirs and copy any
found device certs into the correct location automatically.

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


# ── Device cert search ────────────────────────────────────────────────────────

def get_cert_common_name(cert_path: Path) -> str | None:
    """Extract the CN from a PEM certificate using openssl CLI."""
    try:
        result = subprocess.run(
            ['openssl', 'x509', '-noout', '-subject', '-in', str(cert_path)],
            capture_output=True, text=True, timeout=5
        )
        if result.returncode == 0:
            # "subject=CN = GLOF3813734089.bambulab.com"  or  "subject= /CN=GLOF38..."
            m = re.search(r'CN\s*=\s*([^\s,/]+)', result.stdout)
            if m:
                return m.group(1).strip()
    except Exception:
        pass
    return None


def slicer_config_dirs() -> list[Path]:
    """Return candidate slicer config directories where certs might live."""
    home = Path.home()
    candidates = [
        # OrcaSlicer
        home / '.config' / 'OrcaSlicer',
        home / '.var' / 'app' / 'io.github.softfever.OrcaSlicer' / 'config' / 'OrcaSlicer',
        home / '.var' / 'app' / 'com.bambulab.BambuStudio' / 'config' / 'OrcaSlicer',
        # BambuStudio
        home / '.config' / 'BambuStudio',
        home / '.var' / 'app' / 'com.bambulab.BambuStudio' / 'config' / 'BambuStudio',
        # macOS
        home / 'Library' / 'Application Support' / 'OrcaSlicer',
        home / 'Library' / 'Application Support' / 'BambuStudio',
        # Windows (via WSL path)
        Path('/mnt/c/Users') / os.environ.get('USER', '') / 'AppData' / 'Roaming' / 'OrcaSlicer',
        Path('/mnt/c/Users') / os.environ.get('USER', '') / 'AppData' / 'Roaming' / 'BambuStudio',
        # BambuConnect own data directory (may contain per-device certs)
        home / '.local' / 'share' / 'BambuConnect',
        home / '.var' / 'app' / 'com.bambulab.BambuConnect' / 'data' / 'BambuConnect',
    ]
    return [d for d in candidates if d.exists()]


def find_device_certs(search_dirs: list[Path]) -> dict[str, Path]:
    """
    Scan slicer config dirs for PEM files that look like Bambu device certs
    (CN=<serial>.bambulab.com).

    Returns dict of {serial: cert_path}.
    """
    found: dict[str, Path] = {}
    pem_globs = ['**/*.pem', '**/*.crt', '**/*.cer']

    for base in search_dirs:
        for pattern in pem_globs:
            for pem_file in base.glob(pattern):
                try:
                    cn = get_cert_common_name(pem_file)
                    if cn and cn.endswith('.bambulab.com'):
                        serial = cn.replace('.bambulab.com', '')
                        if serial not in found:
                            print(f"[+] Found device cert: {pem_file}  (CN={cn})")
                            found[serial] = pem_file
                except Exception:
                    pass
    return found


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
    parser.add_argument('--find-device-certs', action='store_true',
                        help='Also scan slicer config dirs for device-specific certs and '
                             'copy them to <out>/certs/<serial>.pem')
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

        # ── Device-specific certificate search ───────────────────────────────
        if args.find_device_certs:
            print()
            print("[*] Scanning for device-specific certificates …")
            search_dirs = slicer_config_dirs()
            if not search_dirs:
                print("[!] No slicer config directories found.")
            else:
                print(f"[*] Searching in: {', '.join(str(d) for d in search_dirs)}")
                device_certs = find_device_certs(search_dirs)
                if device_certs:
                    certs_dir = args.out / 'certs'
                    certs_dir.mkdir(exist_ok=True)
                    for serial, src_cert in device_certs.items():
                        dst = certs_dir / f"{serial}.pem"
                        shutil.copy2(src_cert, dst)
                        print(f"[+] Device cert  → {dst}  (serial={serial})")
                    print(f"[+] {len(device_certs)} device cert(s) copied to {certs_dir}/")
                else:
                    print("[!] No device-specific certs found.")
                    print("    Make sure you have paired your printer with BambuStudio or OrcaSlicer.")
                    print("    The cert CN must end with .bambulab.com")

        print()
        print("Copy these files to your slicer's plugin config directory:")
        print("  Linux  : ~/.config/BambuStudio/plugins/   (or OrcaSlicer)")
        print("  macOS  : ~/Library/Application Support/BambuStudio/plugins/")
        print("  Windows: %APPDATA%\\BambuStudio\\plugins\\")
        print()
        print("For non-Developer-Mode printing, also copy certs/<serial>.pem to")
        print("<plugin_config_dir>/certs/<serial>.pem")
        print()
        print("Restart the slicer — non-Developer-Mode printing will be enabled.")

    finally:
        if tmpdir:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == '__main__':
    main()
