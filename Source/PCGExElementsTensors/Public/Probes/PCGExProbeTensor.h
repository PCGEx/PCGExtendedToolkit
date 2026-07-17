// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExProbeFactoryProvider.h"
#include "UObject/Object.h"

#include "Core/PCGExProbeOperation.h"
#include "Core/PCGExTensorHandler.h"
#include "Probes/PCGExProbeDirection.h"

#include "PCGExProbeTensor.generated.h"

USTRUCT(BlueprintType)
struct FPCGExProbeConfigTensor : public FPCGExProbeConfigBase
{
	GENERATED_BODY()

	FPCGExProbeConfigTensor()
		: FPCGExProbeConfigBase()
	{
	}

	/** Mirror (*-1) the direction sampled from the tensors. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvertTensorDirection = false;

	/** What matters more? */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExProbeDirectionPriorization Favor = EPCGExProbeDirectionPriorization::Dot;

	/** Use separate angle thresholds for pitch, yaw, and roll instead of a single max angle. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bUseComponentWiseAngle = false;

	/** Max angle to search within. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="!bUseComponentWiseAngle", ClampMin=0, ClampMax=180))
	double MaxAngle = 45;

	/** Max angle to search within. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bUseComponentWiseAngle", ClampMin=0, ClampMax=180))
	FRotator MaxAngles = FRotator(45);

	/** This probe will sample candidates after the other. Can yield different results. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bDoChainedProcessing = false;

	/** Tensor sampling settings. Note that these are applied on the flattened sample, e.g after & on top of individual tensors' mutations. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Tensor Sampling Settings"))
	FPCGExTensorHandlerDetails TensorHandlerDetails;

#if WITH_EDITOR
	virtual void ApplyDeprecation() override;
	virtual void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const override;
#endif
};

/**
 * 
 */
class FPCGExProbeTensor : public FPCGExProbeOperation
{
public:
	virtual bool RequiresChainProcessing() const override;
	virtual bool Prepare(FPCGExContext* InContext) override;
	virtual void ProcessCandidates(const int32 Index, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container) override;

	virtual void PrepareBestCandidate(const int32 Index, PCGExProbing::FBestCandidate& InBestCandidate, PCGExMT::FScopedContainer* Container) override;
	virtual void ProcessCandidateChained(const int32 Index, const int32 CandidateIndex, PCGExProbing::FCandidate& Candidate, PCGExProbing::FBestCandidate& InBestCandidate, PCGExMT::FScopedContainer* Container) override;
	virtual void ProcessBestCandidate(const int32 Index, PCGExProbing::FBestCandidate& InBestCandidate, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container) override;

	FPCGExProbeConfigTensor Config;
	const TArray<TObjectPtr<const UPCGExTensorFactoryData>>* TensorFactories = nullptr;
	TSharedPtr<PCGExTensor::FTensorsHandler> TensorsHandler;

protected:
	bool bUseConstantDir = false;
	double Mirror = 1;
	double MinDot = 0;
	bool bUseBestDot = false;
	FVector Direction = FVector::ForwardVector;
	TSharedPtr<PCGExData::TBuffer<FVector>> DirectionCache;
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExProbeFactoryTensor : public UPCGExProbeFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExProbeConfigTensor Config;

	UPROPERTY()
	TArray<TObjectPtr<const UPCGExTensorFactoryData>> TensorFactories;

	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const override;

	virtual bool WantsPreparation(FPCGExContext* InContext) override
	{
		return true;
	}

	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points/probe-tensor"))
class UPCGExProbeTensorProviderSettings : public UPCGExProbeFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ProbeTensor, "Probe : Tensor", "Sample a tensor at point location and probe in that direction.", FName(GetDisplayName()))
#endif
	//~End UPCGSettings

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

public:
	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExProbeConfigTensor Config;


#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
