// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExSegmentLengthFilter.h"

#include "PCGExVersion.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExSegmentLengthFilterDefinition"
#define PCGEX_NAMESPACE PCGExSegmentLengthFilterDefinition

#if WITH_EDITOR
void FPCGExSegmentLengthFilterConfig::ApplyDeprecation()
{
	Threshold.Update(ThresholdInput_DEPRECATED, ThresholdAttribute_DEPRECATED, ThresholdConstant_DEPRECATED);
}

void FPCGExSegmentLengthFilterConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("ThresholdConstant")), FName(TEXT("Threshold")), FName(TEXT("Constant")), FName(TEXT("Threshold")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("ThresholdAttribute")), FName(TEXT("Threshold")), FName(TEXT("Attribute")), FName(TEXT("Threshold (Attr)")));
}

void FPCGExSegmentLengthFilterConfig::ApplyIndexDeprecation()
{
	Index.Update(CompareAgainst_DEPRECATED, IndexAttribute_DEPRECATED, IndexConstant_DEPRECATED);
}

void FPCGExSegmentLengthFilterConfig::RenameIndexPins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("IndexAttribute")), FName(TEXT("Index")), FName(TEXT("Attribute")), FName(TEXT("Index (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("IndexConstant")), FName(TEXT("Index")), FName(TEXT("Constant")), FName(TEXT("Index")));
}
#endif

bool UPCGExSegmentLengthFilterFactory::Init(FPCGExContext* InContext)
{
	if (!Super::Init(InContext))
	{
		return false;
	}
	Config.Sanitize();
	return true;
}

bool UPCGExSegmentLengthFilterFactory::DomainCheck()
{
	return false;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExSegmentLengthFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FSegmentLengthFilter>(this);
}

void UPCGExSegmentLengthFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	if (Config.Threshold.Input == EPCGExInputValueType::Attribute)
	{
		FacadePreloader.Register<double>(InContext, Config.Threshold.Attribute);
	}
	Config.Index.RegisterBufferDependencies(InContext, FacadePreloader);
}

bool PCGExPointFilter::FSegmentLengthFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	bClosedLoop = PCGExPaths::Helpers::GetClosedLoop(InPointDataFacade->GetIn());
	LastIndex = InPointDataFacade->GetNum() - 1;
	InTransforms = InPointDataFacade->GetIn()->GetConstTransformValueRange();
	bOffset = TypedFilterFactory->Config.IndexMode == EPCGExIndexMode::Offset;

	if (TypedFilterFactory->Config.bForceTileIfClosedLoop && bClosedLoop)
	{
		IndexSafety = EPCGExIndexSafety::Tile;
	}
	else
	{
		IndexSafety = TypedFilterFactory->Config.IndexSafety;
	}

	Threshold = TypedFilterFactory->Config.Threshold.GetValueSetting(PCGEX_QUIET_HANDLING);
	Threshold->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
	if (!Threshold->Init(PointDataFacade))
	{
		return false;
	}

	Index = TypedFilterFactory->Config.Index.GetValueSetting(PCGEX_QUIET_HANDLING);
	Index->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
	if (!Index->Init(PointDataFacade))
	{
		return false;
	}

	return true;
}

bool PCGExPointFilter::FSegmentLengthFilter::Test(const int32 PointIndex) const
{
	const int32 IndexValue = Index->Read(PointIndex);
	const int32 TargetIndex = PCGExMath::SanitizeIndex(bOffset ? PointIndex + IndexValue : IndexValue, LastIndex, IndexSafety);

	bool bResult = true;
	if (TargetIndex == -1)
	{
		bResult = TypedFilterFactory->Config.InvalidPointFallback != EPCGExFilterFallback::Fail;
	}
	else
	{
		bResult = PCGExCompare::Compare(TypedFilterFactory->Config.Comparison, TypedFilterFactory->Config.bCompareAgainstSquaredDistance ? FVector::DistSquared(InTransforms[TargetIndex].GetLocation(), InTransforms[PointIndex].GetLocation()) : FVector::Dist(InTransforms[TargetIndex].GetLocation(), InTransforms[PointIndex].GetLocation()), Threshold->Read(PointIndex), TypedFilterFactory->Config.Tolerance);
	}

	return TypedFilterFactory->Config.bInvert ? !bResult : bResult;
}

PCGEX_CREATE_FILTER_FACTORY(SegmentLength)

#if WITH_EDITOR
void UPCGExSegmentLengthFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.RenameIndexPins(this, InOutNode);
		RetireInputPin(InOutNode, FName(TEXT("CompareAgainst")));
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExSegmentLengthFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.ApplyIndexDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExSegmentLengthFilterProviderSettings::GetDisplayName() const
{
	FString TargetStr = Config.Index.Input == EPCGExInputValueType::Attribute ? PCGExMetaHelpers::GetSelectorDisplayName(Config.Index.Attribute) : FString::Printf(TEXT("%d"), Config.Index.Constant);
	FString OtherStr = Config.Threshold.Input == EPCGExInputValueType::Attribute ? PCGExMetaHelpers::GetSelectorDisplayName(Config.Threshold.Attribute) : FString::Printf(TEXT("%.1f"), Config.Threshold.Constant);
	FString Str = TEXT("Dist to ") + TargetStr + PCGExCompare::ToString(Config.Comparison) + OtherStr;
	return PCGExCommon::FlagInvertLabel(Str, Config.bInvert);
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
