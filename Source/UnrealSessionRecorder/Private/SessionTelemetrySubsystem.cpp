#include "SessionTelemetrySubsystem.h"

#include "SessionTelemetryEventBuffer.h"
#include "SessionTelemetryPawnSampler.h"
#include "SessionTelemetrySettings.h"

#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "IImageWrapperModule.h"
#include "ImageCore.h"
#include "InputKeyEventArgs.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SessionTelemetrySubsystem)

DEFINE_LOG_CATEGORY(LogSessionTelemetry)

struct FSessionTelemetryReadbackState
{
	TUniquePtr<FRHIGPUTextureReadback> Readback;
};

namespace
{
	TAtomic<bool> GEncodeFailureReported{false};

	bool IsSessionDirectoryName(const FString& Name)
	{
		if (Name.Len() != 15 || Name[8] != TEXT('-'))
		{
			return false;
		}

		for (int32 Index{0}; Index < Name.Len(); ++Index)
		{
			if (Index != 8 && !FChar::IsDigit(Name[Index]))
			{
				return false;
			}
		}
		return true;
	}

	void EncodeAndWriteJpeg(TArray<FColor>&& Pixels, const int32 Width, const int32 Height, const int32 Quality,
		const FString& Path)
	{
		auto& ImageWrapperModule{FModuleManager::GetModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"))};
		TArray64<uint8> Compressed;
		const FImageView View{Pixels.GetData(), Width, Height};
		const bool bCompressed{ImageWrapperModule.CompressImage(Compressed, EImageFormat::JPEG, View, Quality)};
		const bool bWritten{bCompressed && FFileHelper::SaveArrayToFile(Compressed, *Path)};
		if (!bWritten && !GEncodeFailureReported.Exchange(true))
		{
			UE_LOG(LogSessionTelemetry, Warning, TEXT("SESSION TELEMETRY failed to encode or write frame: %s"), *Path);
		}
	}
}

USessionTelemetrySubsystem::~USessionTelemetrySubsystem() = default;

bool USessionTelemetrySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void USessionTelemetrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Settings = GetDefault<USessionTelemetrySettings>();
	if (Settings != nullptr && Settings->bEnabled)
	{
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		EventBuffer = MakeUnique<FSessionTelemetryEventBuffer>();
	}
}

void USessionTelemetrySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (Settings == nullptr || !Settings->bEnabled || !EventBuffer.IsValid())
	{
		return;
	}
	if (InWorld.WorldType != EWorldType::PIE)
	{
		PruneOldSessionsForNewSession(Settings->MaximumRetainedSessions);
	}
	if (!EnsureRunDirectory())
	{
		return;
	}

	bSessionActive = true;
	if (Settings->bCaptureInput)
	{
		if (UGameViewportClient* ViewportClient{InWorld.GetGameViewport()})
		{
			BoundViewportClient = ViewportClient;
			InputKeyDelegateHandle = ViewportClient->OnInputKey().AddUObject(this, &ThisClass::HandleInputKey);
		}
	}

	auto Header{MakeShared<FJsonObject>()};
	Header->SetStringField(TEXT("project"), FApp::GetProjectName());
	Header->SetStringField(TEXT("level"), InWorld.GetMapName());
	Header->SetStringField(TEXT("wallClockStart"), FDateTime::Now().ToIso8601());
	Header->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());

	const FString GitHash{TryReadGitHash()};
	if (!GitHash.IsEmpty())
	{
		Header->SetStringField(TEXT("gitHash"), GitHash);
	}

	EventBuffer->Append(TEXT("header"), Header, 0, GFrameCounter);

	if (Settings->FrameCaptureHz > 0.0f)
	{
		SetupFrameCapture(InWorld);
	}

	if (Settings->SampleHz > 0.0f)
	{
		InWorld.GetTimerManager().SetTimer(SampleTimer, this, &ThisClass::HandleSampleTick,
			1.0f / Settings->SampleHz, true);
	}

	InWorld.GetTimerManager().SetTimer(FlushTimer, this, &ThisClass::FlushToDisk,
		FMath::Max(1.0f, Settings->FlushIntervalSeconds), true);

	UE_LOG(LogSessionTelemetry, Display, TEXT("SESSION TELEMETRY started dir=%s sampleHz=%.1f frameHz=%.1f"),
		*RunDirectory, Settings->SampleHz, Settings->FrameCaptureHz);
}

void USessionTelemetrySubsystem::Deinitialize()
{
	FinalizeSession();

	EventBuffer.Reset();
	Settings = nullptr;

	Super::Deinitialize();
}

