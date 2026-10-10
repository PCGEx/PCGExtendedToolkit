// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "PCGExMediatorLibrary.generated.h"

/**
 * The mediator's four operations as string-only reflected functions: the surface a separately shipped plugin
 * (the PCGEx Assistant) reaches by name through UFunction, with no link or header dependency, and the surface
 * Python and Blueprint see. Every result is a JSON object string; a failure is { "ok": false, "diagnostics": [...] }.
 */
UCLASS()
class PCGEXMEDIATOR_API UPCGExMediatorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** { formats: [{ id, version, struct, summary }], bindings: [{ class, members, summary }] } */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Mediator")
	static FString ListFormats();

	/** JSON Schema of a format id or of a bound class's object document; a failure object when nothing matches. */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Mediator")
	static FString DescribeFormat(const FString& FormatIdOrClass);

	/** The object document of a bound target (asset path, or <graph path>:<node>); a failure object otherwise. */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Mediator")
	static FString ExportJson(const FString& Target);

	/** Imports a document into a bound target, undoable. Always { ok, diagnostics }. */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Mediator")
	static FString ImportJson(const FString& Target, const FString& Json);
};
