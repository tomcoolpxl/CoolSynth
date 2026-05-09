<!-- markdownlint-disable MD013 MD024 MD025 -->

# Phase 15 Blueprint: Windows CI/CD Build and Release Pipeline

Do not begin implementation until this blueprint is reviewed.

## Phase Selection

Selected phase: `Phase 15 - Windows CI build pipeline`

Selection basis:

- `DONE.md` and `README.md` both show `Phase 14` complete.
- `TODO.md` still reflects the completed `Phase 14` slice, so it is stale and cannot be used directly as the active execution list for the next milestone.
- `IMPLEMENTATION_PLAN.md` defines `Phase 15` as the next unsliced milestone.
- The current repository has no top-level `.github` directory, so this phase must introduce the GitHub Actions and release surface from scratch rather than modifying an existing pipeline.
- The current build already has the two product outputs that matter for this phase:
  - one shared JUCE plugin target named `CoolSynth` with `FORMATS Standalone VST3` in `CMakeLists.txt`
  - one JUCE console test target named `CoolSynthMidiLearnTests`
- The current build tree already exposes one important packaging risk: the observed debug artefacts match the expected split between `Standalone` and `VST3`, but the checked-in release tree is incomplete. That means release packaging must validate artefact paths explicitly rather than assuming they exist.

## Scope Correction Required Before Implementation

`IMPLEMENTATION_PLAN.md` currently describes `Phase 15` as build-only CI and explicitly says release artefact publishing is deferred beyond the phase.

That is no longer sufficient.

The explicit user directive for this blueprint is stronger and must win:

- CI and CD must run only when triggered manually or when a release tag is pushed.
- Nothing in this phase may run on every commit.
- The release path must create a proper GitHub release with release notes.
- The release path must publish downloadable Windows release assets for both:
  - the standalone application
  - the VST3 plugin
- The implementation phase must include live validation by actually exercising the workflows, not just reviewing YAML.

This blueprint therefore treats `Phase 15` as a controlled Windows CI/CD milestone, not just a branch CI milestone.

That scope correction must be acknowledged in review before implementation starts.

## Working Hypothesis

This is not a general CI problem.

It is a trigger-control and packaging-integrity problem.

The smallest correct design is:

1. One manual validation workflow triggered only by `workflow_dispatch`.
2. One tag-triggered release workflow triggered only by `push.tags`.
3. Shared Windows build logic moved into repository-owned PowerShell scripts so the two workflows cannot drift.
4. Release creation delegated to a proven GitHub release action with built-in asset upload and generated release notes support.

This design is preferred over push-on-branch CI because:

- it obeys the explicit "manual or release tag only" trigger rule
- it reuses the existing local build path instead of inventing a second one
- it avoids duplicated YAML command sequences by centralizing build logic in scripts
- it allows one disposable prerelease tag to test the full CD path without polluting normal development

Cheap disconfirming checks already satisfied by the current repository:

