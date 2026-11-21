// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Pickers/PCGExPickerConstantRange.h"

#include "Containers/PCGExManagedObjects.h"
#include "Math/PCGExMath.h"

#define LOCTEXT_NAMESPACE "PCGExCreatePickerConstantRange"
#define PCGEX_NAMESPACE CreatePickerConstantRange

PCGEX_PICKER_BOILERPLATE(ConstantRange, {})

#if WITH_EDITOR
FString UPCGExPickerConstantRangeSettings::GetDisplayName() const
{
	FString DisplayName = TEXT("Pick [");

	if (Config.bTreatAsNormalized)
	{
		DisplayName += FString::Printf(TEXT("%.2f"), Config.RelativeStartIndex);
		DisplayName += TEXT(":");
		DisplayName += FString::Printf(TEXT("%.2f"), Config.RelativeEndIndex);
	}
	else
	{
		DisplayName += FString::Printf(TEXT("%d"), Config.DiscreteStartIndex);
		DisplayName += TEXT(":");
		DisplayName += FString::Printf(TEXT("%d"), Config.DiscreteEndIndex);
	}

	return DisplayName + TEXT("]");
}
#endif

void FPCGExPickerConstantRangeConfig::Sanitize()
{
	FPCGExPickerConfigBase::Sanitize();

	if (DiscreteStartIndex > DiscreteEndIndex)
	{
		std::swap(DiscreteStartIndex, DiscreteEndIndex);
	}
	if (RelativeStartIndex > RelativeEndIndex)
	{
		std::swap(RelativeStartIndex, RelativeEndIndex);
	}
}

bool FPCGExPickerConstantRangeConfig::IsWithin(const double Value) const
{
	return FMath::IsWithin(Value, RelativeStartIndex, RelativeEndIndex);
}

bool FPCGExPickerConstantRangeConfig::IsWithinInclusive(const double Value) const
{
	return FMath::IsWithinInclusive(Value, RelativeStartIndex, RelativeEndIndex);
}

void UPCGExPickerConstantRangeFactory::AddPicksFromConfig(const FPCGExPickerConstantRangeConfig& InConfig, int32 InNum, TSet<int32>& OutPicks)
{
	if (InNum <= 0)
	{
		return;
	}

	int32 TargetStartIndex = 0;
	int32 TargetEndIndex = 0;
	const int32 MaxIndex = InNum - 1;

	if (!InConfig.bTreatAsNormalized)
	{
		TargetStartIndex = PCGExPickers::ResolveDiscrete(InConfig.DiscreteStartIndex, InNum);
		TargetEndIndex = PCGExPickers::ResolveDiscrete(InConfig.DiscreteEndIndex, InNum);
	}
	else
	{
		TargetStartIndex = PCGExPickers::ResolveRelative(InConfig.RelativeStartIndex, InNum, InConfig.TruncateMode);
		TargetEndIndex = PCGExPickers::ResolveRelative(InConfig.RelativeEndIndex, InNum, InConfig.TruncateMode);
	}

	// Ordered once from-the-end indices are resolved: the config can only order the raw values.
	if (TargetStartIndex > TargetEndIndex)
	{
		Swap(TargetStartIndex, TargetEndIndex);
	}

	if (InConfig.Safety == EPCGExIndexSafety::Ignore)
	{
		// Ignore drops out-of-bounds indices one by one, not the range they belong to.
		TargetStartIndex = FMath::Max(TargetStartIndex, 0);
		TargetEndIndex = FMath::Min(TargetEndIndex, MaxIndex);
	}
	else
	{
		TargetStartIndex = PCGExMath::SanitizeIndex(TargetStartIndex, MaxIndex, InConfig.Safety);
		TargetEndIndex = PCGExMath::SanitizeIndex(TargetEndIndex, MaxIndex, InConfig.Safety);
	}

	if (TargetStartIndex > TargetEndIndex)
	{
		return;
	}

	OutPicks.Reserve(OutPicks.Num() + TargetEndIndex - TargetStartIndex + 1);
	for (int32 i = TargetStartIndex; i <= TargetEndIndex; i++)
	{
		OutPicks.Add(i);
	}
}

void UPCGExPickerConstantRangeFactory::AddPicks(const int32 InNum, TSet<int32>& OutPicks) const
{
	AddPicksFromConfig(Config, InNum, OutPicks);
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
