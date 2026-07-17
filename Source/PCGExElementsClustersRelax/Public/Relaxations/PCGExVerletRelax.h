// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExRelaxClusterOperation.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsDetails.h"
#include "Elements/Meta/NeighborSamplers/PCGExNeighborSampleAttribute.h"
#include "PCGExVerletRelax.generated.h"

UENUM()
enum class EPCGExRelaxEdgeRestLength : uint8
{
	Fixed     = 0 UMETA(DisplayName = "Fixed", ToolTip="Aim for constant edge length while fitting"),
	Existing  = 1 UMETA(DisplayName = "Existing", ToolTip="Attempts to preserve existing edge length"),
	Attribute = 2 UMETA(DisplayName = "Attribute", ToolTip="Uses an attribute on the edges as target length"),
};

/**
 *
 */
UCLASS(MinimalAPI, meta=(DisplayName = "Verlet (Gravity)", PCGExNodeLibraryDoc="clusters/relax-cluster/Gravity"))
class UPCGExVerletRelax : public UPCGExRelaxClusterOperation
{
	GENERATED_BODY()

public:
	UPCGExVerletRelax(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{
	}

	virtual void RegisterPrimaryBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;


	/** Gravity. Think of it as gravity vector. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Gravity"))
	FPCGExInputShorthandSelectorVector GravityValue = FPCGExInputShorthandSelectorVector(FString(TEXT("")), FVector(0, 0, -100), false);

	/** Friction. Expected to be in the [0..1] range. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Friction"))
	FPCGExInputShorthandSelectorDouble01 FrictionValue = FPCGExInputShorthandSelectorDouble01(FString(TEXT("")), 0, false);

	/** Edge Scaling. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Edge Scaling"))
	FPCGExInputShorthandSelectorDouble EdgeScalingValue = FPCGExInputShorthandSelectorDouble(FString(TEXT("")), 1, false);

	/** Edge Stiffness. Expected to be in the [0..1] range. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Edge Stiffness"))
	FPCGExInputShorthandSelectorDouble01 EdgeStiffnessValue = FPCGExInputShorthandSelectorDouble01(FString(TEXT("")), 0.5, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType GravityInput_DEPRECATED = EPCGExInputValueType::Constant;
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector GravityAttribute_DEPRECATED;
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FVector Gravity_DEPRECATED = FVector(0, 0, -100);

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType FrictionInput_DEPRECATED = EPCGExInputValueType::Constant;
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector FrictionAttribute_DEPRECATED;
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double Friction_DEPRECATED = 0;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType EdgeScalingInput_DEPRECATED = EPCGExInputValueType::Constant;
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector EdgeScalingAttribute_DEPRECATED;
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double EdgeScaling_DEPRECATED = 1;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType EdgeStiffnessInput_DEPRECATED = EPCGExInputValueType::Constant;
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector EdgeStiffnessAttribute_DEPRECATED;
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double EdgeStiffness_DEPRECATED = 0.5;

#pragma endregion

#if WITH_EDITOR
	virtual void ApplyShorthandDeprecation() override;
#endif

	/** If this was a physic simulation, represent the time advance each iteration */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	double TimeStep = 0.1;

	/** Velocity damping multiplier applied each iteration. Lower values = more damping, smoother convergence. Higher values retain momentum for more natural sag. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin=0, ClampMax=1, UIMin=0, UIMax=1))
	double DampingScale = 0.99;

	virtual bool PrepareForCluster(FPCGExContext* InContext, const TSharedPtr<PCGExClusters::FCluster>& InCluster) override;

	virtual int32 GetNumSteps() override
	{
		return 3;
	}

	virtual EPCGExClusterElement PrepareNextStep(const int32 InStep) override;
	virtual void Step1(const PCGExClusters::FNode& Node) override;
	virtual void Step2(const PCGExGraphs::FEdge& Edge) override;
	virtual void Step3(const PCGExClusters::FNode& Node) override;

protected:
	TSharedPtr<TArray<double>> EdgeLengths;
	TSharedPtr<PCGExDetails::TSettingValue<FVector>> GravityBuffer;
	TSharedPtr<PCGExDetails::TSettingValue<double>> StiffnessBuffer;
	TSharedPtr<PCGExDetails::TSettingValue<double>> ScalingBuffer;
	TSharedPtr<PCGExDetails::TSettingValue<double>> FrictionBuffer;

	TArray<int8> Hits;
	TArray<FVector> HitLocations;
};
