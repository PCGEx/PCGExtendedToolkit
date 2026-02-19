// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"

#include "PCGExConnectorPatternGraphNode.generated.h"

class UPCGExValencyConnectorSet;
class UPCGExConnectorPatternGraph;

/**
 * Serialized connector pin entry — stores stable identity independent of ConnectorSet.
 */
USTRUCT()
struct FPCGExConnectorPinEntry
{
	GENERATED_BODY()

	/** Stable type identity from FPCGExValencyConnectorEntry::TypeId */
	UPROPERTY()
	int32 StoredTypeId = 0;

	/** Cached display name (survives ConnectorSet removal) */
	UPROPERTY()
	FName StoredTypeName;

	/** Whether this entry has an output pin */
	UPROPERTY()
	bool bOutput = false;

	/** Whether this entry has an input pin */
	UPROPERTY()
	bool bInput = false;
};

/**
 * Graph node representing a pattern entry (module placeholder).
 * Pins represent connector type slots; wires represent adjacencies via type pairs.
 */
UCLASS()
class PCGEXELEMENTSVALENCYEDITOR_API UPCGExConnectorPatternGraphNode : public UEdGraphNode
{
	GENERATED_BODY()

public:
	//~ Entry properties (editable in details panel)

	/** Module names that match this entry. Empty = wildcard (any module). */
	UPROPERTY(EditAnywhere, Category = "Entry")
	TArray<FName> ModuleNames;

	/** If true, this entry is consumed by the pattern; if false, constraint-only */
	UPROPERTY(EditAnywhere, Category = "Entry")
	bool bIsActive = true;

	/** Connector types that must have NO connections (boundary constraint).
	 * Now derived from Boundary constraint nodes in graph — kept for serialization. */
	UPROPERTY()
	TArray<FName> BoundaryConnectorTypes;

	/** Connector types that must have at least one connection (wildcard constraint).
	 * Now derived from Wildcard constraint nodes in graph — kept for serialization. */
	UPROPERTY()
	TArray<FName> WildcardConnectorTypes;

	//~ Pin storage

	/** User-added connector pins (excludes "Any" which is always present) */
	UPROPERTY()
	TArray<FPCGExConnectorPinEntry> ConnectorPins;

	//~ Pin management

	/** Add a connector type pin in the given direction. If the type already exists, enables the additional direction. */
	void AddConnectorPin(int32 TypeId, FName TypeName, EEdGraphPinDirection Direction);

	/** Add both input and output pins for a connector type (convenience for BuildGraphFromAsset) */
	void AddConnectorPinBoth(int32 TypeId, FName TypeName);

	/** Remove a connector type pin by TypeId and direction. Removes the entry entirely if no directions remain. */
	void RemoveConnectorPin(int32 TypeId, EEdGraphPinDirection Direction);

	/** Resolve pin display names and colors from current ConnectorSet */
	void ResolveConnectorPins(const UPCGExValencyConnectorSet* InConnectorSet);

	/** Check if this node already has a pin for the given TypeId in the given direction */
	bool HasConnectorPin(int32 TypeId, EEdGraphPinDirection Direction) const;

	/** Check if this node has any pin (input or output) for the given TypeId */
	bool HasAnyConnectorPin(int32 TypeId) const;

	/** Remove all connector pins whose TypeId is no longer in the ConnectorSet. Returns true if any were removed. */
	bool RemoveStalePins(const UPCGExValencyConnectorSet* InConnectorSet);

	/** Get the owning pattern graph */
	UPCGExConnectorPatternGraph* GetPatternGraph() const;

	//~ UEdGraphNode interface
	virtual void PostLoad() override;
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	//~ Pin name helpers
	static FName MakeOutputPinName(int32 TypeId, FName TypeName);
	static FName MakeInputPinName(int32 TypeId, FName TypeName);
	static int32 GetTypeIdFromPinName(FName PinName);

	static const FName AnyPinCategory;
	static const FName ConnectorPinCategory;
	static const FName PatternRootPinCategory;
};
