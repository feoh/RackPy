#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# RP-40: installs a macOS release candidate in fresh Rack user folders and runs it in Rack Free 2.6.6, as
# bin/rackpy-release verify does on a Mac. Arguments: the artifacts folder (bin/rackpy-release build's
# DEST/artifacts), the Rack app ("VCV Rack 2 Free.app") and the logs folder; /logs/summary.txt says what
# passed. bin/rackpy-release runs it with an empty environment, so no developer search path reaches Rack.
# A CI runner is not a clean Mac: the loaded-image check below shows which libraries Rack actually used.
# macOS's /bin/bash is 3.2: no mapfile, no associative arrays.
set -uo pipefail
ARTIFACTS=$1
APP=$(cd "$2" && pwd -P)  # lsof reports real paths: /private/tmp for /tmp
LOGS=$3
RACK="$APP/Contents/MacOS/Rack"
JQ=${JQ:-jq}
case "$(uname -m)" in arm64) PLATFORM=mac-arm64 ;; x86_64) PLATFORM=mac-x64 ;; *) echo "unknown CPU"; exit 2 ;; esac
PLUGINS=plugins-$PLATFORM
USERS=$(cd "$(mktemp -d /tmp/rackpy-verify-users.XXXXXX)" && pwd -P)
SUMMARY=$LOGS/summary.txt
: > "$SUMMARY"
failed=0
note() { echo "$*" | tee -a "$SUMMARY"; }
check() {  # check "description" command...
    local what=$1; shift
    if "$@" > /dev/null 2>&1; then note "ok    $what"; else note "FAIL  $what"; failed=1; fi
}
has() { grep -qF -- "$2" "$1"; }
lacks() { ! grep -qF -- "$2" "$1"; }
jqr() { "$JQ" "$@"; }

note "machine: macOS $(sw_vers -productVersion) ($(sw_vers -buildVersion)), $(uname -m), $PLATFORM"
note "Rack: $(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$APP/Contents/Info.plist" 2>/dev/null)"
note "environment: $(env | cut -d= -f1 | sort | tr '\n' ' ')"
(cd "$ARTIFACTS" && shasum -a 256 -c SHA256SUMS) > "$LOGS/sha256sum.txt" 2>&1
check "every artifact matches SHA256SUMS" grep -q ": OK" "$LOGS/sha256sum.txt"
check "no artifact fails SHA256SUMS" lacks "$LOGS/sha256sum.txt" "FAILED"

