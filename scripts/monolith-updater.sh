#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# MONOLITH AUTONOMOUS UPDATER & RELEASE MANAGER
# ══════════════════════════════════════════════════════════════════════════════
set -e
export GIT_TERMINAL_PROMPT=0
export GIT_ASKPASS=true

# Prefer user monolith config directory if it contains .git or settings.json
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/monolith"
if [ -d "$CONFIG_DIR" ] && ([ -d "${CONFIG_DIR}/.git" ] || [ -f "${CONFIG_DIR}/settings.json" ]); then
    BASE_DIR="$CONFIG_DIR"
else
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    BASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
fi
SETTINGS_FILE="${BASE_DIR}/settings.json"
VERSION_FILE="${BASE_DIR}/src/version.hpp"

# Extract current version from version.hpp if present
CURRENT_VERSION="1.2.0"
if [ -f "$VERSION_FILE" ]; then
    V_EXTRACT=$(grep -E '^#define MONOLITH_VERSION ' "$VERSION_FILE" | awk -F'"' '{print $2}')
    if [ -n "$V_EXTRACT" ]; then
        CURRENT_VERSION="$V_EXTRACT"
    fi
fi

CURRENT_COMMIT="unknown"
if command -v git &>/dev/null && [ -d "${BASE_DIR}/.git" ]; then
    CURRENT_COMMIT=$(git -C "$BASE_DIR" rev-parse --short HEAD 2>/dev/null || echo "unknown")
fi

# Determine default repo from settings.json or git remote or fallback
REPO="filipboski1313-a11y/monolith"
CHANNEL="releases"
if [ -d "${BASE_DIR}/.git" ] && command -v git &>/dev/null; then
    GIT_REMOTE=$(git -C "$BASE_DIR" remote get-url origin 2>/dev/null || true)
    PARSED_REPO=$(echo "$GIT_REMOTE" | sed -E 's#^.*github.com[:/]([^/]+/[^/.]+)(\.git)?$#\1#')
    if [ -n "$PARSED_REPO" ] && [ "$PARSED_REPO" != "$GIT_REMOTE" ]; then
        REPO="$PARSED_REPO"
    fi
fi
if [ -f "$SETTINGS_FILE" ] && command -v jq &>/dev/null; then
    SETTINGS_REPO=$(jq -r '.updates.repo // empty' "$SETTINGS_FILE" 2>/dev/null || true)
    if [ -n "$SETTINGS_REPO" ] && [ "$SETTINGS_REPO" != "boski/monolith" ]; then
        REPO="$SETTINGS_REPO"
    fi
    SETTINGS_CHAN=$(jq -r '.updates.channel // empty' "$SETTINGS_FILE" 2>/dev/null || true)
    if [ -n "$SETTINGS_CHAN" ]; then
        CHANNEL="$SETTINGS_CHAN"
    fi
fi

