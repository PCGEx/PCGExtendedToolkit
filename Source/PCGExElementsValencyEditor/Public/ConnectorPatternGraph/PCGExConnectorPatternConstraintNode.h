// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExConnectorPatternGraphNode.h"

#include "PCGExConnectorPatternConstraintNode.generated.h"

/** Constraint type for pattern constraint nodes */
UENUM()
enum class EPCGExPatternConstraintType : uint8
{
	/** Connected connector types must have NO connections (exposed/free) */
	Boundary,
	/** Connected connector types must have at least one connection */
	Wildcard
};

/**
 * Constraint marker node for the pattern graph.
 * Wiring a pattern entry's typed pin to this node's input applies the constraint to that type.
 * Inherits full connector pin management from pattern graph node.
 */
UCLASS()
class PCGEXELEMENTSVALENCYEDITOR_API UPCGExConnectorPatternConstraintNode : public UPCGExConnectorPatternGraphNode
{
	GENERATED_BODY()

public:
	/** Type of constraint this node represents */
	UPROPERTY(EditAnywhere, Category = "Constraint")
	EPCGExPatternConstraintType ConstraintType = EPCGExPatternConstraintType::Boundary;

	//~ UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
