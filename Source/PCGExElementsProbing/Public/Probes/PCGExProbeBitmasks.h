// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"
#include "Data/Bitmasks/PCGExBitmaskDetails.h"

#include "PCGExProbeBitmasks.generated.h"

namespace PCGExBitmask
{
	class FBitmaskData;
}

UENUM()
enum class EPCGExProbeBitmasksPriorization : uint8
{
	Dot  = 0 UMETA(DisplayName = "Best alignment", ToolTip="Favor the candidates that best align with the direction, as opposed to closest ones."),
	Dist = 1 UMETA(DisplayName = "Closest position", ToolTip="Favor the candidates that are the closest, even if they were not the best aligned."),
};

namespace PCGExProbeBitmasks
{
	class FScopedContainer : public PCGExMT::FScopedContainer
	{
	public:
		FScopedContainer(const PCGExMT::FScope& InScope);

		TArray<double> BestDotsBuffer;
		TArray<double> BestDistsBuffer;
		TArray<int32> BestIdxBuffer;
		TArray<FVector> WorkingDirs;

		void Init(const TSharedPtr<PCGExBitmask::FBitmaskData>& BitmaskData, const bool bCopyDirs);
		virtual void Reset() override;
	};
}

USTRUCT(BlueprintType)
struct FPCGExProbeConfigBitmasks : public FPCGExProbeConfigBase
{
	GENERATED_BODY()

	FPCGExProbeConfigBitmasks()
		: FPCGExProbeConfigBase()
	{
	}

	/** Transform the direction with the point's */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bTransformDirection = true;

	/** What matters more? */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExProbeBitmasksPriorization Favor = EPCGExProbeBitmasksPriorization::Dist;

	/** Shared angle threshold */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	double Angle = 22.5;

	/** Operations executed on the flag if all filters pass (or if no filter is set) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	TArray<FPCGExBitmaskRef> Compositions;

	/** Operations executed on the flag if all filters pass (or if no filter is set) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	TMap<TObjectPtr<UPCGExBitmaskCollection>, EPCGExBitOp_OR> Collections;
};

/**
 * 
 */
class FPCGExProbeBitmasks : public FPCGExProbeOperation
{
public:
	virtual TSharedPtr<PCGExMT::FScopedContainer> GetScopedContainer(const PCGExMT::FScope& InScope) const override;
	virtual bool RequiresChainProcessing() const override;
	virtual bool Prepare(FPCGExContext* InContext) override;
	virtual void ProcessCandidates(const int32 Index, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container) override;

	FPCGExProbeConfigBitmasks Config;
	TSharedPtr<PCGExBitmask::FBitmaskData> BitmaskData = nullptr;

protected:
	double DirectionMultiplier = 1;
	bool bUseConstantDir = false;
	double MinDot = 0;
	bool bUseBestDot = false;

	TSharedPtr<PCGExDetails::TSettingValue<FVector>> Direction;
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points/probe-bitmasks"))
class UPCGExProbeFactoryBitmasks : public UPCGExProbeFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExProbeConfigBitmasks Config;

	TSharedPtr<PCGExBitmask::FBitmaskData> BitmaskData = nullptr;

	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params")
class UPCGExProbeBitmasksProviderSettings : public UPCGExProbeFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ProbeBitmasks, "Probe : Bitmasks", "Probe using bitmasks references & collections.", FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExProbeConfigBitmasks Config;


#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
