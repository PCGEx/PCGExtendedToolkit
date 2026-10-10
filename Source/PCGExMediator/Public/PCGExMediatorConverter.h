// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "JsonObjectStructInterface.h"
#include "Dom/JsonObject.h"

namespace PCGExMediator
{
	/** Runs Body under a private sink; every report is forwarded, and a failure without an error gets one. Any error = FailAndAbort. */
	PCGEXMEDIATOR_API EJsonObjectConvertResult Guarded(TFunctionRef<bool()> Body);

	enum class EConverterOutcome : uint8
	{
		/** OutBody holds the converter's object (empty for IgnoreAndContinue). */
		Done,
		/** The converter declined; the caller walks the struct itself. */
		UseDefault,
		/** Reported at the current path. */
		Failed
	};

	PCGEXMEDIATOR_API EConverterOutcome ConverterToJson(const IJsonObjectStructConverter& Converter, const void* Memory, TSharedPtr<FJsonObject>& OutBody);
	PCGEXMEDIATOR_API EConverterOutcome ConverterFromJson(const IJsonObjectStructConverter& Converter, void* Memory, const TSharedPtr<FJsonObject>& In);
}

/**
 * Base of a domain converter over a struct known at compile time. Encode writes the body; Decode fills Temp, a copy
 * of Live, and the base swaps it in only when Decode and every report succeed. The domain never writes Live.
 */
template <typename T>
class TPCGExMediatorStructConverter : public IJsonObjectStructConverter
{
public:
	virtual EJsonObjectConvertResult ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const override
	{
		if (!OutJsonObject.IsValid()) { OutJsonObject = MakeShared<FJsonObject>(); }
		return PCGExMediator::Guarded([&]() { return Encode(*static_cast<const T*>(StructMemory), *OutJsonObject); });
	}

	virtual EJsonObjectConvertResult ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const override
	{
		if (!InJsonObject.IsValid()) { return EJsonObjectConvertResult::FailAndAbort; }
		T& Live = *static_cast<T*>(StructMemory);
		return PCGExMediator::Guarded([&]()
		{
			T Temp = Live;
			if (!Decode(*InJsonObject, Temp, Live)) { return false; }
			Live = MoveTemp(Temp);
			return true;
		});
	}

protected:
	virtual bool Encode(const T& Value, FJsonObject& Out) const = 0;
	virtual bool Decode(const FJsonObject& In, T& Temp, const T& Live) const = 0;
};

/** Same contract for a struct chosen at construction (one instance per UScriptStruct); the copy goes through FInstancedStruct. */
class PCGEXMEDIATOR_API FPCGExMediatorScriptStructConverter : public IJsonObjectStructConverter
{
public:
	explicit FPCGExMediatorScriptStructConverter(const UScriptStruct* InStruct);

	virtual EJsonObjectConvertResult ConvertToJson(const void* StructMemory, TSharedPtr<FJsonObject>& OutJsonObject) const override;
	virtual EJsonObjectConvertResult ConvertFromJson(void* StructMemory, const TSharedPtr<FJsonObject>& InJsonObject) const override;

	const UScriptStruct* GetStruct() const { return Struct; }

protected:
	virtual bool Encode(const void* Value, FJsonObject& Out) const = 0;
	virtual bool Decode(const FJsonObject& In, void* Temp, const void* Live) const = 0;

private:
	const UScriptStruct* Struct = nullptr;
};
