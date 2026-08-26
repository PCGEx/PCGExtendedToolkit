// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Collections/PCGExDataBoundsFilter.h"

#include "Data/PCGExPointIO.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

TSharedPtr<PCGExPointFilter::IFilter> UPCGExDataBoundsFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FDataBoundsFilter>(this);
}

bool PCGExPointFilter::FDataBoundsFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	double A = 0;
	FVector AV = FVector::ZeroVector;
	const FBox Bounds = IO->GetIn()->GetBounds();
	double RatioNum = 0;
	double RatioDen = 0;

	switch (TypedFilterFactory->Config.OperandA)
	{
	case EPCGExDataBoundsAspect::Extents:
		AV = Bounds.GetExtent();
		break;
	case EPCGExDataBoundsAspect::Min:
		AV = Bounds.Min;
		break;
	case EPCGExDataBoundsAspect::Max:
		AV = Bounds.Max;
		break;
	case EPCGExDataBoundsAspect::Size:
		AV = Bounds.GetSize();
		break;
	case EPCGExDataBoundsAspect::Volume:
		AV = Bounds.GetSize();
		A = AV.X * AV.Y * AV.Z;
		break;
	case EPCGExDataBoundsAspect::AspectRatio:
		AV = Bounds.GetSize();
		// The enum reads Numerator|Denominator: XY is X/Y.
		switch (TypedFilterFactory->Config.Ratio)
		{
		case EPCGExDataBoundsRatio::XY:
			RatioNum = AV.X;
			RatioDen = AV.Y;
			break;
		case EPCGExDataBoundsRatio::XZ:
			RatioNum = AV.X;
			RatioDen = AV.Z;
			break;
		case EPCGExDataBoundsRatio::YZ:
			RatioNum = AV.Y;
			RatioDen = AV.Z;
			break;
		case EPCGExDataBoundsRatio::YX:
			RatioNum = AV.Y;
			RatioDen = AV.X;
			break;
		case EPCGExDataBoundsRatio::ZX:
			RatioNum = AV.Z;
			RatioDen = AV.X;
			break;
		case EPCGExDataBoundsRatio::ZY:
			RatioNum = AV.Z;
			RatioDen = AV.Y;
			break;
		}
		break;
	case EPCGExDataBoundsAspect::SortedRatio:
		AV = Bounds.GetSize();
		RatioNum = FMath::Max3(AV.X, AV.Y, AV.Z);
		RatioDen = FMath::Min3(AV.X, AV.Y, AV.Z);
		break;
	}

	if (TypedFilterFactory->Config.OperandA == EPCGExDataBoundsAspect::AspectRatio || TypedFilterFactory->Config.OperandA == EPCGExDataBoundsAspect::SortedRatio)
	{
		A = RatioNum / RatioDen;
	}
	else
	{
		switch (TypedFilterFactory->Config.SubOperand)
		{
		case EPCGExDataBoundsComponent::Length:
			A = AV.Length();
			break;
		case EPCGExDataBoundsComponent::LengthSquared:
			A = AV.SquaredLength();
			break;
		case EPCGExDataBoundsComponent::X:
			A = AV.X;
			break;
		case EPCGExDataBoundsComponent::Y:
			A = AV.Y;
			break;
		case EPCGExDataBoundsComponent::Z:
			A = AV.Z;
			break;
		}
	}

	double B = 0;
	if (!TypedFilterFactory->Config.OperandB.TryReadDataValue(IO, B, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}
	const bool bResult = TypedFilterFactory->Config.OperandB.Compare(A, B);

	return TypedFilterFactory->Config.bInvert ? !bResult : bResult;
}

PCGEX_CREATE_FILTER_FACTORY(DataBounds)

#if WITH_EDITOR
FString UPCGExDataBoundsFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = TEXT("Bound's ");

	switch (Config.OperandA)
	{
	case EPCGExDataBoundsAspect::Extents:
		DisplayName += TEXT("Extents");
		break;
	case EPCGExDataBoundsAspect::Min:
		DisplayName += TEXT("Min");
		break;
	case EPCGExDataBoundsAspect::Max:
		DisplayName += TEXT("Max");
		break;
	case EPCGExDataBoundsAspect::Size:
		DisplayName += TEXT("Size");
		break;
	case EPCGExDataBoundsAspect::Volume:
		DisplayName += TEXT("Volume");
		break;
	case EPCGExDataBoundsAspect::AspectRatio:
		DisplayName += TEXT("Ratio");
		break;
	case EPCGExDataBoundsAspect::SortedRatio:
		DisplayName += TEXT("Ratio");
		break;
	}

	if (static_cast<uint8>(Config.OperandA) < 4)
	{
		switch (Config.SubOperand)
		{
		case EPCGExDataBoundsComponent::Length:
			DisplayName += TEXT(".Len");
			break;
		case EPCGExDataBoundsComponent::LengthSquared:
			DisplayName += TEXT(".LenSq");
			break;
		case EPCGExDataBoundsComponent::X:
			DisplayName += TEXT(".X");
			break;
		case EPCGExDataBoundsComponent::Y:
			DisplayName += TEXT(".Y");
			break;
		case EPCGExDataBoundsComponent::Z:
			DisplayName += TEXT(".Z");
			break;
		}
	}

	DisplayName += Config.OperandB.GetDisplayNamePostfix();
	return PCGExCommon::FlagInvertLabel(DisplayName, Config.bInvert);
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
