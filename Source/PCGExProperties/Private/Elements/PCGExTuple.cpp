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
const FPCGExPropertySchemaCollection* UPCGExTupleSettings::EDITOR_GetRowSchema() const
{
	return &Composition;
}

void UPCGExTupleSettings::EDITOR_ForEachPropertyRows(TFunctionRef<void(FPCGExPropertyOverrides& Rows, bool bIsImportLayer)> Visitor)
{
	Visitor(Composition.ImportOverrides, true);
	for (FPCGExPropertyOverrides& Row : Values)
	{
		Visitor(Row, false);
	}
}

void UPCGExTupleSettings::EDITOR_NotifyRowsChanged()
{
	// A value-level edit of Values: dirties the cache and refreshes the node, no schema sync.
	FPropertyChangedEvent Event(FindFProperty<FProperty>(GetClass(), GET_MEMBER_NAME_CHECKED(UPCGExTupleSettings, Values)), EPropertyChangeType::ValueSet);
	PostEditChangeProperty(Event);
}

void UPCGExTupleSettings::EDITOR_OnImportedSchemaChanged(UPCGExPropertySchemaAsset* Asset)
{
	bool bChanged = Composition.SyncImportOverridesInPlace();
	if (Composition.ApplyToRows(Values))
	{
		bChanged = true;
		// The rows may have been rebuilt, exactly as after an edit of Composition.
		PCGExPropertyRowHost::BroadcastRowsRebuilt(this, GET_MEMBER_NAME_CHECKED(UPCGExTupleSettings, Values));
	}
	if (bChanged)
	{
		(void)MarkPackageDirty();
	}
	// Whatever the rows did: a disabled column outputs the asset's own default, which may be what changed.
	EDITOR_NotifyRowsChanged();
}

void UPCGExTupleSettings::PostLoad()
{
	// An import can change while this graph is unloaded. Before Super, so the CRC it caches sees the healed rows.
	bool bChanged = Composition.ReconcileImportOverridesAtLoad();
	bChanged |= Composition.SyncRowsAtLoad(Values);
	if (bChanged)
	{
		(void)MarkPackageDirty();
	}

	Super::PostLoad();
}

void UPCGExTupleSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGExTupleSettings::PostEditChangeProperty);

	const PCGExPropertyRowHost::FEditKind Edit = PCGExPropertyRowHost::ClassifyEdit(
		PropertyChangedEvent, GET_MEMBER_NAME_CHECKED(UPCGExTupleSettings, Composition), GET_MEMBER_NAME_CHECKED(UPCGExTupleSettings, Values));

	if (Edit.bSyncRows)
	{
		// Canonicalize outer->inner identity on Schemas (remapping rows first: SyncToSchema aliases collided rows
		// without it), align ImportOverrides with the imports tree, then apply the resolved schema to every row.
		Composition.SyncAllSchemasAndRemapRows(Values);
		Composition.ReconcileImportOverrides();
		Composition.ApplyToRows(Values);

		(void)MarkPackageDirty();

		if (Edit.bSchemaChanged)
		{
			PCGExPropertyRowHost::BroadcastRowsRebuilt(this, GET_MEMBER_NAME_CHECKED(UPCGExTupleSettings, Values));
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
	// Regular only when a column feeds it; the Map is emitted whenever one does.
	PCGExProperties::AddOutputMapPin(PinProperties, PCGExProperties::WritesOutputMap(Composition));
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

	// The edit and load syncs keep Values[k].Overrides parallel to these columns; nothing below relies on it.
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

		// A cell is its row's enabled entry of this column's name and type, never the entry at i on position alone:
		// WriteMetadataValue casts the attribute to the entry's own type unchecked.
		const FName PropertyName = Resolved[i].Source->Name;
		const UScriptStruct* PropertyType = Resolved[i].GetEffectiveProperty().GetScriptStruct();

		for (int k = 0; k < Keys.Num(); k++)
		{
			const FInstancedStruct* Cell = Settings->Values[k].FindEnabledSlot(PropertyName, PropertyType, i);
			if (const FPCGExProperty* Property = Cell ? Cell->GetPtr<FPCGExProperty>() : nullptr)
			{
				Property->WriteMetadataValue(Attribute, Keys[k]);
				if (!Property->GetOutputSidecarPin().IsNone())
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
