// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"

#include "PCGExProbeDirection.generated.h"

UENUM()
enum class EPCGExProbeDirectionPriorization : uint8
{
	Dot  = 0 UMETA(DisplayName = "Best alignment", ToolTip="Favor the candidates that best align with the direction, as opposed to closest ones."),
	Dist = 1 UMETA(DisplayName = "Closest position", ToolTip="Favor the candidates that are the closest, even if they were not the best aligned."),
};

USTRUCT(BlueprintType)
struct FPCGExProbeConfigDirection : public FPCGExProbeConfigBase
{
	GENERATED_BODY()

	FPCGExProbeConfigDirection()
		: FPCGExProbeConfigBase()
	{
	}

	/** Use separate angle thresholds for pitch, yaw, and roll instead of a single max angle. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bUseComponentWiseAngle = false;

	/** Max angle to search within. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="!bUseComponentWiseAngle", ClampMin=0, ClampMax=180))
	double MaxAngle = 45;

	/** Max angle to search within. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bUseComponentWiseAngle", ClampMin=0, ClampMax=180))
	FRotator MaxAngles = FRotator(45);

	/** Use absolute angle values (ignores direction sign, treating opposite directions as equivalent). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bUnsignedCheck = false;

	/** Direction to probe in. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Direction"))
	FPCGExInputShorthandSelectorVector Direction = FPCGExInputShorthandSelectorVector(FName("@Last"), FVector::ForwardVector, false);

	/** Invert the probe direction. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Invert", EditCondition="Direction.Input != EPCGExInputValueType::Constant", EditConditionHides))
	bool bInvertDirection = false;

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType DirectionInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector DirectionAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FVector DirectionConstant_DEPRECATED = FVector::ForwardVector;

#pragma endregion

	/** Transform the direction with the point's */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bTransformDirection = true;

	/** What matters more? */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExProbeDirectionPriorization Favor = EPCGExProbeDirectionPriorization::Dist;

	/** This probe will sample candidates after the other. Can yield different results. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bDoChainedProcessing = false;

#if WITH_EDITOR
	virtual void ApplyDeprecation() override;
	virtual void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const override;
#endif
};

/**
 * 
 */
class FPCGExProbeDirection : public FPCGExProbeOperation
{
public:
	virtual bool RequiresChainProcessing() const override;
	virtual bool Prepare(FPCGExContext* InContext) override;
	virtual void ProcessCandidates(const int32 Index, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container) override;

	virtual void PrepareBestCandidate(const int32 Index, PCGExProbing::FBestCandidate& InBestCandidate, PCGExMT::FScopedContainer* Container) override;
	virtual void ProcessCandidateChained(const int32 Index, const int32 CandidateIndex, PCGExProbing::FCandidate& Candidate, PCGExProbing::FBestCandidate& InBestCandidate, PCGExMT::FScopedContainer* Container) override;
	virtual void ProcessBestCandidate(const int32 Index, PCGExProbing::FBestCandidate& InBestCandidate, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container) override;

	FPCGExProbeConfigDirection Config;

protected:
	double DirectionMultiplier = 1;
	bool bUseConstantDir = false;
	double MinDot = 0;
	bool bUseBestDot = false;

	TSharedPtr<PCGExDetails::TSettingValue<FVector>> Direction;
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points/probe-direction"))
class UPCGExProbeFactoryDirection : public UPCGExProbeFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExProbeConfigDirection Config;

	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params")
class UPCGExProbeDirectionProviderSettings : public UPCGExProbeFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ProbeDirection, "Probe : Direction", "Probe in a given direction.", FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExProbeConfigDirection Config;


#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
