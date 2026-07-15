// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExDetailsNoise.h"

#include "Core/PCGExContext.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGHelpers.h"

int32 FPCGExRandomRatioDetails::GetNumPicks(FPCGExContext* InContext, const UPCGData* InData, const int32 NumMaxItems) const
{
	int32 NumPicks = 0;
	if (Units == EPCGExMeanMeasure::Relative)
	{
		double NumPicksDbl = 0;
		RelativeAmount.TryReadDataValue(InContext, InData, NumPicksDbl);
		NumPicks = FMath::Clamp(FMath::RoundToInt(NumMaxItems * NumPicksDbl), 0, NumMaxItems);
	}
	else
	{
		DiscreteAmount.TryReadDataValue(InContext, InData, NumPicks);
		NumPicks = FMath::Clamp(NumPicks, 0, NumMaxItems);
	}

	int32 MinPicks = 0;
	int32 MaxPicks = NumMaxItems;

	if (bDoClampMin)
	{
		ClampMin.TryReadDataValue(InContext, InData, MinPicks);
		MinPicks = FMath::Clamp(MinPicks, 0, NumMaxItems);
	}

	if (bDoClampMax)
	{
		ClampMax.TryReadDataValue(InContext, InData, MaxPicks);
		MaxPicks = FMath::Clamp(MaxPicks, 0, NumMaxItems);
	}

	if (MaxPicks < MinPicks)
	{
		Swap(MinPicks, MaxPicks);
	}
	NumPicks = FMath::Clamp(NumPicks, MinPicks, MaxPicks);

	return NumPicks;
}

void FPCGExRandomRatioDetails::GetPicks(FPCGExContext* InContext, const UPCGData* InData, const int32 NumMaxItems, TSet<int32>& OutPicks) const
{
	TArray<int32> Picks;
	GetPicks(InContext, InData, NumMaxItems, Picks);
	OutPicks.Append(Picks);
}

void FPCGExRandomRatioDetails::GetPicks(FPCGExContext* InContext, const UPCGData* InData, const int32 NumMaxItems, TArray<int32>& OutPicks) const
{
	const int32 NumPicks = GetNumPicks(InContext, InData, NumMaxItems);

	PCGExArrayHelpers::ArrayOfIndices(OutPicks, NumMaxItems);

	int32 S = 0;
	BaseSeed.TryReadDataValue(InContext, InData, S);
	FRandomStream Random = PCGHelpers::GetRandomStreamFromSeed(PCGHelpers::ComputeSeed(S), InContext->GetInputSettings<UPCGSettings>(), InContext->ExecutionSource.Get());

	for (int32 i = NumMaxItems - 1; i > 0; --i)
	{
		OutPicks.Swap(i, Random.RandRange(0, i));
	}
	OutPicks.SetNum(NumPicks);
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