void USessionTelemetrySubsystem::FinalizeSession()
{
	if (!bSessionActive)
	{
		return;
	}

	UnbindInputCapture();

	if (UWorld* World{GetWorld()})
	{
		World->GetTimerManager().ClearTimer(SampleTimer);
		World->GetTimerManager().ClearTimer(FrameCaptureTimer);
		World->GetTimerManager().ClearTimer(FlushTimer);
	}

	ReleasePendingReadback();

	FlushToDisk();
	UE_LOG(LogSessionTelemetry, Display, TEXT("SESSION TELEMETRY ended events=%d dir=%s"),
		EventBuffer.IsValid() ? EventBuffer->Num() : 0, *RunDirectory);
	LaunchVideoExport();

	bSessionActive = false;
	CaptureRig = nullptr;
	CaptureTarget = nullptr;
}

void USessionTelemetrySubsystem::PruneOldSessionsForNewSession(const int32 MaximumRetainedSessions)
{
	const int32 OldSessionsToKeep{FMath::Max(0, MaximumRetainedSessions - 1)};
	const FString RootDirectory{FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".session-telemetry")))};
	if (!IFileManager::Get().DirectoryExists(*RootDirectory))
	{
		return;
	}

	TArray<FString> DirectoryNames;
	IFileManager::Get().FindFiles(DirectoryNames, *FPaths::Combine(RootDirectory, TEXT("*")), false, true);
	DirectoryNames.RemoveAll([](const FString& Name) { return !IsSessionDirectoryName(Name); });
	DirectoryNames.Sort(TGreater<FString>());

	for (int32 Index{OldSessionsToKeep}; Index < DirectoryNames.Num(); ++Index)
	{
		const FString DirectoryPath{FPaths::Combine(RootDirectory, DirectoryNames[Index])};
		if (IFileManager::Get().DeleteDirectory(*DirectoryPath, false, true))
		{
			UE_LOG(LogSessionTelemetry, Display, TEXT("SESSION TELEMETRY pruned dir=%s"), *DirectoryPath);
		}
		else
		{
			UE_LOG(LogSessionTelemetry, Warning, TEXT("SESSION TELEMETRY failed to prune dir=%s"), *DirectoryPath);
		}
	}
}

FString USessionTelemetrySubsystem::GetBundledFfmpegPath()
{
	const TSharedPtr<IPlugin> Plugin{IPluginManager::Get().FindPlugin(TEXT("UnrealSessionRecorder"))};
	return Plugin.IsValid()
		? FPaths::ConvertRelativePathToFull(FPaths::Combine(Plugin->GetBaseDir(), TEXT("ThirdParty"),
			TEXT("FFmpeg"), TEXT("Win64"), TEXT("ffmpeg.exe")))
		: FString();
}

void USessionTelemetrySubsystem::Record(const UObject* WorldContextObject, const FString& Type,
	const TSharedRef<FJsonObject>& Fields)
{
	const UWorld* World{IsValid(WorldContextObject) ? WorldContextObject->GetWorld() : nullptr};
	auto* Subsystem{World != nullptr ? World->GetSubsystem<USessionTelemetrySubsystem>() : nullptr};
	if (IsValid(Subsystem))
	{
		Subsystem->RecordInternal(Type, Fields);
	}
}

void USessionTelemetrySubsystem::RecordInternal(const FString& Type, const TSharedRef<FJsonObject>& Fields)
{
	if (!bSessionActive || !EventBuffer.IsValid() || Type.IsEmpty())
	{
		return;
	}

	EventBuffer->Append(Type, Fields, NowMs(), GFrameCounter);
}

bool USessionTelemetrySubsystem::EnsureRunDirectory()
{
	const FString Stamp{FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))};
	RunDirectory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".session-telemetry"), Stamp));
	FramesDirectory = FPaths::Combine(RunDirectory, TEXT("frames"));

	IFileManager::Get().MakeDirectory(*FramesDirectory, true);
	if (!IFileManager::Get().DirectoryExists(*FramesDirectory))
	{
		UE_LOG(LogSessionTelemetry, Error, TEXT("SESSION TELEMETRY cannot create run directory: %s"),
			*FramesDirectory);
		return false;
	}

	return true;
}

void USessionTelemetrySubsystem::FlushToDisk()
{
	if (!bSessionActive || !EventBuffer.IsValid())
	{
		return;
	}

	const FString TimelinePath{FPaths::Combine(RunDirectory, TEXT("timeline.json"))};
	if (!FFileHelper::SaveStringToFile(EventBuffer->Serialize(), *TimelinePath) && !bFlushWarningEmitted)
	{
		bFlushWarningEmitted = true;
		UE_LOG(LogSessionTelemetry, Warning, TEXT("SESSION TELEMETRY failed to write timeline: %s"), *TimelinePath);
	}
}

