# SPDX-License-Identifier: GPL-3.0-or-later
# RP-41: installs a Windows release candidate in fresh Rack user folders and runs it in Rack Free 2.6.6, as
# bin/rackpy-release verify does on Windows. Arguments: the artifacts folder (bin/rackpy-release build's
# DEST/artifacts), the Rack folder (Rack2Free, unpacked from Rack's installer) and the logs folder;
# logs/summary.txt says what passed. bin/rackpy-release runs it with a minimal environment (PATH: Windows'
# own folders only), so no developer tool or Python on PATH can reach Rack. Windows PowerShell 5.1: every
# Windows 10 machine has it. A CI runner is not a clean PC: the loaded-DLL check shows which DLLs Rack used.
param(
    [Parameter(Mandatory = $true)][string]$Artifacts,
    [Parameter(Mandatory = $true)][string]$RackDir,
    [Parameter(Mandatory = $true)][string]$Logs
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version 3.0

$Artifacts = (Resolve-Path $Artifacts).Path
$RackDir = (Resolve-Path $RackDir).Path
$Logs = (Resolve-Path $Logs).Path
$Rack = Join-Path $RackDir "Rack.exe"
$Platform = "win-x64"
$Plugins = "plugins-$Platform"
$Users = Join-Path ([IO.Path]::GetTempPath()) ("rackpy-verify-" + [Guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Path $Users | Out-Null
$Summary = Join-Path $Logs "summary.txt"
[IO.File]::WriteAllText($Summary, "")
$script:failed = 0
$script:openGates = 0
$Utf8 = New-Object System.Text.UTF8Encoding($false)  # JSON for Rack: UTF-8 without a byte order mark

function Note([string]$text) {
    Write-Host $text
    [IO.File]::AppendAllText($Summary, $text + "`r`n", $Utf8)
}
function Check([string]$what, [scriptblock]$test) {
    $ok = $false
    try { $ok = [bool](& $test) } catch { $ok = $false }
    if ($ok) { Note "ok    $what" } else { Note "FAIL  $what"; $script:failed = 1 }
}
function Gate([string]$what) {
    Note "OPEN  $what"
    $script:openGates++
    if ($env:GITHUB_ACTIONS) { Write-Host "::warning title=RP-41 open gate::$what" }
}
function ReadText([string]$path) {
    # Rack keeps log.txt open for writing: read it as a log viewer does, sharing it, or Windows refuses.
    if (-not (Test-Path -LiteralPath $path)) { return "" }
    $stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]"ReadWrite, Delete")
    try { (New-Object IO.StreamReader($stream)).ReadToEnd() } finally { $stream.Dispose() }
}
function Has([string]$path, [string]$text) { (ReadText $path).Contains($text) }
function WriteJson([string]$path, [string]$text) { [IO.File]::WriteAllText($path, $text, $Utf8) }

# The DLLs a process has loaded, as the loader records them (PSAPI's EnumProcessModulesEx and
# GetModuleFileNameExW, full UTF-16 paths of any length), compiled by Windows PowerShell's own C# compiler.
# Process.Modules gave no file names here, and Toolhelp snapshots gave paths outside the ANSI code page with
# '?' in them (RP-41, runs 41288915853 and 41302010771).
Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class RackPyModules {
    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("psapi.dll", SetLastError = true)]
    static extern bool EnumProcessModulesEx(IntPtr process, [Out] IntPtr[] modules, int size, out int needed, uint filter);
    [DllImport("psapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern int GetModuleFileNameExW(IntPtr process, IntPtr module, StringBuilder name, int size);
    public static string[] Modules(int pid) {
        IntPtr process = OpenProcess(0x0410, false, pid);  // PROCESS_QUERY_INFORMATION | PROCESS_VM_READ
        if (process == IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        try {
            IntPtr[] modules = new IntPtr[1024];
            int needed;
            int size = modules.Length * IntPtr.Size;
            if (!EnumProcessModulesEx(process, modules, size, out needed, 0x3))  // LIST_MODULES_ALL
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            if (needed > size) {
                modules = new IntPtr[needed / IntPtr.Size];
                if (!EnumProcessModulesEx(process, modules, needed, out needed, 0x3))
                    throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            }
            var result = new List<string>();
            var name = new StringBuilder(32768);
            for (int i = 0; i < Math.Min(modules.Length, needed / IntPtr.Size); i++) {
                int length = GetModuleFileNameExW(process, modules[i], name, name.Capacity);
                if (length > 0) result.Add(name.ToString(0, length));
            }
            return result.ToArray();
        } finally { CloseHandle(process); }
    }
}
"@

$os = Get-CimInstance Win32_OperatingSystem
Note "machine: $($os.Caption) $($os.Version) (build $($os.BuildNumber)), $env:PROCESSOR_ARCHITECTURE, $Platform"
Note "Rack: $((Get-Item $Rack).VersionInfo.ProductVersion) at $RackDir"
Note "environment: $((Get-ChildItem env: | ForEach-Object Name | Sort-Object) -join ' ')"
Note "PATH: $env:PATH"
$redist = Test-Path (Join-Path $env:SystemRoot "System32\vcruntime140.dll")
Note "info  System32\vcruntime140.dll present (a Visual C++ redistributable is installed): $redist"

# Checksums.
$bad = @()
foreach ($line in [IO.File]::ReadAllLines((Join-Path $Artifacts "SHA256SUMS"))) {
    if (-not $line) { continue }
    $sum, $name = $line -split "  ", 2
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $Artifacts $name)).Hash.ToLower()
    if ($actual -ne $sum) { $bad += $name }
}
Check "every artifact matches SHA256SUMS" { $bad.Count -eq 0 }

function FreshUser([string]$user, [string[]]$packages) {
    if (Test-Path -LiteralPath $user) { Remove-Item -LiteralPath $user -Recurse -Force }
    New-Item -ItemType Directory -Path (Join-Path $user $Plugins) | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $user "autosave") | Out-Null
    foreach ($p in $packages) { Copy-Item -LiteralPath (Join-Path $Artifacts $p) -Destination (Join-Path $user $Plugins) }
    WriteJson (Join-Path $user "settings.json") '{"showTipsOnLaunch": false}'
}
function PatchOf([string]$user, [string]$slug, [string]$version, [string[]]$models) {
    $modules = @()
    $id = 1
    foreach ($m in $models) {
        $modules += "{""id"": $id, ""plugin"": ""$slug"", ""model"": ""$m"", ""version"": ""$version"", ""params"": [], ""pos"": [$(($id - 1) * 14), 0]}"
        $id++
    }
    WriteJson (Join-Path $user "autosave\patch.json") ("{""version"": ""2.6.6"", ""modules"": [" + ($modules -join ", ") + "], ""cables"": []}")
}