1. `CMakeLists.txt` already defines the shared plugin target with `FORMATS Standalone VST3`.
2. `CMakeLists.txt` already defines `CoolSynthMidiLearnTests` under `BUILD_TESTING` and registers it with `add_test(...)`.
3. `README.md` already documents the local bootstrap path as:
   - `git submodule update --init --recursive`
   - `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
   - `cmake --build build --config Debug`
4. The JUCE submodule already contains GitHub workflow examples under `external/JUCE/.github`, so there is a nearby reference for style and mechanics.
5. There is no top-level repository workflow yet, so no backwards-compatibility burden exists.

## Current Code Anchors

These are the repository surfaces that directly constrain the Phase 15 design.

- `CMakeLists.txt`
  - Owns the canonical build targets.
  - Already defines the shared plugin target `CoolSynth`.
  - Already defines the console test target `CoolSynthMidiLearnTests`.
  - Must remain the source of truth for what CI builds and tests.

- `CMakePresets.json`
  - Currently contains only `vs2022-debug`, `vs2022-release`, `build-debug`, and `build-release`.
  - Both configure presets point at the same `build` directory.
  - That is acceptable for local one-off builds but weak for CI because cross-configuration reuse can hide stale artefacts.

- `README.md`
  - Already documents the local bootstrap path and expected artefact families.
  - Must be extended so manual workflow dispatch and release tagging are documented alongside local build commands.

- `build/CoolSynth_artefacts/...`
  - Current observed debug output shape is compatible with the expected JUCE output split:
    - `build/CoolSynth_artefacts/Debug/Standalone/CoolSynth.exe`
    - `build/CoolSynth_artefacts/Debug/VST3/CoolSynth.vst3/`
  - The currently checked-in release artefact tree is incomplete, which means packaging must assert output presence instead of trusting static paths.

- `external/JUCE/.github/workflows/juce_private_build.yml`
  - Confirms JUCE itself uses `workflow_dispatch` and pinned actions.
  - Confirms the local repository already contains a nearby example of GitHub Actions usage, even though it is not directly reusable as-is.

- `external/JUCE/.github/actions/upload_artifact/action.yaml`
  - Shows JUCE solves Windows/permissions artefact concerns with a dedicated wrapper around `actions/upload-artifact`.
  - For CoolSynth, the simpler first pass is ordinary zipped release bundles plus `actions/upload-artifact`, not custom tar wrappers.

## Exact `TODO.md` Entries This Blueprint Expands

The six plan-derived items that must appear in the `TODO.md` refresh are:

- [ ] Add a Windows CI workflow for clean-checkout configure and build.
- [ ] Run standalone and VST3 targets in CI.
- [ ] Reuse the documented JUCE bootstrap path in CI.
- [ ] Publish enough CI logs to review build results without relying on release artifacts.
- [ ] Verify the CI workflow succeeds on a branch.
- [ ] Document CI expectations in `README.md`.

Because the user explicitly widened the phase, the `TODO.md` refresh for actual implementation must also add these two scope-correction items before work starts:

- [ ] Add a tag-triggered Windows release workflow that creates or updates a GitHub release with generated release notes and uploads standalone and VST3 assets.
- [ ] Prove the CI and CD paths end-to-end by running one manual validation workflow and one disposable prerelease-tag release workflow.

## 1. Architectural Design

### 1.1 Controlling Design Decisions

1. No branch-triggered or PR-triggered workflows.

   Required consequence:

   - No `push` branch filters.
   - No `pull_request` or `pull_request_target` triggers.
   - No scheduled workflows.
   - The only allowed triggers in this phase are:
     - `workflow_dispatch`
     - `push.tags`

2. Build logic lives in repository scripts, not duplicated inline in two YAML files.

   Required consequence:

   - The workflows orchestrate checkout, permissions, artefact upload, and release publishing.
   - PowerShell scripts own configure, build, test, path resolution, packaging, and manifest generation.

3. Use the documented JUCE bootstrap path exactly.

   Required consequence:

   - `actions/checkout` must fetch submodules recursively.
   - CI must not download JUCE from a second source.
   - The CI path must continue to work for clean checkouts that rely on `external/JUCE`.

4. Release publishing must be tag-driven and idempotent.

   Required consequence:

   - The release workflow triggers on semantic-version tags only.
   - Re-running the workflow for the same tag must update the release assets instead of creating duplicate releases.
   - Asset upload must fail loudly if expected files are missing.

5. Generated release notes should use GitHub's native release-note engine, with light repository configuration.

   Required consequence:

   - Add `.github/release.yml`.
   - Use `generate_release_notes: true` in the release publishing step.
   - Keep any custom text limited to a short preamble that explains the packaged Windows assets.

6. Initial CI/CD must favour reproducibility over speed.

   Required consequence:

   - No build cache in the first pass.
   - Explicit clean CI build directories.
   - Explicit runner image `windows-2022`, not `windows-latest`.

### 1.2 Workflow Topology

#### 1.2.1 Manual validation workflow

File:

- `.github/workflows/windows-manual-validation.yml`

Trigger:

```yaml
on:
  workflow_dispatch:
```

Purpose:

- manually validate clean-checkout configure, build, test, and optional packaging from any chosen branch or ref
- never publish a GitHub release
- upload logs and optional packaged artefacts as workflow artefacts only

Suggested workflow inputs:

| Input | Type | Default | Allowed Values | Purpose |
| --- | --- | --- | --- | --- |
| `configuration` | `choice` | `Release` | `Debug`, `Release` | Selects the CI preset and test configuration |
| `run_tests` | `boolean` | `true` | `true`, `false` | Controls whether `ctest` runs |
| `package_assets` | `boolean` | `true` | `true`, `false` | Allows manual verification of release bundles without publishing a release |

Suggested job structure:

- `validate-windows-build`
  - checkout repository with recursive submodules
  - call `scripts/ci/BuildAndTest.ps1`
  - optionally call `scripts/ci/PackageRelease.ps1`
  - upload logs and package artefacts with `actions/upload-artifact`

#### 1.2.2 Tag-triggered release workflow

File:

- `.github/workflows/windows-release.yml`

Trigger:

```yaml
on:
  push:
    tags:
      - 'v*.*.*'
      - 'v*.*.*-*'
