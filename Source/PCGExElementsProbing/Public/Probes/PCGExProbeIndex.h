// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <functional>

#include "CoreMinimal.h"
#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"
#include "Math/PCGExMath.h"
#include "UObject/Object.h"

#include "PCGExProbeIndex.generated.h"

namespace PCGExProbing
{
	struct FCandidate;
}

UENUM()
enum class EPCGExProbeTargetMode : uint8
{
	Target       = 0 UMETA(DisplayName = "Target", ToolTip="Target index is used as-is to create a connection"),
	OneWayOffset = 1 UMETA(DisplayName = "One-way Offset", ToolTip="Target index is used as an offset value from the current point' index"),
	TwoWayOffset = 2 UMETA(DisplayName = "Two-way Offset", ToolTip="Target index is used as both a positive and negative offset value from the current point' index"),
};

USTRUCT(BlueprintType)
struct FPCGExProbeConfigIndex : public FPCGExProbeConfigBase
{
	GENERATED_BODY()

	FPCGExProbeConfigIndex()
		: FPCGExProbeConfigBase(false)
	{
	}

	/** How to use the resolved index for probing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExProbeTargetMode Mode = EPCGExProbeTargetMode::Target;

	/** How to handle out-of-range indices. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExIndexSafety IndexSafety = EPCGExIndexSafety::Ignore;


	/** Target index. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Index"))
	FPCGExInputShorthandSelectorInteger32Abs Index = FPCGExInputShorthandSelectorInteger32Abs(FName("@Last"), 1, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType IndexInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector IndexAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	int32 IndexConstant_DEPRECATED = 1;

#pragma endregion

#if WITH_EDITOR
	virtual void ApplyDeprecation() override;
	virtual void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const override;
#endif
};

/**
 * 
 */
class FPCGExProbeIndex : public FPCGExProbeOperation
{
public:
	virtual bool IsDirectProbe() const override;
	virtual bool Prepare(FPCGExContext* InContext) override;

	virtual void ProcessNode(const int32 Index, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container) override
	{
		TryCreateEdge(Index, OutEdges, *AcceptConnections);
	}

	FPCGExProbeConfigIndex Config;
	TSharedPtr<PCGExDetails::TSettingValue<int32>> TargetCache;

	using TryCreateEdgeCallback = std::function<void(const int32, TSet<uint64>*, const TArray<int8>&)>;
	TryCreateEdgeCallback TryCreateEdge;

protected:
	int32 MaxIndex = -1;
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points/probe-index"))
class UPCGExProbeFactoryIndex : public UPCGExProbeFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExProbeConfigIndex Config;

	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params")
class UPCGExProbeIndexProviderSettings : public UPCGExProbeFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ProbeIndex, "Probe : Index", "Connects to a specific index, ignoring search radius.", FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExProbeConfigIndex Config;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
