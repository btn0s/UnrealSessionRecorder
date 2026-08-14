# Unreal Session Recorder

A self-contained, Hotjar-like PIE session recorder for Unreal Engine. It combines structured gameplay telemetry, automatic input capture, asynchronous player-view screenshots, and an MP4 with timed input overlays.

It gives humans a replayable record and agents a queryable account of what happened during a play session.

## What it produces

Each PIE or game session creates a timestamped directory under the host project's `.session-telemetry` folder:

```text
.session-telemetry/
  20260814-105336/
    timeline.json
    overlays.ass
    session.mp4
    frames.ffconcat
    video-launch.log
    video-export.log
    frames/
      t00000524.jpg
      t00000610.jpg
      ...
```

The timeline contains ordered JSON events correlated by game time and Unreal's global frame counter. The default sampler records generic pawn transform, movement, controller, character-movement, animation-instance, and montage state. The captured player view is encoded from observed timing, preserving session duration across variable capture cadence.

## Requirements

- Unreal Engine 5.8. The plugin has been built and exercised against UE 5.8.1.
- Windows for automatic post-session MP4 export.

The plugin bundles FFmpeg 8.1.1 Essentials for Windows. Recording and export require no machine-level FFmpeg installation or `PATH` configuration.

## Installation

1. Copy or clone this repository to `<Project>/Plugins/UnrealSessionRecorder`.
2. Run `git lfs pull` when installing from a Git clone.
3. Enable **Unreal Session Recorder** in the project's plugin list.
4. Regenerate project files if the host project uses generated IDE files.
5. Build the host project's Editor target.

Installation is code-only; existing content assets and Blueprints remain untouched.

## Configuration

Open **Project Settings → Unreal Session Recorder**. Defaults are:

- Enabled: `true`
- Pawn sampling: `3 Hz`
- Timeline flush: every `5 seconds`
- Player-view capture: `30 Hz`
- Frame dimensions: `480 × 270`
- JPEG quality: `80`
- Input capture: `true`
- Input overlay: `true`
- Input tap display: `0.4 seconds`
- Input overlay lead time: `0.1 seconds`
- Input overlay bottom margin: `24 px`
- Maximum retained sessions: `10`
- Build video when the session ends: `true`
- Video filename: `session.mp4`

Set either sampling frequency to zero to disable that stream.

## Input overlays

The recorder captures keyboard, mouse, touch, and gamepad press/release transitions from the PIE game viewport. Each transition enters the timeline as an `input` event with its key, display label, phase, device family, controller, game time, and frame.

UE 5.8's `FEditorDelegates::EndPIE` event finalizes the timeline and starts the bundled FFmpeg exporter. The exporter converts input events into `overlays.ass`, and simultaneously held controls appear as a compact keycap display near the bottom center of `session.mp4`. **Input Overlay Lead Time** moves each keycap's onset earlier to align it with the first visible gameplay response.

Before each PIE session begins, the editor module removes the oldest timestamped session directories beyond **Maximum Retained Sessions**. The default keeps the newest ten recordings.

## Recording semantic events

Systems emit meaningful gameplay transitions into the session timeline. Unreal's normal log remains the right place for programmer warnings and debugging breadcrumbs.

### C++

```cpp
#include "SessionTelemetrySubsystem.h"

auto Fields = MakeShared<FJsonObject>();
Fields->SetStringField(TEXT("actor"), GetName());
Fields->SetStringField(TEXT("weapon"), EquippedWeapon->GetName());
Fields->SetStringField(TEXT("slot"), TEXT("Primary"));

USessionTelemetrySubsystem::Record(
    this,
    TEXT("weapon.equipped"),
    Fields.ToSharedRef());
```

The recorder owns the reserved `type`, `t`, and `f` fields. Caller-provided values under those names are overwritten.

### Blueprint

Use **Record Telemetry Event (JSON)** with an event name and `JsonObjectWrapper`. The call validates the JSON before recording.

## Instrumentation model

- Semantic events: record facts such as `weapon.equipped`, `weapon.fired`, or `guard.alerted`.
- Sampled state: use periodic samplers for continuous values such as position, speed, or awareness.
- Diagnostic logs: continue using `UE_LOG` for configuration problems, warnings, and implementation details.

Together, the MP4 and timeline support visual playback and structured session analysis.

## Verification

`Tests/Verify-SessionTelemetry.ps1` validates a fresh session's JSON structure, local-player animation sample, monotonically increasing timestamps, JPEG existence and dimensions, non-black frame content, timed input overlays, and optional H.264 MP4 output.

```powershell
.\Plugins\UnrealSessionRecorder\Tests\Verify-SessionTelemetry.ps1 `
    -ProjectRoot $PWD `
    -NotBefore (Get-Date).AddMinutes(-5) `
    -RequireInputOverlay `
    -MinimumFrameRate 27 `
    -RequireVideo
```

The plugin also includes five Unreal automation tests under the `UnrealSessionRecorder` test prefix.

## Current scope

- Automatic video export targets Windows.
- The recorder captures the first local player's view.
- Current recordings combine video, structured telemetry, and timed input overlays.
- Audio capture, multiple-player presentation, and event visualization can join as additional layers.

## Bundled FFmpeg

The Windows exporter is FFmpeg `8.1.1-essentials_build-www.gyan.dev`, distributed under GPLv3. Its license, build configuration, archive checksum, and corresponding source link live under `ThirdParty/FFmpeg/`.

## License

Unreal Session Recorder source code is all rights reserved. Bundled FFmpeg is distributed separately under GPLv3 with its license and corresponding source information under `ThirdParty/FFmpeg/`.
