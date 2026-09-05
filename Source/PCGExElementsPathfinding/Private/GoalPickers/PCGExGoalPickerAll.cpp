// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "GoalPickers/PCGExGoalPickerAll.h"

#include "Data/PCGExData.h"

void UPCGExGoalPickerAll::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
}

bool UPCGExGoalPickerAll::PrepareForData(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InSeedsDataFacade, const TSharedPtr<PCGExData::FFacade>& InGoalsDataFacade)
{
	if (!Super::PrepareForData(InContext, InSeedsDataFacade, InGoalsDataFacade))
	{
		return false;
	}
	GoalsNum = InGoalsDataFacade->GetNum();
	return GoalsNum > 0;
}

void UPCGExGoalPickerAll::GetGoalIndices(const PCGExData::FConstPoint& Seed, TArray<int32>& OutIndices) const
{
	OutIndices.Reserve(OutIndices.Num() + GoalsNum);
	for (int i = 0; i < GoalsNum; i++)
	{
		OutIndices.Add(i);
	}
}

bool UPCGExGoalPickerAll::OutputMultipleGoals() const
{
	return true;
}

void UPCGExGoalPickerAll::Cleanup()
{
	Super::Cleanup();
}
