// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <functional>

#include "CoreMinimal.h"
#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Math/PCGExMath.h"
#include "UObject/Object.h"

#include "PCGExGlobalProbeKNN.generated.h"

namespace PCGExProbing
{
	struct FCandidate;
}

UENUM()
enum class EPCGExProbeKNNMode : uint8
{
	Default = 0 UMETA(DisplayName = "Default", ToolTip=""),
	Mutual  = 1 UMETA(DisplayName = "Mutual", ToolTip=""),
};

USTRUCT(BlueprintType)
struct FPCGExProbeConfigKNN : public FPCGExProbeConfigBase
{
	GENERATED_BODY()

	FPCGExProbeConfigKNN()
		: FPCGExProbeConfigBase(false)
	{
	}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorInteger32Abs K = FPCGExInputShorthandSelectorInteger32Abs(FName("K"), 5, false);

	/** How K-nearest neighbors are connected. Mutual requires both points to be nearest. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExProbeKNNMode Mode = EPCGExProbeKNNMode::Mutual;
};

/**
 * 
 */
class FPCGExProbeKNN : public FPCGExProbeOperation
{
public:
	virtual bool IsGlobalProbe() const override;

	virtual bool Prepare(FPCGExContext* InContext) override;
	virtual void ProcessAll(TSet<uint64>& OutEdges) const override;

	FPCGExProbeConfigKNN Config;
	TSharedPtr<PCGExDetails::TSettingValue<int32>> K;
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExProbeFactoryKNN : public UPCGExProbeFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExProbeConfigKNN Config;

	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points/g-probe-knn"))
class UPCGExProbeKNNProviderSettings : public UPCGExProbeFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(ProbeKNN, "G-Probe : KNN", "K-Nearest Neighbors")
#endif
	//~End UPCGSettings

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExProbeConfigKNN Config;
};
