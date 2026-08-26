// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGElement.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExRelaxClusterOperation.h"
#include "Data/PCGExData.h"


#include "PCGExFittingRelaxBase.generated.h"

UENUM()
enum class EPCGExRelaxEdgeFitting : uint8
{
	Ignore    = 0 UMETA(DisplayName = "Ignore", ToolTip="Ignore edges during fitting."),
	Fixed     = 1 UMETA(DisplayName = "Fixed", ToolTip="Aim for constant edge length while fitting"),
	Existing  = 2 UMETA(DisplayName = "Existing", ToolTip="Attempts to preserve existing edge length"),
	Attribute = 3 UMETA(DisplayName = "Attribute", ToolTip="Uses an attribute on the edges as target length"),
};

/**
 *
 */
UCLASS(Abstract, MinimalAPI, DisplayName = "Abstract Fitting", meta=(PCGExNodeLibraryDoc="clusters/transform/cluster-relax/abstract-fitting"))
class UPCGExFittingRelaxBase : public UPCGExRelaxClusterOperation
{
	GENERATED_BODY()

public:
	UPCGExFittingRelaxBase(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{
	}


	/** Amount of translation for a single step. Relative to other parameters. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	double RepulsionConstant = 100;

	/** Which edge length should the computation attempt to preserve. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExRelaxEdgeFitting EdgeFitting = EPCGExRelaxEdgeFitting::Existing;

	/** The desired edge length. Low priority in the algorithm, but help keep edge topology more consistent. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Desired Edge Length", EditCondition="EdgeFitting == EPCGExRelaxEdgeFitting::Fixed", EditConditionHides))
	double DesiredEdgeLength = 100;

	/** Per-edge attribute */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Desired Edge Length", EditCondition="EdgeFitting == EPCGExRelaxEdgeFitting::Attribute", EditConditionHides))
	FPCGAttributePropertyInputSelector DesiredEdgeLengthAttribute;

	/** Scale factor applied to the edge length. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" └─ Scale", EditCondition="EdgeFitting == EPCGExRelaxEdgeFitting::Attribute || EdgeFitting == EPCGExRelaxEdgeFitting::Existing", EditConditionHides))
	double Scale = 2;

	/** Stiffness of the edges. Lower values yield better placement (less overlap), but edge topology may be affected. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="EdgeFitting != EPCGExRelaxEdgeFitting::Ignore", EditConditionHides))
	double SpringConstant = 0.1;

	/** If this was a physic simulation, represent the time advance each iteration */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	double TimeStep = 0.01;

	virtual bool PrepareForCluster(FPCGExContext* InContext, const TSharedPtr<PCGExClusters::FCluster>& InCluster) override;

	virtual int32 GetNumSteps() override
	{
		return 3;
	}

	virtual EPCGExClusterElement PrepareNextStep(const int32 InStep) override;
	virtual void Step1(const PCGExGraphs::FEdge& Edge) override;
	virtual void Step3(const PCGExClusters::FNode& Node) override;

protected:
	TSharedPtr<TArray<double>> EdgeLengths;
};
