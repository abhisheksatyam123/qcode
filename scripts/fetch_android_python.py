#!/usr/bin/env python3
"""Vendor a portable aarch64 Android CPython into apps/android assets.

Downloads selected Termux aarch64 .debs and unpacks bin/lib into
apps/android/app/src/main/assets/python/{bin,lib}.
"""

from __future__ import annotations

import fnmatch
import io
import os
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "apps" / "android" / "app" / "src" / "main" / "assets" / "python"
BASE = "https://packages.termux.dev/apt/termux-main"

# Filenames resolved from Termux Packages index (aarch64).
DEBS = [
    "pool/main/liba/libandroid-support/libandroid-support_29-1_aarch64.deb",
    "pool/main/liba/libandroid-posix-semaphore/libandroid-posix-semaphore_0.1-4_aarch64.deb",
    "pool/main/liba/libandroid-spawn/libandroid-spawn_0.3_aarch64.deb",
    "pool/main/libc/libc++/libc++_29_aarch64.deb",
    "pool/main/libc/libcrypt/libcrypt_0.2-6_aarch64.deb",
    "pool/main/libb/libbz2/libbz2_1.0.8-8_aarch64.deb",
    "pool/main/libe/libexpat/libexpat_2.8.3_aarch64.deb",
    "pool/main/libf/libffi/libffi_3.5.2_aarch64.deb",
    "pool/main/libl/liblzma/liblzma_5.8.3_aarch64.deb",
    "pool/main/libs/libsqlite/libsqlite_3.53.4_aarch64.deb",
    "pool/main/n/ncurses/ncurses_6.6.20260307+really6.5.20250830_aarch64.deb",
    "pool/main/n/ncurses-ui-libs/ncurses-ui-libs_6.6.20260307+really6.5.20250830_aarch64.deb",
    "pool/main/o/openssl/openssl_1:3.6.3_aarch64.deb",
    "pool/main/r/readline/readline_8.3.3_aarch64.deb",
    "pool/main/g/gdbm/gdbm_1.26-1_aarch64.deb",
    "pool/main/z/zlib/zlib_1.3.2_aarch64.deb",
    "pool/main/z/zstd/zstd_1.5.7-1_aarch64.deb",
    "pool/main/p/python/python_3.14.6-1_aarch64.deb",
]


def download(url: str) -> bytes:
    print("GET", url)
    req = urllib.request.Request(url, headers={"User-Agent": "qcode-fetch-android-python"})
    with urllib.request.urlopen(req, timeout=120) as resp:
        return resp.read()


def extract_deb(data: bytes, dest: Path) -> None:
    with tempfile.TemporaryDirectory() as td:
        deb = Path(td) / "pkg.deb"
        deb.write_bytes(data)
        subprocess.check_call(["ar", "x", str(deb)], cwd=td)
        data_tar = None
        for name in Path(td).iterdir():
            if name.name.startswith("data.tar"):
                data_tar = name
                break
        if data_tar is None:
            raise RuntimeError("data.tar* missing in deb")
        with tarfile.open(data_tar) as tf:
            tf.extractall(dest)


