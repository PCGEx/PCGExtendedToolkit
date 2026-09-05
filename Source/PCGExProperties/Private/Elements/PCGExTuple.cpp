// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExTuple.h"

#include "PCGExPropertyTypes.h"
#include "PCGGraph.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "PCGExPropertyWriter.h"
#include "Containers/PCGExManagedObjects.h"
#include "Helpers/PCGExArrayHelpers.h"

#if WITH_EDITOR
#include "Editor.h"
#include "UObject/UObjectGlobals.h"
#endif

#define LOCTEXT_NAMESPACE "PCGExGraphSettings"
#define PCGEX_NAMESPACE Tuple

#if WITH_EDITOR
void UPCGExTupleSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExTupleSettings::PostEditChangeProperty);

	bool bNeedsSync = false;
	bool bNeedsUIRefresh = false;

	if (PropertyChangedEvent.MemberProperty)
	{
		FName PropName = PropertyChangedEvent.MemberProperty->GetFName();
		EPropertyChangeType::Type ChangeType = PropertyChangedEvent.ChangeType;

		if (PropName == GET_MEMBER_NAME_CHECKED(UPCGExTupleSettings, Composition))
		{
			bNeedsSync = true;
			bNeedsUIRefresh = true;
		}
		// Also catch changes to Composition array elements (e.g., changing Property type or Name)
		else if (PropertyChangedEvent.MemberProperty->GetOwnerStruct() == FPCGExPropertySchema::StaticStruct())
		{
			bNeedsSync = true;
			bNeedsUIRefresh = true;
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UPCGExTupleSettings, Values) && (ChangeType == EPropertyChangeType::ArrayAdd || ChangeType == EPropertyChangeType::ArrayRemove || ChangeType == EPropertyChangeType::ArrayClear || ChangeType == EPropertyChangeType::ArrayMove))
		{
			bNeedsSync = true;
		}
	}

	if (!bNeedsSync && !bNeedsUIRefresh)
	{
		DirtyCache();
		Super::PostEditChangeProperty(PropertyChangedEvent);
		return; // Skip processing
	}

	// Sync composition schemas to values (only if we need to sync). Explicit three-step
	// pipeline: canonicalize outer->inner identity on Schemas, align ImportOverrides with
	// the imports tree, then apply the resolved schema to every row's Overrides array.
	if (bNeedsSync)
	{
		// Remap rows before ApplyToOverrides -- it calls SyncToSchema per row, which aliases
		// collided rows without the remap.
		Composition.SyncAllSchemasAndRemapRows(Values);
		Composition.ReconcileImportOverrides();
		Composition.ApplyToOverrides(Values);
	}

	(void)MarkPackageDirty();

	// Force UI refresh BEFORE Super - this ensures details panel rebuilds customizations
	if (bNeedsUIRefresh)
	{
		// Mark Values as changed to force full customization rebuild
		FProperty* ValuesProperty = FindFProperty<FProperty>(GetClass(), TEXT("Values"));
		if (ValuesProperty)
		{
			// Use ArrayClear type to force aggressive rebuild
			FPropertyChangedEvent RefreshEvent(ValuesProperty, EPropertyChangeType::ArrayClear);
			FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(this, RefreshEvent);
		}
	}

	DirtyCache();

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

TArray<FPCGPinProperties> UPCGExTupleSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExTupleSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_PARAM(FName("Tuple"), TEXT("Tuple."), Required)
	PCGExProperties::AddOutputMapPin(PinProperties, bOutputMap);
	return PinProperties;
}

FPCGElementPtr UPCGExTupleSettings::CreateElement() const
{
	return MakeShared<FPCGExTupleElement>();
}

void FPCGExTupleContext::RegisterAssetDependencies()
{
	FPCGExContext::RegisterAssetDependencies();

	const UPCGExTupleSettings* Settings = GetInputSettings<UPCGExTupleSettings>();
	if (!Settings)
	{
		return;
	}

	TSet<FSoftObjectPath> Paths;
	PCGExProperties::GatherOutputDependencies(Settings->Composition, Paths);
	for (const FPCGExPropertyOverrides& Row : Settings->Values)
	{
		PCGExProperties::GatherOutputDependencies(Row, Paths);
	}
	AddAssetDependencies(Paths);
}