# Semver comparison: returns 0 if $1 > $2
version_gt() {
    local v1="${1#v}"
    local v2="${2#v}"
    [ "$v1" = "$v2" ] && return 1
    local IFS=.
    local i ver1=($v1) ver2=($v2)
    for ((i=0; i<${#ver1[@]} || i<${#ver2[@]}; i++)); do
        local n1=${ver1[i]:-0}
        local n2=${ver2[i]:-0}
        if ((10#$n1 > 10#$n2)); then
            return 0
        elif ((10#$n1 < 10#$n2)); then
            return 1
        fi
    done
    return 1
}

# ──────────────────────────────────────────────────────────────────────────────
# ACTION: CHECK RELEASES / COMMITS
# ──────────────────────────────────────────────────────────────────────────────
check_updates() {
    local target_repo="${1:-$REPO}"
    local target_channel="${2:-$CHANNEL}"

    # Verify curl is available
    if ! command -v curl &>/dev/null; then
        echo "{\"status\":\"error\",\"error\":\"curl not installed\",\"current_version\":\"$CURRENT_VERSION\",\"current_commit\":\"$CURRENT_COMMIT\",\"update_available\":false}"
        return
    fi

    local api_url="https://api.github.com/repos/${target_repo}/releases/latest"
    local http_code
    local response_file
    response_file=$(mktemp)

    http_code=$(curl -s -m 8 -w "%{http_code}" -H "Accept: application/vnd.github+json" -H "User-Agent: Monolith-Updater" "$api_url" -o "$response_file" 2>/dev/null || echo "000")

    if [ "$http_code" = "200" ] && [ "$target_channel" != "commits" ]; then
        local raw_tag name body pub_date url
        raw_tag=$(jq -r '.tag_name // ""' "$response_file" 2>/dev/null || true)
        name=$(jq -r '.name // ""' "$response_file" 2>/dev/null || true)
        pub_date=$(jq -r '.published_at // ""' "$response_file" 2>/dev/null || true)
        body=$(jq -r '.body // ""' "$response_file" 2>/dev/null || true)
        url=$(jq -r '.html_url // ""' "$response_file" 2>/dev/null || true)
        rm -f "$response_file"

        local clean_tag="${raw_tag#v}"
        local update_avail=false
        if version_gt "$clean_tag" "$CURRENT_VERSION"; then
            update_avail=true
        fi

        jq -n \
            --arg status "success" \
            --arg current_version "$CURRENT_VERSION" \
            --arg current_commit "$CURRENT_COMMIT" \
            --arg latest_version "$clean_tag" \
            --arg tag_name "$raw_tag" \
            --arg release_name "$name" \
            --arg release_date "$pub_date" \
            --arg changelog "$body" \
            --arg url "$url" \
            --arg channel "releases" \
            --argjson update_available "$update_avail" \
            '{
                status: $status,
                current_version: $current_version,
                current_commit: $current_commit,
                latest_version: $latest_version,
                tag_name: $tag_name,
                release_name: $release_name,
                release_date: $release_date,
                changelog: $changelog,
                url: $url,
                channel: $channel,
                update_available: $update_available
            }'
        return
    fi

    # Fallback: Check latest commit on main branch
    local commit_url="https://api.github.com/repos/${target_repo}/commits/main"
    http_code=$(curl -s -m 8 -w "%{http_code}" -H "Accept: application/vnd.github+json" -H "User-Agent: Monolith-Updater" "$commit_url" -o "$response_file" 2>/dev/null || echo "000")

    if [ "$http_code" = "200" ]; then
        local remote_sha msg commit_date
        remote_sha=$(jq -r '.sha[0:7] // ""' "$response_file" 2>/dev/null || true)
        msg=$(jq -r '.commit.message // ""' "$response_file" 2>/dev/null || true)
        commit_date=$(jq -r '.commit.committer.date // ""' "$response_file" 2>/dev/null || true)
        rm -f "$response_file"

        local update_avail=false
        if [ -n "$remote_sha" ] && [ "$remote_sha" != "$CURRENT_COMMIT" ]; then
            update_avail=true
        fi

        jq -n \
            --arg status "success" \
            --arg current_version "$CURRENT_VERSION" \
            --arg current_commit "$CURRENT_COMMIT" \
            --arg latest_version "$CURRENT_VERSION" \
            --arg tag_name "$remote_sha" \
            --arg release_name "Latest Commit: $remote_sha" \
            --arg release_date "$commit_date" \
            --arg changelog "$msg" \
            --arg url "https://github.com/${target_repo}/commit/${remote_sha}" \
            --arg channel "commits" \
            --argjson update_available "$update_avail" \
            '{
                status: $status,
                current_version: $current_version,
                current_commit: $current_commit,
                latest_version: $latest_version,
                tag_name: $tag_name,
                release_name: $release_name,
                release_date: $release_date,
                changelog: $changelog,
                url: $url,
                channel: $channel,
                update_available: $update_available
            }'
        return
    fi

    rm -f "$response_file"

    # Upstream unreachable or not published yet
    jq -n \
        --arg status "unreachable" \
        --arg current_version "$CURRENT_VERSION" \
        --arg current_commit "$CURRENT_COMMIT" \
        --arg latest_version "$CURRENT_VERSION" \
        --arg tag_name "v${CURRENT_VERSION}" \
        --arg release_name "Local Monolith Build" \
        --arg release_date "$(date -I)" \
        --arg changelog "Upstream repository '${target_repo}' has not been published to GitHub yet or network is unreachable. Once pushed to GitHub with releases, new versions and changelogs will automatically appear here." \
        --arg url "" \
        --arg channel "$target_channel" \
        --argjson update_available false \
        '{
            status: $status,
            current_version: $current_version,
            current_commit: $current_commit,
            latest_version: $latest_version,
            tag_name: $tag_name,
            release_name: $release_name,
            release_date: $release_date,
            changelog: $changelog,
            url: $url,
            channel: $channel,
            update_available: $update_available
        }'
}

# ──────────────────────────────────────────────────────────────────────────────
# ACTION: PERFORM UPGRADE
# ──────────────────────────────────────────────────────────────────────────────
perform_upgrade() {
    local target_repo="${1:-$REPO}"
    echo "================================================================================"
    echo "MONOLITH UPGRADE PROCESS INITIATED"
    echo "Repository: ${target_repo} | Current: v${CURRENT_VERSION} (${CURRENT_COMMIT})"
    echo "================================================================================"
    sleep 0.2

    # Step 1: Backup configuration files
    echo "[STEP 1/6] Backing up configuration and user parameters..."
    local backup_dir="${BASE_DIR}/backups/monolith-backup-$(date +%Y%m%d_%H%M%S)"
    mkdir -p "$backup_dir"
    if [ -f "${BASE_DIR}/settings.json" ]; then
        cp -f "${BASE_DIR}/settings.json" "${backup_dir}/"
    fi
    if [ -f "${BASE_DIR}/hyprland.conf" ]; then
        cp -f "${BASE_DIR}/hyprland.conf" "${backup_dir}/"
    fi
    if [ -f "${BASE_DIR}/user-settings.conf" ]; then
        cp -f "${BASE_DIR}/user-settings.conf" "${backup_dir}/"
    fi
    if [ -d "${BASE_DIR}/waybar" ]; then
        cp -rf "${BASE_DIR}/waybar" "${backup_dir}/"
    fi
    echo "Backup snapshot created at: ${backup_dir}"
    sleep 0.3

    # Step 2: Synchronize repository with remote
    echo "[STEP 2/6] Synchronizing repository tree from upstream..."
    if command -v git &>/dev/null && [ -d "${BASE_DIR}/.git" ]; then
        local remote_url
        remote_url=$(git -C "$BASE_DIR" remote get-url origin 2>/dev/null || true)

        if [ -n "$remote_url" ]; then
            echo "Fetching changes from remote origin (${remote_url})..."
            git -C "$BASE_DIR" fetch --all --tags 2>&1 || true

            # If there are local untracked or modified files, stash them cleanly
            local had_stash=false
            if ! git -C "$BASE_DIR" diff --quiet 2>/dev/null; then
                echo "Preserving local modifications in temporary stash..."
                git -C "$BASE_DIR" stash save "Auto-stash before upgrade $(date +%s)" 2>&1 || true
                had_stash=true
            fi

            echo "Rebasing local branch to origin/main..."
            if ! git -C "$BASE_DIR" pull --rebase origin main 2>&1; then
                echo "Warning: Rebase encountered conflict, attempting git checkout origin/main..."
                git -C "$BASE_DIR" reset --hard origin/main 2>&1 || true
            fi

            if [ "$had_stash" = true ]; then
                echo "Restoring local user modifications..."
                git -C "$BASE_DIR" stash pop 2>&1 || true
            fi
        else
            echo "Remote 'origin' is not configured yet. Checking if target repository is accessible..."
            local target_git_url="https://github.com/${target_repo}.git"
            if git ls-remote "$target_git_url" &>/dev/null; then
                echo "Configuring remote origin -> ${target_git_url}..."
                git -C "$BASE_DIR" remote add origin "$target_git_url" 2>/dev/null || git -C "$BASE_DIR" remote set-url origin "$target_git_url"
                git -C "$BASE_DIR" fetch origin 2>&1 || true
                git -C "$BASE_DIR" pull --rebase origin main 2>&1 || true
            else
                echo "Remote repository not accessible or not created yet. Proceeding with local sources compilation."
            fi
        fi
    else
        echo "No git repository found; proceeding with direct source compilation."
    fi
    sleep 0.3

    # Step 3: Compile native C++ suite
    echo "[STEP 3/6] Compiling native C++ suite (Settings, TaskGraph, Toolbox, Popups)..."
    make -C "$BASE_DIR" clean
    local nthreads
    nthreads=$(nproc 2>/dev/null || echo 4)
    echo "Running parallel build with ${nthreads} threads..."
    if ! make -C "$BASE_DIR" -j"${nthreads}"; then
        echo "[ERROR] Native C++ compilation failed. Upgrade aborted."
        exit 1
    fi
    sleep 0.3

    # Step 4: Install updated binaries
    echo "[STEP 4/6] Installing updated binaries to system and local paths..."
    if command -v sudo &>/dev/null && sudo -n true 2>/dev/null; then
        echo "Installing to /usr/local/bin via passwordless sudo..."
        sudo -n make -C "$BASE_DIR" install
    else
        echo "sudo -n unavailable, installing directly to ~/.local/bin..."
    fi

    mkdir -p "${HOME}/.local/bin"
    if [ -d "${BASE_DIR}/bin" ]; then
        cp -f "${BASE_DIR}/bin/"* "${HOME}/.local/bin/"
    fi
    if [ -f "${BASE_DIR}/scripts/monolith-ram-preloader.py" ]; then
        cp -f "${BASE_DIR}/scripts/monolith-ram-preloader.py" "${HOME}/.local/bin/monolith-ram-preloader"
        chmod +x "${HOME}/.local/bin/monolith-ram-preloader"
    fi
    sleep 0.3

    # Step 5: Update permissions & systemd units
    echo "[STEP 5/6] Updating executable permissions and user systemd service units..."
    chmod +x "${BASE_DIR}/scripts/"*.sh "${BASE_DIR}/scripts/"*.py 2>/dev/null || true

    local systemd_user_dir="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
    mkdir -p "$systemd_user_dir"
    if [ -d "${BASE_DIR}/systemd" ]; then
        cp -f "${BASE_DIR}/systemd/"*.service "$systemd_user_dir/"
    fi
    if command -v systemctl &>/dev/null; then
        systemctl --user daemon-reload 2>/dev/null || true
    fi
    sleep 0.3

    # Step 6: Reload components & desktop session
    echo "[STEP 6/6] Reloading shell components and active desktop session..."
    if command -v systemctl &>/dev/null; then
        systemctl --user restart monolith-popup.service 2>/dev/null || true
        systemctl --user restart monolith-waybar.service 2>/dev/null || true
        systemctl --user restart monolith-taskgraph.service 2>/dev/null || true
        systemctl --user restart monolith-ram-preload.service 2>/dev/null || true
    fi

    if command -v hyprctl &>/dev/null; then
        hyprctl reload >/dev/null 2>&1 || true
    fi

    if command -v notify-send &>/dev/null; then
        notify-send -a "Monolith System" -u normal "Monolith Upgrade Complete" "System successfully rebuilt and updated to the latest build."
    fi

    echo "================================================================================"
    echo "[COMPLETE] Monolith update process finished successfully!"
    echo "================================================================================"
}

# ──────────────────────────────────────────────────────────────────────────────
# MAIN CLI ENTRY POINT
# ──────────────────────────────────────────────────────────────────────────────
case "$1" in
    --check)
        check_updates "$2" "$3"
        ;;
    --upgrade)
        perform_upgrade "$2"
        ;;
    --version)
        echo "Monolith v${CURRENT_VERSION} (Commit: ${CURRENT_COMMIT})"
        ;;
    *)
        echo "Usage: monolith-updater.sh [--check [owner/repo] [channel] | --upgrade [owner/repo] | --version]"
        exit 1
        ;;
esac
