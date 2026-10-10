// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExMediatorScope.h"

class FJsonValue;

enum class EPCGExMediatorSeverity : uint8
{
	Info = 0,
	Warning = 1,
	Error = 2
};

struct PCGEXMEDIATOR_API FPCGExMediatorDiagnostic
{
	EPCGExMediatorSeverity Severity = EPCGExMediatorSeverity::Info;

	/** Slash-separated location in the document, e.g. "properties/3/default". Empty = whole document. */
	FString Path;

	FString Message;

	FString ToString() const;
};

/** Diagnostics a transport call collects. Codecs never throw: they report here and return failure. */
struct PCGEXMEDIATOR_API FPCGExMediatorDiagnostics
{
	TArray<FPCGExMediatorDiagnostic> Items;

	bool HasErrors() const;
	int32 Num(EPCGExMediatorSeverity Severity) const;

	void Add(EPCGExMediatorSeverity Severity, const FString& Path, const FString& Message);
	void Reset();

	/** One diagnostic per line, severity first. */
	FString ToString() const;

	/** JSON array of { severity, path, message }. */
	TSharedRef<FJsonValue> ToJson() const;
};

/** Per-thread sink stack; exported so every module reports into the same scope. */
struct PCGEXMEDIATOR_API FPCGExMediatorSinkStack
{
	using FValue = FPCGExMediatorDiagnostics*;
	static TArray<FValue>& Get();
};

/** Per-thread path-segment stack behind FPathScope. */
struct PCGEXMEDIATOR_API FPCGExMediatorPathStack
{
	using FValue = FString;
	static TArray<FValue>& Get();
};

namespace PCGExMediator
{
	/**
	 * Binds a sink for the calling thread; scopes nest and the innermost one receives reports. The engine
	 * converter interface carries no diagnostics channel, so converters report through the current scope.
	 * Outside any scope, reports go to LogPCGEx instead -- never asserted, never dropped.
	 */
	class FScope : public TPCGExMediatorScope<FPCGExMediatorSinkStack>
	{
		using Super = TPCGExMediatorScope<FPCGExMediatorSinkStack>;

	public:
		explicit FScope(FPCGExMediatorDiagnostics& InSink)
			: Super(&InSink)
		{
		}
	};

	/** Pushes one path segment for the lifetime of the object; Report() prefixes the joined segments. */
	class FPathScope : public TPCGExMediatorScope<FPCGExMediatorPathStack>
	{
		using Super = TPCGExMediatorScope<FPCGExMediatorPathStack>;

	public:
		explicit FPathScope(const FString& InSegment)
			: Super(InSegment)
		{
		}

		explicit FPathScope(const int32 InIndex)
			: Super(FString::FromInt(InIndex))
		{
		}
	};

	PCGEXMEDIATOR_API bool HasScope();
	PCGEXMEDIATOR_API FString CurrentPath();

	/** Reports at the current path. */
	PCGEXMEDIATOR_API void Report(EPCGExMediatorSeverity Severity, const FString& Message);

	/** Reports at the current path extended by one segment. */
	PCGEXMEDIATOR_API void Report(EPCGExMediatorSeverity Severity, const FString& Segment, const FString& Message);

	/** Re-emits collected diagnostics with their paths as recorded (no prefixing), to the current sink or the log. */
	PCGEXMEDIATOR_API void Forward(const FPCGExMediatorDiagnostics& Collected);

	/** Writes every item to LogPCGEx at its severity. */
	PCGEXMEDIATOR_API void LogDiagnostics(const FPCGExMediatorDiagnostics& Diagnostics);

	PCGEXMEDIATOR_API const TCHAR* SeverityToString(EPCGExMediatorSeverity Severity);
}
