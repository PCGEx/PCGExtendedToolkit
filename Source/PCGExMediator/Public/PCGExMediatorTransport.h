// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

/**
 * Documents in and out. Every document is an envelope { "format", "version", "data" }; a struct document's
 * data is its format's body, an object document's data is { "class", "members": { <member>: <body> } }.
 * All calls are game-thread, editor-side, and report through the caller's PCGExMediator::FScope (or the log).
 * Imports never partially mutate: every member decodes into a temporary first, then all are applied inside
 * one transaction, each followed by the host's own PostEditChangeProperty.
 */
namespace PCGExMediator
{
	namespace Keys
	{
		inline const TCHAR* Format = TEXT("format");
		inline const TCHAR* Version = TEXT("version");
		inline const TCHAR* Data = TEXT("data");
		inline const TCHAR* Class = TEXT("class");
		inline const TCHAR* Members = TEXT("members");
	}

	inline const FName ObjectFormatId = FName(TEXT("pcgex.object"));
	inline constexpr int32 ObjectFormatVersion = 1;

	PCGEXMEDIATOR_API TSharedRef<FJsonObject> MakeEnvelope(FName FormatId, int32 Version, const TSharedPtr<FJsonValue>& Data);

	/** False (with diagnostics) on a foreign format or a version above SupportedVersion; a missing version reads as 1. */
	PCGEXMEDIATOR_API bool ReadEnvelope(const FJsonObject& Doc, FName ExpectedFormat, int32 SupportedVersion, TSharedPtr<FJsonValue>& OutData);

	// --- Structs (through their registered format) ---

	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> ExportStruct(const UScriptStruct* Struct, const void* Memory);
	PCGEXMEDIATOR_API bool ImportStruct(const FJsonObject& Doc, const UScriptStruct* Struct, void* Memory);

	template <typename T>
	TSharedPtr<FJsonObject> ExportStruct(const T& Value)
	{
		return ExportStruct(T::StaticStruct(), &Value);
	}

	template <typename T>
	bool ImportStruct(const FJsonObject& Doc, T& Value)
	{
		return ImportStruct(Doc, T::StaticStruct(), &Value);
	}

	// --- Objects (through their registered binding) ---

	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> ExportObject(const UObject* Host);
	PCGEXMEDIATOR_API bool ImportObject(const FJsonObject& Doc, UObject* Host);

	// --- Describe ---

	/** JSON Schema of a whole struct document (envelope included); null for an unknown format. */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeFormat(FName FormatId);

	/** JSON Schema of an object document for HostClass: its bound members, each with its format's body schema. */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeObject(const UClass* HostClass);

	// --- Text and files ---

	PCGEXMEDIATOR_API FString ToString(const TSharedRef<FJsonObject>& Doc, bool bPretty = true);
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> FromString(const FString& Text);
	PCGEXMEDIATOR_API bool WriteFile(const FString& FilePath, const TSharedRef<FJsonObject>& Doc);
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> ReadFile(const FString& FilePath);

	/**
	 * An asset by object path, or a node's settings by "<graph object path>:<node title or name>". Loads what it
	 * needs; null (with diagnostics) when nothing resolves.
	 */
	PCGEXMEDIATOR_API UObject* ResolveTarget(const FString& Target);

	/** A class by path, short name, or prefixed C++ name (UFoo / AFoo); null when nothing matches. */
	PCGEXMEDIATOR_API const UClass* FindClass(const FString& NameOrPath);
}