# Rack headless with user folder USER; quits through stdin once the patch has run for 3 s. Records the log,
# the exit status and the DLLs the process had loaded. WORKDIR and EXTRAPATH: a current directory and PATH
# entries to start Rack with (the search-path cases).
function Headless([string]$user, [string]$name, [string]$workDir = "", [string]$extraPath = "") {
    $log = Join-Path $user "log.txt"
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Rack
    $info.Arguments = "-h -u ""$user"""
    $info.WorkingDirectory = if ($workDir) { $workDir } else { $RackDir }
    $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    if ($extraPath) { $info.EnvironmentVariables["PATH"] = "$extraPath;$env:PATH" }
    $proc = [Diagnostics.Process]::Start($info)
    $stdout = $proc.StandardOutput.ReadToEndAsync()
    $stderr = $proc.StandardError.ReadToEndAsync()
    # Every DLL seen in module lists taken while Rack runs. One list after the autosave is not enough:
    # here headless Rack can pass "Press enter to exit" at once and be gone (RP-41, run 41292904958).
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $snapError = $null
    $loaded = $null
    $deadline = (Get-Date).AddSeconds(120)
    while ((Get-Date) -lt $deadline -and -not $proc.HasExited) {
        try { foreach ($image in [RackPyModules]::Modules($proc.Id)) { [void]$seen.Add($image) } }
        catch { $snapError = "$_" }
        if ($null -eq $loaded) { if ((ReadText $log).Contains("Loading autosave")) { $loaded = (Get-Date).AddSeconds(3) } }
        elseif ((Get-Date) -gt $loaded) { break }
        Start-Sleep -Milliseconds 100
    }
    $images = if ($seen.Count) { [string[]]$seen } else { @("(modules unavailable: $snapError)") }
    [IO.File]::WriteAllLines((Join-Path $Logs "$name.images"), [string[]]($images | Sort-Object -Unique))
    try { $proc.StandardInput.WriteLine(""); $proc.StandardInput.Close() } catch {}
    if (-not $proc.WaitForExit(60000)) { $proc.Kill(); $proc.WaitForExit(); $rc = "killed" } else { $rc = [string]$proc.ExitCode }
    [IO.File]::WriteAllText((Join-Path $Logs "$name.rc"), $rc)
    [IO.File]::WriteAllText((Join-Path $Logs "$name.out"), $stdout.Result + $stderr.Result)
    if (Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination (Join-Path $Logs "$name.log") -Force }
    else { [IO.File]::WriteAllText((Join-Path $Logs "$name.log"), "") }
}
function Rc([string]$name) { ReadText (Join-Path $Logs "$name.rc") }
function Images([string]$name) { [IO.File]::ReadAllLines((Join-Path $Logs "$name.images")) }
function OwnPython([string]$name, [string]$user) {
    # Exactly one CPython DLL (python314.dll), the plugin's bundled one; python3.dll, CPython's stable-ABI
    # forwarder that extension modules such as _ctypes.pyd load, must be the bundled one too.
    $prefix = (Join-Path $user $Plugins) + "\"
    $pythons = @(Images $name | Where-Object { (Split-Path $_ -Leaf) -match '^python3\d*\.dll$' })
    $versioned = @($pythons | Where-Object { (Split-Path $_ -Leaf) -match '^python3\d+\.dll$' })
    $outside = @($pythons | Where-Object { -not $_.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) })
    ($versioned.Count -eq 1) -and ($outside.Count -eq 0)
}
function ForeignImages([string]$name, [string]$user) {
    # Every DLL Rack loaded comes from Rack's folder, the user folder (plugins) or Windows.
    # Parenthesized: a comma binds tighter than +, so without them this is one string (RP-41, run 41298939078).
    $allowed = @(($RackDir + "\"), ($user + "\"), ($env:SystemRoot + "\"))
    $outside = @(Images $name | Where-Object { $img = $_; -not ($allowed | Where-Object { $img.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) }) })
    if ($outside.Count) { [IO.File]::WriteAllLines((Join-Path $Logs "$name.foreign"), [string[]]$outside) }
    $outside.Count -eq 0
}
function CleanRun([string]$name, [string]$slug, [string]$user = "") {
    $log = Join-Path $Logs "$name.log"
    Check "${name}: Rack exited cleanly" { (Rc $name) -eq "0" }
    Check "${name}: RackPy started" { Has $log "RackPy interpreter started: Python 3.14.9" }
    Check "${name}: loaded $slug" { Has $log "Loaded plugin $slug " }
    Check "${name}: no module failed" { -not (Has $log "could not create its module") }
    Check "${name}: no Python error" { -not (Has $log "Traceback") }
    Check "${name}: Python finalized" { Has $log "Py_FinalizeEx returned 0" }
    if ($user) {
        Check "${name}: the only Python Rack loaded is the plugin's bundled runtime" { OwnPython $name $user }
        Check "${name}: every DLL Rack loaded is Rack's, the plugin's or Windows'" { ForeignImages $name $user }
    }
}

# Whether Rack gets an OpenGL window here: Rack windowed on an empty user folder, then killed. Windows'
# own OpenGL is 1.1; Rack needs 2.0, which only a GPU driver (or Mesa) provides.
function GlWindows {
    $user = Join-Path $Users "gl-probe"
    New-Item -ItemType Directory -Path $user -Force | Out-Null
    WriteJson (Join-Path $user "settings.json") '{"showTipsOnLaunch": false}'
    $proc = Start-Process -FilePath $Rack -ArgumentList "-u", """$user""" -WorkingDirectory $RackDir -PassThru
    $result = $null
    $deadline = (Get-Date).AddSeconds(30)
    while ($null -eq $result -and (Get-Date) -lt $deadline -and -not $proc.HasExited) {
        $text = ReadText (Join-Path $user "log.txt")
        if ($text.Contains("Renderer: ")) { $result = $true }
        elseif ($text -match "WGL: |Could not create Window") { $result = $false }
        Start-Sleep -Milliseconds 200
    }
    if (-not $proc.HasExited) { $proc.Kill() }
    Copy-Item -LiteralPath (Join-Path $user "log.txt") -Destination (Join-Path $Logs "gl-probe.log") -ErrorAction SilentlyContinue
    [bool]$result
}
$GL = GlWindows
if ($GL) { Note "windowed: Rack opens OpenGL windows here" }
else { Note "windowed: Rack gets no OpenGL window here: $(((ReadText (Join-Path $Logs 'gl-probe.log')) -split "`n" | Select-String 'GLFW error|WGL' | Select-Object -First 1))" }

$manifest = [IO.File]::ReadAllText((Join-Path $Artifacts "manifest.json")) | ConvertFrom-Json
$examples = @($manifest.artifacts | Where-Object { $_.PSObject.Properties.Name -contains "slug" })
Note "example plugins: $($examples.Count)"
foreach ($a in $examples) {
    $slug = $a.slug
    $file = $a.file
    $version = $file.Substring($slug.Length + 1).Replace("-$Platform.vcvplugin", "")
    $models = [string[]]$a.models
    $user = Join-Path $Users $slug
    FreshUser $user @($file)
    PatchOf $user $slug $version $models
    Headless $user "$slug-1"
    CleanRun "$slug-1" $slug $user
    Check "${slug}-1: Rack extracted the package" { Test-Path (Join-Path $user "$Plugins\$slug\plugin.json") }
    $expected = ($models | Sort-Object) -join ","
    $saved = { ((([IO.File]::ReadAllText((Join-Path $user "autosave\patch.json")) | ConvertFrom-Json).modules | ForEach-Object model) | Sort-Object) -join "," }
    Check "${slug}-1: the autosave keeps all $($models.Count) modules" { (& $saved) -eq $expected }
    Headless $user "$slug-2"  # reopens the first run's autosave
    CleanRun "$slug-2" $slug
    Check "${slug}-2: reopened, all $($models.Count) modules still there" { (& $saved) -eq $expected }
    if (-not $GL) {
        Gate "$slug-window: windowed rendering of $($models.Count) model(s) not run: Rack gets no OpenGL 2.0 window on this PC"
        continue
    }
    # Rack's screenshot mode (-t): loads the patch, draws every model's widget to screenshots\<plugin>\<model>.png, quits.
    $shots = Join-Path $user "screenshots"
    if (Test-Path $shots) { Remove-Item $shots -Recurse -Force }
    $proc = Start-Process -FilePath $Rack -ArgumentList "-u", """$user""", "-t", "1" -WorkingDirectory $RackDir -PassThru
    if (-not $proc.WaitForExit(300000)) { $proc.Kill(); $rc = "killed" } else { $rc = [string]$proc.ExitCode }
    Copy-Item -LiteralPath (Join-Path $user "log.txt") -Destination (Join-Path $Logs "$slug-window.log") -Force
    Check "${slug}-window: Rack drew its window and quit cleanly" { $rc -eq "0" }
    foreach ($m in $models) { Check "${slug}-window: drew $m" { (Get-Item (Join-Path $shots "$slug\$m.png")).Length -gt 0 } }
    Check "${slug}-window: no Python error" { -not (Has (Join-Path $Logs "$slug-window.log") "Traceback") }
}

# Failures a user can meet, each reported in Rack's log while Rack goes on.
$first = $examples[0].file
$second = $examples[1].file
$slug1 = $examples[0].slug
$plugin1 = { Join-Path $user "$Plugins\$slug1" }
$user = Join-Path $Users "two"
FreshUser $user @($first, $second)
Headless $user "two-plugins"
Check "two RackPy plugins: one is refused, by name" { Has (Join-Path $Logs "two-plugins.log") "a CPython runtime is already loaded in this process" }
Check "two RackPy plugins: Rack exits cleanly" { (Rc "two-plugins") -eq "0" }

$user = Join-Path $Users "cases"
function Installed { FreshUser $user @($first); Headless $user "extract" }  # Rack unpacks the package

Installed
$config = Join-Path (& $plugin1) "rackpy-loader.json"
WriteJson $config ((ReadText $config) -replace '"abi":\s*\d+', '"abi": 999')
Headless $user "abi-mismatch"
Check "a loader of another RackPy version: refused with the reason" { Has (Join-Path $Logs "abi-mismatch.log") '"abi" must be' }
Check "a loader of another RackPy version: Rack exits cleanly" { (Rc "abi-mismatch") -eq "0" }

Installed
Remove-Item (Join-Path (& $plugin1) "python\Lib\os.py")
Headless $user "runtime-missing"
Check "an incomplete Python runtime: refused with the reason" { Has (Join-Path $Logs "runtime-missing.log") "bundled Python runtime missing or incomplete" }
Check "an incomplete Python runtime: Rack exits cleanly" { (Rc "runtime-missing") -eq "0" }

# Windows' own cases. A missing DLL the runtime needs: vcruntime140.dll removed from the plugin's runtime.
# Where Windows has its own copy (a Visual C++ redistributable in System32), that one serves instead.
Installed
Remove-Item (Join-Path (& $plugin1) "python\vcruntime140.dll")
Headless $user "dll-missing"
if ($redist) {
    Note "info  a missing vcruntime140.dll: System32 has one, so this PC cannot show the failure"
    CleanRun "dll-missing" $slug1
} else {
    Check "a missing DLL: refused, naming python314.dll and Windows' reason" { (ReadText (Join-Path $Logs "dll-missing.log")) -match "cannot load .*python314\.dll: .*\(Windows error 126\)" }
    Check "a missing DLL: Rack exits cleanly" { (Rc "dll-missing") -eq "0" }
}

# An unloadable runtime: python314.dll replaced by a file that is no DLL (a damaged or foreign download).
Installed
$dll = Join-Path (& $plugin1) "python\python314.dll"
[IO.File]::WriteAllBytes($dll, [byte[]](1..64))
Headless $user "python-invalid"
Check "a damaged python314.dll: refused with Windows' reason" { (ReadText (Join-Path $Logs "python-invalid.log")) -match "cannot load .*python314\.dll: .*\(Windows error 193\)" }
Check "a damaged python314.dll: Rack exits cleanly" { (Rc "python-invalid") -eq "0" }

# The DLL search: a decoy python314.dll (not a DLL at all) and vcruntime140.dll in Rack's current directory and
# on PATH must never be used: the loader names its own runtime by full path and searches only its folder and
# System32 for that runtime's dependencies.
Installed
$decoy = Join-Path $Users "decoy"
New-Item -ItemType Directory -Path $decoy -Force | Out-Null
foreach ($n in @("python314.dll", "vcruntime140.dll", "python3.dll")) { [IO.File]::WriteAllBytes((Join-Path $decoy $n), [byte[]](1..64)) }
Headless $user "decoys" -workDir $decoy -extraPath $decoy
CleanRun "decoys" $slug1 $user
Check "decoys: no DLL from the current directory or PATH was loaded" { -not (Images "decoys" | Where-Object { $_.StartsWith($decoy, [StringComparison]::OrdinalIgnoreCase) }) }

# A permission failure: python314.dll unreadable for this user.
Installed
$dll = Join-Path (& $plugin1) "python\python314.dll"
$who = [Security.Principal.WindowsIdentity]::GetCurrent().Name
& icacls $dll /deny "${who}:(RX)" | Out-Null
Headless $user "permission"
& icacls $dll /remove:d $who | Out-Null
Check "an unreadable python314.dll: refused with Windows' reason" { (ReadText (Join-Path $Logs "permission.log")) -match "cannot load .*python314\.dll: .*\(Windows error 5\)" }
Check "an unreadable python314.dll: Rack exits cleanly" { (Rc "permission") -eq "0" }

# The Mark of the Web, as a browser leaves on a download (a Zone.Identifier stream, zone 3: the Internet).
# The package itself: Rack extracts it with its own code. Then every file of the extracted folder marked, as
# extracting a downloaded zip with Explorer would. What Windows does then is recorded, not assumed.
$motw = "[ZoneTransfer]`r`nZoneId=3`r`nReferrerUrl=https://example.invalid/`r`n"
FreshUser $user @($first)
Set-Content -LiteralPath (Join-Path $user "$Plugins\$first") -Stream Zone.Identifier -Value $motw
Headless $user "motw-package"
CleanRun "motw-package" $slug1
$marked = @(Get-ChildItem -LiteralPath (& $plugin1) -Recurse -File | Where-Object { Get-Item -LiteralPath $_.FullName -Stream Zone.Identifier -ErrorAction SilentlyContinue })
Note "info  a downloaded package: $($marked.Count) extracted file(s) carry the Mark of the Web"
Get-ChildItem -LiteralPath (& $plugin1) -Recurse -File | ForEach-Object { Set-Content -LiteralPath $_.FullName -Stream Zone.Identifier -Value $motw }
Headless $user "motw-folder"
$loaded = Has (Join-Path $Logs "motw-folder.log") "Loaded plugin $slug1 "
Note "info  a folder whose every file has the Mark of the Web: Rack's exit status $(Rc 'motw-folder'); plugin loaded: $loaded; RackPy started: $(Has (Join-Path $Logs 'motw-folder.log') 'RackPy interpreter started')"

# A user folder whose path is not ASCII: Rack passes RackPy UTF-8 paths, and the loader turns them into UTF-16.
# Built from code points: Windows PowerShell 5.1 reads a script without a byte order mark as ANSI, so it stays ASCII.
$unicodeName = -join ([char[]](0x00DC, 0x006E, 0x00EF, 0x0063, 0x00F8, 0x0064, 0x00E9, 0x0020, 0x97F3, 0x697D, 0x0020, 0x0391, 0x03A9))
$user = Join-Path $Users $unicodeName
FreshUser $user @($first)
PatchOf $user $slug1 $examples[0].file.Substring($slug1.Length + 1).Replace("-$Platform.vcvplugin", "") ([string[]]$examples[0].models)
Headless $user "unicode"
CleanRun "unicode" $slug1 $user

# A user folder whose path is longer than Windows' classic 260-character limit: recorded, not asserted, since
# Rack itself and Windows' long-path setting decide it.
$long = Join-Path $Users ("long-" + ("x" * 60) + "\" + ("y" * 60) + "\" + ("z" * 60) + "\" + ("w" * 60))
try {
    FreshUser $long @($first)
    Headless $long "long-path"
    $loaded = Has (Join-Path $Logs "long-path.log") "Loaded plugin $slug1 "
    Note "info  a $(($long + "\$Plugins\$slug1\python\python314.dll").Length)-character runtime path: Rack's exit status $(Rc 'long-path'); plugin loaded: $loaded"
} catch { Note "info  a long user-folder path: could not be set up here ($_)" }
$longPaths = (Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name LongPathsEnabled -ErrorAction SilentlyContinue).LongPathsEnabled
Note "info  LongPathsEnabled: $longPaths"

# Upgrade and uninstall as docs/installation/windows.md says: remove the folder, install the package again;
# then remove the folder for good. After the uninstall the patch is emptied first: Rack's dialog about
# missing modules would wait for an answer nobody gives here.
$a = $examples[0]
$models = [string[]]$a.models
$version = $a.file.Substring($slug1.Length + 1).Replace("-$Platform.vcvplugin", "")
$user = Join-Path $Users "upgrade"
FreshUser $user @($a.file)
PatchOf $user $slug1 $version $models
Headless $user "before-upgrade"
Remove-Item -LiteralPath (Join-Path $user "$Plugins\$slug1") -Recurse -Force
Copy-Item -LiteralPath (Join-Path $Artifacts $a.file) -Destination (Join-Path $user $Plugins)
Headless $user "upgrade"
CleanRun "upgrade" $slug1 $user
Check "upgrade: the patch still has its $($models.Count) modules" { @(([IO.File]::ReadAllText((Join-Path $user "autosave\patch.json")) | ConvertFrom-Json).modules).Count -eq $models.Count }
Remove-Item -LiteralPath (Join-Path $user "$Plugins\$slug1") -Recurse -Force
PatchOf $user $slug1 $version @()
Headless $user "uninstall"
Check "uninstall: Rack exits cleanly" { (Rc "uninstall") -eq "0" }
Check "uninstall: RackPy is gone" { -not (Has (Join-Path $Logs "uninstall.log") "RackPy") }

Remove-Item -LiteralPath $Users -Recurse -Force -ErrorAction SilentlyContinue
Note ""
if ($script:failed) { Note "RESULT: some checks FAILED" }
elseif ($script:openGates) { Note "RESULT: every check run here passed; $($script:openGates) OPEN (not run on this machine, not passed)" }
else { Note "RESULT: all checks passed" }
exit $script:failed
