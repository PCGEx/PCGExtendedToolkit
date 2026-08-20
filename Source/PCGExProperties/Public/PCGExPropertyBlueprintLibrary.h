// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/SubclassOf.h"

#include "PCGExPropertyBlueprintLibrary.generated.h"

class UPCGExPropertyCollectionComponent;

/**
 * Blueprint-callable backing for the PCGEx property-read K2 node.
 *
 * The single CustomThunk function exposes FPCGExProperty::TryWriteValue to Blueprint
 * with a wildcard output: the OutValue pin's actual type at compile time drives the
 * EPCGMetadataTypes conversion that the property's virtual TryWriteValue uses.
 *
 * The function is marked BlueprintInternalUseOnly so it doesn't surface in the
 * standard right-click palette -- UK2Node_GetPCGExProperty is the user-facing entry,
 * and it expands to a call to this function at compile time.
 */
UCLASS(meta=(DisplayName="Property Blueprint Library", PCGExNodeLibraryDoc="staging/utilities/property-schema-asset/property-blueprint-library-internal"))
class PCGEXPROPERTIES_API UPCGExPropertyBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, CustomThunk, Category = "PCGEx|Property",
		meta = (CustomStructureParam = "OutValue", BlueprintInternalUseOnly = "true",
			DisplayName = "Try Get PCGEx Property Value"))
	static bool TryGetPCGExPropertyValue(
		const UPCGExPropertyCollectionComponent* Component,
		FName PropertyName,
		int32& OutValue);

	DECLARE_FUNCTION(execTryGetPCGExPropertyValue);

	/**
	 * Try to write a value to a named property on a Property Collection Component. NewValue
	 * is a wildcard input whose concrete type at compile time drives the EPCGMetadataTypes
	 * conversion that the property's virtual TryReadValue uses.
	 *
	 * The Set K2 node's readback is implemented by chaining a separate TryGetPCGExPropertyValue
	 * call after this one -- multi-wildcard CustomStructureParam doesn't reliably size/construct
	 * the output buffer for non-trivially-copyable structs (FSoftObjectPath, FString fields), so
	 * each thunk here keeps a single wildcard.
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "PCGEx|Property",
		meta = (CustomStructureParam = "NewValue", BlueprintInternalUseOnly = "true",
			DisplayName = "Try Set PCGEx Property Value"))
	static bool TrySetPCGExPropertyValue(
		UPCGExPropertyCollectionComponent* Component,
		FName PropertyName,
		const int32& NewValue);

	DECLARE_FUNCTION(execTrySetPCGExPropertyValue);

	/**
	 * Read a property as an object reference. The underlying property is treated as a soft
	 * object path; the resolved object is filtered against ExpectedClass before being returned.
	 *
	 * Separate from the wildcard TryGetPCGExPropertyValue thunk because Object pins don't
	 * round-trip cleanly through CustomStructureParam: the BP compiler doesn't have a
	 * marshalling path for "Object pin → wildcard slot declared as `int32&`", which results
	 * in mis-sized frame slots and downstream memory corruption inside FSoftObjectPath.
	 *
	 * DeterminesOutputType retypes the return pin to ExpectedClass at the BP call site.
	 */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Property",
		meta = (DeterminesOutputType = "ExpectedClass", BlueprintInternalUseOnly = "true",
			DisplayName = "Try Get PCGEx Property Object"))
	static UObject* TryGetPCGExPropertyObject(
		const UPCGExPropertyCollectionComponent* Component,
		FName PropertyName,
		UPARAM(meta = (AllowAbstract = "true")) TSubclassOf<UObject> ExpectedClass,
		bool& bSuccess);

	UFUNCTION(BlueprintCallable, Category = "PCGEx|Property",
		meta = (BlueprintInternalUseOnly = "true",
			DisplayName = "Try Set PCGEx Property Object"))
	static bool TrySetPCGExPropertyObject(
		UPCGExPropertyCollectionComponent* Component,
		FName PropertyName,
		UObject* NewObject);

	/**
	 * Class-pin variants of the Object accessors. The underlying property is treated as a
	 * soft class path; the resolved class is filtered against ExpectedClass.
	 */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Property",
		meta = (DeterminesOutputType = "ExpectedClass", BlueprintInternalUseOnly = "true",
			DisplayName = "Try Get PCGEx Property Class"))
	static TSubclassOf<UObject> TryGetPCGExPropertyClass(
		const UPCGExPropertyCollectionComponent* Component,
		FName PropertyName,
		UPARAM(meta = (AllowAbstract = "true")) TSubclassOf<UObject> ExpectedClass,
		bool& bSuccess);

	UFUNCTION(BlueprintCallable, Category = "PCGEx|Property",
		meta = (BlueprintInternalUseOnly = "true",
			DisplayName = "Try Set PCGEx Property Class"))
	static bool TrySetPCGExPropertyClass(
		UPCGExPropertyCollectionComponent* Component,
		FName PropertyName,
		UClass* NewClass);
};
