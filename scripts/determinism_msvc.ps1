# The MSVC leg of metalog's determinism golden proof: build the canon+metalog TOWER's det_harness from
# source at /O2 /fp:fast and write its digest — every corpus file under scripts/determinism_corpus/ and
# every row of scripts/determinism_sections.txt — to digest-msvc.txt at the workspace root: the bytes
# the four Linux legs (scripts/determinism_bitidentity.sh) emit, byte-compared by `compare`.
#
# Run by malf-toolchain's coderoast-golden-proof.yml on its MSVC leg, after `setup-proof-msvc` has
# activated MSVC 14.52, CMake 4.3.x, Ninja and Conan, staged the windows-msvc-release profile and
# global.conf into $env:CONAN_HOME, and checked insight-canon's source out into _canon/. It stays a
# committed script of this repository because WHAT a leg builds and emits is the proof's subject.
#
# WHY this leg emits in PowerShell and not by calling the Linux legs' driver: `bash` on a Windows
# runner is Git Bash only by accident of PATH; insight-eidos documents its self-hosted Windows host
# resolving `bash` to WSL (and Git's `link` shadowing MSVC's link.exe). The section LIST is what is
# shared (scripts/determinism_sections.txt); the loop stays native to each side.

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
Set-Location $env:GITHUB_WORKSPACE

# Export insight_canon (recipe + sources) into the local cache. `conan export` registers canon's
# recipe+exports_sources WITHOUT building; the install below resolves the graph for os=Windows. The
# harness rebuilds canon from source under the cell flags (add_subdirectory), the export only satisfies
# the graph. insight_semantic_github is test_required by metalog and, post-ADR-17, is a SEPARATE package
# inside the same _canon/ tree — `conan install .` resolves the FULL graph, test_requires included, so
# it needs its own export. This leg seeds via `export` where the Linux legs use `editable add`: the
# mechanism differs, the completeness requirement does not (INV-13).
conan export _canon/core
conan export _canon/semantic/github
conan list "insight_canon/*#*"
conan list "insight_semantic_github/*#*"

# Build the det_harness @ O2-fast: the canon+metalog tower FROM SOURCE.
$prof = "$env:CONAN_HOME/profiles/windows-msvc-release"
$work = "$env:RUNNER_TEMP/det-measure"
New-Item -ItemType Directory -Force -Path $work | Out-Null
# Resolve metalog's deps (+ canon from the export) + a conan toolchain for the standalone harness.
conan install . --profile:host="$prof" --profile:build="$prof" --build=missing -of "$work/conan"
$toolchain = (Get-ChildItem -Recurse "$work/conan" -Filter conan_toolchain.cmake | Select-Object -First 1).FullName
if (-not $toolchain) { Write-Error "no conan_toolchain.cmake from conan install"; exit 1 }
# CELL_FLAGS = SPDLOG off (digest is the fixture's stdout) + /O2 /fp:fast (ship optimization +
# contraction ON — the dominating corner; matching the other legs proves contraction-invariance).
cmake -S scripts/det_harness -B "$work/build" -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" `
  -DCANON_ROOT="$env:GITHUB_WORKSPACE/_canon" `
  -DMETA_ROOT="$env:GITHUB_WORKSPACE" `
  -DCELL_FLAGS="-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_OFF /O2 /fp:fast"
cmake --build "$work/build" --target det_fixture --parallel 2
$fixture = (Get-ChildItem -Recurse "$work/build" -Filter det_fixture.exe | Select-Object -First 1).FullName
if (-not $fixture) { Write-Error "det_fixture.exe not produced — the MSVC det harness build failed"; exit 1 }

# Emit the digest: the corpus off disk + every determinism_sections.txt row — the EXACT bytes the Linux
# legs emit.
$work = "$env:RUNNER_TEMP/det-emit"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$outPath = "$work/msvc.out"

