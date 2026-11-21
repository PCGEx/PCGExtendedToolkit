// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "EdGraph/EdGraphPin.h"

#include "K2Node_PCGExPropertyAccessBase.generated.h"

class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;
class UEdGraph;
class UGraphNodeContextMenuContext;
class UK2Node_CallFunction;
class UToolMenu;

/**
 * Shared machinery for the nodes that read or write a PCGEx custom property through a wildcard pin.
 * Subclasses name the target pin, an optional key pin and the backing library functions; pin layout,
 * wildcard typing, Object/Class dispatch and expansion live here.
 *
 * Get nodes are pure with one wildcard output; Set nodes carry exec pins plus a NewValue / Readback pair
 * locked to one type. The connected pin's type drives the EPCGMetadataTypes conversion; any concrete type
 * can also be picked from the wildcard's right-click menu. Object/Class pins compile to dedicated typed
 * library functions: they don't round-trip through the CustomStructureParam wildcard.
 *
 * Library function contract: parameters are named after the target pin, the key pin (when there is one)
 * and PropertyName. Wildcard get: OutValue out, returns success. Typed get: ExpectedClass in, bSuccess out,
 * returns the object / class. Set: NewValue (wildcard), NewObject or NewClass in, returns success.
 */
UCLASS(Abstract)
class PCGEXPROPERTIESEDITOR_API UK2Node_PCGExPropertyAccessBase : public UK2Node
{
	GENERATED_BODY()

public:
	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	// UK2Node
	virtual bool IsNodePure() const override
	{
		return !IsSetNode();
	}

	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

protected:
	/** Which library function a resolved pin type compiles to. */
	enum class EFlavor : uint8
	{
		Wildcard,
		Object,
		Class
	};

	/** True for Set nodes (exec pins, NewValue input + Readback output); false for pure Get nodes. */
	virtual bool IsSetNode() const PURE_VIRTUAL(UK2Node_PCGExPropertyAccessBase::IsSetNode, return false;);

	/** Name of the object pin the property is read from or written to. */
	virtual FName GetTargetPinName() const PURE_VIRTUAL(UK2Node_PCGExPropertyAccessBase::GetTargetPinName, return NAME_None;);

	/** Class accepted by the target pin. */
	virtual UClass* GetTargetPinClass() const PURE_VIRTUAL(UK2Node_PCGExPropertyAccessBase::GetTargetPinClass, return nullptr;);

	/** Name of the key pin that sits between the target and the property name; None when there is none. */
	virtual FName GetKeyPinName() const
	{
		return NAME_None;
	}

	/** Creates the key pin. Called from AllocateDefaultPins right after the target pin. */
	virtual void CreateKeyPin()
	{
	}

	/** Blueprint function library holding the backing functions. */
	virtual UClass* GetLibraryClass() const PURE_VIRTUAL(UK2Node_PCGExPropertyAccessBase::GetLibraryClass, return nullptr;);

	/** Backing library function for (direction, flavor). Set nodes ask for both directions: the get is the readback. */
	virtual FName GetLibraryFunctionName(bool bSet, EFlavor Flavor) const PURE_VIRTUAL(UK2Node_PCGExPropertyAccessBase::GetLibraryFunctionName, return NAME_None;);

	/** Persisted wildcard type; AllocateDefaultPins re-stamps it on reload so a type picked with no
	 *  connections survives save/reopen. */
	UPROPERTY()
	FEdGraphPinType ResolvedPinType;

	UEdGraphPin* GetTargetPin() const;
	/** Null when the node has no key pin. */
	UEdGraphPin* GetKeyPin() const;
	UEdGraphPin* GetPropertyNamePin() const;
	/** OutValue on Get nodes, NewValue on Set nodes. */
	UEdGraphPin* GetValuePin() const;
	/** Set nodes only. */
	UEdGraphPin* GetReadbackPin() const;
	UEdGraphPin* GetSuccessPin() const;

	/** The wildcard pins that share one concrete type: {OutValue} or {NewValue, Readback}. */
	void GetWildcardPins(TArray<UEdGraphPin*>& OutPins) const;

	/** Revert the wildcard pins to PC_Wildcard, breaking their links. */
	void ResetWildcardPins();
	/** Adopt another pin's type (container stripped) onto the wildcard pins. */
	void AdoptTypeFromOther(const UEdGraphPin* OtherPin);
	/** Stamp a specific type onto the wildcard pins (right-click menu), breaking incompatible links. */
	void SetWildcardPinsType(const FEdGraphPinType& InType);

	static const FName PropertyNamePinName;
	static const FName OutValuePinName;
	static const FName NewValuePinName;
	static const FName ReadbackPinName;
	static const FName SuccessPinName;

private:
	/** Spawn the intermediate call for (direction, flavor) with pins allocated. */
	UK2Node_CallFunction* SpawnLibraryCall(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, bool bSet, EFlavor Flavor);
};
