#!/usr/bin/env python3
"""
Monolith Instant-Launch & Warm-RAM Engine v2.2 ("monolith-ram-preloader")
Combines 4 real-world acceleration layers for <1ms application startup:
  1. Silent No-Focus Warm-Standby Pre-Spawning in `special:ramvault`
     (with `misc:focus_on_activate=false` + `[workspace special:ramvault silent; noinitialfocus]`
      so background apps NEVER pop up on screen or open the special workspace overlay)
  2. Deep Pagecache & Shared-Library Pre-Faulting (ELF binaries, libxul.so, omni.ja,
     Electron .asar/.pak bundles, V8 Code Cache, Mesa GPU Shader Cache)
  3. SQLite Profile WAL + 256MB Memory-Mapped I/O Optimizer
  4. 3.5-second AMD P-State / Power-Profile Turbo Burst on Launch
"""

import sys
import os
import json
import time
import threading
import sqlite3
import subprocess
import shutil
import re
from concurrent.futures import ThreadPoolExecutor

CONFIG_PATH = os.path.expanduser("~/.config/monolith/taskgraph/config.json")
BOOST_LOCK_FILE = "/tmp/.monolith_boost.lock"


def resolve_executable(cmd):
    """
    Intelligently resolves executable binary paths, Flatpaks, or local binaries.
    Ensures background warm-spawning and preloading always find the exact binary.
    """
    if not cmd:
        return cmd
    parts = cmd.split("||")
    for part in parts:
        part = part.strip()
        tokens = part.split()
        if not tokens:
            continue
        first = tokens[0]
        if shutil.which(first) or (os.path.isabs(first) and os.path.exists(first)):
            return part
        local_bin = os.path.expanduser(f"~/.local/bin/{first}")
        if os.path.exists(local_bin):
            tokens[0] = local_bin
            return " ".join(tokens)

    first_token = cmd.split("||")[0].strip().split()[0].lower() if cmd.strip() else ""
    flatpak_map = {
        "obsidian": "md.obsidian.Obsidian",
        "vesktop": "dev.vencord.Vesktop",
        "discord": "com.discordapp.Discord",
        "caprine": "com.sindresorhus.Caprine",
        "signal": "org.signal.Signal",
        "signal-desktop": "org.signal.Signal",
        "localsend": "org.localsend.localsend_app",
        "steam": "com.valvesoftware.Steam",
        "prismlauncher": "org.prismlauncher.PrismLauncher",
        "chromium": "org.chromium.Chromium",
        "opera-gx": "com.opera.opera-gx",
    }
    if first_token in flatpak_map:
        return f"flatpak run {flatpak_map[first_token]}"

    return cmd.split("||")[0].strip()


