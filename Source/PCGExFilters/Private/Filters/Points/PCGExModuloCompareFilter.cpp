// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExModuloCompareFilter.h"


#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"


#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

PCGEX_SETTING_VALUE_IMPL(FPCGExModuloCompareFilterConfig, OperandB, double, OperandBSource, OperandB, OperandBConstant)
PCGEX_SETTING_VALUE_IMPL(FPCGExModuloCompareFilterConfig, OperandC, double, CompareAgainst, OperandC, OperandCConstant)

bool UPCGExModuloCompareFilterFactory::DomainCheck()
{
	return PCGExMetaHelpers::IsDataDomainAttribute(Config.OperandA) && (Config.OperandBSource == EPCGExInputValueType::Constant || PCGExMetaHelpers::IsDataDomainAttribute(Config.OperandB)) && (Config.CompareAgainst == EPCGExInputValueType::Constant || PCGExMetaHelpers::IsDataDomainAttribute(Config.OperandC));
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExModuloCompareFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FModuloComparisonFilter>(this);
}

void UPCGExModuloCompareFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	FacadePreloader.Register<double>(InContext, Config.OperandA);
	if (Config.OperandBSource == EPCGExInputValueType::Attribute)
	{
		FacadePreloader.Register<double>(InContext, Config.OperandB);
	}
	if (Config.CompareAgainst == EPCGExInputValueType::Attribute)
	{
		FacadePreloader.Register<double>(InContext, Config.OperandC);
	}
}

bool UPCGExModuloCompareFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_SELECTOR(Config.OperandA, Consumable)
	PCGEX_CONSUMABLE_CONDITIONAL(Config.OperandBSource == EPCGExInputValueType::Attribute, Config.OperandB, Consumable)
	PCGEX_CONSUMABLE_CONDITIONAL(Config.CompareAgainst == EPCGExInputValueType::Attribute, Config.OperandC, Consumable)

	return true;
}

bool PCGExPointFilter::FModuloComparisonFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	OperandA = PointDataFacade->GetBroadcaster<double>(TypedFilterFactory->Config.OperandA, true, false, PCGEX_QUIET_HANDLING);

	if (!OperandA)
	{
		PCGEX_LOG_INVALID_SELECTOR_HANDLED_C(InContext, Operand A, TypedFilterFactory->Config.OperandA)
		return false;
	}

	OperandB = TypedFilterFactory->Config.GetValueSettingOperandB(PCGEX_QUIET_HANDLING);
	if (!OperandB->Init(PointDataFacade))
	{
		return false;
	}

	OperandC = TypedFilterFactory->Config.GetValueSettingOperandC(PCGEX_QUIET_HANDLING);
	if (!OperandC->Init(PointDataFacade))
	{
		return false;
	}

	return true;
}

namespace PCGExModuloCompareFilter
{
	// For ~= / ~!= with cyclic remainder on, the modulo result is a residue on a ring
	// of circumference |B|: a remainder near 0 OR near |B| both count as "near a multiple",
	// which catches values landing just below a multiple (179.996 % 90 -> 89.996 ~= 0 cyclically).
	bool Evaluate(const FPCGExModuloCompareFilterConfig& Config, const double A, const double B, const double C)
	{
		const double OffsetB = B + Config.OperandBOffset;
		if (A == 0 || OffsetB == 0) { return Config.ZeroResult; }

		const double M = FMath::Fmod(A, OffsetB);

		if (Config.bCyclicRemainder &&
			(Config.Comparison == EPCGExComparison::NearlyEqual || Config.Comparison == EPCGExComparison::NearlyNotEqual))
		{
			const double AbsB = FMath::Abs(OffsetB);
			const double Rm = M < 0 ? M + AbsB : M;
			double Rc = FMath::Fmod(C, AbsB);
			if (Rc < 0) { Rc += AbsB; }
			double D = FMath::Abs(Rm - Rc);
			D = FMath::Min(D, AbsB - D);
			const bool bIsNear = D <= Config.Tolerance;
			return Config.Comparison == EPCGExComparison::NearlyEqual ? bIsNear : !bIsNear;
		}

		return PCGExCompare::Compare(Config.Comparison, M, C, Config.Tolerance);
	}
}

bool PCGExPointFilter::FModuloComparisonFilter::Test(const int32 PointIndex) const
{
	return PCGExModuloCompareFilter::Evaluate(
		TypedFilterFactory->Config,
		OperandA->Read(PointIndex),
		OperandB->Read(PointIndex),
		OperandC->Read(PointIndex));
}

bool PCGExPointFilter::FModuloComparisonFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	double A = 0;
	double B = 0;
	double C = 0;

	if (!PCGExData::Helpers::TryReadDataValue(IO, TypedFilterFactory->Config.OperandA, A, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}
	if (!PCGExData::Helpers::TryGetSettingDataValue(IO, TypedFilterFactory->Config.OperandBSource, TypedFilterFactory->Config.OperandB, TypedFilterFactory->Config.OperandBConstant, B, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}
	if (!PCGExData::Helpers::TryGetSettingDataValue(IO, TypedFilterFactory->Config.CompareAgainst, TypedFilterFactory->Config.OperandC, TypedFilterFactory->Config.OperandCConstant, C, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}

	return PCGExModuloCompareFilter::Evaluate(TypedFilterFactory->Config, A, B, C);
}

PCGEX_CREATE_FILTER_FACTORY(ModuloCompare)

#if WITH_EDITOR
FString UPCGExModuloCompareFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandA) + " % ";

	if (Config.OperandBSource == EPCGExInputValueType::Attribute)
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandB);
	}
	else
	{
		DisplayName += FString::Printf(TEXT("%.3f "), (static_cast<int32>(1000 * Config.OperandBConstant) / 1000.0));
	}

	DisplayName += PCGExCompare::ToString(Config.Comparison);

	if (Config.CompareAgainst == EPCGExInputValueType::Attribute)
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandC);
	}
	else
	{
		DisplayName += FString::Printf(TEXT(" %.3f"), (static_cast<int32>(1000 * Config.OperandCConstant) / 1000.0));
	}

	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