void USessionTelemetrySubsystem::HandleSampleTick()
{
	UWorld* World{GetWorld()};
	if (!bSessionActive || World == nullptr)
	{
		return;
	}

	const APawn* LocalPlayerPawn{UGameplayStatics::GetPlayerPawn(World, 0)};
	for (TActorIterator<APawn> Iterator{World}; Iterator; ++Iterator)
	{
		APawn* Pawn{*Iterator};
		if (IsValid(Pawn))
		{
			Record(Pawn, TEXT("sample"), FSessionTelemetryPawnSampler::Sample(*Pawn, LocalPlayerPawn));
		}
	}
}

void USessionTelemetrySubsystem::HandleFrameCaptureTick()
{
	PollFrameReadback();
	KickFrameCapture();
}

bool USessionTelemetrySubsystem::SetupFrameCapture(UWorld& World)
{
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.Name = TEXT("SessionTelemetryCaptureRig");

	CaptureRig = World.SpawnActor<ASceneCapture2D>(SpawnParameters);
	if (!IsValid(CaptureRig))
	{
		UE_LOG(LogSessionTelemetry, Warning, TEXT("SESSION TELEMETRY frame capture rig failed to spawn; frames off."));
		return false;
	}

	CaptureRig->SetActorHiddenInGame(true);
	CaptureTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("SessionTelemetryFrameTarget"));
	CaptureTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	CaptureTarget->ClearColor = FLinearColor::Black;
	CaptureTarget->bAutoGenerateMips = false;
	CaptureTarget->InitAutoFormat(Settings->FrameCaptureWidth, Settings->FrameCaptureHeight);
	CaptureTarget->UpdateResourceImmediate(true);

	USceneCaptureComponent2D* Capture{CaptureRig->GetCaptureComponent2D()};
	if (Capture == nullptr)
	{
		UE_LOG(LogSessionTelemetry, Warning,
			TEXT("SESSION TELEMETRY frame capture component is missing; frames off."));
		CaptureRig->Destroy();
		CaptureRig = nullptr;
		CaptureTarget = nullptr;
		return false;
	}

	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->TextureTarget = CaptureTarget;

	World.GetTimerManager().SetTimer(FrameCaptureTimer, this, &ThisClass::HandleFrameCaptureTick,
		1.0f / Settings->FrameCaptureHz, true);
	return true;
}

void USessionTelemetrySubsystem::KickFrameCapture()
{
	if (!bSessionActive || Settings == nullptr || Settings->FrameCaptureHz <= 0.0f || !IsValid(CaptureRig) ||
		!IsValid(CaptureTarget) || PendingReadback.IsValid())
	{
		return;
	}

	UWorld* World{GetWorld()};
	const APlayerController* PlayerController{World != nullptr ? World->GetFirstPlayerController() : nullptr};
	if (PlayerController == nullptr)
	{
		return;
	}

	FVector ViewLocation{ForceInit};
	FRotator ViewRotation{ForceInit};
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	CaptureRig->SetActorLocationAndRotation(ViewLocation, ViewRotation);

	USceneCaptureComponent2D* Capture{CaptureRig->GetCaptureComponent2D()};
	if (Capture == nullptr)
	{
		return;
	}

	if (IsValid(PlayerController->PlayerCameraManager))
	{
		Capture->FOVAngle = PlayerController->PlayerCameraManager->GetFOVAngle();
	}

	Capture->CaptureScene();
	FTextureRenderTargetResource* RenderTargetResource{CaptureTarget->GameThread_GetRenderTargetResource()};
	if (RenderTargetResource == nullptr)
	{
		return;
	}

	PendingReadback = MakeUnique<FSessionTelemetryReadbackState>();
	PendingReadback->Readback = MakeUnique<FRHIGPUTextureReadback>(TEXT("SessionTelemetryFrame"));
	FRHIGPUTextureReadback* ReadbackPointer{PendingReadback->Readback.Get()};

	ENQUEUE_RENDER_COMMAND(SessionTelemetryEnqueueReadback)
	([ReadbackPointer, RenderTargetResource](FRHICommandListImmediate& RHICmdList)
	{
		if (FRHITexture* Texture{RenderTargetResource->GetRenderTargetTexture()})
		{
			ReadbackPointer->EnqueueCopy(RHICmdList, Texture);
		}
	});

}

