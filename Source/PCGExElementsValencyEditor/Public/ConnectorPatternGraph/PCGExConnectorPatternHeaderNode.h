// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "Core/PCGExValencyPattern.h"

#include "PCGExConnectorPatternHeaderNode.generated.h"

class UPCGExConnectorPatternGraph;

/**
 * Pattern header node — holds pattern-level settings and connects to exactly one
 * entry node via a "Root" wire. That entry becomes the match center (entry 0).
 * Visually distinct: purple title, shows PatternName prominently.
 */
UCLASS()
class PCGEXELEMENTSVALENCYEDITOR_API UPCGExConnectorPatternHeaderNode : public UEdGraphNode
{
	GENERATED_BODY()

public:
	//~ Pattern-level settings

	/** If false, this pattern is excluded from compilation */
	UPROPERTY(EditAnywhere, Category = "Pattern")
	bool bEnabled = true;

	/** Pattern name for identification and filtering */
	UPROPERTY(EditAnywhere, Category = "Pattern")
	FName PatternName;

	/** Weight for probabilistic selection among competing patterns */
	UPROPERTY(EditAnywhere, Category = "Pattern", meta = (ClampMin = "0.001"))
	float Weight = 1.0f;

	/** If true, matched points are claimed exclusively */
	UPROPERTY(EditAnywhere, Category = "Pattern")
	bool bExclusive = true;

	/** Minimum times this pattern must be matched (0 = no minimum) */
	UPROPERTY(EditAnywhere, Category = "Pattern", meta = (ClampMin = 0))
	int32 MinMatches = 0;

	/** Maximum times this pattern can be matched (-1 = unlimited) */
	UPROPERTY(EditAnywhere, Category = "Pattern", meta = (ClampMin = -1))
	int32 MaxMatches = -1;

	/** Output strategy for matched points */
	UPROPERTY(EditAnywhere, Category = "Pattern")
	EPCGExPatternOutputStrategy OutputStrategy = EPCGExPatternOutputStrategy::Annotate;

	/** Transform computation mode for Collapse strategy */
	UPROPERTY(EditAnywhere, Category = "Pattern")
	EPCGExPatternTransformMode TransformMode = EPCGExPatternTransformMode::Centroid;

	//~ UEdGraphNode interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual bool CanUserDeleteNode() const override { return true; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Pin category for the Root wire */
	static const FName PatternRootPinCategory;
};
