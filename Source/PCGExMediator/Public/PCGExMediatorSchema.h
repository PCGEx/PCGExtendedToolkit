// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

/** JSON Schema (draft 2020-12) fragment builders, so describe code reads as data. Every call returns a fresh object. */
namespace PCGExMediator::Schema
{
	/** { "type": Type, "description"?: Description }; a null Type omits the key. */
	PCGEXMEDIATOR_API TSharedRef<FJsonObject> Typed(const TCHAR* Type, const FString& Description = FString());

	PCGEXMEDIATOR_API TSharedRef<FJsonObject> String(const FString& Description = FString());
	PCGEXMEDIATOR_API TSharedRef<FJsonObject> Number(const FString& Description = FString());
	PCGEXMEDIATOR_API TSharedRef<FJsonObject> Integer(const FString& Description = FString());
	PCGEXMEDIATOR_API TSharedRef<FJsonObject> Boolean(const FString& Description = FString());

	/** Array of Items; a negative bound is left open. */
	PCGEXMEDIATOR_API TSharedRef<FJsonObject> Array(const TSharedPtr<FJsonObject>& Items, const FString& Description = FString(), int32 MinItems = -1, int32 MaxItems = -1);

	/** Object over a "properties" map, with an optional "required" list. */
	PCGEXMEDIATOR_API TSharedRef<FJsonObject> Object(const TSharedRef<FJsonObject>& Properties, const FString& Description = FString(), TConstArrayView<FString> Required = {});

	PCGEXMEDIATOR_API TSharedRef<FJsonObject> Const(const FString& Value);
	PCGEXMEDIATOR_API TSharedRef<FJsonObject> Const(int32 Value);

	/** String restricted to Names. */
	PCGEXMEDIATOR_API TSharedRef<FJsonObject> Enum(const TArray<FString>& Names, const FString& Description = FString());

	PCGEXMEDIATOR_API TSharedRef<FJsonObject> OneOf(const TArray<TSharedPtr<FJsonObject>>& Options, const FString& Description = FString());

	/** Sets "description" / "$comment" and returns the same object, for chaining. */
	PCGEXMEDIATOR_API TSharedRef<FJsonObject> Describe(const TSharedRef<FJsonObject>& S, const FString& Description);
	PCGEXMEDIATOR_API TSharedRef<FJsonObject> Comment(const TSharedRef<FJsonObject>& S, const FString& Comment);

	/** JSON string values from Values. */
	PCGEXMEDIATOR_API TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& Values);
}
