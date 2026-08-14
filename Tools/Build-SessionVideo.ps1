[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $SessionDirectory,

	[string] $FfmpegExecutable = '',

	[string] $OutputFileName = 'session.mp4',

	[int] $FrameWriteTimeoutSeconds = 30,

	[switch] $DisableInputOverlay,

	[double] $InputTapDisplaySeconds = 0.4,

	[double] $InputOverlayLeadSeconds = 0.1,

	[int] $InputOverlayBottomMargin = 24,

	[int] $FrameWidth = 480,

	[int] $FrameHeight = 270
)

$ErrorActionPreference = 'Stop'

function Format-AssTime
{
	param([int64] $Milliseconds)

	$centiseconds = [int64] [Math]::Floor([Math]::Max(0, $Milliseconds) / 10.0)
	$hours = [int64] [Math]::Floor($centiseconds / 360000)
	$minutes = [int64] [Math]::Floor(($centiseconds % 360000) / 6000)
	$seconds = [int64] [Math]::Floor(($centiseconds % 6000) / 100)
	$remainder = $centiseconds % 100
	return ('{0}:{1:00}:{2:00}.{3:00}' -f $hours, $minutes, $seconds, $remainder)
}

function ConvertTo-AssText
{
	param([string] $Text)

	return $Text.Replace('\', '\\').Replace('{', '\{').Replace('}', '\}').Replace("`r", '').Replace("`n", ' ')
}

try
{
	if ([string]::IsNullOrWhiteSpace($FfmpegExecutable))
	{
		$FfmpegExecutable = [IO.Path]::GetFullPath(
			(Join-Path $PSScriptRoot '..\ThirdParty\FFmpeg\Win64\ffmpeg.exe'))
	}
	if (-not (Test-Path -LiteralPath $FfmpegExecutable -PathType Leaf))
	{
		throw "Bundled FFmpeg is missing: $FfmpegExecutable"
	}

	$session = (Resolve-Path -LiteralPath $SessionDirectory).Path
	$timelinePath = Join-Path $session 'timeline.json'
	if (-not (Test-Path -LiteralPath $timelinePath))
	{
		throw "Timeline is missing: $timelinePath"
	}

	# Windows PowerShell 5.1 returns a top-level JSON array as one pipeline object;
	# assigning directly lets the following pipeline enumerate its elements.
	$events = Get-Content -LiteralPath $timelinePath -Raw | ConvertFrom-Json
	$frames = @($events | Where-Object type -eq 'frame' | Sort-Object { [int64] $_.t }, { [int64] $_.f })
	if ($frames.Count -eq 0)
	{
		throw "Timeline contains no frame events: $timelinePath"
	}

	$framePaths = @($frames | ForEach-Object {
		Join-Path $session (([string] $_.file) -replace '/', [IO.Path]::DirectorySeparatorChar)
	})
	$deadline = (Get-Date).AddSeconds([Math]::Max(0, $FrameWriteTimeoutSeconds))
	do
	{
		$missing = @($framePaths | Where-Object { -not (Test-Path -LiteralPath $_) })
		if ($missing.Count -eq 0) { break }
		Start-Sleep -Milliseconds 100
	}
	while ((Get-Date) -lt $deadline)

	if ($missing.Count -ne 0)
	{
		throw "$($missing.Count) referenced frame(s) were not written; first missing: $($missing[0])"
	}

	$durations = [Collections.Generic.List[double]]::new()
	for ($index = 0; $index -lt ($frames.Count - 1); ++$index)
	{
		$seconds = ([int64] $frames[$index + 1].t - [int64] $frames[$index].t) / 1000.0
		$durations.Add([Math]::Min(10.0, [Math]::Max(0.001, $seconds)))
	}

	if ($durations.Count -gt 0)
	{
		$orderedDurations = @($durations | Sort-Object)
		$lastDuration = $orderedDurations[[int] [Math]::Floor(($orderedDurations.Count - 1) / 2)]
	}
	else
	{
		$lastDuration = 1.0
	}

	$concatLines = [Collections.Generic.List[string]]::new()
	$concatLines.Add('ffconcat version 1.0')
	for ($index = 0; $index -lt $framePaths.Count; ++$index)
	{
		$ffconcatPath = ([IO.Path]::GetFullPath($framePaths[$index])).Replace('\', '/').Replace("'", "'\''")
		$concatLines.Add("file '$ffconcatPath'")
		$duration = if ($index -lt $durations.Count) { $durations[$index] } else { $lastDuration }
		$concatLines.Add(('duration {0:0.000000}' -f $duration))
	}

	# FFmpeg ignores the final duration unless the last image appears once more.
	$lastPath = ([IO.Path]::GetFullPath($framePaths[-1])).Replace('\', '/').Replace("'", "'\''")
	$concatLines.Add("file '$lastPath'")

	$concatPath = Join-Path $session 'frames.ffconcat'
	[IO.File]::WriteAllLines($concatPath, $concatLines, [Text.UTF8Encoding]::new($false))

	$overlayPath = Join-Path $session 'overlays.ass'
	$overlayLines = [Collections.Generic.List[string]]::new()
	$overlayLines.Add('[Script Info]')
	$overlayLines.Add('ScriptType: v4.00+')
	$overlayLines.Add("PlayResX: $([Math]::Max(16, $FrameWidth))")
	$overlayLines.Add("PlayResY: $([Math]::Max(16, $FrameHeight))")
	$overlayLines.Add('ScaledBorderAndShadow: yes')
	$overlayLines.Add('WrapStyle: 2')
	$overlayLines.Add('')
	$overlayLines.Add('[V4+ Styles]')
	$overlayLines.Add('Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding')
	$margin = [Math]::Max(0, $InputOverlayBottomMargin)
	$fontSize = [Math]::Max(16, [Math]::Round([Math]::Max(16, $FrameHeight) * 0.089))
	$overlayLines.Add("Style: Input,Arial,$fontSize,&H00FFFFFF,&H00FFFFFF,&H80000000,&H80000000,1,0,0,0,100,100,0,0,3,2,0,2,20,20,$margin,1")
	$overlayLines.Add('')
	$overlayLines.Add('[Events]')
	$overlayLines.Add('Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text')

	$inputOverlayCount = 0
	if (-not $DisableInputOverlay)
	{
		$tapDurationMs = [int64] [Math]::Round([Math]::Max(0.05, $InputTapDisplaySeconds) * 1000.0)
		$leadDurationMs = [int64] [Math]::Round([Math]::Max(0.0, $InputOverlayLeadSeconds) * 1000.0)
		$inputEvents = @($events | Where-Object type -eq 'input' |
			Sort-Object @{ Expression = { [int64] $_.t } }, @{ Expression = { [int64] $_.f } })
		$openInputs = @{}
		$intervals = [Collections.Generic.List[object]]::new()

		foreach ($inputEvent in $inputEvents)
		{
			$key = [string] $inputEvent.key
			$label = [string] $inputEvent.label
			$phase = ([string] $inputEvent.phase).ToLowerInvariant()
			if ([string]::IsNullOrWhiteSpace($key)) { $key = $label }
			if ([string]::IsNullOrWhiteSpace($label)) { $label = $key }
			if ([string]::IsNullOrWhiteSpace($key) -or [string]::IsNullOrWhiteSpace($label)) { continue }

			$identity = "$([int] $inputEvent.controllerId)|$key"
			$timeMs = [int64] $inputEvent.t
			if ($phase -eq 'pressed')
			{
				if ($openInputs.ContainsKey($identity))
				{
					$previous = $openInputs[$identity]
					$intervals.Add([pscustomobject]@{
						Id = $identity
						Label = $previous.Label
						Start = [int64] $previous.Start
						End = [Math]::Max([int64] $previous.Start + $tapDurationMs, $timeMs)
					})
				}
				$openInputs[$identity] = [pscustomobject]@{ Label = $label; Start = $timeMs }
			}
			elseif ($phase -eq 'released' -and $openInputs.ContainsKey($identity))
			{
				$pressed = $openInputs[$identity]
				$intervals.Add([pscustomobject]@{
					Id = $identity
					Label = $pressed.Label
					Start = [int64] $pressed.Start
					End = [Math]::Max([int64] $pressed.Start + $tapDurationMs, $timeMs)
				})
				$openInputs.Remove($identity)
			}
		}

		foreach ($entry in $openInputs.GetEnumerator())
		{
			$intervals.Add([pscustomobject]@{
				Id = [string] $entry.Key
				Label = [string] $entry.Value.Label
				Start = [int64] $entry.Value.Start
				End = [int64] $entry.Value.Start + $tapDurationMs
			})
		}

		$boundaries = [Collections.Generic.List[object]]::new()
		foreach ($interval in $intervals)
		{
			$adjustedStart = [Math]::Max(0, [int64] $interval.Start - $leadDurationMs)
			$boundaries.Add([pscustomobject]@{ Time = $adjustedStart; Order = 1; Id = $interval.Id; Label = $interval.Label })
			$boundaries.Add([pscustomobject]@{ Time = [int64] $interval.End; Order = 0; Id = $interval.Id; Label = $interval.Label })
		}

		$orderedBoundaries = @($boundaries | Sort-Object @{ Expression = { [int64] $_.Time } }, Order)
		$activeInputs = [ordered]@{}
		$cursorMs = [int64] 0
		$boundaryIndex = 0
		while ($boundaryIndex -lt $orderedBoundaries.Count)
		{
			$boundaryTime = [int64] $orderedBoundaries[$boundaryIndex].Time
			if ($boundaryTime -gt $cursorMs -and $activeInputs.Count -gt 0)
			{
				$labels = @($activeInputs.GetEnumerator() | ForEach-Object Value | Sort-Object -Unique)
				$text = ($labels | ForEach-Object { '[ ' + (ConvertTo-AssText ([string] $_)) + ' ]' }) -join '\h'
				$overlayLines.Add("Dialogue: 0,$(Format-AssTime $cursorMs),$(Format-AssTime $boundaryTime),Input,,0,0,0,,$text")
				++$inputOverlayCount
			}

			while ($boundaryIndex -lt $orderedBoundaries.Count -and
				[int64] $orderedBoundaries[$boundaryIndex].Time -eq $boundaryTime)
			{
				$boundary = $orderedBoundaries[$boundaryIndex]
				if ([int] $boundary.Order -eq 0)
				{
					$activeInputs.Remove([string] $boundary.Id)
				}
				else
				{
					$activeInputs[[string] $boundary.Id] = [string] $boundary.Label
				}
				++$boundaryIndex
			}
			$cursorMs = $boundaryTime
		}
	}

	[IO.File]::WriteAllLines($overlayPath, $overlayLines, [Text.UTF8Encoding]::new($false))

	$command = Get-Command -Name $FfmpegExecutable -CommandType Application -ErrorAction Stop
	$cleanOutputName = [IO.Path]::GetFileName($OutputFileName)
	if ([string]::IsNullOrWhiteSpace($cleanOutputName))
	{
		throw 'Output filename is empty.'
	}

	$outputPath = Join-Path $session $cleanOutputName
	$tempPath = Join-Path $session (([IO.Path]::GetFileNameWithoutExtension($cleanOutputName)) + '.tmp.mp4')
	$videoFilters = [Collections.Generic.List[string]]::new()
	$videoFilters.Add('scale=in_range=full:out_range=tv')
	if ($inputOverlayCount -gt 0)
	{
		$assFilterPath = ([IO.Path]::GetFullPath($overlayPath)).Replace('\', '/').Replace(':', '\:').Replace("'", "\'")
		$videoFilters.Add("subtitles=filename='$assFilterPath':original_size=$([Math]::Max(16, $FrameWidth))x$([Math]::Max(16, $FrameHeight))")
	}
	$videoFilters.Add('format=yuv420p')
	$arguments = @(
		'-hide_banner', '-loglevel', 'warning', '-y',
		'-safe', '0', '-f', 'concat', '-i', $concatPath,
		'-an', '-vf', ($videoFilters -join ','),
		'-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-color_range', 'tv',
		'-movflags', '+faststart', '-fps_mode', 'vfr', $tempPath
	)

	$startInfo = [Diagnostics.ProcessStartInfo]::new()
	$startInfo.FileName = $command.Path
	$startInfo.UseShellExecute = $false
	$startInfo.CreateNoWindow = $true
	$startInfo.RedirectStandardOutput = $true
	$startInfo.RedirectStandardError = $true
	$startInfo.Arguments = (($arguments | ForEach-Object {
		'"' + ([string] $_).Replace('"', '\"') + '"'
	}) -join ' ')

	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $startInfo
	$null = $process.Start()
	$standardOutput = $process.StandardOutput.ReadToEnd()
	$standardError = $process.StandardError.ReadToEnd()
	$process.WaitForExit()
	$exitCode = $process.ExitCode
	@("ffmpeg=$($command.Path)", $standardOutput, $standardError) |
		Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
		Set-Content -LiteralPath (Join-Path $session 'video-export.log') -Encoding utf8
	$process.Dispose()
	if ($exitCode -ne 0)
	{
		throw "FFmpeg exited with code $exitCode. See video-export.log."
	}

	if (-not (Test-Path -LiteralPath $tempPath) -or (Get-Item -LiteralPath $tempPath).Length -le 0)
	{
		throw "FFmpeg did not create a non-empty video: $tempPath"
	}

	Move-Item -LiteralPath $tempPath -Destination $outputPath -Force
	Remove-Item -LiteralPath (Join-Path $session 'video-export-error.txt') -ErrorAction SilentlyContinue
	"Created $outputPath from $($frames.Count) frames with $inputOverlayCount input overlay segment(s)." |
		Add-Content -LiteralPath (Join-Path $session 'video-export.log')
	Write-Output $outputPath
}
catch
{
	$errorPath = Join-Path $SessionDirectory 'video-export-error.txt'
	$_.Exception.ToString() | Set-Content -LiteralPath $errorPath -Encoding utf8
	Write-Error $_
	exit 1
}