fresh_user() {  # $1: user folder; the .vcvplugin files after it are installed as Rack installs a download
    local user=$1; shift
    rm -rf "$user"; mkdir -p "$user/$PLUGINS" "$user/autosave"
    for p in "$@"; do cp "$ARTIFACTS/$p" "$user/$PLUGINS/"; done
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
watchdog() {  # $1: pid, $2: seconds; kills the process if it is still running then (macOS has no timeout(1))
    ( sleep "$2"; kill -9 "$1" 2>/dev/null ) > /dev/null 2>&1 &
    echo $!
}
images() {  # $1: pid, $2: file: the executable images (txt) the process has mapped
    lsof -a -p "$1" -d txt -Fn 2>/dev/null | sed -n 's/^n//p' | sort -u > "$2"
}
headless() {  # $1: user folder, $2: name for the logs. Quits through stdin once the patch has run for 3 s.
    local user=$1 name=$2 fifo dog
    fifo=$(mktemp -u /tmp/rackpy-fifo.XXXXXX); mkfifo "$fifo"
    rm -f "$user/log.txt"
    "$RACK" -h -u "$user" < "$fifo" > "$LOGS/$name.out" 2>&1 &
    local rack=$!
    dog=$(watchdog $rack 180)
    exec 9> "$fifo"
    for _ in $(seq 600); do
        grep -qF "Loading autosave" "$user/log.txt" 2>/dev/null && break
        kill -0 $rack 2>/dev/null || break
        sleep 0.1
    done
    sleep 3
    images $rack "$LOGS/$name.images"
    echo >&9; exec 9>&-
    wait $rack; echo $? > "$LOGS/$name.rc"
    kill "$dog" 2>/dev/null
    rm -f "$fifo"
    cp "$user/log.txt" "$LOGS/$name.log" 2>/dev/null || touch "$LOGS/$name.log"
}
own_python() {  # $1: images file, $2: user folder: every Python image Rack mapped is the plugin's own runtime
    local found
    found=$(grep -i python "$1") || return 1
    ! echo "$found" | grep -vqF "$2/$PLUGINS/"
}
foreign_images() {  # $1: images file: apart from Rack's app and the user folders, none from Homebrew,
    # /usr/local, /Library/Frameworks (python.org's Python) or a home folder
    ! grep -vF -e "$APP/" -e "$USERS/" "$1" | grep -qE "^(/opt/homebrew|/usr/local|/Library/Frameworks|/Users/)"
}
clean_run() {  # $1: log name, $2: slug, $3: user folder: the plugin loaded, nothing failed, Python finalized
    local log=$LOGS/$1.log
    check "$1: Rack exited cleanly" test "$(cat "$LOGS/$1.rc")" = 0
    check "$1: RackPy started" has "$log" "RackPy interpreter started: Python 3.14.9"
    check "$1: loaded $2" has "$log" "Loaded plugin $2 "
    check "$1: no module failed" lacks "$log" "could not create its module"
    check "$1: no Python error" lacks "$log" "Traceback"
    check "$1: Python finalized" has "$log" "Py_FinalizeEx returned 0"
    if [ -n "${3:-}" ]; then
        check "$1: the only Python Rack loaded is the plugin's bundled runtime" own_python "$LOGS/$1.images" "$3"
        check "$1: no library from Homebrew, /usr/local, a Python framework or a home folder" \
            foreign_images "$LOGS/$1.images"
    fi
}

manifest=$ARTIFACTS/manifest.json
plugins=()
while IFS= read -r line; do plugins+=("$line"); done < <(jqr -r '.artifacts[] | select(.slug) | .file' "$manifest")
note "example plugins: ${#plugins[@]}"
for file in "${plugins[@]}"; do
    slug=$(jqr -r --arg f "$file" '.artifacts[] | select(.file == $f) | .slug' "$manifest")
    version=${file#"$slug"-}; version=${version%%-"$PLATFORM".vcvplugin}
    models=()
    while IFS= read -r line; do models+=("$line"); done < <(jqr -r --arg f "$file" '.artifacts[] | select(.file == $f) | .models[]' "$manifest")
    user=$USERS/$slug
    fresh_user "$user" "$file"
    patch_of "$user" "$slug" "$version" "${models[@]}"
    headless "$user" "$slug-1"
    clean_run "$slug-1" "$slug" "$user"
    check "$slug-1: Rack extracted the package" test -f "$user/$PLUGINS/$slug/plugin.json"
    saved=$(jqr -c '[.modules[] | .model] | sort' "$user/autosave/patch.json" 2>/dev/null)
    expected=$(printf '%s\n' "${models[@]}" | jqr -R . | jqr -cs 'sort')
    check "$slug-1: the autosave keeps all ${#models[@]} modules" test "$saved" = "$expected"
    headless "$user" "$slug-2"  # reopens the first run's autosave
    clean_run "$slug-2" "$slug"
    saved=$(jqr -c '[.modules[] | .model] | sort' "$user/autosave/patch.json" 2>/dev/null)
    check "$slug-2: reopened, all ${#models[@]} modules still there" test "$saved" = "$expected"
    # In a window: Rack's screenshot mode (-t) loads the patch, then draws every model's widget without a
    # module, as the module browser's preview does, to screenshots/<plugin>/<model>.png with OpenGL, and
    # quits. Unlike keystrokes into the browser, it needs no Accessibility permission.
    rm -f "$user/log.txt"; rm -rf "$user/screenshots"
    "$RACK" -u "$user" -t 1 > "$LOGS/$slug-window.out" 2>&1 &
    rack=$!; dog=$(watchdog $rack 300)
    wait $rack; echo $? > "$LOGS/$slug-window.rc"; kill "$dog" 2>/dev/null
    cp "$user/log.txt" "$LOGS/$slug-window.log" 2>/dev/null || touch "$LOGS/$slug-window.log"
    mkdir -p "$LOGS/screenshots"
    cp -R "$user/screenshots/$slug" "$LOGS/screenshots/" 2>/dev/null
    check "$slug-window: Rack drew its window and quit cleanly" test "$(cat "$LOGS/$slug-window.rc")" = 0
    for model in "${models[@]}"; do
        check "$slug-window: drew $model" test -s "$user/screenshots/$slug/$model.png"
    done
    check "$slug-window: no module widget failed" lacks "$LOGS/$slug-window.log" "could not create its module widget"
    check "$slug-window: no Python error" lacks "$LOGS/$slug-window.log" "Traceback"
    check "$slug-window: Python finalized" has "$LOGS/$slug-window.log" "Py_FinalizeEx returned 0"
done

# Failures a user can meet, each reported in Rack's log while Rack goes on.
first=${plugins[0]}; second=${plugins[1]}
slug1=${first%%-*}
user=$USERS/two
fresh_user "$user" "$first" "$second"
headless "$user" two-plugins
check "two RackPy plugins: one is refused, by name" has "$LOGS/two-plugins.log" "a CPython runtime is already loaded in this process"
check "two RackPy plugins: Rack exits cleanly" test "$(cat "$LOGS/two-plugins.rc")" = 0

user=$USERS/mismatch
fresh_user "$user" "$first"
headless "$user" extract  # installs the package
sed -i '' -E 's/"abi": *[0-9]+/"abi": 999/' "$user/$PLUGINS/$slug1/rackpy-loader.json"
headless "$user" abi-mismatch
check "a loader of another RackPy version: refused with the reason" has "$LOGS/abi-mismatch.log" '"abi" must be'
check "a loader of another RackPy version: Rack exits cleanly" test "$(cat "$LOGS/abi-mismatch.rc")" = 0

fresh_user "$user" "$first"
headless "$user" extract
rm "$user/$PLUGINS/$slug1/python/lib/python3.14/os.py"
headless "$user" runtime-missing
check "an incomplete Python runtime: refused with the reason" has "$LOGS/runtime-missing.log" "bundled Python runtime missing or incomplete"
check "an incomplete Python runtime: Rack exits cleanly" test "$(cat "$LOGS/runtime-missing.rc")" = 0

fresh_user "$user" "$first"
headless "$user" extract
entry=$(jqr -r .entry "$user/$PLUGINS/$slug1/rackpy-loader.json")
echo 'raise RuntimeError("deliberately broken entry package")' >> "$user/$PLUGINS/$slug1/pythonpath/$entry/__init__.py"
headless "$user" entry-fails
check "a failing entry package: its traceback is logged" has "$LOGS/entry-fails.log" "deliberately broken entry package"
check "a failing entry package: host initialization fails, named" has "$LOGS/entry-fails.log" "RackPy host initialization failed (3)"
check "a failing entry package: Rack exits cleanly" test "$(cat "$LOGS/entry-fails.rc")" = 0

# macOS's own cases. Each records what Rack does; docs/installation/macos.md gives the recovery.
# 1. The wrong architecture: a package for the other Mac CPU in this one's plugins folder (Rack extracts any
# .vcvplugin there). ALIEN is a loader built for the other CPU, supplied by bin/rackpy-release verify.
if [ -n "${ALIEN_LOADER:-}" ]; then
    fresh_user "$user" "$first"
    headless "$user" extract
    cp "$ALIEN_LOADER" "$user/$PLUGINS/$slug1/plugin.dylib"
    headless "$user" wrong-architecture
    check "a package for the other CPU: Rack refuses its library, saying why" grep -qiE "incompatible architecture|wrong architecture|mach-o file, but is an incompatible" "$LOGS/wrong-architecture.log"
    check "a package for the other CPU: Rack exits cleanly" test "$(cat "$LOGS/wrong-architecture.rc")" = 0
else
    note "skip  a package for the other CPU: no ALIEN_LOADER given"
fi

# 2. A broken signature: the loader edited after signing. arm64 macOS runs no code whose signature fails;
# x86_64 macOS does not require one, so there the edited loader may still load. Recorded either way.
fresh_user "$user" "$first"
headless "$user" extract
codesign --remove-signature "$user/$PLUGINS/$slug1/plugin.dylib"
headless "$user" unsigned
note "info  an unsigned loader: Rack's exit status $(cat "$LOGS/unsigned.rc")"
if [ "$PLATFORM" = mac-arm64 ]; then
    check "an unsigned loader on arm64: Rack refuses it" lacks "$LOGS/unsigned.log" "Loaded plugin $slug1 "
else
    if has "$LOGS/unsigned.log" "Loaded plugin $slug1 "; then note "info  an unsigned loader on x86_64: loaded"; else note "info  an unsigned loader on x86_64: refused"; fi
fi
grep -iE "$slug1|signature|code sign" "$LOGS/unsigned.log" | head -5 > "$LOGS/unsigned.excerpt"
codesign --force --sign - "$user/$PLUGINS/$slug1/plugin.dylib" > "$LOGS/resign.out" 2>&1
headless "$user" resigned
clean_run resigned "$slug1"

# 3. Quarantine, as a browser marks a download. The package itself: Rack extracts it with its own code, which
# does not copy the attribute. Then the extracted folder marked too, as unzipping a quarantined folder
# download would, which makes macOS assess each library as it loads.
QUARANTINE="0083;$(printf %x "$(date +%s)");Safari;"
fresh_user "$user" "$first"
xattr -w com.apple.quarantine "$QUARANTINE" "$user/$PLUGINS/$first"
headless "$user" quarantined-package
clean_run quarantined-package "$slug1"
xattr -r "$user/$PLUGINS/$slug1" 2>/dev/null | grep -c com.apple.quarantine > "$LOGS/quarantined-package.xattr-count"
check "a quarantined package: the extracted files are not quarantined" test "$(cat "$LOGS/quarantined-package.xattr-count")" = 0
xattr -r -w com.apple.quarantine "$QUARANTINE" "$user/$PLUGINS/$slug1"
headless "$user" quarantined-folder
note "info  a quarantined folder: Rack's exit status $(cat "$LOGS/quarantined-folder.rc") (137: killed after 180 s)"
if has "$LOGS/quarantined-folder.log" "Loaded plugin $slug1 "; then note "info  a quarantined folder: loaded"; else note "info  a quarantined folder: refused"; fi
grep -iE "$slug1|quarantine|not opened|malware|verify" "$LOGS/quarantined-folder.log" | head -5 > "$LOGS/quarantined-folder.excerpt"
xattr -r -d com.apple.quarantine "$user/$PLUGINS/$slug1"
headless "$user" unquarantined
clean_run unquarantined "$slug1"

# Upgrade and uninstall as docs/installation/macos.md says: remove the folder, install the package again; then
# remove the folder for good. The patch keeps the modules across the upgrade. After the uninstall the patch is
# emptied first: Rack's dialog about uninstalled modules would wait for an answer that nobody gives here.
file=${plugins[0]}; slug=$slug1
models=()
while IFS= read -r line; do models+=("$line"); done < <(jqr -r --arg f "$file" '.artifacts[] | select(.file == $f) | .models[]' "$manifest")
user=$USERS/upgrade
fresh_user "$user" "$file"
version=${file#"$slug"-}; version=${version%%-"$PLATFORM".vcvplugin}
patch_of "$user" "$slug" "$version" "${models[@]}"
headless "$user" before-upgrade
rm -rf "${user:?}/$PLUGINS/$slug"
cp "$ARTIFACTS/$file" "$user/$PLUGINS/"
headless "$user" upgrade
clean_run upgrade "$slug" "$user"
check "upgrade: the patch still has its ${#models[@]} modules" test "$(jqr '.modules | length' "$user/autosave/patch.json")" = "${#models[@]}"
rm -rf "${user:?}/$PLUGINS/$slug"
patch_of "$user" "$slug" "$version"
headless "$user" uninstall
check "uninstall: Rack exits cleanly" test "$(cat "$LOGS/uninstall.rc")" = 0
check "uninstall: RackPy is gone" lacks "$LOGS/uninstall.log" "RackPy"

rm -rf "$USERS"
note ""
if [ "$failed" = 0 ]; then note "RESULT: all checks passed"; else note "RESULT: some checks FAILED"; fi
exit "$failed"
