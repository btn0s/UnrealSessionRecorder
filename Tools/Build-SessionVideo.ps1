[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $SessionDirectory,

	[string] $FfmpegExecutable = 'ffmpeg',

	[string] $OutputFileName = 'session.mp4',

	[int] $FrameWriteTimeoutSeconds = 30
)

$ErrorActionPreference = 'Stop'

try
{
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

	$command = Get-Command -Name $FfmpegExecutable -CommandType Application -ErrorAction Stop
	$cleanOutputName = [IO.Path]::GetFileName($OutputFileName)
	if ([string]::IsNullOrWhiteSpace($cleanOutputName))
	{
		throw 'Output filename is empty.'
	}

	$outputPath = Join-Path $session $cleanOutputName
	$tempPath = Join-Path $session (([IO.Path]::GetFileNameWithoutExtension($cleanOutputName)) + '.tmp.mp4')
	$arguments = @(
		'-hide_banner', '-loglevel', 'warning', '-y',
		'-safe', '0', '-f', 'concat', '-i', $concatPath,
		'-an', '-vf', 'scale=in_range=full:out_range=tv,format=yuv420p',
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
	@($standardOutput, $standardError) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
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
	"Created $outputPath from $($frames.Count) frames." | Add-Content -LiteralPath (Join-Path $session 'video-export.log')
	Write-Output $outputPath
}
catch
{
	$errorPath = Join-Path $SessionDirectory 'video-export-error.txt'
	$_.Exception.ToString() | Set-Content -LiteralPath $errorPath -Encoding utf8
	Write-Error $_
	exit 1
}