# THE SYNTHETIC SECTION ROSTER, READ — never enumerated here. Until 2026-08-24 this step
# carried its own copy of the list, and when --latency-shift and --collapse-depths landed
# in the fixture and the bash driver (1b4f7b4) the copy was not touched: this leg emitted
# 6 section kinds against the Linux legs' 8, under a compare that demands five
# byte-identical digests. Same parse as the driver: strip trailing space, drop `#` and
# blanks, keep file order — the row IS the header text and its first field IS the flag.
$rows = @(Get-Content "$env:GITHUB_WORKSPACE/scripts/determinism_sections.txt" |
          ForEach-Object { $_.TrimEnd() } |
          Where-Object { $_ -ne '' -and -not $_.StartsWith('#') })
if ($rows.Count -eq 0) { throw "scripts/determinism_sections.txt lists no section — this digest would carry the corpus only, and the compare would call that agreement" }

# Corpus order IS digest order, so it must be BYTE order. `Sort-Object Name` collates
# culture-aware, where punctuation is ignorable at the primary level: measured on pwsh
# 7.4.6, `service.log` / `service_a.log` / `service-b.log` come back in a different order
# than `LC_ALL=C sort` gives — a five-leg divergence with no code change behind it, waiting
# for the first corpus filename that mixes `_`/`.`/`-` or case. The Linux driver pins
# LC_ALL=C; this pins the ordinal comparer, which is the same order.
# -Path wildcard, never -Filter: `-Filter` goes to FindFirstFile, which matches a file's
# 8.3 SHORT name too, so `*.log` there also takes `<name>.logfile`. The Linux driver globs
# `*.log`. Two legs enumerating different file sets is the same failure as two legs
# enumerating different sections, one directory over.
$corpus = @{}
Get-ChildItem -Path "$env:GITHUB_WORKSPACE/scripts/determinism_corpus/*.log" -File |
  ForEach-Object { $corpus[$_.Name] = $_.FullName }
$names = [string[]]$corpus.Keys
[Array]::Sort($names, [StringComparer]::Ordinal)
if ($names.Count -eq 0) { throw "no *.log under scripts/determinism_corpus" }

# One section = ASCII "### <name> ###`n" (LF, matching bash `echo`) + the fixture's RAW
# stdout (binary — the fixture _setmode's stdout, so the bytes are exactly what the engine
# wrote). Both loops below are the same emitter over the two sources the Linux driver uses.
function Add-Section {
  param([System.IO.Stream] $Stream, [string] $Header, [string] $Fixture,
        [string] $ArgumentLine, [string] $TempOut)
  $hdr = [System.Text.Encoding]::ASCII.GetBytes("### $Header ###`n")
  $Stream.Write($hdr, 0, $hdr.Length)
  $p = Start-Process -FilePath $Fixture -ArgumentList $ArgumentLine -NoNewWindow -Wait -PassThru -RedirectStandardOutput $TempOut
  if ($p.ExitCode -ne 0) { throw "det_fixture failed (exit $($p.ExitCode)) on section '$Header'" }
  $b = [System.IO.File]::ReadAllBytes($TempOut)
  $Stream.Write($b, 0, $b.Length)
}

$fs = [System.IO.File]::Create($outPath)
try {
  foreach ($name in $names) {
    Add-Section -Stream $fs -Header $name -Fixture $fixture `
                -ArgumentLine "`"$($corpus[$name])`"" -TempOut "$work/corpus.out"
  }
  foreach ($row in $rows) {
    Add-Section -Stream $fs -Header $row -Fixture $fixture `
                -ArgumentLine (($row -split '\s+')[0]) -TempOut "$work/section.out"
  }
} finally { $fs.Close() }
Write-Host "MSVC digest: $($names.Count) corpus section(s) + $($rows.Count) synthetic — $(($rows | ForEach-Object { ($_ -split '\s+')[0] }) -join ' ')"
Copy-Item $outPath "$env:GITHUB_WORKSPACE/digest-msvc.txt"
Write-Host "MSVC digest sha256: $((Get-FileHash -Algorithm SHA256 -Path digest-msvc.txt).Hash.ToLower())"
