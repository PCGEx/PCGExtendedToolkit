// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExEnumFilter.h"

#include "PCGExVersion.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Helpers/PCGExMetaHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExEnumFilterDefinition"
#define PCGEX_NAMESPACE PCGExEnumFilterDefinition

#pragma region FPCGExEnumFilterConfig

#if WITH_EDITOR
namespace PCGExEnumFilter
{
	int32 NumEntries(const UEnum* InEnum)
	{
		return InEnum->ContainsExistingMax() ? InEnum->NumEnums() - 1 : InEnum->NumEnums();
	}

	// Display name as the map key: readable for both native and Blueprint enums. Editor-only by design.
	FName GetEntryKey(const UEnum* InEnum, const int32 Index)
	{
		return FName(InEnum->GetDisplayNameTextByIndex(Index).BuildSourceString());
	}
}

void FPCGExEnumFilterConfig::SyncValues()
{
	const UEnum* EnumClass = Enum.Class;
	if (!EnumClass)
	{
		Values.Empty();
		SelectedValues.Empty();
		return;
	}

	TMap<FName, bool> NewValues;
	SelectedValues.Reset();

	const int32 Num = PCGExEnumFilter::NumEntries(EnumClass);
	for (int32 i = 0; i < Num; i++)
	{
		if (EnumClass->HasMetaData(TEXT("Hidden"), i) || EnumClass->HasMetaData(TEXT("Spacer"), i))
		{
			continue;
		}

		const FName Key = PCGExEnumFilter::GetEntryKey(EnumClass, i);
		const bool* Existing = Values.Find(Key);
		const bool bChecked = Existing ? *Existing : true;

		NewValues.Add(Key, bChecked);
		if (bChecked)
		{
			SelectedValues.Add(EnumClass->GetValueByIndex(i));
		}
	}

	Values = MoveTemp(NewValues);
}
#endif

#pragma endregion

#pragma region UPCGExEnumFilterFactory

bool UPCGExEnumFilterFactory::Init(FPCGExContext* InContext)
{
	if (!Super::Init(InContext))
	{
		return false;
	}

	if (!Config.Enum.IsValid())
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Enum filter has no enum class selected."));
		return false;
	}

	ValueMask = 0;
	LargeValues.Reset();

	if (Config.Mode == EPCGExEnumFilterMode::AnyOf)
	{
		for (const int64 Value : Config.SelectedValues)
		{
			if (static_cast<uint64>(Value) < 64)
			{
				ValueMask |= (1ull << Value);
			}
			else
			{
				LargeValues.AddUnique(Value);
			}
		}
		LargeValues.Sort();
	}

	return true;
}

bool UPCGExEnumFilterFactory::DomainCheck()
{
	return PCGExMetaHelpers::IsDataDomainAttribute(Config.Attribute);
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExEnumFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FEnumFilter>(this);
}

void UPCGExEnumFilterFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	FacadePreloader.Register<int64>(InContext, Config.Attribute);
}

bool UPCGExEnumFilterFactory::RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const
{
	if (!Super::RegisterConsumableAttributesWithData(InContext, InData))
	{
		return false;
	}

	FName Consumable = NAME_None;
	PCGEX_CONSUMABLE_SELECTOR(Config.Attribute, Consumable)

	return true;
}

#pragma endregion

#pragma region FEnumFilter

bool PCGExPointFilter::FEnumFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	Reader = PointDataFacade->GetBroadcaster<int64>(TypedFilterFactory->Config.Attribute, true, false, PCGEX_QUIET_HANDLING);

	if (!Reader)
	{
		PCGEX_LOG_INVALID_SELECTOR_HANDLED_C(InContext, Attribute, TypedFilterFactory->Config.Attribute)
		return false;
	}

	return true;
}

bool PCGExPointFilter::FEnumFilter::Test(const int32 PointIndex) const
{
	return TypedFilterFactory->Matches(Reader->Read(PointIndex)) != TypedFilterFactory->Config.bInvert;
}

bool PCGExPointFilter::FEnumFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	int64 Value = 0;
	if (!PCGExData::Helpers::TryReadDataValue(IO, TypedFilterFactory->Config.Attribute, Value, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}

	return TypedFilterFactory->Matches(Value) != TypedFilterFactory->Config.bInvert;
}

#pragma endregion

#pragma region UPCGExEnumFilterProviderSettings

void UPCGExEnumFilterProviderSettings::PostLoad()
{
#if WITH_EDITOR
	// Before Super, so the cached CRC sees the restored value.
	PCGEX_IF_VERSION_LOWER(1, 78, 6)
	{
		Config.Enum.EDITOR_RestoreValueFromFlagMask();
	}
#endif

	Super::PostLoad();
#if WITH_EDITOR
	Config.SyncValues();
#endif
}

#if WITH_EDITOR
void UPCGExEnumFilterProviderSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGExEnumFilterProviderSettings, Config))
	{
		Config.SyncValues();
	}
}

TArray<FText> UPCGExEnumFilterProviderSettings::GetNodeTitleAliases() const
{
	return {FTEXT("PCGEx | Filter : by Attribute (enum)")};
}
#endif

PCGEX_CREATE_FILTER_FACTORY(Enum)

#if WITH_EDITOR
FString UPCGExEnumFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = PCGExMetaHelpers::GetSelectorDisplayName(Config.Attribute);

	if (!Config.Enum.IsValid())
	{
		return PCGExCommon::FlagInvertLabel(DisplayName + TEXT(" : (No Enum)"), Config.bInvert);
	}

	const FString EnumName = Config.Enum.Class->GetName();

	if (Config.Mode == EPCGExEnumFilterMode::StrictlyEqual)
	{
		DisplayName += TEXT(" == ") + EnumName + TEXT("::") + Config.Enum.GetCultureInvariantDisplayName();
	}
	else
	{
		DisplayName += FString::Printf(TEXT(" in %s (%d/%d)"), *EnumName, Config.SelectedValues.Num(), Config.Values.Num());
	}

	return PCGExCommon::FlagInvertLabel(DisplayName, Config.bInvert);
}
#endif

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
