// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExGoalPicker.h"
#include "UObject/Object.h"

#include "PCGExGoalPickerAll.generated.h"

/**
 * 
 */
UCLASS(MinimalAPI, meta=(DisplayName = "All", PCGExNodeLibraryDoc="pathfinding/pathfinding-edges/goal-picker-all"))
class UPCGExGoalPickerAll : public UPCGExGoalPicker
{
	GENERATED_BODY()

public:
	virtual bool PrepareForData(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InSeedsDataFacade, const TSharedPtr<PCGExData::FFacade>& InGoalsDataFacade) override;

	virtual void GetGoalIndices(const PCGExData::FConstPoint& Seed, TArray<int32>& OutIndices) const override;
	virtual bool OutputMultipleGoals() const override;

	virtual void Cleanup() override;

protected:
	int32 GoalsNum = -1;
};
