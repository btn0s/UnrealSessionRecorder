#pragma once

#include "Dom/JsonObject.h"
#include "Subsystems/WorldSubsystem.h"

#include "SessionTelemetrySubsystem.generated.h"

class FSessionTelemetryEventBuffer;
struct FSessionTelemetryReadbackState;
struct FInputKeyEventArgs;
class ASceneCapture2D;
class UGameViewportClient;
class USessionTelemetrySettings;
class UTextureRenderTarget2D;

DECLARE_LOG_CATEGORY_EXTERN(LogSessionTelemetry, Log, All);

UCLASS()
class UNREALSESSIONRECORDER_API USessionTelemetrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual ~USessionTelemetrySubsystem() override;

	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	static void Record(const UObject* WorldContextObject, const FString& Type,
		const TSharedRef<FJsonObject>& Fields);

	void FinalizeSession();

	static void PruneOldSessionsForNewSession(int32 MaximumRetainedSessions);

	bool IsSessionActive() const
	{
		return bSessionActive;
	}

private:
	void RecordInternal(const FString& Type, const TSharedRef<FJsonObject>& Fields);

	bool EnsureRunDirectory();

	void FlushToDisk();

	void HandleSampleTick();

	void HandleFrameCaptureTick();

	bool SetupFrameCapture(UWorld& World);

	void KickFrameCapture();

	void PollFrameReadback();

	void ReleasePendingReadback();

	void UnbindInputCapture();

	void HandleInputKey(const FInputKeyEventArgs& EventArgs);

	void LaunchVideoExport() const;

	int64 NowMs() const;

	static FString TryReadGitHash();

private:
	const USessionTelemetrySettings* Settings{nullptr};

	TUniquePtr<FSessionTelemetryEventBuffer> EventBuffer;

	FString RunDirectory;

	FString FramesDirectory;

	bool bSessionActive{false};

	bool bFlushWarningEmitted{false};

	FTimerHandle SampleTimer;

	FTimerHandle FrameCaptureTimer;

	FTimerHandle FlushTimer;

	TWeakObjectPtr<UGameViewportClient> BoundViewportClient;

	FDelegateHandle InputKeyDelegateHandle;

	UPROPERTY(Transient)
	TObjectPtr<ASceneCapture2D> CaptureRig;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> CaptureTarget;

	TUniquePtr<FSessionTelemetryReadbackState> PendingReadback;
};