```

Purpose:

- build the release configuration from a clean checkout
- run the automated test target
- package standalone and VST3 Windows artefacts
- create or update the GitHub release for the tag
- attach assets and generated release notes

Why `push.tags` is preferred here over `release.published`:

- the user explicitly wants the release path to run when they tag a release
- a tag trigger creates a single authoritative path for build plus publish
- it avoids a second workflow that would otherwise fire only after a manually-created release exists
- it avoids double-fire risk if both tag push and release publication are subscribed

Suggested job structure:

1. `build-release-assets`
   - checkout with recursive submodules
   - call `scripts/ci/BuildAndTest.ps1 -Configuration Release -RunTests`
   - call `scripts/ci/PackageRelease.ps1 -Version $env:GITHUB_REF_NAME`
   - upload package bundle and logs as a short-retention workflow artefact

2. `publish-release`
   - depends on `build-release-assets`
   - downloads the package bundle with `actions/download-artifact`
   - creates or updates the GitHub release with `softprops/action-gh-release`
   - enables generated release notes
   - uploads packaged assets

#### 1.2.3 Trigger boundary rules

These rules are mandatory and should be stated directly in review:

- No workflow in this phase may include `push.branches`.
- No workflow in this phase may include `pull_request`.
- No workflow in this phase may include `schedule`.
- Manual validation must remain manual-only.
- Publishing must remain tag-only.

### 1.3 Runner, Permissions, and Concurrency State

#### 1.3.1 Runner choice

Use:

```yaml
runs-on: windows-2022
```

Reason:

- `REQUIREMENTS.md` fixes the toolchain around Visual Studio 2022.
- `windows-latest` can drift underneath the project.

#### 1.3.2 Workflow permissions

Manual validation workflow permissions:

```yaml
permissions:
  contents: read
```

Release workflow permissions:

```yaml
permissions:
  contents: write
```

Do not request broader permissions in the first pass.

#### 1.3.3 Concurrency

Manual workflow:

```yaml
concurrency:
  group: windows-manual-${{ github.ref }}
  cancel-in-progress: true
```

Release workflow:

```yaml
concurrency:
  group: windows-release-${{ github.ref }}
  cancel-in-progress: false
```

Reason:

- manual reruns for the same ref should supersede older manual runs
- release jobs for a tag should not silently cancel an in-progress publish path

### 1.4 Preset and Build-State Definitions

`CMakePresets.json` must stop using one shared build directory for all CI configurations.

Required additional configure presets:

| Preset | Generator | Binary Dir | Cache Variables |
| --- | --- | --- | --- |
| `ci-debug` | `Visual Studio 17 2022` | `${sourceDir}/build/ci-debug` | `BUILD_TESTING=ON`, `COOLSYNTH_ENABLE_VST3_USER_INSTALL=OFF` |
| `ci-release` | `Visual Studio 17 2022` | `${sourceDir}/build/ci-release` | `BUILD_TESTING=ON`, `COOLSYNTH_ENABLE_VST3_USER_INSTALL=OFF` |

Required additional build presets:

| Preset | Configure Preset | Configuration |
| --- | --- | --- |
| `ci-build-debug` | `ci-debug` | `Debug` |
| `ci-build-release` | `ci-release` | `Release` |

Required command contract:

```powershell
cmake --preset ci-release
cmake --build --preset ci-build-release --config Release
ctest --test-dir build/ci-release -C Release --output-on-failure --output-junit build/ci-release/Testing/ctest-results.xml
```

### 1.5 Packaging Data Structures

The release packaging scripts should use simple `PSCustomObject` state rather than ad hoc unstructured strings.

#### 1.5.1 Build context object

```powershell
[pscustomobject]@{
    SourceRoot        = 'C:\actions-runner\_work\CoolSynth\CoolSynth'
    Configuration     = 'Release'
    ConfigurePreset   = 'ci-release'
    BuildPreset       = 'ci-build-release'
    BuildDirectory    = 'C:\actions-runner\_work\CoolSynth\CoolSynth\build\ci-release'
    ProjectName       = 'CoolSynth'
    TagName           = 'v0.1.0'
    PackageDirectory  = 'C:\actions-runner\_work\CoolSynth\CoolSynth\build\ci-release\packages'
}
```

#### 1.5.2 Located artefact paths object

```powershell
[pscustomobject]@{
    StandaloneExe = '...\build\CoolSynth_artefacts\Release\Standalone\CoolSynth.exe'
    StandalonePdb = '...\build\CoolSynth_artefacts\Release\Standalone\CoolSynth.pdb'
    Vst3Bundle    = '...\build\CoolSynth_artefacts\Release\VST3\CoolSynth.vst3'
    TestResults   = '...\build\ci-release\Testing\ctest-results.xml'
}
```

#### 1.5.3 Packaged release asset object

```powershell
[pscustomobject]@{
    LogicalName = 'standalone'
    FileName    = 'CoolSynth-windows-x64-standalone-v0.1.0.zip'
    FilePath    = '...\packages\CoolSynth-windows-x64-standalone-v0.1.0.zip'
    Sha256      = '<computed hash>'
}
```

#### 1.5.4 Release manifest object

```powershell
[pscustomobject]@{
    TagName          = 'v0.1.0'
    CommitSha        = '<github.sha>'
    RunnerImage      = 'windows-2022'
    Configuration    = 'Release'
    StandaloneAsset  = 'CoolSynth-windows-x64-standalone-v0.1.0.zip'
    Vst3Asset        = 'CoolSynth-windows-x64-vst3-v0.1.0.zip'
    ChecksumFile     = 'CoolSynth-windows-x64-sha256-v0.1.0.txt'
    GeneratedAtUtc   = '2026-05-10T12:34:56Z'
}
```

### 1.6 Required PowerShell Function Signatures

Put these in `scripts/ci/Common.ps1`, `scripts/ci/BuildAndTest.ps1`, and `scripts/ci/PackageRelease.ps1`.

```powershell
function New-CoolSynthBuildContext {
    param(
        [Parameter(Mandatory)][string]$SourceRoot,
        [Parameter(Mandatory)][ValidateSet('Debug', 'Release')][string]$Configuration,
        [string]$TagName = ''
    )
}

