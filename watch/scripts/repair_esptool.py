#!/usr/bin/env python3
# Reinstall pioarduino esptool into PlatformIO's penv.
# 5.4.0 crashes in write_flash; an editable 5.4.0 install can also point at a
# deleted packages/tool-esptoolpy tree after a version pin.
import os
import shutil
import subprocess
import sys

# ================================================================================

def core_dir():
    env_dir = os.environ.get("PLATFORMIO_CORE_DIR", "").strip()
    if env_dir:
        return env_dir
    pio = shutil.which("pio")
    if pio:
        result = subprocess.run(
            [pio, "system", "info"],
            capture_output=True,
            text=True,
            check=False,
            timeout=30,
        )
        for line in result.stdout.splitlines():
            if "PlatformIO Core Directory" in line:
                return line.split(None, 3)[-1].strip()
    home = os.path.join(os.path.expanduser("~"), ".platformio")
    if os.path.isdir(home):
        return home
    return ""


# ================================================================================

def package_dir(core):
    candidates = [
        os.path.join(core, "tools", "tool-esptoolpy"),
        os.path.join(core, "packages", "tool-esptoolpy"),
    ]
    for path in candidates:
        if os.path.isdir(os.path.join(path, "esptool")):
            return path
    return ""


# ================================================================================

def penv_python(core):
    for name in ("python", "python3"):
        path = os.path.join(core, "penv", "bin", name)
        if os.path.isfile(path):
            return path
    return ""


# ================================================================================

def uv_exe(python_exe):
    sibling = os.path.join(os.path.dirname(python_exe), "uv")
    if os.path.isfile(sibling):
        return sibling
    return shutil.which("uv") or ""


# ================================================================================

def needs_install(python_exe, pkg):
    probe = (
        "import os, sys\n"
        "try:\n"
        "    import esptool\n"
        "except ImportError:\n"
        "    raise SystemExit(2)\n"
        "ver = getattr(esptool, '__version__', '')\n"
        "if ver.startswith('5.4.0'):\n"
        "    raise SystemExit(3)\n"
        "actual = os.path.realpath(os.path.dirname(esptool.__file__))\n"
        "expected = os.path.realpath(sys.argv[1])\n"
        "raise SystemExit(0 if actual.startswith(expected) else 4)\n"
    )
    result = subprocess.run(
        [python_exe, "-c", probe, pkg],
        capture_output=True,
        text=True,
        timeout=15,
        check=False,
    )
    return result.returncode != 0


# ================================================================================

def install(python_exe, pkg):
    uv = uv_exe(python_exe)
    if uv:
        subprocess.call(
            [uv, "pip", "uninstall", "-y", "esptool", f"--python={python_exe}"],
            timeout=60,
        )
        cmd = [
            uv,
            "pip",
            "install",
            "--quiet",
            f"--python={python_exe}",
            "-e",
            pkg,
        ]
    else:
        subprocess.call(
            [python_exe, "-m", "pip", "uninstall", "-y", "esptool"],
            timeout=60,
        )
        cmd = [
            python_exe,
            "-m",
            "pip",
            "install",
            "--quiet",
            "--force-reinstall",
            "-e",
            pkg,
        ]
    subprocess.check_call(cmd, timeout=120)
    print("Reinstalled esptool from %s into %s" % (pkg, python_exe), flush=True)


# ================================================================================

def main():
    core = core_dir()
    pkg = package_dir(core) if core else ""
    python_exe = penv_python(core) if core else ""
    if core == "" or pkg == "" or python_exe == "":
        print(
            "Skipping esptool refresh (core=%r package=%r python=%r)"
            % (core, pkg, python_exe),
            flush=True,
        )
        return 0
    if needs_install(python_exe, pkg):
        install(python_exe, pkg)
        if needs_install(python_exe, pkg):
            print(
                "esptool is still missing or is 5.4.0 after installing %s" % pkg,
                file=sys.stderr,
                flush=True,
            )
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
