#include "Modules/ModuleManager.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "SessionTelemetrySettings.h"
#include "SessionTelemetrySubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealSessionRecorderEditor, Log, All);

class FUnrealSessionRecorderEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		BeginPIEHandle = FEditorDelegates::BeginPIE.AddRaw(
			this, &FUnrealSessionRecorderEditorModule::HandleBeginPIE);
		EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(
			this, &FUnrealSessionRecorderEditorModule::HandleEndPIE);
	}

	virtual void ShutdownModule() override
	{
		FEditorDelegates::BeginPIE.Remove(BeginPIEHandle);
		FEditorDelegates::EndPIE.Remove(EndPIEHandle);
	}

private:
	void HandleBeginPIE(bool) const
	{
		const USessionTelemetrySettings* Settings{GetDefault<USessionTelemetrySettings>()};
		if (Settings != nullptr && Settings->bEnabled)
		{
			USessionTelemetrySubsystem::PruneOldSessionsForNewSession(Settings->MaximumRetainedSessions);
		}
	}

	void HandleEndPIE(bool) const
	{
		if (GEngine == nullptr)
		{
			return;
		}

		int32 FinalizedWorlds{0};
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World{Context.World()};
			if (Context.WorldType == EWorldType::PIE && World != nullptr)
			{
				if (USessionTelemetrySubsystem* Recorder{World->GetSubsystem<USessionTelemetrySubsystem>()})
				{
					Recorder->FinalizeSession();
					++FinalizedWorlds;
				}
			}
		}
		UE_LOG(LogUnrealSessionRecorderEditor, Display, TEXT("SESSION TELEMETRY EndPIE finalizedWorlds=%d"),
			FinalizedWorlds);
	}

	FDelegateHandle BeginPIEHandle;
	FDelegateHandle EndPIEHandle;
};

IMPLEMENT_MODULE(FUnrealSessionRecorderEditorModule, UnrealSessionRecorderEditor)