def main() -> None:
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)

    with tempfile.TemporaryDirectory() as td:
        stage = Path(td) / "stage"
        stage.mkdir()
        for rel in DEBS:
            url = f"{BASE}/{rel}"
            try:
                blob = download(url)
                extract_deb(blob, stage)
            except Exception as exc:
                print(f"WARN skip {rel}: {exc}")

        usr = None
        for candidate in stage.rglob("usr"):
            if (candidate / "bin").is_dir():
                usr = candidate
                break
        if usr is None:
            raise SystemExit("failed to locate usr/ in extracted packages")

        bin_src = usr / "bin"
        lib_src = usr / "lib"
        (OUT / "bin").mkdir(parents=True)
        (OUT / "lib").mkdir(parents=True)

        # Prefer versioned binary, then aliases.
        copied = False
        for name in sorted(bin_src.glob("python3.*"), reverse=True):
            if name.is_file() and not name.name.endswith(("m", "config")):
                shutil.copy2(name.resolve() if name.is_symlink() else name,
                             OUT / "bin" / "python3")
                copied = True
                break
        if not copied:
            for name in ("python3", "python"):
                src = bin_src / name
                if src.exists():
                    shutil.copy2(src.resolve(), OUT / "bin" / "python3")
                    copied = True
                    break
        if not copied:
            raise SystemExit("python binary not found in packages")

        patterns = [
            "libpython3*.so*",
            "libandroid-support.so*",
            "libandroid-spawn.so*",
            "libandroid-posix-semaphore.so*",
            "libc++_shared.so*",
            "libffi.so*",
            "libncursesw.so*",
            "libreadline.so*",
            "libsqlite3.so*",
            "libssl.so*",
            "libcrypto.so*",
            "libcrypt.so*",
            "liblzma.so*",
            "libbz2.so*",
            "libexpat.so*",
            "libgdbm.so*",
            "libz.so*",
            "libzstd.so*",
            "libtinfo.so*",
            "libpanelw.so*",
            "libmenuw.so*",
            "libformw.so*",
        ]
        for entry in lib_src.iterdir():
            if not any(fnmatch.fnmatch(entry.name, pat) for pat in patterns):
                continue
            target = OUT / "lib" / entry.name
            real = entry.resolve() if entry.is_symlink() else entry
            if real.is_file():
                shutil.copy2(real, target)

        for pyver in sorted(lib_src.glob("python3.*"), reverse=True):
            if pyver.is_dir():
                shutil.copytree(pyver, OUT / "lib" / pyver.name, dirs_exist_ok=True)
                break

    py = OUT / "bin" / "python3"
    py.chmod(0o755)
    alias = OUT / "bin" / "python"
    shutil.copy2(py, alias)
    alias.chmod(0o755)

    # Termux debs hardcode RUNPATH to /data/data/com.termux/... which won't
    # resolve inside the qcode app sandbox. Rewrite to $ORIGIN-relative paths.
    try:
        subprocess.run(["patchelf", "--version"], check=True, capture_output=True)
    except (FileNotFoundError, subprocess.CalledProcessError) as e:
        raise SystemExit(
            "patchelf is required to relocate Termux python RUNPATHs; install it"
        ) from e

    for elf in [OUT / "bin" / "python3", OUT / "bin" / "python"]:
        if elf.is_file():
            subprocess.check_call(
                ["patchelf", "--set-rpath", "$ORIGIN/../lib", str(elf)])
    for so in (OUT / "lib").glob("*.so*"):
        if so.is_file():
            subprocess.check_call(["patchelf", "--set-rpath", "$ORIGIN", str(so)])

    
    # Also stage interpreter + shared libs into jniLibs so Android 10+ can exec
    # them from nativeLibraryDir (app files dir is noexec for targetSdk>=29).
    jni = ROOT / "apps" / "android" / "app" / "src" / "main" / "jniLibs" / "arm64-v8a"
    if jni.exists():
        shutil.rmtree(jni)
    jni.mkdir(parents=True)
    shutil.copy2(OUT / "bin" / "python3", jni / "libqcode_python3.so")
    (jni / "libqcode_python3.so").chmod(0o755)
    subprocess.check_call(["patchelf", "--set-rpath", "$ORIGIN", str(jni / "libqcode_python3.so")])
    # AGP only packages "*.so" (not "*.so.3"); keep unversioned names only.
    for so in (OUT / "lib").glob("*.so"):
        if so.is_file() and so.name.endswith(".so") and ".so." not in so.name:
            shutil.copy2(so, jni / so.name)
    # Rewrite any remaining versioned NEEDED entries to unversioned basenames.
    import re
    def needed(path: Path) -> list[str]:
        out = subprocess.check_output(["readelf", "-d", str(path)], text=True)
        vals = []
        for line in out.splitlines():
            if "NEEDED" in line:
                m = re.search(r"\[([^\]]+)\]", line)
                if m:
                    vals.append(m.group(1))
        return vals
    for elf in jni.glob("*.so"):
        for n in needed(elf):
            if ".so." in n:
                base = n.split(".so.")[0] + ".so"
                if (jni / base).exists():
                    subprocess.check_call(
                        ["patchelf", "--replace-needed", n, base, str(elf)])
        # Align SONAME to filename for linker lookups.
        out = subprocess.check_output(["readelf", "-d", str(elf)], text=True)
        m = re.search(r"SONAME\s+Library soname:\s+\[([^\]]+)\]", out)
        if m and m.group(1) != elf.name:
            subprocess.check_call(["patchelf", "--set-soname", elf.name, str(elf)])
    print("staged native libs into", jni)

print("vendored python into", OUT)
    print("size_mb", round(sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file()) / 1e6, 1))


if __name__ == "__main__":
    main()
