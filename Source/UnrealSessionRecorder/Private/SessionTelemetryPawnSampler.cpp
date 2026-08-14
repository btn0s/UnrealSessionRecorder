#include "SessionTelemetryPawnSampler.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

TArray<TSharedPtr<FJsonValue>> FSessionTelemetryPawnSampler::VectorValues(const FVector& Vector)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(3);
	Values.Add(MakeShared<FJsonValueNumber>(Vector.X));
	Values.Add(MakeShared<FJsonValueNumber>(Vector.Y));
	Values.Add(MakeShared<FJsonValueNumber>(Vector.Z));
	return Values;
}

TArray<TSharedPtr<FJsonValue>> FSessionTelemetryPawnSampler::RotatorValues(const FRotator& Rotator)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(3);
	Values.Add(MakeShared<FJsonValueNumber>(Rotator.Pitch));
	Values.Add(MakeShared<FJsonValueNumber>(Rotator.Yaw));
	Values.Add(MakeShared<FJsonValueNumber>(Rotator.Roll));
	return Values;
}

TSharedRef<FJsonObject> FSessionTelemetryPawnSampler::Sample(const APawn& Pawn, const APawn* LocalPlayerPawn)
{
	auto Fields{MakeShared<FJsonObject>()};
	Fields->SetStringField(TEXT("actor"), Pawn.GetName());
	Fields->SetStringField(TEXT("class"), Pawn.GetClass()->GetPathName());
	Fields->SetStringField(TEXT("role"), &Pawn == LocalPlayerPawn ? TEXT("localPlayer") : TEXT("pawn"));
	Fields->SetArrayField(TEXT("pos"), VectorValues(Pawn.GetActorLocation()));
	Fields->SetArrayField(TEXT("rot"), RotatorValues(Pawn.GetActorRotation()));

	const FVector Velocity{Pawn.GetVelocity()};
	Fields->SetArrayField(TEXT("vel"), VectorValues(Velocity));
	Fields->SetNumberField(TEXT("speed"), Velocity.Size());

	if (const AController* Controller{Pawn.GetController()})
	{
		Fields->SetArrayField(TEXT("controlRot"), RotatorValues(Controller->GetControlRotation()));
	}

	if (const auto* Character{Cast<ACharacter>(&Pawn)})
	{
		Fields->SetBoolField(TEXT("crouched"), Character->bIsCrouched);
		if (const UCharacterMovementComponent* Movement{Character->GetCharacterMovement()})
		{
			Fields->SetStringField(TEXT("movementMode"), Movement->GetMovementName());
		}
	}

	if (const USkeletalMeshComponent* Mesh{Pawn.FindComponentByClass<USkeletalMeshComponent>()})
	{
		if (UAnimInstance* Animation{Mesh->GetAnimInstance()})
		{
			Fields->SetStringField(TEXT("animInstance"), Animation->GetClass()->GetPathName());
			if (UAnimMontage* Montage{Animation->GetCurrentActiveMontage()})
			{
				Fields->SetStringField(TEXT("montage"), Montage->GetPathName());
				Fields->SetNumberField(TEXT("montagePosition"), Animation->Montage_GetPosition(Montage));
			}
		}
	}

	return Fields;
}
