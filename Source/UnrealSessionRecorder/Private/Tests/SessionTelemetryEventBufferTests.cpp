#if WITH_DEV_AUTOMATION_TESTS

#include "SessionTelemetryEventBuffer.h"
#include "SessionTelemetryPawnSampler.h"
#include "SessionTelemetrySettings.h"
#include "SessionTelemetrySubsystem.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSessionTelemetryReservedFieldsTest,
	"UnrealSessionRecorder.EventBuffer.ReservedFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSessionTelemetryReservedFieldsTest::RunTest(const FString& Parameters)
{
	auto Fields{MakeShared<FJsonObject>()};
	Fields->SetStringField(TEXT("type"), TEXT("spoofed"));
	Fields->SetNumberField(TEXT("t"), -1);
	Fields->SetNumberField(TEXT("f"), -1);
	Fields->SetStringField(TEXT("note"), TEXT("kept"));

	FSessionTelemetryEventBuffer Buffer;
	Buffer.Append(TEXT("sample"), Fields, 42, 7);

	TArray<TSharedPtr<FJsonValue>> Events;
	const auto Reader{TJsonReaderFactory<>::Create(Buffer.Serialize())};
	TestTrue(TEXT("Serialized event buffer parses"), FJsonSerializer::Deserialize(Reader, Events));
	TestEqual(TEXT("One event is serialized"), Events.Num(), 1);

	if (Events.Num() != 1 || !Events[0].IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Event{Events[0]->AsObject()};
	TestTrue(TEXT("Event is an object"), Event.IsValid());
	if (!Event.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("Type is authoritative"), Event->GetStringField(TEXT("type")), FString(TEXT("sample")));
	TestEqual(TEXT("Game time is authoritative"), Event->GetIntegerField(TEXT("t")), 42);
	TestEqual(TEXT("Frame is authoritative"), Event->GetIntegerField(TEXT("f")), 7);
	TestEqual(TEXT("Caller field is preserved"), Event->GetStringField(TEXT("note")), FString(TEXT("kept")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSessionTelemetryFrameFileNameTest,
	"UnrealSessionRecorder.EventBuffer.FrameFileNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSessionTelemetryFrameFileNameTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Negative time clamps to zero"), FSessionTelemetryEventBuffer::FrameFileName(-1),
		FString(TEXT("t00000000.jpg")));
	TestEqual(TEXT("Time is zero padded"), FSessionTelemetryEventBuffer::FrameFileName(1234),
		FString(TEXT("t00001234.jpg")));
	TestEqual(TEXT("Time clamps to eight digits"), FSessionTelemetryEventBuffer::FrameFileName(100000000),
		FString(TEXT("t99999999.jpg")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSessionTelemetrySettingsDefaultsTest,
	"UnrealSessionRecorder.Settings.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSessionTelemetrySettingsDefaultsTest::RunTest(const FString& Parameters)
{
	const USessionTelemetrySettings* Settings{GetDefault<USessionTelemetrySettings>()};
	TestNotNull(TEXT("Default settings exist"), Settings);
	if (Settings == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("Telemetry is enabled"), Settings->bEnabled);
	TestEqual(TEXT("Sample cadence"), Settings->SampleHz, 3.0f);
	TestEqual(TEXT("Flush cadence"), Settings->FlushIntervalSeconds, 5.0f);
	TestEqual(TEXT("Frame cadence"), Settings->FrameCaptureHz, 30.0f);
	TestEqual(TEXT("Frame width"), Settings->FrameCaptureWidth, 480);
	TestEqual(TEXT("Frame height"), Settings->FrameCaptureHeight, 270);
	TestEqual(TEXT("JPEG quality"), Settings->JpegQuality, 80);
	TestTrue(TEXT("Input capture is enabled"), Settings->bCaptureInput);
	TestTrue(TEXT("Input overlay is enabled"), Settings->bRenderInputOverlay);
	TestEqual(TEXT("Input tap display duration"), Settings->InputTapDisplaySeconds, 0.4f);
	TestEqual(TEXT("Input overlay lead time"), Settings->InputOverlayLeadSeconds, 0.1f);
	TestEqual(TEXT("Input overlay bottom margin"), Settings->InputOverlayBottomMargin, 24);
	TestEqual(TEXT("Maximum retained sessions"), Settings->MaximumRetainedSessions, 10);
	TestTrue(TEXT("Video export is enabled"), Settings->bBuildVideoOnSessionEnd);
	TestEqual(TEXT("Video filename"), Settings->VideoFileName, FString(TEXT("session.mp4")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSessionTelemetryBundledFfmpegTest,
	"UnrealSessionRecorder.Video.BundledFfmpeg",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSessionTelemetryBundledFfmpegTest::RunTest(const FString& Parameters)
{
	const FString FfmpegPath{USessionTelemetrySubsystem::GetBundledFfmpegPath()};
	TestTrue(TEXT("Bundled FFmpeg path is absolute"), FPaths::IsRelative(FfmpegPath) == false);
	TestTrue(TEXT("Bundled FFmpeg exists"), FPaths::FileExists(FfmpegPath));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSessionTelemetrySamplerValueEncodingTest,
	"UnrealSessionRecorder.PawnSampler.ValueEncoding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSessionTelemetrySamplerValueEncodingTest::RunTest(const FString& Parameters)
{
	const TArray<TSharedPtr<FJsonValue>> VectorValues{
		FSessionTelemetryPawnSampler::VectorValues(FVector{1.0, 2.0, 3.0})};
	TestEqual(TEXT("Vector component count"), VectorValues.Num(), 3);
	if (VectorValues.Num() == 3)
	{
		TestEqual(TEXT("Vector X"), VectorValues[0]->AsNumber(), 1.0);
		TestEqual(TEXT("Vector Y"), VectorValues[1]->AsNumber(), 2.0);
		TestEqual(TEXT("Vector Z"), VectorValues[2]->AsNumber(), 3.0);
	}

	const TArray<TSharedPtr<FJsonValue>> RotatorValues{
		FSessionTelemetryPawnSampler::RotatorValues(FRotator{10.0, 20.0, 30.0})};
	TestEqual(TEXT("Rotator component count"), RotatorValues.Num(), 3);
	if (RotatorValues.Num() == 3)
	{
		TestEqual(TEXT("Rotator pitch"), RotatorValues[0]->AsNumber(), 10.0);
		TestEqual(TEXT("Rotator yaw"), RotatorValues[1]->AsNumber(), 20.0);
		TestEqual(TEXT("Rotator roll"), RotatorValues[2]->AsNumber(), 30.0);
	}

	return true;
}

#endif
