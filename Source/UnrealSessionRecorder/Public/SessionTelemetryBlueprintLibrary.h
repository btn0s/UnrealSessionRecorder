#pragma once

#include "JsonObjectWrapper.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "SessionTelemetryBlueprintLibrary.generated.h"

UCLASS()
class UNREALSESSIONRECORDER_API USessionTelemetryBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Unreal Session Recorder",
		Meta = (WorldContext = "WorldContextObject", DisplayName = "Record Telemetry Event (JSON)"))
	static bool RecordEventJson(const UObject* WorldContextObject, FName Type, const FJsonObjectWrapper& Fields);
};
