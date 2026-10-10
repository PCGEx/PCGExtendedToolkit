// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorConverter.h"

#include "PCGExMediatorDiagnostics.h"
#include "Dom/JsonObject.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Class.h"

EJsonObjectConvertResult PCGExMediator::Guarded(TFunctionRef<bool()> Body)
{
	check(IsInGameThread());

	FPCGExMediatorDiagnostics Local;
	bool bOk = false;
	{
		FScope Scope(Local);
		bOk = Body();
		if (!bOk && !Local.HasErrors()) { Report(EPCGExMediatorSeverity::Error, TEXT("rejected")); }
	}
	Forward(Local);
	return (bOk && !Local.HasErrors()) ? EJsonObjectConvertResult::Converted : EJsonObjectConvertResult::FailAndAbort;
}

PCGExMediator::EConverterOutcome PCGExMediator::ConverterToJson(const IJsonObjectStructConverter& Converter, const void* Memory, TSharedPtr<FJsonObject>& OutBody)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	switch (Converter.ConvertToJson(Memory, Body))
	{
	case EJsonObjectConvertResult::Converted:
		OutBody = Body;
		return EConverterOutcome::Done;
	case EJsonObjectConvertResult::IgnoreAndContinue:
		OutBody = MakeShared<FJsonObject>();
		return EConverterOutcome::Done;
	case EJsonObjectConvertResult::UseDefaultConverter:
		return EConverterOutcome::UseDefault;
	default:
		return EConverterOutcome::Failed;
	}
}

PCGExMediator::EConverterOutcome PCGExMediator::ConverterFromJson(const IJsonObjectStructConverter& Converter, void* Memory, const TSharedPtr<FJsonObject>& In)
{
	switch (Converter.ConvertFromJson(Memory, In))
	{
	case EJsonObjectConvertResult::Converted:
	case EJsonObjectConvertResult::IgnoreAndContinue:
		return EConverterOutcome::Done;
	case EJsonObjectConvertResult::UseDefaultConverter:
		return EConverterOutcome::UseDefault;
	default:
		return EConverterOutcome::Failed;
	}
}

#pragma region FPCGExMediatorScriptStructConverter

FPCGExMediatorScriptStructConverter::FPCGExMediatorScriptStructConverter(const UScriptStruct* InStruct)
	: Struct(InStruct)
{
}

EJsonObjectConvertResult FPCGExMediatorScriptStructConverter::ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const
{
	if (!OutJsonObject.IsValid()) { OutJsonObject = MakeShared<FJsonObject>(); }
	return PCGExMediator::Guarded([&]() { return Encode(StructMemory, *OutJsonObject); });
}

EJsonObjectConvertResult FPCGExMediatorScriptStructConverter::ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const
{
	if (!InJsonObject.IsValid()) { return EJsonObjectConvertResult::FailAndAbort; }
	return PCGExMediator::Guarded([&]()
	{
		FInstancedStruct Temp;
		Temp.InitializeAs(Struct, static_cast<const uint8*>(StructMemory));
		if (!Decode(*InJsonObject, Temp.GetMutableMemory(), StructMemory)) { return false; }
		Struct->CopyScriptStruct(StructMemory, Temp.GetMemory());
		return true;
	});
}

#pragma endregion
