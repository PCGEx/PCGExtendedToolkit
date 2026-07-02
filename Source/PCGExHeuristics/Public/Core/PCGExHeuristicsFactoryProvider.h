// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExHeuristicsCommon.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "Factories/PCGExFactoryData.h"
#include "Factories/PCGExFactoryProvider.h"
#include "UObject/Object.h"
#include "Utils/PCGExCurveLookup.h"

#include "PCGExHeuristicsFactoryProvider.generated.h"

#define PCGEX_HEURISTIC_FACTORY_BOILERPLATE \
virtual void RegisterAssetDependencies(TSet<FSoftObjectPath>& InDependencies) const override;

#define PCGEX_HEURISTIC_FACTORY_BOILERPLATE_IMPL(_TYPE, _REGISTER_ASSET_BODY)\
void UPCGExHeuristicsFactory##_TYPE::RegisterAssetDependencies(TSet<FSoftObjectPath>& InDependencies) const{\
	Super::RegisterAssetDependencies(InDependencies); }
//	InContext->AddAssetDependency(Config.ScoreCurve.ToSoftObjectPath()); _REGISTER_ASSET_BODY }

#define PCGEX_FORWARD_HEURISTIC_FACTORY \
	NewFactory->WeightFactor = Config.WeightFactor; \
	NewFactory->Config = Config; \
	NewFactory->Config.Init(); \
	NewFactory->ConfigBase = NewFactory->Config;

#define PCGEX_FORWARD_HEURISTIC_CONFIG \
	NewOperation->WeightFactor = Config.WeightFactor; \
	NewOperation->bInvert = Config.bInvert; \
	NewOperation->UVWSeed = Config.UVWSeed; \
	NewOperation->UVWGoal = Config.UVWGoal; \
	NewOperation->ScoreCurve = Config.ScoreLUT; \
	NewOperation->bUseLocalWeightMultiplier = Config.bUseLocalWeightMultiplier; \
	NewOperation->LocalWeightMultiplierSource = Config.LocalWeightMultiplierSource; \
	NewOperation->WeightMultiplierAttribute = Config.WeightMultiplierAttribute;

class FPCGExHeuristicOperation;

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Heuristic"))
struct FPCGExDataTypeInfoHeuristics : public FPCGExFactoryDataTypeInfo
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXHEURISTICS_API)
};

USTRUCT(BlueprintType)
struct PCGEXHEURISTICS_API FPCGExHeuristicConfigBase
{
	GENERATED_BODY()

	FPCGExHeuristicConfigBase()
	{
		LocalScoreCurve.EditorCurveData.AddKey(0, 0);
		LocalScoreCurve.EditorCurveData.AddKey(1, 1);
	}

	~FPCGExHeuristicConfigBase()
	{
	}

	UPROPERTY(meta=(PCG_NotOverridable, EditCondition="false", EditConditionHides))
	bool bRawSettings = false;

	/** The weight factor for this heuristic.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayPriority=-1))
	double WeightFactor = 1;

	/** Invert the final heuristics score. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayPriority=-1, EditCondition="!bRawSettings", EditConditionHides, HideEditConditionToggle))
	bool bInvert = false;

	/** Whether to use in-editor curve or an external asset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayPriority=-1, EditCondition="!bRawSettings", EditConditionHides, HideEditConditionToggle))
	bool bUseLocalCurve = false;

	// TODO: DirtyCache for OnDependencyChanged when this float curve is an external asset
	/** Curve the value will be remapped over. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, DisplayName="Score Curve", EditCondition = "!bRawSettings && bUseLocalCurve", EditConditionHides, DisplayPriority=-1, HideEditConditionToggle))
	FRuntimeFloatCurve LocalScoreCurve;

	/** Curve the value will be remapped over. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Score Curve", EditCondition="!bRawSettings && !bUseLocalCurve", EditConditionHides, DisplayPriority=-1, HideEditConditionToggle))
	TSoftObjectPtr<UCurveFloat> ScoreCurve = TSoftObjectPtr<UCurveFloat>(PCGExCurves::WeightDistributionLinear);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayPriority=-1))
	FPCGExCurveLookupDetails ScoreCurveLookup;

	PCGExFloatLUT ScoreLUT = nullptr;

	/** Use a local attribute */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Local Weight", meta=(PCG_Overridable))
	bool bUseLocalWeightMultiplier = false;

	/** Bound-relative seed position used when this heuristic is used in a "roaming" context */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Roaming", meta=(PCG_Overridable))
	FVector UVWSeed = FVector::ZeroVector;

	/** Bound-relative goal position used when this heuristic is used in a "roaming" context */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Roaming", meta=(PCG_Overridable))
	FVector UVWGoal = FVector::ZeroVector;

	/** Local multiplier attribute source */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Local Weight", meta=(PCG_Overridable, EditCondition="bUseLocalWeightMultiplier", EditConditionHides))
	EPCGExClusterElement LocalWeightMultiplierSource = EPCGExClusterElement::Vtx;

	/** Attribute to read multiplier value from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Local Weight", meta=(PCG_Overridable, EditCondition="bUseLocalWeightMultiplier", EditConditionHides))
	FPCGAttributePropertyInputSelector WeightMultiplierAttribute;

	void Init();
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXHEURISTICS_API UPCGExHeuristicsFactoryData : public UPCGExFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoHeuristics)

	FPCGExHeuristicConfigBase ConfigBase;

	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;

	virtual TSharedPtr<FPCGExHeuristicOperation> CreateOperation(FPCGExContext* InContext) const;
	double WeightFactor = 1;
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params")
class PCGEXHEURISTICS_API UPCGExHeuristicsFactoryProviderSettings : public UPCGExFactoryProviderSettings
{
	GENERATED_BODY()

protected:
	PCGEX_FACTORY_TYPE_ID(FPCGExDataTypeInfoHeuristics)

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(AbstractHeuristics, "Heuristics Definition", "Creates a single heuristic computational node, to be used with pathfinding nodes.")

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Heuristics);
	}
#endif
	//~End UPCGSettings

	virtual FName GetMainOutputPin() const override
	{
		return PCGExHeuristics::Labels::OutputHeuristicsLabel;
	}

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
