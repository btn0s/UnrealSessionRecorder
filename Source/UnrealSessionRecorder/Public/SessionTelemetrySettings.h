#pragma once

#include "Engine/DeveloperSettings.h"

#include "SessionTelemetrySettings.generated.h"

UCLASS(Config = Game, DefaultConfig, Meta = (DisplayName = "Unreal Session Recorder"))
class UNREALSESSIONRECORDER_API USessionTelemetrySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override;

	UPROPERTY(Config, EditAnywhere, Category = "Telemetry")
	bool bEnabled{true};

	UPROPERTY(Config, EditAnywhere, Category = "Telemetry", Meta = (ClampMin = 0, ForceUnits = "Hz"))
	float SampleHz{3.0f};

	UPROPERTY(Config, EditAnywhere, Category = "Telemetry", Meta = (ClampMin = 1, ForceUnits = "s"))
	float FlushIntervalSeconds{5.0f};

	UPROPERTY(Config, EditAnywhere, Category = "Telemetry|Frames", Meta = (ClampMin = 0, ForceUnits = "Hz"))
	float FrameCaptureHz{30.0f};

	UPROPERTY(Config, EditAnywhere, Category = "Telemetry|Frames", Meta = (ClampMin = 16))
	int32 FrameCaptureWidth{480};

	UPROPERTY(Config, EditAnywhere, Category = "Telemetry|Frames", Meta = (ClampMin = 16))
	int32 FrameCaptureHeight{270};

	UPROPERTY(Config, EditAnywhere, Category = "Telemetry|Frames", Meta = (ClampMin = 1, ClampMax = 100))
	int32 JpegQuality{80};

	UPROPERTY(Config, EditAnywhere, Category = "Telemetry|Input")
	bool bCaptureInput{true};

	UPROPERTY(Config, EditAnywhere, Category = "Telemetry|Input")
	bool bRenderInputOverlay{true};

	UPROPERTY(Config, EditAnywhere, Category = "Telemetry|Input", Meta = (ClampMin = 0.05, ForceUnits = "s"))
	float InputTapDisplaySeconds{0.4f};

	UPROPERTY(Config, EditAnywhere, Category = "Telemetry|Input", Meta = (ClampMin = 0, ForceUnits = "px"))
	int32 InputOverlayBottomMargin{24};

	UPROPERTY(Config, EditAnywhere, Category = "Telemetry|Video")
	bool bBuildVideoOnSessionEnd{true};

	UPROPERTY(Config, EditAnywhere, Category = "Telemetry|Video")
	FString FfmpegExecutable{TEXT("ffmpeg")};

	UPROPERTY(Config, EditAnywhere, Category = "Telemetry|Video")
	FString VideoFileName{TEXT("session.mp4")};
};
