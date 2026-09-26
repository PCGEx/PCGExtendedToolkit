// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "GoalPickers/PCGExGoalPickerRandom.h"

#include "PCGExVersion.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExPointElements.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExRandomHelpers.h"

#if WITH_EDITOR
void UPCGExGoalPickerRandom::PCGExApplyDeprecation(const int64 PCGExDataVersion)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		NumGoalsValue.Update(NumGoalsType_DEPRECATED, NumGoalAttribute_DEPRECATED, NumGoals_DEPRECATED);
	}

	Super::PCGExApplyDeprecation(PCGExDataVersion);
}
#endif

void UPCGExGoalPickerRandom::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
	if (const UPCGExGoalPickerRandom* TypedOther = Cast<UPCGExGoalPickerRandom>(Other))
	{
		LocalSeed = TypedOther->LocalSeed;
		GoalCount = TypedOther->GoalCount;
		NumGoalsValue = TypedOther->NumGoalsValue;
	}
}

bool UPCGExGoalPickerRandom::PrepareForData(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InSeedsDataFacade, const TSharedPtr<PCGExData::FFacade>& InGoalsDataFacade)
{
	if (!Super::PrepareForData(InContext, InSeedsDataFacade, InGoalsDataFacade))
	{
		return false;
	}

	NumGoalsBuffer = NumGoalsValue.GetValueSetting();
	if (!NumGoalsBuffer->Init(InSeedsDataFacade, false))
	{
		return false;
	}

	return true;
}

int32 UPCGExGoalPickerRandom::GetGoalIndex(const PCGExData::FConstPoint& Seed) const
{
	FRandomStream Random = PCGExRandomHelpers::GetRandomStreamFromPoint(Seed.Data->GetSeed(Seed.Index), Seed.Index);
	return Random.RandRange(0, MaxGoalIndex);
}

void UPCGExGoalPickerRandom::GetGoalIndices(const PCGExData::FConstPoint& Seed, TArray<int32>& OutIndices) const
{
	int32 Picks = NumGoalsBuffer->Read(Seed.Index);

	FRandomStream Random = PCGExRandomHelpers::GetRandomStreamFromPoint(Seed.Data->GetSeed(Seed.Index), Seed.Index);

	if (GoalCount == EPCGExGoalPickRandomAmount::Random)
	{
		Picks = Random.RandRange(0, Picks);
	}

	for (int i = 0; i < Picks; i++)
	{
		OutIndices.Emplace(Random.RandRange(0, MaxGoalIndex));
	}
}

bool UPCGExGoalPickerRandom::OutputMultipleGoals() const
{
	return GoalCount != EPCGExGoalPickRandomAmount::Single;
}

void UPCGExGoalPickerRandom::Cleanup()
{
	Super::Cleanup();
}
