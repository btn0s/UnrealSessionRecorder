[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $ProjectRoot,

	[Parameter(Mandatory = $true)]
	[datetime] $NotBefore,

	[switch] $RequireVideo
)

$ErrorActionPreference = 'Stop'

$telemetryRoot = Join-Path $ProjectRoot '.session-telemetry'
$session = Get-ChildItem -LiteralPath $telemetryRoot -Directory -ErrorAction SilentlyContinue |
	Where-Object { $_.CreationTime -ge $NotBefore } |
	Sort-Object Name -Descending |
	Select-Object -First 1

if ($null -eq $session)
{
	throw "No telemetry session newer than $($NotBefore.ToString('o'))."
}

$timelinePath = Join-Path $session.FullName 'timeline.json'
$events = @(Get-Content -LiteralPath $timelinePath -Raw | ConvertFrom-Json)
if ($events.Count -eq 0)
{
	throw "Timeline is empty: $timelinePath"
}

$headers = @($events | Where-Object type -eq 'header')
$samples = @($events | Where-Object type -eq 'sample')
$localSamples = @($samples | Where-Object role -eq 'localPlayer')
$richLocalSamples = @($localSamples | Where-Object { $null -ne $_.PSObject.Properties['animInstance'] })
$frames = @($events | Where-Object type -eq 'frame')

if ($headers.Count -ne 1) { throw "Expected one header; found $($headers.Count)." }
if ($samples.Count -eq 0) { throw 'No sample events found.' }
if ($localSamples.Count -eq 0) { throw 'No local-player sample found.' }
if ($richLocalSamples.Count -eq 0) { throw 'No local-player sample contains an animation instance.' }
if ($frames.Count -eq 0) { throw 'No frame events found.' }

$requiredSampleFields = @('pos', 'rot', 'vel', 'speed', 'animInstance')
foreach ($field in $requiredSampleFields)
{
	if ($null -eq $richLocalSamples[0].PSObject.Properties[$field])
	{
		throw "Local-player sample is missing '$field'."
	}
}

for ($index = 1; $index -lt $events.Count; ++$index)
{
	if ([int64] $events[$index].t -lt [int64] $events[$index - 1].t)
	{
		throw "Timeline timestamp decreased at event $index."
	}
}

$allFramePaths = @($frames | ForEach-Object {
	Join-Path $session.FullName (([string] $_.file) -replace '/', [IO.Path]::DirectorySeparatorChar)
})
$writeDeadline = (Get-Date).AddSeconds(15)
do
{
	$missingFramePaths = @($allFramePaths | Where-Object { -not (Test-Path -LiteralPath $_) })
	if ($missingFramePaths.Count -eq 0) { break }
	Start-Sleep -Milliseconds 100
}
while ((Get-Date) -lt $writeDeadline)

if ($missingFramePaths.Count -ne 0)
{
	throw "$($missingFramePaths.Count) frame event(s) have no JPEG; first missing: $($missingFramePaths[0])"
}

Add-Type -AssemblyName System.Drawing.Common
$inspected = 0
$nonBlack = $false
foreach ($frame in ($frames | Select-Object -First 5))
{
	$relative = [string] $frame.file
	$framePath = Join-Path $session.FullName ($relative -replace '/', [IO.Path]::DirectorySeparatorChar)
	if ([IO.Path]::GetFileName($framePath) -notmatch '^t(?<ms>\d{8})\.jpg$')
	{
		throw "Frame filename is not canonical: $framePath"
	}

	$expectedMs = [Math]::Min([Math]::Max([int64] $frame.t, 0), 99999999)
	if ([int64] $Matches.ms -ne $expectedMs)
	{
		throw "Frame filename time $($Matches.ms) does not match event time $expectedMs."
	}

	$bitmap = [System.Drawing.Bitmap]::new($framePath)
	try
	{
		if ($bitmap.Width -ne 480 -or $bitmap.Height -ne 270)
		{
			throw "Unexpected frame size $($bitmap.Width)x$($bitmap.Height): $framePath"
		}

		foreach ($x in 48, 144, 240, 336, 432)
		{
			foreach ($y in 27, 81, 135, 189, 243)
			{
				$pixel = $bitmap.GetPixel($x, $y)
				if (($pixel.R + $pixel.G + $pixel.B) -gt 9)
				{
					$nonBlack = $true
				}
			}
		}
	}
	finally
	{
		$bitmap.Dispose()
	}

	++$inspected
}

if (-not $nonBlack)
{
	throw "All $inspected inspected frames are uniformly near-black."
}

$videoPath = Join-Path $session.FullName 'session.mp4'
if ($RequireVideo)
{
	if (-not (Test-Path -LiteralPath $videoPath))
	{
		throw "Session video is missing: $videoPath"
	}

	if ((Get-Item -LiteralPath $videoPath).Length -le 0)
	{
		throw "Session video is empty: $videoPath"
	}

	$ffprobe = Get-Command ffprobe -CommandType Application -ErrorAction Stop
	$probe = & $ffprobe.Path -v error -select_streams v:0 -show_entries 'stream=codec_name,width,height' -of json $videoPath |
		ConvertFrom-Json
	if ($LASTEXITCODE -ne 0 -or $probe.streams.Count -ne 1)
	{
		throw "FFprobe could not read one video stream: $videoPath"
	}

	$stream = $probe.streams[0]
	if ($stream.codec_name -ne 'h264' -or $stream.width -ne 480 -or $stream.height -ne 270)
	{
		throw "Unexpected video stream $($stream.codec_name) $($stream.width)x$($stream.height): $videoPath"
	}
}

[pscustomobject]@{
	Session = $session.FullName
	Events = $events.Count
	Samples = $samples.Count
	Frames = $frames.Count
	InspectedFrames = $inspected
	Video = $(if (Test-Path -LiteralPath $videoPath) { $videoPath } else { $null })
	Result = 'PASS'
} | Format-List
