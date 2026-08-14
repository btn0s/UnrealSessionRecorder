#include "SessionTelemetryEventBuffer.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

void FSessionTelemetryEventBuffer::Append(const FString& Type, const TSharedRef<FJsonObject>& Fields,
	const int64 TimeMs, const uint64 Frame)
{
	Fields->SetStringField(TEXT("type"), Type);
	Fields->SetNumberField(TEXT("t"), static_cast<double>(TimeMs));
	Fields->SetNumberField(TEXT("f"), static_cast<double>(Frame));
	Events.Add(MakeShared<FJsonValueObject>(Fields));
}

FString FSessionTelemetryEventBuffer::Serialize() const
{
	FString Output;
	const auto Writer{TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output)};
	FJsonSerializer::Serialize(Events, Writer);
	return Output;
}

FString FSessionTelemetryEventBuffer::FrameFileName(const int64 TimeMs)
{
	return FString::Printf(TEXT("t%08d.jpg"), static_cast<int32>(FMath::Clamp<int64>(TimeMs, 0, 99999999)));
}