function Invoke-CoolSynthBootstrap {
    param(
        [Parameter(Mandatory)][string]$SourceRoot
    )
}

function Invoke-CoolSynthConfigure {
    param(
        [Parameter(Mandatory)][pscustomobject]$BuildContext
    )
}

function Invoke-CoolSynthBuild {
    param(
        [Parameter(Mandatory)][pscustomobject]$BuildContext
    )
}

function Invoke-CoolSynthTests {
    param(
        [Parameter(Mandatory)][pscustomobject]$BuildContext
    )
}

function Get-CoolSynthArtifactPaths {
    param(
        [Parameter(Mandatory)][pscustomobject]$BuildContext
    )
}

function New-CoolSynthReleasePackages {
    param(
        [Parameter(Mandatory)][pscustomobject]$BuildContext,
        [Parameter(Mandatory)][pscustomobject]$ArtifactPaths
    )
}

function Write-CoolSynthChecksumFile {
    param(
        [Parameter(Mandatory)][array]$PackagedAssets,
        [Parameter(Mandatory)][string]$DestinationPath
    )
}

function Write-CoolSynthReleaseManifest {
    param(
        [Parameter(Mandatory)][pscustomobject]$BuildContext,
        [Parameter(Mandatory)][array]$PackagedAssets,
        [Parameter(Mandatory)][string]$DestinationPath
    )
}

function Test-CoolSynthPrereleaseTag {
    param(
        [Parameter(Mandatory)][string]$TagName
    )
}
```

### 1.7 Release Asset Contract

Required public release assets:

| Asset | Required | Contents | Reason |
| --- | --- | --- | --- |
| `CoolSynth-windows-x64-standalone-<tag>.zip` | Yes | `CoolSynth.exe` and any directly required runtime companion files | End-user standalone delivery |
| `CoolSynth-windows-x64-vst3-<tag>.zip` | Yes | top-level `CoolSynth.vst3` bundle preserved as a directory inside the zip | End-user plugin delivery |
| `CoolSynth-windows-x64-sha256-<tag>.txt` | Yes | SHA256 hashes for each public asset | Download integrity |

Allowed CI-only artefacts that should not be public release assets in the first pass:

| Artefact | Destination |
| --- | --- |
| build logs | workflow artefact |
| `ctest-results.xml` | workflow artefact |
| PDB files | workflow artefact |
| release manifest JSON | workflow artefact |

Reason:

- the user asked for release executables and release notes
- public symbol packaging is useful but not required for the first pass
- keeping symbols in CI artefacts avoids bloating the public release page unnecessarily

### 1.8 Release Notes Strategy

Use GitHub-generated release notes rather than a hand-written changelog generator.

Required file:

- `.github/release.yml`

Required release step behaviour:

- `generate_release_notes: true`
- `fail_on_unmatched_files: true`
- `overwrite_files: true`
- `prerelease: true` when the tag contains a hyphen suffix such as `-rc.1`

Required note policy:

- prepend one short asset summary only if needed
- do not hand-maintain full markdown release notes in the repo in this phase
- let GitHub provide merged PRs, contributors, and changelog links

Suggested `.github/release.yml` category shape:

```yaml
changelog:
  categories:
    - title: Features
      labels:
        - enhancement
        - feature
    - title: Fixes
      labels:
        - bug
        - fix
    - title: Build and Tooling
      labels:
        - build
        - ci
    - title: Other Changes
      labels:
        - '*'
