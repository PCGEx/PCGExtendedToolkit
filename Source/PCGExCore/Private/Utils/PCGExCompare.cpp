// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Utils/PCGExCompare.h"

#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataCommon.h"
#include "Data/PCGExDataTags.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"
#include "Math/PCGExMath.h"

namespace PCGExCompare
{
	FString ToString(const EPCGExComparison Comparison)
	{
		switch (Comparison)
		{
		case EPCGExComparison::StrictlyEqual:
			return " == ";
		case EPCGExComparison::StrictlyNotEqual:
			return " != ";
		case EPCGExComparison::EqualOrGreater:
			return " >= ";
		case EPCGExComparison::EqualOrSmaller:
			return " <= ";
		case EPCGExComparison::StrictlyGreater:
			return " > ";
		case EPCGExComparison::StrictlySmaller:
			return " < ";
		case EPCGExComparison::NearlyEqual:
			return " ~= ";
		case EPCGExComparison::NearlyNotEqual:
			return " !~= ";
		default:
			return " ?? ";
		}
	}

	FString ToString(const EPCGExStringComparison Comparison)
	{
		switch (Comparison)
		{
		case EPCGExStringComparison::StrictlyEqual:
			return " == ";
		case EPCGExStringComparison::StrictlyNotEqual:
			return " != ";
		case EPCGExStringComparison::LengthStrictlyEqual:
			return " L == L ";
		case EPCGExStringComparison::LengthStrictlyUnequal:
			return " L != L ";
		case EPCGExStringComparison::LengthEqualOrGreater:
			return " L >= L ";
		case EPCGExStringComparison::LengthEqualOrSmaller:
			return " L <= L ";
		case EPCGExStringComparison::StrictlyGreater:
			return " L > L ";
		case EPCGExStringComparison::StrictlySmaller:
			return " L < L ";
		case EPCGExStringComparison::LocaleStrictlyGreater:
			return " > ";
		case EPCGExStringComparison::LocaleStrictlySmaller:
			return " < ";
		case EPCGExStringComparison::Contains:
			return " contains ";
		case EPCGExStringComparison::StartsWith:
			return " starts with ";
		case EPCGExStringComparison::EndsWith:
			return " ends with ";
		case EPCGExStringComparison::Matches:
			return " matches ";
		default:
			return " ?? ";
		}
	}

	FString ToString(const EPCGExStringMatchMode MatchMode)
	{
		switch (MatchMode)
		{
		case EPCGExStringMatchMode::Equals:
			return " == ";
		case EPCGExStringMatchMode::Contains:
			return " contains ";
		case EPCGExStringMatchMode::StartsWith:
			return " starts w ";
		case EPCGExStringMatchMode::EndsWith:
			return " ends w ";
		case EPCGExStringMatchMode::Matches:
			return " matches w ";
		default:
			return " ?? ";
		}
	}

	bool Compare(const EPCGExStringComparison Method, const FString& A, const FString& B)
	{
		switch (Method)
		{
		case EPCGExStringComparison::StrictlyEqual:
			return A == B;
		case EPCGExStringComparison::StrictlyNotEqual:
			return A != B;
		case EPCGExStringComparison::LengthStrictlyEqual:
			return A.Len() == B.Len();
		case EPCGExStringComparison::LengthStrictlyUnequal:
			return A.Len() != B.Len();
		case EPCGExStringComparison::LengthEqualOrGreater:
			return A.Len() >= B.Len();
		case EPCGExStringComparison::LengthEqualOrSmaller:
			return A.Len() <= B.Len();
		case EPCGExStringComparison::StrictlyGreater:
			return A.Len() > B.Len();
		case EPCGExStringComparison::StrictlySmaller:
			return A.Len() < B.Len();
		case EPCGExStringComparison::LocaleStrictlyGreater:
			return A > B;
		case EPCGExStringComparison::LocaleStrictlySmaller:
			return A < B;
		case EPCGExStringComparison::Contains:
			return A.Contains(B);
		case EPCGExStringComparison::StartsWith:
			return A.StartsWith(B);
		case EPCGExStringComparison::EndsWith:
			return A.EndsWith(B);
		case EPCGExStringComparison::Matches:
			return A.MatchesWildcard(B);
		default:
			return false;
		}
	}

	bool Compare(const EPCGExStringComparison Method, const FName& A, const FName& B)
	{
		switch (Method)
		{
		case EPCGExStringComparison::StrictlyEqual:
			return A == B;
		case EPCGExStringComparison::StrictlyNotEqual:
			return A != B;
		default:
			// Non-equality comparisons need string content -- defer to the FString overload.
			return Compare(Method, A.ToString(), B.ToString());
		}
	}

