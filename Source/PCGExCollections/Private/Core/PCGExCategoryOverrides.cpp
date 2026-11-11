// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExCategoryOverrides.h"

#include "PCGExProperty.h"

FString FPCGExCategoryOverrides::DescribeCollisionLoss(const FPCGExCategoryOverrides& InDropped) const
{
	TArray<FName> LostOverrides;
	for (const FPCGExPropertyOverrideEntry& Slot : InDropped.PropertyOverrides.Overrides)
	{
		if (!Slot.bEnabled)
		{
			continue;
		}
		const FName SlotName = Slot.GetPropertyName();
		if (SlotName.IsNone())
		{
			continue;
		}
		// Values only: two rows saying the same thing may still differ by a pick or a mirrored choice list.
		const FInstancedStruct* KeptSlot = PropertyOverrides.GetOverride(SlotName);
		if (!KeptSlot || !PCGExProperties::ValuesIdentical(*KeptSlot, Slot.Value))
		{
			LostOverrides.Add(SlotName);
		}
	}

	TArray<FName> LostTags;
	for (const FName& Tag : InDropped.Tags)
	{
		if (!Tag.IsNone() && !Tags.Contains(Tag))
		{
			LostTags.Add(Tag);
		}
	}

	if (LostOverrides.IsEmpty() && LostTags.IsEmpty())
	{
		return FString();
	}

	auto JoinNames = [](const TArray<FName>& InNames)
	{
		return InNames.IsEmpty() ? FString(TEXT("none")) : FString::JoinBy(InNames, TEXT(", "), [](const FName InName) { return InName.ToString(); });
	};
	return FString::Printf(TEXT("Overrides: %s. Tags: %s."), *JoinNames(LostOverrides), *JoinNames(LostTags));
}
