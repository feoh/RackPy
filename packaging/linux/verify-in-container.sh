#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# RP-39: runs inside packaging/linux/ubuntu-*.Dockerfile, without a network. /artifacts holds the release
# candidate (read-only), /opt/Rack2Free the pinned Rack Free 2.6.6 (read-only); logs and screenshots go to
# /logs, and /logs/summary.txt says what passed. Each Rack run gets a fresh user folder, as a fresh install.
set -uo pipefail
RACK=/opt/Rack2Free
SUMMARY=/logs/summary.txt
: > "$SUMMARY"
failed=0
note() { echo "$*" | tee -a "$SUMMARY"; }
check() {  # check "description" command...
    local what=$1; shift
    if "$@" > /dev/null 2>&1; then note "ok    $what"; else note "FAIL  $what"; failed=1; fi
}
has() { grep -qF -- "$2" "$1"; }
lacks() { ! grep -qF -- "$2" "$1"; }

note "machine: $(. /etc/os-release; echo "$PRETTY_NAME"), glibc $(ldd --version | head -1 | awk '{print $NF}')"
note "python on the system: $(command -v python3 python 2>/dev/null || echo none)"
note "network: $(cat /sys/class/net/*/operstate 2>/dev/null | sort | uniq -c | tr '\n' ' ')"
(cd /artifacts && sha256sum -c SHA256SUMS) > /logs/sha256sum.txt 2>&1
check "every artifact matches SHA256SUMS" grep -q ": OK" /logs/sha256sum.txt
check "no artifact fails SHA256SUMS" lacks /logs/sha256sum.txt "FAILED"

fresh_user() {  # $1: user folder; the .vcvplugin files after it are installed as Rack installs a download
    local user=$1; shift
    rm -rf "$user"; mkdir -p "$user/plugins-lin-x64" "$user/autosave"
    for p in "$@"; do cp "/artifacts/$p" "$user/plugins-lin-x64/"; done
    echo '{"showTipsOnLaunch": false}' > "$user/settings.json"
}
patch_of() {  # $1: user folder, $2: slug, $3: version, rest: models
    local user=$1 slug=$2 version=$3; shift 3
    local id=1 modules=""
    for model in "$@"; do
        modules+="${modules:+,}{\"id\": $id, \"plugin\": \"$slug\", \"model\": \"$model\", \"version\": \"$version\", \"params\": [], \"pos\": [$(( (id - 1) * 14 )), 0]}"
        id=$((id + 1))
    done
    echo "{\"version\": \"2.6.6\", \"modules\": [$modules], \"cables\": []}" > "$user/autosave/patch.json"
}
headless() {  # $1: user folder, $2: name for the logs. Quits through stdin once the patch has run for 3 s.
    local user=$1 name=$2 fifo
    fifo=$(mktemp -u); mkfifo "$fifo"
    rm -f "$user/log.txt"
    (cd "$RACK" && exec timeout 180 ./Rack -h -u "$user" < "$fifo" > "/logs/$name.out" 2>&1) &
    local rack=$!
    exec 9> "$fifo"
    for _ in $(seq 600); do
        grep -qF "Loading autosave" "$user/log.txt" 2>/dev/null && break
        kill -0 $rack 2>/dev/null || break
        sleep 0.1
    done
    sleep 3; echo >&9; exec 9>&-
    wait $rack; echo $? > "/logs/$name.rc"
    rm -f "$fifo"
    cp "$user/log.txt" "/logs/$name.log" 2>/dev/null || touch "/logs/$name.log"
}
clean_run() {  # $1: log name, $2: slug: the plugin loaded, nothing failed, and Python finalized
    local log=/logs/$1.log
    check "$1: Rack exited cleanly" test "$(cat "/logs/$1.rc")" = 0
    check "$1: RackPy started" has "$log" "RackPy interpreter started: Python 3.14.9"
    check "$1: loaded $2" has "$log" "Loaded plugin $2 "
    check "$1: no module failed" lacks "$log" "could not create its module"
    check "$1: no Python error" lacks "$log" "Traceback"
    check "$1: Python finalized" has "$log" "Py_FinalizeEx returned 0"
}

