// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExHeuristicsFactoryProvider.h"

#include "PCGExCommon.h"
#include "Helpers/PCGExMetaHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExCreateHeuristics"
#define PCGEX_NAMESPACE CreateHeuristics

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoHeuristics, UPCGExHeuristicsFactoryData)

void FPCGExHeuristicConfigBase::Init()
{
	// PCG_Overridable bypasses ClampMin; 0 divides in the GeometricMean/HarmonicMean/Min/Max score modes.
	WeightFactor = FMath::Max(WeightFactor, 0.001);
	ScoreLUT = ScoreCurveLookup.MakeLookup(bUseLocalCurve, LocalScoreCurve, ScoreCurve);
}

bool UPCGExHeuristicsFactoryData::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_CONDITIONAL(ConfigBase.bUseLocalWeightMultiplier, ConfigBase.WeightMultiplierAttribute, Consumable)

	return true;
}

TSharedPtr<FPCGExHeuristicOperation> UPCGExHeuristicsFactoryData::CreateOperation(FPCGExContext* InContext) const
{
	return nullptr; // Create heuristic operation
}

UPCGExFactoryData* UPCGExHeuristicsFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	return Super::CreateFactory(InContext, InFactory);
}

#if WITH_EDITOR
FString UPCGExHeuristicsFactoryProviderSettings::GetDisplayName() const
{
	const FPCGExHeuristicConfigBase* BaseConfig = GetBaseConfig();
	if (!BaseConfig)
	{
		return Super::GetDisplayName();
	}

	return PCGExCommon::FlagInvertLabel(
		GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Heuristics"), TEXT("HX")) + TEXT(" @ ") + FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * BaseConfig->WeightFactor) / 1000.0)),
		BaseConfig->bInvert);
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
