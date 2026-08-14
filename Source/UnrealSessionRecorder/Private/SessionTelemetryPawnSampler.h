#pragma once

#include "CoreMinimal.h"

class APawn;
class FJsonObject;
class FJsonValue;

class FSessionTelemetryPawnSampler
{
public:
	static TArray<TSharedPtr<FJsonValue>> VectorValues(const FVector& Vector);

	static TArray<TSharedPtr<FJsonValue>> RotatorValues(const FRotator& Rotator);

	static TSharedRef<FJsonObject> Sample(const APawn& Pawn, const APawn* LocalPlayerPawn);
};
