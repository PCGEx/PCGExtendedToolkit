// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExGoalPicker.h"
#include "Data/PCGExDataHelpers.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "UObject/Object.h"

#include "PCGExGoalPickerRandom.generated.h"

UENUM()
enum class EPCGExGoalPickRandomAmount : uint8
{
	Single = 0 UMETA(DisplayName = "Single", Tooltip="A single random goal is picked"),
	Fixed  = 1 UMETA(DisplayName = "Multiple Fixed", Tooltip="A fixed number of random goals is picked"),
	Random = 2 UMETA(DisplayName = "Multiple Random", Tooltip="A random number of random goals is picked."),
};

/**
 * 
 */
UCLASS(MinimalAPI, meta=(DisplayName = "Random", PCGExNodeLibraryDoc="pathfinding/pathfinding-edges/goal-picker-random"))
class UPCGExGoalPickerRandom : public UPCGExGoalPicker
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	int32 LocalSeed = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExGoalPickRandomAmount GoalCount = EPCGExGoalPickRandomAmount::Single;

	/** Number of goals to pick per seed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName="Num Goals", EditCondition="GoalCount != EPCGExGoalPickRandomAmount::Single", ClampMin=1))
	FPCGExInputShorthandSelectorInteger32Abs NumGoalsValue = FPCGExInputShorthandSelectorInteger32Abs(FPCGAttributePropertyInputSelector(), 5);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType NumGoalsType_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector NumGoalAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	int32 NumGoals_DEPRECATED = 5;

#pragma endregion

#if WITH_EDITOR
	virtual void PCGExApplyDeprecation(const int64 PCGExDataVersion) override;
#endif

	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;

	virtual bool PrepareForData(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InSeedsDataFacade, const TSharedPtr<PCGExData::FFacade>& InGoalsDataFacade) override;

	virtual int32 GetGoalIndex(const PCGExData::FConstPoint& Seed) const override;
	virtual void GetGoalIndices(const PCGExData::FConstPoint& Seed, TArray<int32>& OutIndices) const override;
	virtual bool OutputMultipleGoals() const override;

	virtual void Cleanup() override;

protected:
	TSharedPtr<PCGExDetails::TSettingValue<int32>> NumGoalsBuffer;
};
