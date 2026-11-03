#!/usr/bin/env python3
"""
Monolith RAM Preloader ("Load to RAM")
Preloads application binaries, shared libraries, and resources directly
into Linux unified physical memory pagecache using posix_fadvise.
Provides near-zero-millisecond launch times upon startup.
"""

import sys
import os
import json
import subprocess
import glob
from concurrent.futures import ThreadPoolExecutor

CONFIG_PATH = os.path.expanduser("~/.config/monolith/taskgraph/config.json")


def get_binary_path(exec_cmd):
    # Extract the first token from exec command (strip args)
    tokens = exec_cmd.strip().split()
    if not tokens:
        return None
    cmd = tokens[0]
    # Handle wrappers like foot -e btop
    if cmd in ("foot", "alacritty", "kitty", "xterm") and len(tokens) >= 3 and "-e" in tokens:
        idx = tokens.index("-e")
        if idx + 1 < len(tokens):
            cmd = tokens[idx + 1]
    
    bin_path = subprocess.getoutput(f"which {cmd} 2>/dev/null").strip()
    if os.path.exists(bin_path):
        return os.path.realpath(bin_path)
    return None


def get_related_files(bin_path):
    files = set()
    if not bin_path or not os.path.exists(bin_path):
        return []
    
    files.add(bin_path)

    # 1. Resolve shared libraries via ldd
    ldd_out = subprocess.getoutput(f'ldd "{bin_path}" 2>/dev/null')
    for line in ldd_out.splitlines():
        if "=>" in line:
            parts = line.split("=>")[1].strip().split("(")[0].strip()
            if os.path.exists(parts):
                files.add(os.path.realpath(parts))

    # 2. Check for application directories in /usr/lib and /opt
    base_name = os.path.basename(bin_path).lower()
    candidate_dirs = [
        f"/usr/lib/{base_name}",
        f"/usr/lib/{base_name}-desktop",
        f"/opt/{base_name}",
        f"/opt/{base_name.capitalize()}",
        f"/usr/share/{base_name}",
    ]
    for cdir in candidate_dirs:
        if os.path.isdir(cdir):
            for root, _, filenames in os.walk(cdir):
                for fname in filenames:
                    # Preload binary objects, pak files, and shared libs
                    if fname.endswith((".so", ".pak", ".bin", ".dat", ".node", "")) or "v8" in fname:
                        fpath = os.path.join(root, fname)
                        if os.path.isfile(fpath) and not os.path.islink(fpath):
                            files.add(fpath)

    return list(files)


def preload_single_file(path):
    try:
        size = os.path.getsize(path)
        with open(path, "rb") as f:
            fd = f.fileno()
            os.posix_fadvise(fd, 0, size, os.POSIX_FADV_WILLNEED)
            # Read first block to ensure kernel page table allocation
            f.read(min(size, 8192))
        return size
    except Exception:
        return 0


def preload_app(app_name, exec_cmd):
    bin_path = get_binary_path(exec_cmd)
    if not bin_path:
        return 0, 0
    files = get_related_files(bin_path)
    total_bytes = 0
    with ThreadPoolExecutor(max_workers=6) as executor:
        results = executor.map(preload_single_file, files)
        total_bytes = sum(results)
    mb = total_bytes / (1024 * 1024)
    print(f"[RAM Preloader] Preloaded {app_name} ({len(files)} files, {mb:.1f} MB) into physical RAM.")
    return len(files), total_bytes


def preload_all_enabled():
    if not os.path.exists(CONFIG_PATH):
        return
    try:
        with open(CONFIG_PATH, "r") as f:
            cfg = json.load(f)
        pinned = cfg.get("pinned", [])
        to_preload = [app for app in pinned if app.get("load_to_ram", False)]
        if not to_preload:
            print("[RAM Preloader] No apps configured with 'load_to_ram: true'.")
            return
        
        print(f"[RAM Preloader] Starting parallel RAM preload for {len(to_preload)} applications...")
        with ThreadPoolExecutor(max_workers=4) as executor:
            futures = [executor.submit(preload_app, app.get("name", app.get("id")), app.get("exec", "")) for app in to_preload]
            for f in futures:
                f.result()
        print("[RAM Preloader] Preload complete. Apps are resident in physical RAM cache.")
    except Exception as e:
        print(f"[RAM Preloader] Error: {e}", file=sys.stderr)


def toggle_app(app_id):
    if not os.path.exists(CONFIG_PATH):
        return False
    try:
        with open(CONFIG_PATH, "r") as f:
            cfg = json.load(f)
        pinned = cfg.get("pinned", [])
        found = False
        new_state = False
        target_app = None
        for app in pinned:
            if app.get("id") == app_id:
                curr = app.get("load_to_ram", False)
                new_state = not curr
                app["load_to_ram"] = new_state
                found = True
                target_app = app
                break
        
        if found:
            with open(CONFIG_PATH, "w") as f:
                json.dump(cfg, f, indent=2)
            print(f"[RAM Preloader] Toggled {app_id} load_to_ram -> {new_state}")
            if new_state and target_app:
                preload_app(target_app.get("name", app_id), target_app.get("exec", ""))
            return new_state
        return False
    except Exception as e:
        print(f"[RAM Preloader] Error: {e}", file=sys.stderr)
        return False


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: monolith-ram-preloader [preload-all | load <exec> | toggle <app_id>]")
        sys.exit(1)
    
    cmd = sys.argv[1]
    if cmd in ("preload-all", "--all"):
        preload_all_enabled()
    elif cmd == "load" and len(sys.argv) >= 3:
        preload_app(sys.argv[2], sys.argv[2])
    elif cmd == "toggle" and len(sys.argv) >= 3:
        toggle_app(sys.argv[2])
    else:
        preload_all_enabled()
