// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExBitmaskFilter.h"

#include "PCGExVersion.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointIO.h"
#include "Data/Bitmasks/PCGExBitmaskDetails.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

#if WITH_EDITOR
void FPCGExBitmaskFilterConfig::ApplyDeprecation()
{
	BitmaskValue.Update(MaskInput_DEPRECATED, BitmaskAttribute_DEPRECATED, Bitmask_DEPRECATED);
}

void FPCGExBitmaskFilterConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("Bitmask")), FName(TEXT("BitmaskValue")), FName(TEXT("Constant")), FName(TEXT("Bitmask")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("BitmaskAttribute")), FName(TEXT("BitmaskValue")), FName(TEXT("Attribute")), FName(TEXT("Bitmask (Attr)")));
}
#endif

bool UPCGExBitmaskFilterFactory::DomainCheck()
{
	return Config.BitmaskValue.CanSupportDataOnly() && PCGExMetaHelpers::IsDataDomainAttribute(Config.FlagsAttribute);
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExBitmaskFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FBitmaskFilter>(this);
}

void UPCGExBitmaskFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	FacadePreloader.Register<int64>(InContext, Config.FlagsAttribute);
	if (Config.BitmaskValue.Input == EPCGExInputValueType::Attribute)
	{
		FacadePreloader.Register<int64>(InContext, Config.BitmaskValue.Attribute);
	}
}

bool UPCGExBitmaskFilterFactory::RegisterConsumableAttributes(FPCGExContext* InContext) const
{
	if (!Super::RegisterConsumableAttributes(InContext))
	{
		return false;
	}

	InContext->AddConsumableAttributeName(Config.FlagsAttribute);

	return true;
}

bool PCGExPointFilter::FBitmaskFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	// Cache config-derived compositions up front so they survive a per-point Init failure: the
	// collection-level Test(IO,...) path relies on them and may run even when the broadcaster setup
	// below fails against the seed facade (see IFilter::Test(IO, ParentCollection) contract).
	Compositions.Reserve(TypedFilterFactory->Config.Compositions.Num());
	for (const FPCGExBitmaskRef& Ref : TypedFilterFactory->Config.Compositions)
	{
		Compositions.Add(Ref.GetSimpleBitmask());
	}

	FlagsReader = PointDataFacade->GetReadable<int64>(TypedFilterFactory->Config.FlagsAttribute, PCGExData::EIOSide::In, true);

	if (!FlagsReader)
	{
		PCGEX_LOG_INVALID_ATTR_HANDLED_C(InContext, Flags, TypedFilterFactory->Config.FlagsAttribute)
		return false;
	}

	MaskReader = TypedFilterFactory->Config.BitmaskValue.GetValueSetting(PCGEX_QUIET_HANDLING);
	MaskReader->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
	if (!MaskReader->Init(PointDataFacade))
	{
		return false;
	}

	return true;
}

bool PCGExPointFilter::FBitmaskFilter::Test(const int32 PointIndex) const
{
	// Start with the base mask value, then apply each composition's bitwise mutation (AND/OR/XOR/NOT) in sequence.
	int64 OutMask = MaskReader->Read(PointIndex);
	for (const FPCGExSimpleBitmask& Comp : Compositions)
	{
		Comp.Mutate(OutMask);
	}
	const bool Result = PCGExBitmask::Compare(TypedFilterFactory->Config.Comparison, FlagsReader->Read(PointIndex), OutMask);
	return TypedFilterFactory->Config.bInvertResult ? !Result : Result;
}

bool PCGExPointFilter::FBitmaskFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	int64 OutFlags = 0;
	int64 OutMask = 0;

	if (!PCGExData::Helpers::TryReadDataValue(IO, TypedFilterFactory->Config.FlagsAttribute, OutFlags, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}

	if (!TypedFilterFactory->Config.BitmaskValue.TryReadDataValue(IO, OutMask, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}

	for (const FPCGExSimpleBitmask& Comp : Compositions)
	{
		Comp.Mutate(OutMask);
	}

	const bool Result = PCGExBitmask::Compare(TypedFilterFactory->Config.Comparison, OutFlags, OutMask);
	return TypedFilterFactory->Config.bInvertResult ? !Result : Result;
}

PCGEX_CREATE_FILTER_FACTORY(Bitmask)

#if WITH_EDITOR
void UPCGExBitmaskFilterProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExBitmaskFilterProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExBitmaskFilterProviderSettings::GetDisplayName() const
{
	FString A = Config.BitmaskValue.Input == EPCGExInputValueType::Attribute ? Config.BitmaskValue.Attribute.ToString() : TEXT("(Const)");
	FString B = Config.FlagsAttribute.ToString();
	FString DisplayName;

	switch (Config.Comparison)
	{
	case EPCGExBitflagComparison::MatchPartial:
		DisplayName = TEXT("Contains Any");
		//DisplayName = TEXT("A & B != 0");
		//DisplayName = A + " & " + B + TEXT(" != 0");
		break;
	case EPCGExBitflagComparison::MatchFull:
		DisplayName = TEXT("Contains All");
		//DisplayName = TEXT("A & B == B");
		//DisplayName = A + " Any " + B + TEXT(" == B");
		break;
	case EPCGExBitflagComparison::MatchStrict:
		DisplayName = TEXT("Is Exactly");
		//DisplayName = TEXT("A == B");
		//DisplayName = A + " == " + B;
		break;
	case EPCGExBitflagComparison::NoMatchPartial:
		DisplayName = TEXT("Not Contains Any");
		//DisplayName = TEXT("A & B == 0");
		//DisplayName = A + " & " + B + TEXT(" == 0");
		break;
	case EPCGExBitflagComparison::NoMatchFull:
		DisplayName = TEXT("Not Contains All");
		//DisplayName = TEXT("A & B != B");
		//DisplayName = A + " & " + B + TEXT(" != B");
		break;
	default:
		DisplayName = " ?? ";
		break;
	}

	return DisplayName;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