manifest=/artifacts/manifest.json
mapfile -t plugins < <(jq -r '.artifacts[] | select(.slug) | .file' "$manifest")
note "example plugins: ${#plugins[@]}"
for file in "${plugins[@]}"; do
    slug=$(jq -r --arg f "$file" '.artifacts[] | select(.file == $f) | .slug' "$manifest")
    version=${file#"$slug"-}; version=${version%%-lin-x64.vcvplugin}
    mapfile -t models < <(jq -r --arg f "$file" '.artifacts[] | select(.file == $f) | .models[]' "$manifest")
    user=/tmp/users/$slug
    fresh_user "$user" "$file"
    patch_of "$user" "$slug" "$version" "${models[@]}"
    headless "$user" "$slug-1"
    clean_run "$slug-1" "$slug"
    check "$slug-1: Rack extracted the package" test -f "$user/plugins-lin-x64/$slug/plugin.json"
    saved=$(jq -c '[.modules[] | .model] | sort' "$user/autosave/patch.json" 2>/dev/null)
    expected=$(printf '%s\n' "${models[@]}" | jq -R . | jq -cs 'sort')
    check "$slug-1: the autosave keeps all ${#models[@]} modules" test "$saved" = "$expected"
    headless "$user" "$slug-2"  # reopens the first run's autosave
    clean_run "$slug-2" "$slug"
    saved=$(jq -c '[.modules[] | .model] | sort' "$user/autosave/patch.json" 2>/dev/null)
    check "$slug-2: reopened, all ${#models[@]} modules still there" test "$saved" = "$expected"
    # The module browser, in a window: Enter opens it, typing filters it to this plugin, and Rack draws a
    # preview (a module widget without a module) of each model.
    name=$(jq -r .name "$user/plugins-lin-x64/$slug/plugin.json")
    rm -f "$user/log.txt"
    xvfb-run -a -s "-screen 0 1600x1000x24" bash -c '
        cd /opt/Rack2Free && ./Rack -u "$1" > "/logs/$2-window.out" 2>&1 &
        rack=$!
        for _ in $(seq 60); do sleep 0.5; win=$(xdotool search --name "VCV Rack" 2>/dev/null | head -1); [ -n "$win" ] && break; done
        sleep 4
        xdotool windowactivate --sync "$win" 2>/dev/null; xdotool mousemove 800 600 click 1
        xdotool key Return; sleep 1.5; xdotool type --delay 40 "$3"; sleep 4
        import -window root "/logs/$2-browser.png"
        xdotool key Escape; sleep 0.5; xdotool key ctrl+q
        for _ in $(seq 40); do kill -0 $rack 2>/dev/null || break; sleep 0.5; done
        kill $rack 2>/dev/null; wait $rack; echo $? > "/logs/$2-window.rc"
    ' _ "$user" "$slug" "$name"
    cp "$user/log.txt" "/logs/$slug-window.log" 2>/dev/null || touch "/logs/$slug-window.log"
    check "$slug-window: Rack drew its window and quit cleanly" test "$(cat "/logs/$slug-window.rc" 2>/dev/null)" = 0
    check "$slug-window: no module widget failed" lacks "/logs/$slug-window.log" "could not create its module widget"
    check "$slug-window: no Python error" lacks "/logs/$slug-window.log" "Traceback"
    check "$slug-window: Python finalized" has "/logs/$slug-window.log" "Py_FinalizeEx returned 0"
done

# Failures a user can meet, each reported in Rack's log while Rack goes on.
first=${plugins[0]}; second=${plugins[1]}
slug1=${first%%-*}
user=/tmp/users/two
fresh_user "$user" "$first" "$second"
headless "$user" two-plugins
check "two RackPy plugins: one is refused, by name" has /logs/two-plugins.log "a CPython runtime is already loaded in this process"
check "two RackPy plugins: Rack exits cleanly" test "$(cat /logs/two-plugins.rc)" = 0

user=/tmp/users/mismatch
fresh_user "$user" "$first"
headless "$user" extract  # installs the package
sed -i 's/"abi": *[0-9]*/"abi": 999/' "$user/plugins-lin-x64/$slug1/rackpy-loader.json"
headless "$user" abi-mismatch
check "a loader of another RackPy version: refused with the reason" has /logs/abi-mismatch.log '"abi" must be'
check "a loader of another RackPy version: Rack exits cleanly" test "$(cat /logs/abi-mismatch.rc)" = 0

fresh_user "$user" "$first"
headless "$user" extract
rm "$user/plugins-lin-x64/$slug1/python/lib/python3.14/os.py"
headless "$user" runtime-missing
check "an incomplete Python runtime: refused with the reason" has /logs/runtime-missing.log "bundled Python runtime missing or incomplete"

fresh_user "$user" "$first"
headless "$user" extract
entry=$(jq -r .entry "$user/plugins-lin-x64/$slug1/rackpy-loader.json")
echo 'raise RuntimeError("deliberately broken entry package")' >> "$user/plugins-lin-x64/$slug1/pythonpath/$entry/__init__.py"
headless "$user" entry-fails
check "a failing entry package: its traceback is logged" has /logs/entry-fails.log "deliberately broken entry package"
check "a failing entry package: host initialization fails, named" has /logs/entry-fails.log "RackPy host initialization failed (3)"
check "a failing entry package: Rack exits cleanly" test "$(cat /logs/entry-fails.rc)" = 0

# Upgrade and uninstall as docs/installation/linux.md says: remove the folder, install the package again; then
# remove the folder for good. The patch keeps the modules across the upgrade. After the uninstall the patch is
# emptied first: Rack's dialog about uninstalled modules would wait for an answer that nobody gives here.
file=${plugins[0]}; slug=$slug1
mapfile -t models < <(jq -r --arg f "$file" '.artifacts[] | select(.file == $f) | .models[]' "$manifest")
user=/tmp/users/upgrade
fresh_user "$user" "$file"
version=${file#"$slug"-}; version=${version%%-lin-x64.vcvplugin}
patch_of "$user" "$slug" "$version" "${models[@]}"
headless "$user" before-upgrade
rm -rf "$user/plugins-lin-x64/$slug"
cp "/artifacts/$file" "$user/plugins-lin-x64/"
headless "$user" upgrade
clean_run upgrade "$slug"
check "upgrade: the patch still has its ${#models[@]} modules" test "$(jq '.modules | length' "$user/autosave/patch.json")" = "${#models[@]}"
rm -rf "$user/plugins-lin-x64/$slug"
patch_of "$user" "$slug" "$version"
headless "$user" uninstall
check "uninstall: Rack exits cleanly" test "$(cat /logs/uninstall.rc)" = 0
check "uninstall: RackPy is gone" lacks /logs/uninstall.log "RackPy"

note ""
if [ "$failed" = 0 ]; then note "RESULT: all checks passed"; else note "RESULT: some checks FAILED"; fi
exit "$failed"
