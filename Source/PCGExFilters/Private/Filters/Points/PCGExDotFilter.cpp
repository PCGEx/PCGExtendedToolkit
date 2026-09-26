// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExDotFilter.h"

#include "PCGExVersion.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"

#define LOCTEXT_NAMESPACE "PCGExDotFilterDefinition"
#define PCGEX_NAMESPACE PCGExDotFilterDefinition

#if WITH_EDITOR
void FPCGExDotFilterConfig::ApplyDeprecation()
{
	OperandBValue.Update(CompareAgainst_DEPRECATED, OperandB_DEPRECATED, OperandBConstant_DEPRECATED);
	// The legacy invert only applies to attribute input.
	OperandBValue.bFlip = bInvertOperandB_DEPRECATED && CompareAgainst_DEPRECATED == EPCGExInputValueType::Attribute;
}

void FPCGExDotFilterConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("OperandB")), FName(TEXT("OperandBValue")), FName(TEXT("Attribute")), FName(TEXT("Operand B (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("OperandBConstant")), FName(TEXT("OperandBValue")), FName(TEXT("Constant")), FName(TEXT("Operand B")));
	// No display fallback: bInvertOperandA displays as " └─ Invert" too, so that label is ambiguous.
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("bInvertOperandB")), FName(TEXT("OperandBValue")), FName(TEXT("bFlip")));
}
#endif

bool UPCGExDotFilterFactory::Init(FPCGExContext* InContext)
{
	if (!Super::Init(InContext))
	{
		return false;
	}
	Config.Sanitize();
	return true;
}

bool UPCGExDotFilterFactory::DomainCheck()
{
	return PCGExMetaHelpers::IsDataDomainAttribute(Config.OperandA) && Config.OperandBValue.CanSupportDataOnly() && Config.DotComparisonDetails.GetOnlyUseDataDomain() && !Config.bTransformOperandA && !Config.bTransformOperandB;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExDotFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FDotFilter>(this);
}

void UPCGExDotFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	FacadePreloader.Register<FVector>(InContext, Config.OperandA);
	Config.OperandBValue.RegisterBufferDependencies(InContext, FacadePreloader);
	Config.DotComparisonDetails.RegisterBuffersDependencies(InContext, FacadePreloader);
}

bool UPCGExDotFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_SELECTOR(Config.OperandA, Consumable)
	Config.DotComparisonDetails.RegisterConsumableAttributesWithData(InContext, InData);

	return true;
}

bool PCGExPointFilter::FDotFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	DotComparison = TypedFilterFactory->Config.DotComparisonDetails;
	if (!DotComparison.Init(InContext, InPointDataFacade.ToSharedRef()))
	{
		return false;
	}

	OperandA = PointDataFacade->GetBroadcaster<FVector>(TypedFilterFactory->Config.OperandA, true, false, PCGEX_QUIET_HANDLING);
	OperandAMultiplier = TypedFilterFactory->Config.bInvertOperandA ? -1 : 1;
	if (!OperandA)
	{
		PCGEX_LOG_INVALID_SELECTOR_HANDLED_C(InContext, Operand A, TypedFilterFactory->Config.OperandA)
		return false;
	}

	OperandB = TypedFilterFactory->Config.OperandBValue.GetValueSetting(PCGEX_QUIET_HANDLING);
	OperandB->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
	if (!OperandB->Init(PointDataFacade))
	{
		return false;
	}

	InTransforms = InPointDataFacade->GetIn()->GetConstTransformValueRange();

	return true;
}

// Optionally rotate each operand from local to world space using the point's transform,
// then compute their dot product. Operand B's getter already applied its flip.
bool PCGExPointFilter::FDotFilter::Test(const int32 PointIndex) const
{
	const FVector A = (OperandA->Read(PointIndex) * OperandAMultiplier).GetSafeNormal();
	const FVector B = OperandB->Read(PointIndex).GetSafeNormal();
	return DotComparison.Test(FVector::DotProduct(TypedFilterFactory->Config.bTransformOperandA ? InTransforms[PointIndex].TransformVectorNoScale(A) : A, TypedFilterFactory->Config.bTransformOperandB ? InTransforms[PointIndex].TransformVectorNoScale(B) : B), PointIndex);
}

bool PCGExPointFilter::FDotFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	PCGEX_SHARED_CONTEXT(IO->GetContextHandle())

	FVector A = FVector::ZeroVector;
	FVector B = FVector::ZeroVector;

	if (!TypedFilterFactory->Config.OperandBValue.TryReadDataValue(IO, B, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}
	B = B.GetSafeNormal();

	if (!PCGExData::Helpers::TryReadDataValue(IO, TypedFilterFactory->Config.OperandA, A, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}
	A = A.GetSafeNormal();
	if (TypedFilterFactory->Config.bInvertOperandA)
	{
		A *= -1;
	}

	FPCGExDotComparisonDetails TempComparison = TypedFilterFactory->Config.DotComparisonDetails;
	PCGEX_MAKE_SHARED(TempFacade, PCGExData::FFacade, IO.ToSharedRef())
	if (!TempComparison.Init(SharedContext.Get(), TempFacade.ToSharedRef(), PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}

	return TempComparison.Test(FVector::DotProduct(A, B), 0);
}

PCGEX_CREATE_FILTER_FACTORY(Dot)

#if WITH_EDITOR
void UPCGExDotFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.RenamePins(this, InOutNode);
		RetireInputPin(InOutNode, FName(TEXT("CompareAgainst")));
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExDotFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExDotFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandA) + TEXT(" ⋅ ");

	if (Config.OperandBValue.Input == EPCGExInputValueType::Attribute)
	{
		DisplayName += PCGExMetaHelpers::GetSelectorDisplayName(Config.OperandBValue.Attribute);
	}
	else
	{
		DisplayName += " (v3) ";
	}

	return DisplayName + Config.DotComparisonDetails.GetDisplayComparison();
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