void USessionTelemetrySubsystem::PollFrameReadback()
{
	if (!PendingReadback.IsValid() || !PendingReadback->Readback.IsValid() ||
		!PendingReadback->Readback->IsReady())
	{
		return;
	}

	TUniquePtr<FRHIGPUTextureReadback> Readback{MoveTemp(PendingReadback->Readback)};
	PendingReadback.Reset();

	const int64 TimeMs{NowMs()};
	const FString FileName{FSessionTelemetryEventBuffer::FrameFileName(TimeMs)};
	const FString Path{FPaths::Combine(FramesDirectory, FileName)};

	auto Fields{MakeShared<FJsonObject>()};
	Fields->SetStringField(TEXT("file"), FString::Printf(TEXT("frames/%s"), *FileName));
	EventBuffer->Append(TEXT("frame"), Fields, TimeMs, GFrameCounter);

	const int32 Width{Settings->FrameCaptureWidth};
	const int32 Height{Settings->FrameCaptureHeight};
	const int32 Quality{Settings->JpegQuality};

	ENQUEUE_RENDER_COMMAND(SessionTelemetryLockReadback)
	([Readback = MoveTemp(Readback), Path, Width, Height, Quality](FRHICommandListImmediate&) mutable
	{
		int32 RowPitchInPixels{0};
		void* Data{Readback->Lock(RowPitchInPixels, nullptr)};
		if (Data == nullptr)
		{
			return;
		}

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);
		const auto* Source{static_cast<const FColor*>(Data)};
		for (int32 Row{0}; Row < Height; ++Row)
		{
			FMemory::Memcpy(Pixels.GetData() + Row * Width, Source + Row * RowPitchInPixels,
				Width * sizeof(FColor));
		}

		Readback->Unlock();
		Async(EAsyncExecution::ThreadPool,
			[Pixels = MoveTemp(Pixels), Path, Width, Height, Quality]() mutable
			{
				EncodeAndWriteJpeg(MoveTemp(Pixels), Width, Height, Quality, Path);
			});
	});
}

void USessionTelemetrySubsystem::ReleasePendingReadback()
{
	if (!PendingReadback.IsValid() || !PendingReadback->Readback.IsValid())
	{
		PendingReadback.Reset();
		return;
	}

	TUniquePtr<FRHIGPUTextureReadback> Readback{MoveTemp(PendingReadback->Readback)};
	PendingReadback.Reset();
	ENQUEUE_RENDER_COMMAND(SessionTelemetryDisposeReadback)
	([Readback = MoveTemp(Readback)](FRHICommandListImmediate&) mutable
	{
		Readback.Reset();
	});
}

void USessionTelemetrySubsystem::UnbindInputCapture()
{
	if (BoundViewportClient.IsValid() && InputKeyDelegateHandle.IsValid())
	{
		BoundViewportClient->OnInputKey().Remove(InputKeyDelegateHandle);
	}
	InputKeyDelegateHandle.Reset();
	BoundViewportClient.Reset();
}

void USessionTelemetrySubsystem::HandleInputKey(const FInputKeyEventArgs& EventArgs)
{
	if (!bSessionActive || (EventArgs.Event != IE_Pressed && EventArgs.Event != IE_Released))
	{
		return;
	}

	auto Fields{MakeShared<FJsonObject>()};
	Fields->SetStringField(TEXT("key"), EventArgs.Key.GetFName().ToString());
	Fields->SetStringField(TEXT("label"), EventArgs.Key.GetDisplayName(false).ToString());
	Fields->SetStringField(TEXT("phase"), EventArgs.Event == IE_Pressed ? TEXT("pressed") : TEXT("released"));
	Fields->SetStringField(TEXT("device"), EventArgs.bIsTouchEvent ? TEXT("touch") :
		EventArgs.Key.IsGamepadKey() ? TEXT("gamepad") :
		EventArgs.Key.IsMouseButton() ? TEXT("mouse") : TEXT("keyboard"));
	Fields->SetNumberField(TEXT("controllerId"), EventArgs.ControllerId);
	RecordInternal(TEXT("input"), Fields);
}

