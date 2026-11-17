// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPickerFactoryProvider.h"

#include "PCGExPickerConstantRange.generated.h"

USTRUCT(BlueprintType)
struct PCGEXPICKERS_API FPCGExPickerConstantRangeConfig : public FPCGExPickerConfigBase
{
	GENERATED_BODY()

	FPCGExPickerConstantRangeConfig()
		: FPCGExPickerConfigBase()
	{
	}

	/**  Use negative values to select from the end. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="!bTreatAsNormalized", EditConditionHides, DisplayAfter="bTreatAsNormalized"))
	int32 DiscreteStartIndex = 0;

	/** Negative values count back from the last entry: -1 is the first one. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bTreatAsNormalized", EditConditionHides, DisplayAfter="bTreatAsNormalized"))
	double RelativeStartIndex = 0;

	/**  Use negative values to select from the end. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="!bTreatAsNormalized", EditConditionHides, DisplayAfter="bTreatAsNormalized"))
	int32 DiscreteEndIndex = 0;

	/** Negative values count back from the last entry: -1 is the first one. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bTreatAsNormalized", EditConditionHides, DisplayAfter="bTreatAsNormalized"))
	double RelativeEndIndex = 0;

	virtual void Sanitize() override;

	bool IsWithin(double Value) const;
	bool IsWithinInclusive(double Value) const;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data", meta=(PCGExNodeLibraryDoc="utilities/pickers/picker-range"))
class UPCGExPickerConstantRangeFactory : public UPCGExPickerFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExPickerConstantRangeConfig Config;

	static void AddPicksFromConfig(const FPCGExPickerConstantRangeConfig& InConfig, int32 InNum, TSet<int32>& OutPicks);
	virtual void AddPicks(int32 InNum, TSet<int32>& OutPicks) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Pickers|Params")
class UPCGExPickerConstantRangeSettings : public UPCGExPickerFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(PickerConstantRange, "Picker : Range", "A Picker that selects a range of values.", FName(GetDisplayName()))

#endif
	//~End UPCGSettings

	/** Picker properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExPickerConstantRangeConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