	bool Compare(const EPCGExStringMatchMode Method, const FString& A, const FString& B)
	{
		switch (Method)
		{
		case EPCGExStringMatchMode::Equals:
			return A == B;
		case EPCGExStringMatchMode::Contains:
			return A.Contains(B);
		case EPCGExStringMatchMode::StartsWith:
			return A.StartsWith(B);
		case EPCGExStringMatchMode::EndsWith:
			return A.EndsWith(B);
		case EPCGExStringMatchMode::Matches:
			return A.MatchesWildcard(B);
		default:
			return false;
		}
	}

	bool Compare(const EPCGExComparison Method, const TSharedPtr<PCGExData::IDataValue>& A, const double B, const double Tolerance)
	{
		if (!A->IsNumeric())
		{
			return false;
		}
		return Compare(Method, A->AsDouble(), B, Tolerance);
	}

	bool Compare(const EPCGExStringComparison Method, const TSharedPtr<PCGExData::IDataValue>& A, const FString B)
	{
		if (!A->IsText())
		{
			return false;
		}
		return Compare(Method, A->AsString(), B);
	}

	bool HasMatchingTags(const TSharedPtr<PCGExData::FTags>& InTags, const FString& Query, const EPCGExStringMatchMode MatchMode, const bool bStrict)
	{
		if (bStrict)
		{
			for (const TPair<FString, TSharedPtr<PCGExData::IDataValue>>& Pair : InTags->ValueTags)
			{
				if (Compare(MatchMode, Pair.Key, Query))
				{
					return true;
				}
			}

			for (const FString& Tag : InTags->RawTags)
			{
				if (Compare(MatchMode, Tag, Query))
				{
					return true;
				}
			}
		}
		else
		{
			TArray<FString> FlattenedTags = InTags->FlattenToArray();
			for (const FString& Tag : FlattenedTags)
			{
				if (Compare(MatchMode, Tag, Query))
				{
					return true;
				}
			}
		}

		return false;
	}

	bool GetMatchingValueTags(const TSharedPtr<PCGExData::FTags>& InTags, const FString& Query, const EPCGExStringMatchMode MatchMode, TArray<TSharedPtr<PCGExData::IDataValue>>& OutValues)
	{
		for (const TPair<FString, TSharedPtr<PCGExData::IDataValue>>& Pair : InTags->ValueTags)
		{
			if (Compare(MatchMode, Pair.Key, Query))
			{
				OutValues.Add(Pair.Value);
			}
		}

		return !OutValues.IsEmpty();
	}
}

PCGEX_SETTING_VALUE_IMPL(FPCGExVectorHashComparisonDetails, Tolerance, double, HashToleranceInput, HashToleranceAttribute, HashToleranceConstant);

bool FPCGExVectorHashComparisonDetails::Init(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InPrimaryDataFacade, const bool bQuiet)
{
	Tolerance = GetValueSettingTolerance(bQuiet);
	if (!Tolerance->Init(InPrimaryDataFacade, false))
	{
		return false;
	}

	return true;
}

FVector FPCGExVectorHashComparisonDetails::GetCWTolerance(const int32 PointIndex) const
{
	return FVector(PCGEx::SafeScalarTolerance(Tolerance->Read(PointIndex)));
}

void FPCGExVectorHashComparisonDetails::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_CONDITIONAL(HashToleranceInput == EPCGExInputValueType::Attribute, HashToleranceAttribute, Consumable)
}

bool FPCGExVectorHashComparisonDetails::GetOnlyUseDataDomain() const
{
	return HashToleranceInput == EPCGExInputValueType::Constant || PCGExMetaHelpers::IsDataDomainAttribute(HashToleranceAttribute);
}

bool FPCGExVectorHashComparisonDetails::Test(const FVector& A, const FVector& B, const int32 PointIndex) const
{
	const FVector CWTolerance = GetCWTolerance(PointIndex);
	return PCGEx::SH3(A, CWTolerance) == PCGEx::SH3(B, CWTolerance);
}

