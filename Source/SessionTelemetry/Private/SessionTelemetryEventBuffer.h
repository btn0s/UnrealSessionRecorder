#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

class FSessionTelemetryEventBuffer
{
public:
	void Append(const FString& Type, const TSharedRef<FJsonObject>& Fields, int64 TimeMs, uint64 Frame);

	FString Serialize() const;

	int32 Num() const
	{
		return Events.Num();
	}

	static FString FrameFileName(int64 TimeMs);

private:
	TArray<TSharedPtr<FJsonValue>> Events;
};