bool FPCGExTupleElement::Boot(FPCGExContext* InContext) const
{
	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_SETTINGS_C(InContext, Tuple)

	// Duplicate columns resolve first-seen-wins downstream -- same hard failure as Tuple : Distribute.
	TArray<FName> Duplicates;
	if (!Settings->Composition.ValidateUniqueNames(Duplicates))
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Composition has duplicate column names."));
		return false;
	}

	return true;
}

bool FPCGExTupleElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT()
	PCGEX_SETTINGS(Tuple)

	UPCGParamData* TupleData = Context->ManagedObjects->New<UPCGParamData>();

	// ColCount must match Values[k].Overrides.Num() -- the SyncAllSchemas /
	// ReconcileImportOverrides / ApplyToOverrides pipeline (PostEditChangeProperty) keeps
	// them parallel.
	TArray<FPCGExPropertyResolved> Resolved;
	Settings->Composition.Resolve(Resolved);
	const int32 ColCount = Resolved.Num();

	TArray<FPCGMetadataAttributeBase*> Attributes;
	TArray<int64> Keys;

	Attributes.Reserve(ColCount);
	Keys.Reserve(Settings->Values.Num());

	for (const FPCGExPropertyResolved& Entry : Resolved)
	{
		const FPCGExProperty* Property = Entry.GetEffectiveProperty().GetPtr<FPCGExProperty>();
		if (!Property)
		{
			Attributes.Add(nullptr);
			continue;
		}

		const FName ColumnName = Property->ResolveOutputAttributeName(Entry.Source->Name);
		const FPCGMetadataAttributeBase* ExistingAttr = TupleData->Metadata->GetConstAttribute(ColumnName);
		if (ExistingAttr)
		{
			// Resolved-name aliasing (e.g. a column literally named like another column's resolved
			// staging-layer attribute) -- plain duplicates are already rejected in Boot.
			PCGE_LOG_C(Error, GraphAndLog, Context, FText::Format(FTEXT("Output attribute \"{0}\" is declared by more than one column (resolved names collide) -- later column skipped."), FText::FromName(ColumnName)));
			Attributes.Add(nullptr);
			continue;
		}

		Attributes.Add(Property->CreateMetadataAttribute(TupleData->Metadata, ColumnName));
	}

	// Create all keys
	for (int i = 0; i < Settings->Values.Num(); ++i)
	{
		Keys.Add(TupleData->Metadata->AddEntry());
	}

	// Metadata output path. For POINT ATTRIBUTE output, see FPCGExPropertyWriter.
	TArray<const FPCGExProperty*> SidecarSources;
	for (int i = 0; i < ColCount; ++i)
	{
		FPCGMetadataAttributeBase* Attribute = Attributes[i];
		if (!Attribute)
		{
			continue;
		}

		for (int k = 0; k < Keys.Num(); k++)
		{
			const FPCGExPropertyOverrides& Row = Settings->Values[k];

			if (!Row.IsOverrideEnabled(i))
			{
				continue;
			}

			if (const FPCGExProperty* Property = Row.Overrides[i].GetProperty())
			{
				Property->WriteMetadataValue(Attribute, Keys[k]);
				if (Settings->bOutputMap && !Property->GetOutputSidecarPin().IsNone())
				{
					SidecarSources.AddUnique(Property);
				}
			}
		}
	}

	TSet<FString> Tags;
	PCGExArrayHelpers::AppendEntriesFromCommaSeparatedList(Settings->CommaSeparatedTags, Tags);
	Context->StageOutput(TupleData, FName("Tuple"), PCGExData::EStaging::None, Tags);

	if (!SidecarSources.IsEmpty())
	{
		PCGExProperties::StageSidecars(Context, SidecarSources);
	}

	Context->Done();
	return Context->TryComplete();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
