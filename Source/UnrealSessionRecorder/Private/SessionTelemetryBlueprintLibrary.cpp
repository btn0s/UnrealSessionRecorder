#include "SessionTelemetryBlueprintLibrary.h"

#include "SessionTelemetrySubsystem.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SessionTelemetryBlueprintLibrary)

bool USessionTelemetryBlueprintLibrary::RecordEventJson(const UObject* WorldContextObject, const FName Type,
	const FJsonObjectWrapper& Fields)
{
	if (Type.IsNone())
	{
		UE_LOG(LogSessionTelemetry, Warning, TEXT("SESSION TELEMETRY rejected Blueprint event with empty type."));
		return false;
	}

	TSharedPtr<FJsonObject> Source{Fields.JsonObject};
	if (!Source.IsValid())
	{
		if (Fields.JsonString.IsEmpty())
		{
			Source = MakeShared<FJsonObject>();
		}
		else
		{
			const auto Reader{TJsonReaderFactory<>::Create(Fields.JsonString)};
			if (!FJsonSerializer::Deserialize(Reader, Source) || !Source.IsValid())
			{
				UE_LOG(LogSessionTelemetry, Warning,
					TEXT("SESSION TELEMETRY rejected Blueprint event '%s': fields are not a JSON object."),
					*Type.ToString());
				return false;
			}
		}
	}

	auto Copy{MakeShared<FJsonObject>()};
	Copy->Values = Source->Values;
	USessionTelemetrySubsystem::Record(WorldContextObject, Type.ToString(), Copy);
	return true;
}