void FPCGExStaticDotComparisonDetails::Init()
{
	// Remap threshold and tolerance from their native domain into the comparison space.
	// Signed: remap [-1,1] → [0,1] via (1+x)*0.5
	// Unsigned: use absolute value (already in [0,1])
	// Degrees D maps to the dot of that angle, cos(D), so Degrees and Scalar denote the same angle.
	// Tolerances are WIDTHS: a raw-dot width halves under the signed remap and stays as-is unsigned;
	// an angular width T maps to the raw dot span 1-cos(T) (zero at T=0).
	if (bUnsignedComparison)
	{
		if (Domain == EPCGExAngularDomain::Degrees)
		{
			ComparisonThreshold = FMath::Abs(PCGExMath::DegreesToDot(DegreesConstant));
			ComparisonTolerance = 1 - PCGExMath::DegreesToDot(DegreesTolerance);
		}
		else
		{
			ComparisonThreshold = FMath::Abs(DotConstant);
			ComparisonTolerance = FMath::Abs(DotTolerance);
		}
	}
	else
	{
		if (Domain == EPCGExAngularDomain::Degrees)
		{
			ComparisonThreshold = (1 + PCGExMath::DegreesToDot(DegreesConstant)) * 0.5;
			ComparisonTolerance = (1 - PCGExMath::DegreesToDot(DegreesTolerance)) * 0.5;
		}
		else
		{
			ComparisonThreshold = (1 + DotConstant) * 0.5;
			ComparisonTolerance = DotTolerance * 0.5;
		}
	}
}

bool FPCGExStaticDotComparisonDetails::Test(const double A) const
{
	return PCGExCompare::Compare(Comparison, bUnsignedComparison ? FMath::Abs(A) : (1 + A) * 0.5, ComparisonThreshold, ComparisonTolerance);
}

PCGEX_SETTING_VALUE_IMPL(FPCGExDotComparisonDetails, Threshold, double, ThresholdInput, ThresholdAttribute, Domain == EPCGExAngularDomain::Degrees ? DegreesConstant : DotConstant)

bool FPCGExDotComparisonDetails::Init(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InPrimaryDataCache, const bool bQuiet)
{
	ThresholdGetter = GetValueSettingThreshold(bQuiet);
	if (!ThresholdGetter->Init(InPrimaryDataCache, false))
	{
		return false;
	}

	// Tolerances are WIDTHS in the comparison space: signed compares remap through (1+x)*0.5 (span halved),
	// unsigned compares raw magnitudes. An angular width T maps to the raw dot span 1-cos(T) (zero at T=0).
	if (Domain == EPCGExAngularDomain::Degrees)
	{
		const double Width = 1 - PCGExMath::DegreesToDot(DegreesTolerance);
		ComparisonTolerance = bUnsignedComparison ? Width : Width * 0.5;
	}
	else
	{
		ComparisonTolerance = bUnsignedComparison ? FMath::Abs(DotTolerance) : DotTolerance * 0.5;
	}

	return true;
}

double FPCGExDotComparisonDetails::GetComparisonThreshold(const int32 PointIndex) const
{
	if (Domain == EPCGExAngularDomain::Scalar)
	{
		return ThresholdGetter->Read(PointIndex);
	}
	// An angle of D degrees corresponds to a dot of cos(D), keeping Degrees and Scalar consistent.
	return PCGExMath::DegreesToDot(ThresholdGetter->Read(PointIndex));
}

bool FPCGExDotComparisonDetails::Test(const double A, const double B) const
{
	return bUnsignedComparison ? PCGExCompare::Compare(Comparison, FMath::Abs(A), FMath::Abs(B), ComparisonTolerance) : PCGExCompare::Compare(Comparison, (1 + A) * 0.5, (1 + B) * 0.5, ComparisonTolerance);
}

bool FPCGExDotComparisonDetails::Test(const double A, const int32 Index) const
{
	return Test(A, GetComparisonThreshold(Index));
}

void FPCGExDotComparisonDetails::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	if (ThresholdInput == EPCGExInputValueType::Attribute)
	{
		FacadePreloader.Register<double>(InContext, ThresholdAttribute);
	}
}

void FPCGExDotComparisonDetails::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_CONDITIONAL(ThresholdInput == EPCGExInputValueType::Attribute, ThresholdAttribute, Consumable)
}

bool FPCGExDotComparisonDetails::GetOnlyUseDataDomain() const
{
	return ThresholdInput == EPCGExInputValueType::Constant || PCGExMetaHelpers::IsDataDomainAttribute(ThresholdAttribute);
}

#if WITH_EDITOR
FString FPCGExDotComparisonDetails::GetDisplayComparison() const
{
	FString AngleStr = ThresholdInput == EPCGExInputValueType::Attribute ? PCGExMetaHelpers::GetSelectorDisplayName(ThresholdAttribute) : TEXT("");
	if (ThresholdInput == EPCGExInputValueType::Constant)
	{
		if (Domain == EPCGExAngularDomain::Degrees)
		{
			AngleStr = FString::Printf(TEXT("%.1f°"), DegreesConstant);
		}
		else
		{
			AngleStr = FString::Printf(TEXT("%.1f°"), FMath::RadiansToDegrees(FMath::Acos(DotConstant)));
		}
	}

	FString Str = PCGExCompare::ToString(Comparison) + (bUnsignedComparison ? TEXT("±") : TEXT("")) + AngleStr;
	return Str;
}

#endif