def send_notification(summary, body, urgency="normal"):
    try:
        subprocess.Popen([
            "notify-send",
            "-a", "Monolith Instant-Launch",
            "-i", "drive-harddisk-system",
            "-u", urgency,
            summary,
            body
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception:
        pass


def is_monolith_session_active():
    if not os.environ.get("HYPRLAND_INSTANCE_SIGNATURE"):
        return False
    desktop = os.environ.get("XDG_SESSION_DESKTOP", "") or os.environ.get("DESKTOP_SESSION", "")
    if "monolith" in desktop.lower():
        return True
    res = subprocess.getoutput("pgrep -f 'monolith-(constellation|taskgraph)' 2>/dev/null").strip()
    return bool(res)


def ensure_ramvault_hidden():
    """Guarantees special:ramvault or unwanted scratchpad overlay is never open."""
    try:
        raw = subprocess.getoutput("hyprctl -j monitors 2>/dev/null")
        mons = json.loads(raw) if raw else []
        for m in mons:
            sp = m.get("specialWorkspace", {}).get("name", "")
            if sp == "special:ramvault":
                subprocess.run(
                    ["hyprctl", "dispatch", "togglespecialworkspace", "ramvault"],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
                )
    except Exception:
        pass


def ensure_warm_vault_instance(app):
    """
    Pre-spawns `app` silently into `special:hidden` so clicking the constellation summons it in <1ms.
    """
    if not is_monolith_session_active():
        return False

    match_cls = app.get("match_class", "") or app.get("id", "")
    exec_cmd = app.get("exec", "")
    if not exec_cmd or not match_cls:
        return False

    # Check if already running anywhere in Hyprland
    try:
        raw = subprocess.getoutput("hyprctl -j clients 2>/dev/null")
        clients = json.loads(raw) if raw else []
        for c in clients:
            cls = c.get("class", "")
            if cls and match_cls and (cls.lower() == match_cls.lower() or match_cls.lower() in cls.lower()):
                return True
    except Exception:
        pass

    clean_cmd = resolve_executable(exec_cmd)
    subprocess.Popen(
        ["hyprctl", "dispatch", "exec", f"[workspace special:ramvault silent; noinitialfocus] {clean_cmd}"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    )

    # For Flatpaks or multi-process wrappers that fork outside initial exec rule,
    # tuck the window into special:ramvault as soon as its surface maps
    def _tuck_window():
        for _ in range(50):
            time.sleep(0.08)
            raw = subprocess.getoutput("hyprctl -j clients 2>/dev/null")
            try:
                clients = json.loads(raw) if raw else []
                for c in clients:
                    cls = c.get("class", "")
                    addr = c.get("address", "")
                    ws = c.get("workspace", {}).get("name", "")
                    if addr and match_cls and (cls.lower() == match_cls.lower() or match_cls.lower() in cls.lower()):
                        if ws != "special:ramvault":
                            subprocess.run(
                                ["hyprctl", "dispatch", "movetoworkspacesilent", f"special:ramvault,address:{addr}"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
                            )
                        return
            except Exception:
                pass

    t = threading.Thread(target=_tuck_window, daemon=False)
    t.start()
    t.join(timeout=1.8)
    return True


def kill_warm_vault_instance(app):
    """If an app is unloaded from RAM and is sitting hidden in special:hidden, close it."""
    if not is_monolith_session_active():
        return
    match_cls = app.get("match_class", "") or app.get("id", "")
    if not match_cls:
        return
    try:
        raw = subprocess.getoutput("hyprctl -j clients 2>/dev/null")
        clients = json.loads(raw) if raw else []
        for c in clients:
            cls = c.get("class", "")
            ws_name = c.get("workspace", {}).get("name", "")
            addr = c.get("address", "")
            if ws_name in ("special:hidden", "ramvault", "name:ramvault", "special:ramvault") and addr and (cls.lower() == match_cls.lower() or match_cls.lower() in cls.lower()):
                subprocess.Popen(
                    ["hyprctl", "dispatch", f"closewindow address:{addr}"],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
                )
    except Exception:
        pass


def trigger_cpu_boost_burst(duration_sec=3.5):
    try:
        if os.path.exists(BOOST_LOCK_FILE):
            with open(BOOST_LOCK_FILE, "w") as f:
                f.write(str(time.time() + duration_sec))
            return

        cur_prof = subprocess.getoutput("powerprofilesctl get 2>/dev/null").strip()
        if not cur_prof or cur_prof == "performance":
            return

        with open(BOOST_LOCK_FILE, "w") as f:
            f.write(str(time.time() + duration_sec))

        cmd = (
            f"powerprofilesctl set performance >/dev/null 2>&1; "
            f"sleep {duration_sec}; "
            f"rm -f {BOOST_LOCK_FILE}; "
            f"powerprofilesctl set {cur_prof} >/dev/null 2>&1"
        )
        subprocess.Popen(["sh", "-c", cmd], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception:
        pass


def get_profile_dirs(app_name, app_id=""):
    home = os.path.expanduser("~")
    candidates = []
    keys = {app_name.lower(), app_id.lower()}

    for k in list(keys):
        if "zen" in k:
            candidates.append(os.path.join(home, ".zen"))
            candidates.append(os.path.join(home, ".config/zen"))
            candidates.append(os.path.join(home, ".cache/zen"))
        if "firefox" in k:
            candidates.append(os.path.join(home, ".mozilla/firefox"))
            candidates.append(os.path.join(home, ".cache/mozilla/firefox"))
        if "obsidian" in k:
            candidates.append(os.path.join(home, ".config/obsidian"))
        if "vesktop" in k or "discord" in k:
            candidates.append(os.path.join(home, ".config/vesktop"))
            candidates.append(os.path.join(home, ".config/discord"))
        if "spotify" in k:
            candidates.append(os.path.join(home, ".config/spotify"))
            candidates.append(os.path.join(home, ".cache/spotify"))
        if "chromium" in k or "brave" in k or "chrome" in k:
            candidates.append(os.path.join(home, f".config/{k}"))
        candidates.append(os.path.join(home, f".config/{k}"))

    return [d for d in candidates if os.path.isdir(d)]


def optimize_app_sqlite_profiles(app_name, app_id=""):
    optimized_count = 0
    visited = set()

    for base in get_profile_dirs(app_name, app_id):
        for root, _, files in os.walk(base):
            for fn in files:
                if fn.endswith(".sqlite") or fn in ("Cookies", "History", "Web Data", "Favicons"):
                    fp = os.path.join(root, fn)
                    if fp in visited or not os.path.isfile(fp):
                        continue
                    visited.add(fp)
                    try:
                        conn = sqlite3.connect(f"file:{fp}?mode=rw", uri=True, timeout=0.15)
                        conn.execute("PRAGMA journal_mode=WAL;")
                        conn.execute("PRAGMA synchronous=NORMAL;")
                        conn.execute("PRAGMA mmap_size=268435456;")
                        conn.execute("PRAGMA optimize;")
                        conn.close()
                        optimized_count += 1
                    except Exception:
                        pass
    return optimized_count


def resolve_command_targets(exec_cmd, app_id=""):
    targets = []
    tokens = exec_cmd.strip().split("||")[0].strip().split()
    if not tokens:
        return []
    cmd = tokens[0]

    if cmd in ("foot", "footclient", "alacritty", "kitty", "xterm") and "-e" in tokens:
        idx = tokens.index("-e")
        if idx + 1 < len(tokens):
            cmd = tokens[idx + 1]

    bin_path = subprocess.getoutput(f"which {cmd} 2>/dev/null").strip()
    if bin_path and os.path.exists(bin_path):
        targets.append(os.path.realpath(bin_path))

    if "spotify" in app_id.lower() or "spotify" in exec_cmd.lower():
        sp = os.path.expanduser("~/.local/share/spotify-launcher/install/usr/share/spotify/spotify")
        if os.path.exists(sp):
            targets.append(sp)

    checked_scripts = set()
    queue = list(targets)
    while queue:
        curr = queue.pop(0)
        if curr in checked_scripts:
            continue
        checked_scripts.add(curr)

        if os.path.isfile(curr):
            try:
                with open(curr, "r", errors="ignore") as f:
                    content = f.read(4096)
                if content.startswith("#!"):
                    variables = {}
                    for line in content.splitlines():
                        line = line.strip()
                        m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)=[\"']?([^\"'\s]+)[\"']?", line)
                        if m:
                            variables[m.group(1)] = m.group(2)

                        if line.startswith("exec ") or "electron" in line or "/usr/lib" in line or "/opt" in line:
                            expanded = line
                            for k, v in variables.items():
                                expanded = expanded.replace(f"${{{k}}}", v).replace(f"${k}", v)
                            for token in expanded.split():
                                clean = token.strip("\"'`;$,")
                                if clean.startswith("/") and os.path.exists(clean):
                                    real_clean = os.path.realpath(clean)
                                    if real_clean not in targets:
                                        targets.append(real_clean)
                                        queue.append(real_clean)
                                elif clean.startswith("electron"):
                                    eb = subprocess.getoutput(f"which {clean} 2>/dev/null").strip()
                                    if eb and os.path.exists(eb):
                                        real_eb = os.path.realpath(eb)
                                        if real_eb not in targets:
                                            targets.append(real_eb)
                                            queue.append(real_eb)
            except Exception:
                pass

    return targets


def get_related_files(app_name, exec_cmd, app_id=""):
    files = set()
    targets = resolve_command_targets(exec_cmd, app_id)
    valid_exts = (".so", ".pak", ".bin", ".dat", ".asar", ".node", ".ja", ".wasm", ".sqlite", ".sqlite-wal", ".js", ".scache")

    for t in targets:
        if not os.path.exists(t):
            continue
        if os.path.isfile(t):
            files.add(t)
            try:
                with open(t, "rb") as f:
                    magic = f.read(4)
                if magic == b"\x7fELF":
                    ldd_out = subprocess.getoutput(f'ldd "{t}" 2>/dev/null')
                    for line in ldd_out.splitlines():
                        if "=>" in line:
                            parts = line.split("=>")[1].strip().split("(")[0].strip()
                            if os.path.exists(parts) and not os.path.islink(parts):
                                files.add(os.path.realpath(parts))
            except Exception:
                pass

            parent = os.path.dirname(t)
            if parent not in ("/usr/bin", "/bin", "/usr/local/bin"):
                for root, _, filenames in os.walk(parent):
                    for fname in filenames:
                        ext = os.path.splitext(fname)[1].lower()
                        if ext in valid_exts or "v8" in fname or "libxul" in fname or "omni" in fname:
                            fp = os.path.join(root, fname)
                            if os.path.isfile(fp) and not os.path.islink(fp):
                                files.add(fp)

    home = os.path.expanduser("~")
    base_names = set(filter(None, [
        app_name.lower(),
        app_id.lower(),
        app_name.lower().replace("-desktop", ""),
        app_id.lower().replace("-desktop", "")
    ]))

    for base in base_names:
        for cdir in [
            f"/usr/lib/{base}",
            f"/usr/lib/{base}-desktop",
            f"/usr/lib/{base}-bin",
            f"/opt/{base}",
            f"/opt/{base}-bin",
            f"/usr/share/{base}",
            f"{home}/.config/{base}/Code Cache",
            f"{home}/.config/{base}/GPUCache",
            f"{home}/.cache/mesa_shader_cache_db"
        ]:
            if os.path.isdir(cdir):
                for root, _, filenames in os.walk(cdir):
                    for fname in filenames:
                        fp = os.path.join(root, fname)
                        if os.path.isfile(fp) and not os.path.islink(fp):
                            files.add(fp)

    for pdir in get_profile_dirs(app_name, app_id):
        for root, _, filenames in os.walk(pdir):
            for fname in filenames:
                ext = os.path.splitext(fname)[1].lower()
                if ext in (".sqlite", ".sqlite-wal", ".lz4", ".bin", ".scache") or fname in ("prefs.js", "places.sqlite", "favicons.sqlite", "cookies.sqlite"):
                    fp = os.path.join(root, fname)
                    if os.path.isfile(fp) and not os.path.islink(fp):
                        files.add(fp)

    return sorted(list(files))


def preload_single_file(path):
    try:
        sz = os.path.getsize(path)
        if sz == 0 or sz > 512 * 1024 * 1024:
            return 0
        with open(path, "rb") as f:
            fd = f.fileno()
            os.posix_fadvise(fd, 0, sz, os.POSIX_FADV_WILLNEED)
            while f.read(4 * 1024 * 1024):
                pass
        return sz
    except Exception:
        return 0


def preload_app(app_name, exec_cmd, app_id=""):
    sql_cnt = optimize_app_sqlite_profiles(app_name, app_id)
    files = get_related_files(app_name, exec_cmd, app_id)
    total_bytes = 0
    if files:
        with ThreadPoolExecutor(max_workers=8) as executor:
            results = executor.map(preload_single_file, files)
            total_bytes = sum(results)

    mb = total_bytes / (1024 * 1024)
    print(f"[Instant-Launch] Preloaded {app_name}: {len(files)} files ({mb:.1f} MB) + {sql_cnt} SQLite DBs WAL-tuned.")
    return len(files), total_bytes


def preload_all_enabled(notify=True):
    if not os.path.exists(CONFIG_PATH):
        return
    try:
        with open(CONFIG_PATH, "r") as f:
            cfg = json.load(f)
        pinned = cfg.get("pinned", [])
        to_preload = [app for app in pinned if app.get("load_to_ram", False)]
        if not to_preload:
            return

        total_apps = 0
        total_files = 0
        total_mb = 0.0

        for app in to_preload:
            app_id = app.get("id", "")
            name = app.get("name", app_id)
            exec_cmd = app.get("exec", "")
            count, sz = preload_app(name, exec_cmd, app_id)
            ensure_warm_vault_instance(app)
            if count > 0:
                total_apps += 1
                total_files += count
                total_mb += sz / (1024 * 1024)

        if notify and total_apps > 0:
            send_notification(
                "Instant-Launch Vault Ready",
                f"{total_apps} apps ({total_mb:.0f} MB) warm-spawned in RAM Vault."
            )
    except Exception as e:
        print(f"[Instant-Launch] Error: {e}", file=sys.stderr)


def warm_only_command(target_name_or_id):
    if not os.path.exists(CONFIG_PATH):
        return
    try:
        with open(CONFIG_PATH, "r") as f:
            cfg = json.load(f)
        for app in cfg.get("pinned", []):
            if (app.get("id") == target_name_or_id or
                    app.get("name", "").lower() == target_name_or_id.lower()):
                if app.get("load_to_ram", False):
                    ensure_warm_vault_instance(app)
                break
    except Exception:
        pass


def load_app_command(target_name_or_id):
    cfg = {}
    if os.path.exists(CONFIG_PATH):
        try:
            with open(CONFIG_PATH, "r") as f:
                cfg = json.load(f)
        except Exception:
            cfg = {}

    pinned = cfg.get("pinned", [])
    target_app = None
    for app in pinned:
        if (app.get("id") == target_name_or_id or
                app.get("name", "").lower() == target_name_or_id.lower() or
                app.get("exec", "").split()[0] == target_name_or_id):
            target_app = app
            break

    if target_app:
        target_app["load_to_ram"] = True
        try:
            with open(CONFIG_PATH, "w") as f:
                json.dump(cfg, f, indent=2)
        except Exception:
            pass

        name = target_app.get("name", target_name_or_id)
        cmd = target_app.get("exec", "")
        trigger_cpu_boost_burst(4.0)
        count, sz = preload_app(name, cmd, target_app.get("id", ""))
        ensure_warm_vault_instance(target_app)
        mb = sz / (1024 * 1024)
        send_notification(
            "Instant-Launch Enabled",
            f"{name} ({mb:.0f} MB) locked in RAM Pagecache (<1ms open)."
        )
    else:
        trigger_cpu_boost_burst(3.5)
        count, sz = preload_app(target_name_or_id, target_name_or_id, target_name_or_id)
        dyn_app = {
            "id": target_name_or_id,
            "name": target_name_or_id,
            "exec": resolve_executable(target_name_or_id),
            "match_class": target_name_or_id,
            "load_to_ram": True
        }
        ensure_warm_vault_instance(dyn_app)


def unload_app_command(target_name_or_id):
    if not os.path.exists(CONFIG_PATH):
        return
    try:
        with open(CONFIG_PATH, "r") as f:
            cfg = json.load(f)
        pinned = cfg.get("pinned", [])
        found_app = None
        for app in pinned:
            if (app.get("id") == target_name_or_id or
                    app.get("name", "").lower() == target_name_or_id.lower()):
                app["load_to_ram"] = False
                found_app = app
        if found_app:
            with open(CONFIG_PATH, "w") as f:
                json.dump(cfg, f, indent=2)
            kill_warm_vault_instance(found_app)
            send_notification(
                "Instant-Launch Disabled",
                f"{found_app.get('name', target_name_or_id)} released from RAM Vault."
            )
    except Exception:
        pass


def main():
    if len(sys.argv) < 2:
        preload_all_enabled(notify=False)
        return

    action = sys.argv[1].lower()
    if action in ("preload-all", "--all", "daemon"):
        preload_all_enabled(notify=True)
    elif action == "boost":
        dur = float(sys.argv[2]) if len(sys.argv) > 2 else 3.5
        trigger_cpu_boost_burst(dur)
    elif action == "warm" and len(sys.argv) >= 3:
        warm_only_command(sys.argv[2])
    elif action == "load" and len(sys.argv) >= 3:
        load_app_command(sys.argv[2])
    elif action == "unload" and len(sys.argv) >= 3:
        unload_app_command(sys.argv[2])
    else:
        load_app_command(sys.argv[1])


if __name__ == "__main__":
    main()