```

## 2. File-Level Strategy

This phase should touch exactly these files unless CI uncovers a concrete local defect that forces a narrow change elsewhere.

| File | Responsibility |
| --- | --- |
| `.github/workflows/windows-manual-validation.yml` | Manual-only GitHub Actions entrypoint for clean-checkout configure, build, test, and optional package validation |
| `.github/workflows/windows-release.yml` | Tag-only GitHub Actions entrypoint for release build, packaging, GitHub release creation, generated notes, and asset upload |
| `.github/release.yml` | GitHub-generated release-note categories and exclusions |
| `scripts/ci/Common.ps1` | Shared PowerShell helpers for strict mode, logging, assertions, version parsing, zipping, hashing, and JSON manifest writing |
| `scripts/ci/BuildAndTest.ps1` | Canonical configure, build, and `ctest` entry script used by both workflows |
| `scripts/ci/PackageRelease.ps1` | Canonical artefact discovery, packaging, checksum, and manifest generation script |
| `CMakePresets.json` | Adds isolated CI presets and build presets with explicit build directories and CI-safe cache variables |
| `README.md` | Documents manual workflow dispatch, tag naming policy, release behaviour, and the relationship between local build steps and GitHub automation |
| `TODO.md` | Refreshed to the eight Phase 15 items before implementation starts |
| `DONE.md` | Records verified completion only after live manual and tag-based workflow validation succeeds |

Files that should not change in the normal implementation path:

- `CMakeLists.txt`
- `src/**`
- `tests/**`
- standalone settings or patch-state code

If any of those files need changes, that is a signal the CI pipeline has exposed a real build reproducibility bug, and the reason for widening scope must be documented explicitly in review.

## 3. Atomic Execution Steps

Each high-level checkbox must be implemented as a tight `Plan -> Act -> Validate` cycle.

### 3.1 Checkbox: Add a Windows CI workflow for clean-checkout configure and build

#### Plan

- Create a manual-only workflow file.
- Ensure the runner is fixed to `windows-2022`.
- Ensure checkout uses the repository's existing submodule path.
- Keep workflow permissions narrow.
- Call repository-owned PowerShell scripts rather than embedding long commands in YAML.

#### Act

1. Create `.github/workflows/windows-manual-validation.yml`.
2. Add only `workflow_dispatch` as the trigger.
3. Add `permissions: contents: read`.
4. Use `actions/checkout` with:
   - `submodules: recursive`
   - `fetch-depth: 0`
5. Add a `pwsh` step that invokes `scripts/ci/BuildAndTest.ps1`.
6. Upload logs and, if requested, package outputs with `actions/upload-artifact`.

#### Validate

1. Push the workflow branch.
2. Confirm that pushing commits alone produces no workflow run.
3. Manually dispatch the workflow from the Actions tab against the feature branch.
4. Confirm a clean checkout configures and builds successfully.
5. Confirm the logs artefact is downloadable.

### 3.2 Checkbox: Run standalone and VST3 targets in CI

#### Plan

- Build the shared `CoolSynth` target.
- After build, resolve artefact paths explicitly.
- Fail the packaging step if either standalone or VST3 output is missing.

#### Act

1. In `scripts/ci/BuildAndTest.ps1`, invoke the CI preset and build preset for the selected configuration.
2. In `scripts/ci/PackageRelease.ps1`, locate:
   - `build/CoolSynth_artefacts/<Config>/Standalone/CoolSynth.exe`
   - `build/CoolSynth_artefacts/<Config>/VST3/CoolSynth.vst3`
3. Add explicit `Test-Path` assertions for both.
4. Zip the standalone executable payload and the VST3 bundle into separate archives.

#### Validate

1. Run the manual workflow with `configuration=Release` and `package_assets=true`.
2. Download the generated artefacts.
3. Confirm the standalone zip contains `CoolSynth.exe`.
4. Confirm the VST3 zip contains the top-level `CoolSynth.vst3` bundle.
5. Confirm the workflow fails if either path is deliberately broken during local dry-run review.

### 3.3 Checkbox: Reuse the documented JUCE bootstrap path in CI

#### Plan

- Do not download JUCE separately.
- Prove submodule checkout is sufficient.
- Explicitly disable the local-only VST3 user install option.

#### Act

1. Keep the JUCE acquisition path as `external/JUCE`.
2. Configure checkout with recursive submodules.
3. Add CI configure presets that set:
   - `BUILD_TESTING=ON`
   - `COOLSYNTH_ENABLE_VST3_USER_INSTALL=OFF`
4. Keep the configure command based on `cmake --preset ...` so it matches `README.md` and local usage.

#### Validate

1. Confirm the workflow succeeds from a clean GitHub runner with no custom JUCE bootstrap step.
2. Confirm the logs show CMake entering `external/JUCE` through the existing project configuration.
3. Confirm no script attempts to `git clone` JUCE separately.

### 3.4 Checkbox: Publish enough CI logs to review build results without relying on release artifacts

#### Plan

- Preserve configure, build, and test output separately.
- Make failures actionable without requiring live console access.
- Keep public release assets separate from diagnostic artefacts.

#### Act

1. In `scripts/ci/BuildAndTest.ps1`, tee each major command into log files:
   - `logs/configure.log`
   - `logs/build.log`
   - `logs/ctest.log`
2. Emit a JUnit-style file with `ctest --output-junit`.
3. Upload `logs/` and `Testing/ctest-results.xml` as a workflow artefact in both workflows.
4. Keep PDBs and manifest JSON in workflow artefacts, not public release assets.

#### Validate

1. Trigger a manual workflow run.
2. Download the logs artefact.
3. Confirm the logs contain the exact configure, build, and test surfaces needed to diagnose failures.
4. Confirm the release page contains only deliverable assets, not internal logs.

### 3.5 Checkbox: Verify the CI workflow succeeds on a branch

#### Plan

- Because branch pushes must not auto-run, verification must use manual dispatch against the branch.
- The validation branch must contain the workflow file, script files, and CI preset changes.

#### Act

1. Push the implementation branch.
2. Open the Actions tab.
3. Manually run `windows-manual-validation.yml` against that branch.
4. Run it once with:
   - `configuration=Release`
   - `run_tests=true`
   - `package_assets=true`

#### Validate

1. The workflow completes successfully.
2. The artefact bundle contains logs, manifest, checksums, and the two zipped deliverables.
3. No other branch-triggered workflow ran automatically.

### 3.6 Checkbox: Document CI expectations in `README.md`

#### Plan

- Document the exact trigger policy.
- Document the difference between manual validation runs and release-tag runs.
- Keep the README aligned with actual workflow filenames and tag patterns.

#### Act

1. Add a short `CI/CD` section to `README.md`.
2. Document:
   - manual validation via the Actions UI or `gh workflow run`
   - tag naming policy
   - generated Windows release assets
   - that branch pushes do not run CI automatically
3. Keep the existing local bootstrap section intact.

#### Validate

1. Review the README against the actual workflow names and inputs.
2. Confirm a reviewer can tell how to run validation and how to publish a release by tag.

### 3.7 Scope-Correction Checkbox: Add a tag-triggered Windows release workflow that creates or updates a GitHub release with generated release notes and uploads standalone and VST3 assets

#### Plan

- Use a tag trigger, not a release trigger.
- Build first, publish second.
- Use GitHub-generated release notes plus a small release config file.
- Use a proven release action that handles file uploads and idempotent updates.

#### Act

1. Create `.github/workflows/windows-release.yml`.
2. Use only `push.tags` triggers for stable and prerelease semver patterns.
3. Add a build job that runs the release build, tests, and packaging.
4. Upload the package directory as a short-retention workflow artefact.
5. Add a publish job that downloads the package directory.
6. Use `softprops/action-gh-release` pinned to a full commit SHA with:
   - `generate_release_notes: true`
   - `files:` listing the standalone zip, VST3 zip, and checksum file
   - `fail_on_unmatched_files: true`
   - `overwrite_files: true`
   - `prerelease:` computed from the tag shape
7. Add `.github/release.yml` to shape the generated release notes.

#### Validate

1. Push a disposable prerelease tag such as `v0.1.0-rc.1`.
2. Confirm the workflow creates a GitHub prerelease.
3. Confirm generated release notes appear.
4. Confirm both public assets and the checksum file appear on the release page.
5. Confirm rerunning the workflow updates the existing release rather than creating a second release for the same tag.

### 3.8 Scope-Correction Checkbox: Prove the CI and CD paths end-to-end by running one manual validation workflow and one disposable prerelease-tag release workflow

#### Plan

- Treat live workflow execution as mandatory validation, not optional cleanup.
- Use a prerelease tag so the test run does not become the public latest release.

#### Act

1. Run the manual validation workflow against the implementation branch.
2. After it passes, push a disposable prerelease tag from the same commit.
3. Let the release workflow publish the prerelease.
4. Download both public release assets.
5. Launch the standalone binary locally.
6. Place the VST3 bundle in the expected local test location and confirm host visibility or bundle integrity.
7. If the prerelease is only for validation, clean it up after the proof run.

#### Validate

1. Manual workflow passes.
2. Tag workflow passes.
3. GitHub release exists with generated notes.
4. Standalone asset launches.
5. VST3 asset extracts with the correct bundle shape.
6. `DONE.md` is updated only after this live proof is complete.

## 4. Edge Case and Boundary Audit

These are the specific failure modes Phase 15 must handle or explicitly guard against.

### 4.1 Trigger and workflow-boundary failures

- A workflow accidentally includes `push:` without a `tags:` filter and starts running on every commit.
- A workflow includes both `push.tags` and `release.published`, causing duplicate release execution.
- Manual validation is accidentally wired to `push` or `pull_request` instead of `workflow_dispatch`.
- The workflow file is not on the default branch, so `workflow_dispatch` is unavailable in the GitHub UI.

### 4.2 Build reproducibility failures

- Recursive submodule checkout is omitted, causing `external/JUCE` configure failure.
- CI reuses a non-isolated build directory and passes only because of stale artefacts.
- The runner image changes under `windows-latest`, introducing toolchain drift.
- `COOLSYNTH_ENABLE_VST3_USER_INSTALL` is left enabled and tries to copy to a user directory on the runner.

### 4.3 Artefact path and packaging failures

- JUCE artefact layout differs between Debug and Release, and the script assumes one static path.
- The standalone executable is missing from the expected `Standalone/` directory.
- The VST3 bundle is zipped incorrectly and loses the top-level `CoolSynth.vst3` folder.
- Windows path quoting breaks when the workspace path contains spaces.
- `Compress-Archive` or equivalent flattens or duplicates the wrong directory root.

### 4.4 Testing failures

- `BUILD_TESTING` is implicitly off in CI and the build passes without running the console tests.
- `ctest` runs against the wrong build directory or configuration and reports a false success.
- Test output is not uploaded, making a failing run hard to debug.

### 4.5 Release publishing failures

- `contents: write` permission is missing and the release creation step fails.
- The release action is unpinned and changes behaviour unexpectedly.
- `fail_on_unmatched_files` is not enabled and the release publishes without one of the expected assets.
- Rerunning a failed tag workflow creates duplicate assets or duplicate releases.
- Stable tags and prerelease tags are not distinguished, causing a disposable test release to become the latest full release.

### 4.6 Version and naming traps

- The workflow uses the CMake `project(... VERSION 0.1.0)` value instead of the pushed tag name for public release asset naming.
- A future tag does not match the expected semver pattern and silently fails to trigger the workflow.
- Asset names do not include enough metadata to distinguish standalone vs VST3 downloads.

### 4.7 Documentation and process traps

- `README.md` still implies CI runs automatically on commits.
- `TODO.md` is refreshed with only the old six build-only items and omits the new release-publishing scope.
- `DONE.md` is updated before a live manual run and live tag run are completed.

## 5. Verification Protocol

This section is the non-negotiable acceptance checklist for Phase 15 execution.

### 5.1 Manual UX and GitHub workflow checks

1. Confirm no top-level workflow runs automatically when a normal branch commit is pushed.
2. Open the Actions tab and confirm the manual validation workflow exposes a `Run workflow` button.
3. Manually dispatch the validation workflow against the implementation branch.
4. Confirm the workflow summary clearly shows:
   - runner image
   - selected configuration
   - success or failure of configure, build, and test stages
5. Download the logs artefact and confirm it contains:
   - `configure.log`
   - `build.log`
   - `ctest.log`
   - `ctest-results.xml`
6. Download the packaged artefact bundle and confirm it contains:
   - standalone zip
   - VST3 zip
   - checksum file
   - manifest JSON
7. Push a disposable prerelease tag such as `v0.1.0-rc.1`.
8. Confirm the release workflow runs automatically for the tag.
9. Open the resulting GitHub release page and confirm:
   - the release exists for the tag
   - it is marked prerelease when the tag contains a hyphen suffix
   - generated release notes are present
   - the standalone zip is attached
   - the VST3 zip is attached
   - the checksum file is attached
10. Download the standalone zip from the release page and confirm the executable launches locally.
11. Download the VST3 zip from the release page and confirm the bundle extracts with `CoolSynth.vst3` intact.
12. If the tag was a disposable validation tag, delete the prerelease and tag after confirmation.

### 5.2 Automated command-level checks

These must pass inside the GitHub runner.

```powershell
git submodule update --init --recursive
cmake --preset ci-release
cmake --build --preset ci-build-release --config Release
ctest --test-dir build/ci-release -C Release --output-on-failure --output-junit build/ci-release/Testing/ctest-results.xml
```

### 5.3 Packaging assertions

These checks must be coded into `PackageRelease.ps1` rather than done by eye:

- `CoolSynth.exe` exists before the standalone zip is created.
- `CoolSynth.vst3` exists before the VST3 zip is created.
- both zip files exist after packaging.
- the checksum file exists after hash generation.
- the manifest JSON exists after manifest generation.
- each expected public asset is matched by the release upload glob.

### 5.4 Release-page assertions

These checks must pass after the tag-driven workflow finishes:

- exactly one GitHub release exists for the test tag
- the release page lists all expected assets
- the release notes body is non-empty
- the release asset download URLs are reachable

### 5.5 Exit checklist for moving work to `DONE.md`

- Manual validation workflow exists and runs only by `workflow_dispatch`.
- Release workflow exists and runs only by matching release tags.
- No workflow in the repository runs on every commit.
- CI reuses the existing JUCE submodule bootstrap path.
- CI builds the shared `CoolSynth` target and runs `CoolSynthMidiLearnTests` through `ctest`.
- Release packaging produces both the standalone zip and the VST3 zip.
- GitHub-generated release notes are configured and visible on a live test release.
- One manual workflow run and one disposable tag-driven release run have both succeeded.
- `README.md`, `TODO.md`, and `DONE.md` reflect the final verified behaviour.

## 6. Code Scaffolding

These are the minimal structural templates that should be used during implementation.

### 6.1 Manual validation workflow scaffold

```yaml
name: Windows Manual Validation

on:
  workflow_dispatch:
    inputs:
      configuration:
        description: Build configuration
        type: choice
        required: true
        default: Release
        options:
          - Debug
          - Release
      run_tests:
        description: Run CTest
        type: boolean
        required: true
        default: true
      package_assets:
        description: Package standalone and VST3 artefacts
        type: boolean
        required: true
        default: true

permissions:
  contents: read

concurrency:
  group: windows-manual-${{ github.ref }}
  cancel-in-progress: true

jobs:
  validate-windows-build:
    runs-on: windows-2022
    steps:
      - name: Checkout
        uses: actions/checkout@<pinned-sha>
        with:
          fetch-depth: 0
          submodules: recursive

      - name: Build and test
        shell: pwsh
        run: ./scripts/ci/BuildAndTest.ps1 -Configuration "${{ inputs.configuration }}" -RunTests:${{ inputs.run_tests }}

      - name: Package release artefacts
        if: ${{ inputs.package_assets }}
        shell: pwsh
        run: ./scripts/ci/PackageRelease.ps1 -Configuration "${{ inputs.configuration }}"

      - name: Upload workflow artefacts
        uses: actions/upload-artifact@<pinned-sha>
        with:
          name: windows-manual-${{ github.run_id }}
          path: |
            build/ci-*/logs/**
            build/ci-*/Testing/ctest-results.xml
            build/ci-*/packages/**
```

### 6.2 Release workflow scaffold

```yaml
name: Windows Release

on:
  push:
    tags:
      - 'v*.*.*'
      - 'v*.*.*-*'

permissions:
  contents: write

concurrency:
  group: windows-release-${{ github.ref }}
  cancel-in-progress: false

jobs:
  build-release-assets:
    runs-on: windows-2022
    steps:
      - name: Checkout
        uses: actions/checkout@<pinned-sha>
        with:
          fetch-depth: 0
          submodules: recursive

      - name: Build and test
        shell: pwsh
        run: ./scripts/ci/BuildAndTest.ps1 -Configuration Release -RunTests

      - name: Package release artefacts
        shell: pwsh
        run: ./scripts/ci/PackageRelease.ps1 -Configuration Release -TagName "${{ github.ref_name }}"

      - name: Upload package bundle
        uses: actions/upload-artifact@<pinned-sha>
        with:
          name: windows-release-${{ github.ref_name }}
          path: build/ci-release/packages/**

  publish-release:
    needs: build-release-assets
    runs-on: windows-2022
    steps:
      - name: Download package bundle
        uses: actions/download-artifact@<pinned-sha>
        with:
          name: windows-release-${{ github.ref_name }}
          path: release-bundle

      - name: Publish GitHub release
        uses: softprops/action-gh-release@<pinned-sha>
        with:
          tag_name: ${{ github.ref_name }}
          generate_release_notes: true
          prerelease: ${{ contains(github.ref_name, '-') }}
          overwrite_files: true
          fail_on_unmatched_files: true
          files: |
            release-bundle/CoolSynth-windows-x64-standalone-${{ github.ref_name }}.zip
            release-bundle/CoolSynth-windows-x64-vst3-${{ github.ref_name }}.zip
            release-bundle/CoolSynth-windows-x64-sha256-${{ github.ref_name }}.txt
```

### 6.3 Common PowerShell helper scaffold

```powershell
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-PathExists {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Description not found: $Path"
    }
}

function Invoke-LoggedCommand {
    param(
        [Parameter(Mandatory)][string]$LogPath,
        [Parameter(Mandatory)][scriptblock]$ScriptBlock
    )

    & $ScriptBlock 2>&1 | Tee-Object -FilePath $LogPath
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed. See log: $LogPath"
    }
}
```

### 6.4 Packaging script scaffold

```powershell
param(
    [Parameter(Mandatory)][ValidateSet('Debug', 'Release')][string]$Configuration,
    [string]$TagName = ''
)

. "$PSScriptRoot/Common.ps1"

$context = New-CoolSynthBuildContext -SourceRoot $PSScriptRoot/../.. -Configuration $Configuration -TagName $TagName
$artifacts = Get-CoolSynthArtifactPaths -BuildContext $context
$packages = New-CoolSynthReleasePackages -BuildContext $context -ArtifactPaths $artifacts

Write-CoolSynthChecksumFile -PackagedAssets $packages -DestinationPath (Join-Path $context.PackageDirectory "CoolSynth-windows-x64-sha256-$($context.TagName).txt")
Write-CoolSynthReleaseManifest -BuildContext $context -PackagedAssets $packages -DestinationPath (Join-Path $context.PackageDirectory 'release-manifest.json')
```

### 6.5 Release note config scaffold

```yaml
changelog:
  categories:
    - title: Features
      labels:
        - enhancement
        - feature
    - title: Fixes
      labels:
        - bug
        - fix
    - title: Build and Tooling
      labels:
        - build
        - ci
    - title: Other Changes
      labels:
        - '*'
```

## Final Implementation Notes

- Keep this phase Windows-only.
- Keep this phase manual-or-tag-only.
- Keep public release assets limited to user-facing deliverables and checksums.
- Treat one successful manual dispatch and one successful disposable prerelease-tag publish as mandatory proof, not optional smoke testing.
- Do not update `DONE.md` until the live workflow proof is complete.