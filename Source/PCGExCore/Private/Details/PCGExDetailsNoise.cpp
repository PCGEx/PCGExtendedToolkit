// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExDetailsNoise.h"

#include "Core/PCGExContext.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGHelpers.h"

bool FPCGExRandomRatioDetails::GetNumPicks(FPCGExContext* InContext, const UPCGData* InData, const int32 NumMaxItems, int32& OutNumPicks, const bool bQuiet) const
{
	bool bValid = true;

	int32 NumPicks = 0;
	if (Units == EPCGExMeanMeasure::Relative)
	{
		double NumPicksDbl = 0;
		if (!RelativeAmount.TryReadDataValue(InContext, InData, NumPicksDbl, bQuiet))
		{
			bValid = false;
		}
		NumPicks = FMath::Clamp(FMath::RoundToInt(NumMaxItems * NumPicksDbl), 0, NumMaxItems);
	}
	else
	{
		if (!DiscreteAmount.TryReadDataValue(InContext, InData, NumPicks, bQuiet))
		{
			bValid = false;
		}
		NumPicks = FMath::Clamp(NumPicks, 0, NumMaxItems);
	}

	int32 MinPicks = 0;
	int32 MaxPicks = NumMaxItems;

	if (bDoClampMin)
	{
		if (!ClampMin.TryReadDataValue(InContext, InData, MinPicks, bQuiet))
		{
			bValid = false;
		}
		MinPicks = FMath::Clamp(MinPicks, 0, NumMaxItems);
	}

	if (bDoClampMax)
	{
		if (!ClampMax.TryReadDataValue(InContext, InData, MaxPicks, bQuiet))
		{
			bValid = false;
		}
		MaxPicks = FMath::Clamp(MaxPicks, 0, NumMaxItems);
	}

	if (MaxPicks < MinPicks)
	{
		Swap(MinPicks, MaxPicks);
	}
	OutNumPicks = FMath::Clamp(NumPicks, MinPicks, MaxPicks);

	return bValid;
}

bool FPCGExRandomRatioDetails::GetPicks(FPCGExContext* InContext, const UPCGData* InData, const int32 NumMaxItems, TSet<int32>& OutPicks, const bool bQuiet) const
{
	TArray<int32> Picks;
	const bool bValid = GetPicks(InContext, InData, NumMaxItems, Picks, bQuiet);
	OutPicks.Append(Picks);
	return bValid;
}

bool FPCGExRandomRatioDetails::GetPicks(FPCGExContext* InContext, const UPCGData* InData, const int32 NumMaxItems, TArray<int32>& OutPicks, const bool bQuiet) const
{
	int32 NumPicks = 0;
	bool bValid = GetNumPicks(InContext, InData, NumMaxItems, NumPicks, bQuiet);

	PCGExArrayHelpers::ArrayOfIndices(OutPicks, NumMaxItems);

	int32 S = 0;
	if (!BaseSeed.TryReadDataValue(InContext, InData, S, bQuiet))
	{
		bValid = false;
	}
	FRandomStream Random = PCGHelpers::GetRandomStreamFromSeed(PCGHelpers::ComputeSeed(S), InContext->GetInputSettings<UPCGSettings>(), InContext->ExecutionSource.Get());

	for (int32 i = NumMaxItems - 1; i > 0; --i)
	{
		OutPicks.Swap(i, Random.RandRange(0, i));
	}
	OutPicks.SetNum(NumPicks);

	return bValid;
}

#if WITH_EDITOR
void FPCGExRandomRatioDetails::ApplyDeprecation()
{
	BaseSeed.Input = SeedInput_DEPRECATED;
	BaseSeed.Constant = SeedValue_DEPRECATED;
	BaseSeed.Attribute = LocalSeed_DEPRECATED;

	RelativeAmount.Input = AmountInput_DEPRECATED;
	RelativeAmount.Constant = Amount_DEPRECATED;
	RelativeAmount.Attribute = LocalAmount_DEPRECATED;

	DiscreteAmount.Input = AmountInput_DEPRECATED;
	DiscreteAmount.Constant = FixedAmount_DEPRECATED;
	DiscreteAmount.Attribute = LocalAmount_DEPRECATED;
}

void FPCGExRandomRatioDetails::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("LocalSeed")), FName(TEXT("BaseSeed")), FName(TEXT("Attribute")), FName(TEXT("Seed (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("SeedValue")), FName(TEXT("BaseSeed")), FName(TEXT("Constant")), FName(TEXT("Seed")));

	// The old single Amount attribute input split into Relative/Discrete; route its pin by the loaded Units value.
	const FName AmountMember = Units == EPCGExMeanMeasure::Relative ? FName(TEXT("RelativeAmount")) : FName(TEXT("DiscreteAmount"));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("LocalAmount")), AmountMember, FName(TEXT("Attribute")), FName(TEXT("Amount (Attr)")));

	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("Amount")), FName(TEXT("RelativeAmount")), FName(TEXT("Constant")), FName(TEXT("Amount")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("FixedAmount")), FName(TEXT("DiscreteAmount")), FName(TEXT("Constant")), FName(TEXT("Fixed Amount")));
}
#endif
