// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Pickers/PCGExPickerConstant.h"

#include "Containers/PCGExManagedObjects.h"
#include "Math/PCGExMath.h"

#define LOCTEXT_NAMESPACE "PCGExCreatePickerConstant"
#define PCGEX_NAMESPACE CreatePickerConstant

PCGEX_PICKER_BOILERPLATE(Constant, {}, {})

#if WITH_EDITOR
FString UPCGExPickerConstantSettings::GetDisplayName() const
{
	FString DisplayName = TEXT("Pick @");

	if (Config.bTreatAsNormalized)
	{
		DisplayName += FString::Printf(TEXT("%.2f"), Config.RelativeIndex);
	}
	else
	{
		DisplayName += FString::Printf(TEXT("%d"), Config.DiscreteIndex);
	}

	return DisplayName;
}
#endif

void UPCGExPickerConstantFactory::AddPicks(const int32 InNum, TSet<int32>& OutPicks) const
{
	if (InNum <= 0)
	{
		return;
	}

	int32 TargetIndex = Config.bTreatAsNormalized ? PCGExPickers::ResolveRelative(Config.RelativeIndex, InNum, Config.TruncateMode) : PCGExPickers::ResolveDiscrete(Config.DiscreteIndex, InNum);
	TargetIndex = PCGExMath::SanitizeIndex(TargetIndex, InNum - 1, Config.Safety);

	if (!FMath::IsWithin(TargetIndex, 0, InNum))
	{
		return;
	}

	OutPicks.Add(TargetIndex);
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