void USessionTelemetrySubsystem::LaunchVideoExport() const
{
	if (Settings == nullptr || !Settings->bBuildVideoOnSessionEnd || RunDirectory.IsEmpty())
	{
		return;
	}

#if PLATFORM_WINDOWS
	const TSharedPtr<IPlugin> Plugin{IPluginManager::Get().FindPlugin(TEXT("UnrealSessionRecorder"))};
	if (!Plugin.IsValid())
	{
		UE_LOG(LogSessionTelemetry, Warning, TEXT("SESSION TELEMETRY cannot locate its video export script."));
		return;
	}

	const FString ScriptPath{FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tools"), TEXT("Build-SessionVideo.ps1"))};
	if (!FPaths::FileExists(ScriptPath))
	{
		UE_LOG(LogSessionTelemetry, Warning, TEXT("SESSION TELEMETRY video export script is missing: %s"),
			*ScriptPath);
		return;
	}

	const FString VideoFileName{FPaths::GetCleanFilename(Settings->VideoFileName.IsEmpty()
		? TEXT("session.mp4") : Settings->VideoFileName)};
	const FString FfmpegExecutable{GetBundledFfmpegPath()};
	if (!FPaths::FileExists(FfmpegExecutable))
	{
		UE_LOG(LogSessionTelemetry, Error, TEXT("SESSION TELEMETRY bundled FFmpeg is missing: %s"),
			*FfmpegExecutable);
		return;
	}
	const FString InputOverlayArguments{Settings->bRenderInputOverlay
		? FString::Printf(TEXT(" -InputTapDisplaySeconds %.3f -InputOverlayLeadSeconds %.3f -InputOverlayBottomMargin %d -FrameWidth %d -FrameHeight %d"),
			FMath::Max(0.05f, Settings->InputTapDisplaySeconds),
			FMath::Max(0.0f, Settings->InputOverlayLeadSeconds),
			FMath::Max(0, Settings->InputOverlayBottomMargin),
			FMath::Max(16, Settings->FrameCaptureWidth),
			FMath::Max(16, Settings->FrameCaptureHeight))
		: FString(TEXT(" -DisableInputOverlay"))};
	const FString Arguments{FString::Printf(
		TEXT("-NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%s\" -SessionDirectory \"%s\" -FfmpegExecutable \"%s\" -OutputFileName \"%s\"%s"),
		*ScriptPath, *RunDirectory, *FfmpegExecutable, *VideoFileName, *InputOverlayArguments)};

	const FString WindowsDirectory{FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot"))};
	const FString PowerShellPath{FPaths::Combine(WindowsDirectory, TEXT("System32"),
		TEXT("WindowsPowerShell"), TEXT("v1.0"), TEXT("powershell.exe"))};
	const FString LaunchLogPath{FPaths::Combine(RunDirectory, TEXT("video-launch.log"))};
	Async(EAsyncExecution::ThreadPool, [PowerShellPath, Arguments, LaunchLogPath]()
	{
		int32 ReturnCode{INDEX_NONE};
		FString StandardOutput;
		FString StandardError;
		const bool bStarted{FPlatformProcess::ExecProcess(*PowerShellPath, *Arguments, &ReturnCode,
			&StandardOutput, &StandardError)};
		const FString LaunchResult{FString::Printf(
			TEXT("started=%d exitCode=%d\nstdout:\n%s\nstderr:\n%s\n"),
			bStarted ? 1 : 0, ReturnCode, *StandardOutput, *StandardError)};
		FFileHelper::SaveStringToFile(LaunchResult, *LaunchLogPath);
	});
	UE_LOG(LogSessionTelemetry, Display, TEXT("SESSION TELEMETRY video export scheduled dir=%s"), *RunDirectory);
#else
	UE_LOG(LogSessionTelemetry, Warning, TEXT("SESSION TELEMETRY automatic video export currently supports Windows only."));
#endif
}

int64 USessionTelemetrySubsystem::NowMs() const
{
	const UWorld* World{GetWorld()};
	return World != nullptr ? static_cast<int64>(FMath::RoundToDouble(World->GetTimeSeconds() * 1000.0)) : 0;
}

FString USessionTelemetrySubsystem::TryReadGitHash()
{
	const FString GitDirectory{FPaths::Combine(FPaths::ProjectDir(), TEXT(".git"))};
	FString HeadContents;
	if (!FFileHelper::LoadFileToString(HeadContents, *FPaths::Combine(GitDirectory, TEXT("HEAD"))))
	{
		return FString();
	}

	HeadContents.TrimStartAndEndInline();
	if (!HeadContents.StartsWith(TEXT("ref:")))
	{
		return HeadContents;
	}

	FString RefPath{HeadContents.RightChop(4)};
	RefPath.TrimStartAndEndInline();

	FString HashContents;
	if (FFileHelper::LoadFileToString(HashContents, *FPaths::Combine(GitDirectory, RefPath)))
	{
		HashContents.TrimStartAndEndInline();
		return HashContents;
	}

	return FString();
}
