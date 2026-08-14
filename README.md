# Unreal Session Recorder

A lightweight, Hotjar-like session recorder for Unreal Engine. It combines structured gameplay telemetry with asynchronous player-view screenshots and automatically produces an MP4 when a PIE session ends.

This is observational tooling, not deterministic replay. Gameplay never depends on the recorder, and the recorder does not reconstruct world state.

## What it produces

Each PIE or game session creates a timestamped directory under the host project's `.session-telemetry` folder:

```text
.session-telemetry/
  20260814-105336/
    timeline.json
    session.mp4
    frames.ffconcat
    video-launch.log
    video-export.log
    frames/
      t00000524.jpg
      t00000610.jpg
      ...
```

The timeline contains ordered JSON events correlated by game time and Unreal's global frame counter. The default sampler records generic pawn transform, movement, controller, character-movement, animation-instance, and montage state. The captured player view is encoded at its real observed timing, so dropped or delayed captures do not distort session duration.

## Requirements

- Unreal Engine 5.8. The plugin has been built and exercised against UE 5.8.1.
- Windows for automatic post-session MP4 export.
- FFmpeg and FFprobe available on `PATH`. FFmpeg 8.1.1 was used for acceptance.

The structured timeline and JPEG capture are runtime Unreal code. Only the automatic PowerShell/FFmpeg export path is currently Windows-specific.

## Installation

1. Copy or clone this repository to `<Project>/Plugins/SessionTelemetry`.
2. Enable **Session Telemetry** in the project's plugin list.
3. Regenerate project files if the host project uses generated IDE files.
4. Build the host project's Editor target.

No content assets or Blueprint edits are required.

## Configuration

Open **Project Settings → Session Telemetry**. Defaults are:

- Enabled: `true`
- Pawn sampling: `3 Hz`
- Timeline flush: every `5 seconds`
- Player-view capture: `10 Hz`
- Frame dimensions: `480 × 270`
- JPEG quality: `80`
- Build video when the session ends: `true`
- FFmpeg executable: `ffmpeg`
- Video filename: `session.mp4`

Set either sampling frequency to zero to disable that stream.

## Recording semantic events

Systems should emit meaningful gameplay transitions, not tick-by-tick diagnostic text. Unreal's normal log remains the right place for programmer warnings and debugging breadcrumbs.

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

Use **Record Telemetry Event (JSON)** with an event name and `JsonObjectWrapper`. Invalid JSON is rejected without affecting gameplay.

## Design boundaries

- One-way observation: gameplay may emit telemetry, but it must never read telemetry to decide behavior.
- Semantic events: record facts such as `weapon.equipped`, `weapon.fired`, or `guard.alerted`.
- Sampled state: use periodic samplers for continuous values such as position, speed, or awareness.
- Diagnostic logs: continue using `UE_LOG` for configuration problems, warnings, and implementation details.
- No deterministic replay, input playback, state restoration, or event-sourced gameplay.

These boundaries keep the artifact useful to both humans watching the MP4 and agents querying the JSON timeline.

## Verification

`Tests/Verify-SessionTelemetry.ps1` validates a fresh session's JSON structure, local-player animation sample, monotonically increasing timestamps, JPEG existence and dimensions, non-black frame content, and optional H.264 MP4 output.

```powershell
.\Plugins\SessionTelemetry\Tests\Verify-SessionTelemetry.ps1 `
    -ProjectRoot $PWD `
    -NotBefore (Get-Date).AddMinutes(-5) `
    -RequireVideo
```

The plugin also includes four Unreal automation tests under the `SessionTelemetry` test prefix.

## Current limitations

- Automatic video export supports Windows only.
- The recorder captures the first local player's view.
- Audio is not captured.
- Multiple-player presentation, input overlays, and event visualization layers are future consumers of the existing timeline.

## License

No license has been granted yet. All rights are reserved until a license is added explicitly.
