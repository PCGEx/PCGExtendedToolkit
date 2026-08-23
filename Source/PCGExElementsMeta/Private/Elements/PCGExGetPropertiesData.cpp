// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExGetPropertiesData.h"

#include "PCGExProperty.h"
#include "PCGExPropertyCollectionComponent.h"
#include "PCGExPropertySchemaAsset.h"

#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"

#include "PCGContext.h"
#include "PCGParamData.h"

#include "PCGExCoreSettingsCache.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExMTCommon.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExDataHelpers.h"
#include "Helpers/PCGExBulkAttributeHelpers.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Metadata/Accessors/PCGCustomAccessor.h"
#include "Metadata/PCGMetadata.h"

#define LOCTEXT_NAMESPACE "PCGExGetPropertiesDataElement"
#define PCGEX_NAMESPACE GetPropertiesData

PCGEX_INITIALIZE_ELEMENT(GetPropertiesData)

#pragma region UPCGSettings

TArray<FPCGPinProperties> UPCGExGetPropertiesDataSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGExGetPropertiesData::SourcesPin, TEXT("Input points or attribute sets carrying the actor/component reference attribute."), Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExGetPropertiesDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGExGetPropertiesData::SourcesPin, TEXT("One forwarded copy of each input, augmented with resolved property attributes."), Required)
	PCGExProperties::AddOutputMapPin(PinProperties, WantsOutputMap());
	return PinProperties;
}

void FPCGExGetPropertiesDataContext::RegisterAssetDependencies()
{
	FPCGExContext::RegisterAssetDependencies();

	TSet<FSoftObjectPath> Paths;
	for (const TArray<FInstancedStruct>& Schema : SchemaPerComponent)
	{
		PCGExProperties::GatherOutputDependencies(Schema, Paths);
	}
	AddAssetDependencies(Paths);
}

#pragma endregion

namespace PCGExGetPropertiesData
{
	/** Adapter that exposes FPCGExGetPropertiesDataContext's per-component schema arrays + the
	 *  global prototype map as an IPCGExPropertyProvider for FPCGExPropertySetWriter (and any
	 *  other consumer of the provider interface).
	 *
	 *  Critically overrides GetPropertyAt with an O(1) TMap lookup against PropertyLookupPerComponent
	 *  (built in Phase 3c) -- the writer's per-row hot path goes through that method per writer per
	 *  row, so the default linear-scan implementation would defeat the whole point of building the
	 *  lookup tables. */
	struct FContextPropertyProvider final : public IPCGExPropertyProvider
	{
		const FPCGExGetPropertiesDataContext* Context = nullptr;

		explicit FContextPropertyProvider(const FPCGExGetPropertiesDataContext* InContext)
			: Context(InContext)
		{
		}

		virtual TConstArrayView<FInstancedStruct> GetProperties(int32 Index) const override
		{
			return Context->SchemaPerComponent.IsValidIndex(Index)
				? TConstArrayView<FInstancedStruct>(Context->SchemaPerComponent[Index])
				: TConstArrayView<FInstancedStruct>();
		}

		// Empty -- nothing in this node needs the registry; AutoPopulateFromRegistry isn't on our path.
		virtual TConstArrayView<FPCGExPropertyRegistryEntry> GetPropertyRegistry() const override
		{
			return {};
		}

		virtual const FInstancedStruct* FindPrototypeProperty(FName PropertyName) const override
		{
			const FInstancedStruct* const* Found = Context->PrototypeByName.Find(PropertyName);
			return Found ? *Found : nullptr;
		}

		virtual const FInstancedStruct* GetPropertyAt(int32 SourceIndex, FName PropertyName) const override
		{
			if (!Context->PropertyLookupPerComponent.IsValidIndex(SourceIndex))
			{
				return nullptr;
			}
			const FInstancedStruct* const* Found = Context->PropertyLookupPerComponent[SourceIndex].Find(PropertyName);
			return Found ? *Found : nullptr;
		}
	};

	/** Recursive walk of a property schema collection's import tree, accumulating every asset
	 *  reached into OutAssets. Cycle-safe via Visited. */
	void CollectImportedAssetsRecursive(
		const FPCGExPropertySchemaCollection& Collection,
		TSet<const UPCGExPropertySchemaAsset*>& OutAssets,
		TSet<const UPCGExPropertySchemaAsset*>& Visited)
	{
		for (const TObjectPtr<UPCGExPropertySchemaAsset>& AssetPtr : Collection.ImportedSchemas)
		{
			const UPCGExPropertySchemaAsset* Asset = AssetPtr.Get();
			if (!Asset)
			{
				continue;
			}
			bool bAlreadyIn = false;
			Visited.Add(Asset, &bAlreadyIn);
			if (bAlreadyIn)
			{
				continue;
			}
			OutAssets.Add(Asset);
			CollectImportedAssetsRecursive(Asset->Collection, OutAssets, Visited);
		}
	}

	/** Test a component's full import tree against the required-schema set. AllRequired returns
	 *  true only when every required asset is reached; AnyRequired returns true on the first hit. */
	bool ComponentSatisfiesSchemaFilter(
		const UPCGExPropertyCollectionComponent* Component,
		const TSet<const UPCGExPropertySchemaAsset*>& Required,
		const EPCGExSchemaPresenceMode Mode)
	{
		if (Required.IsEmpty())
		{
			return true;
		}
		if (!Component)
		{
			return false;
		}

		TSet<const UPCGExPropertySchemaAsset*> Reached;
		TSet<const UPCGExPropertySchemaAsset*> Visited;
		CollectImportedAssetsRecursive(Component->GetProperties(), Reached, Visited);

		if (Mode == EPCGExSchemaPresenceMode::AnyRequired)
		{
			for (const UPCGExPropertySchemaAsset* RequiredAsset : Required)
			{
				if (Reached.Contains(RequiredAsset))
				{
					return true;
				}
			}
			return false;
		}

		// AllRequired
		for (const UPCGExPropertySchemaAsset* RequiredAsset : Required)
		{
			if (!Reached.Contains(RequiredAsset))
			{
				return false;
			}
		}
		return true;
	}

	/** Bulk-copy every non-property attribute from Src to Dst for the rows whose KeepMask is 1.
	 *  Used during the param-data filter step to rebuild a UPCGParamData with only the kept rows.
	 *  Preserves attribute defaults + interpolation flags + per-attribute types. */
	UPCGParamData* GatherParamData(
		FPCGExContext* InContext,
		const UPCGParamData* Src,
		const TArrayView<int8> KeepMask)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExGetPropertiesData::GatherParamData);

		const UPCGMetadata* SrcM = Src ? Src->ConstMetadata() : nullptr;
		if (!SrcM)
		{
			return nullptr;
		}

		UPCGParamData* Dst = InContext->ManagedObjects->New<UPCGParamData>();
		UPCGMetadata* DstM = Dst->MutableMetadata();

		// Enumerate source rows via the entry-keys accessor -- works for any UPCGParamData regardless
		// of how its keys are spaced internally. The caller (WriteInput) always sizes KeepMask to
		// NumRows == NumSrcRows; mismatches would indicate a slot-bucketing bug upstream.
		TSharedRef<FPCGAttributeAccessorKeysEntries> SrcKeys = MakeShared<FPCGAttributeAccessorKeysEntries>(SrcM);
		const int32 NumSrcRows = SrcKeys->GetNum();
		check(KeepMask.Num() == NumSrcRows);

		TArray<int32> KeptSrcRowIndices;
		KeptSrcRowIndices.Reserve(NumSrcRows);
		for (int32 i = 0; i < NumSrcRows; i++)
		{
			if (KeepMask[i] != 0)
			{
				KeptSrcRowIndices.Add(i);
			}
		}

		if (KeptSrcRowIndices.IsEmpty())
		{
			return Dst; // empty result, still a valid UPCGParamData
		}

		// Allocate destination entries up front so every attribute writes against the same key set.
		TArray<int64> DstEntryKeys;
		DstEntryKeys.SetNumUninitialized(KeptSrcRowIndices.Num());
		for (int32 i = 0; i < KeptSrcRowIndices.Num(); i++)
		{
			DstEntryKeys[i] = DstM->AddEntry();
		}

		// Walk every source attribute and copy values for kept rows. ExecuteWithRightType handles
		// type dispatch -- the per-T branch reads + writes via the typed FPCGMetadataAttribute<T>.
		TArray<FPCGAttributeIdentifier> AttrIds;
		TArray<EPCGMetadataTypes> AttrTypes;
		SrcM->GetAllAttributes(AttrIds, AttrTypes);

		for (int32 a = 0; a < AttrIds.Num(); a++)
		{
			const FPCGAttributeIdentifier& Id = AttrIds[a];
			const FPCGMetadataAttributeBase* SrcAttrBase = SrcM->GetConstAttribute(Id);
			if (!SrcAttrBase)
			{
				continue;
			}

			PCGExMetaHelpers::ExecuteWithRightType(SrcAttrBase->GetTypeId(), [&](auto Dummy)
			{
				using T = decltype(Dummy);
				const FPCGMetadataAttribute<T>* TypedSrc = static_cast<const FPCGMetadataAttribute<T>*>(SrcAttrBase);
				const T DefaultValue = TypedSrc->GetValueFromItemKey(PCGDefaultValueKey);
				FPCGMetadataAttribute<T>* TypedDst = DstM->CreateAttribute<T>(
					Id, DefaultValue, TypedSrc->AllowsInterpolation(), /*bOverrideParent=*/true);
				if (!TypedDst)
				{
					return;
				}

				// Bulk-read every source row once, then index by KeptSrcRowIndices. GetRange is the
				// one accessor API we know is supported here (used everywhere in PCGEx); sparse
				// per-index reads via the IPCGAttributeAccessor::Get template aren't part of the
				// pattern we have working examples of.
				FPCGAttributePropertyInputSelector Selector;
				Selector.Update(Id.Name.ToString());
				Selector = Selector.CopyAndFixLast(Src);
				TUniquePtr<const IPCGAttributeAccessor> ReadAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Src, Selector);
				if (!ReadAccessor)
				{
					return;
				}

				TArray<T> AllSrcValues;
				AllSrcValues.SetNum(NumSrcRows);
				if (!ReadAccessor->GetRange<T>(AllSrcValues, 0, *SrcKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
				{
					return;
				}

				for (int32 i = 0; i < DstEntryKeys.Num(); i++)
				{
					TypedDst->SetValue(DstEntryKeys[i], AllSrcValues[KeptSrcRowIndices[i]]);
				}
			});
		}

		return Dst;
	}

	/** Phase 1: read source paths from every input and append one slot per row. Single-threaded
	 *  outer loop with parallel bulk-reads inside (mirrors GetCollectionData's ParseSourceInputsIntoSlots
	 *  shape, scaled down for our single fanout mode). */
	void ParseInputsIntoSlots(
		FPCGExGetPropertiesDataContext* Context,
		const UPCGExGetPropertiesDataSettings* Settings,
		const TArray<FPCGTaggedData>& Inputs)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExGetPropertiesData::ParseInputsIntoSlots);

		struct FInputParse
		{
			TArray<FSoftObjectPath> Paths;
		};
		TArray<FInputParse> PerInput;
		PerInput.SetNum(Inputs.Num());

		PCGExMT::ParallelOrSequential(Inputs.Num(), [&](const int32 i)
		{
			PCGExData::Helpers::BulkReadSoftPaths(Inputs[i].Data, Settings->SourceAttribute, PerInput[i].Paths);
		}, /*Threshold=*/2, EParallelForFlags::Unbalanced);

		int32 TotalRows = 0;
		for (const FInputParse& P : PerInput)
		{
			TotalRows += P.Paths.Num();
		}
		Context->Slots.SetNum(TotalRows);

		int32 Cursor = 0;
		for (int32 i = 0; i < Inputs.Num(); i++)
		{
			FInputParse& P = PerInput[i];
			for (int32 r = 0; r < P.Paths.Num(); r++)
			{
				FSlot& Slot = Context->Slots[Cursor++];
				Slot.SourceInputIndex = i;
				Slot.RowIndex = r;
				Slot.Path = MoveTemp(P.Paths[r]);
			}
		}
	}

	/** Phase 2: tentative resolve. ResolveObject is thread-safe on already-loaded assets and never
	 *  triggers a load -- rows whose target isn't in memory simply stay Component=nullptr. */
	void ResolveSlots(FPCGExGetPropertiesDataContext* Context)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExGetPropertiesData::ResolveSlots);

		PCGExMT::ParallelOrSequential(Context->Slots.Num(), [&](const int32 i)
		{
			FSlot& Slot = Context->Slots[i];
			if (!Slot.Path.IsValid())
			{
				return;
			}
			UObject* Resolved = Slot.Path.ResolveObject();
			if (!Resolved)
			{
				return;
			}
			if (AActor* Actor = Cast<AActor>(Resolved))
			{
				Slot.Component = UPCGExPropertyCollectionComponent::FindOnActor(Actor);
			}
			else
			{
				Slot.Component = Cast<UPCGExPropertyCollectionComponent>(Resolved);
			}
		}, /*Threshold=*/64, EParallelForFlags::None);
	}

	/** Phase 3: collect unique components + parallel BuildResolvedSchema. Off-thread safe per
	 *  FPCGExPropertySchemaCollection's documented Reconcile contract (no concurrent editor mutation). */
	void BuildUniqueSchemas(FPCGExGetPropertiesDataContext* Context)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExGetPropertiesData::BuildUniqueSchemas);

		Context->UniqueComponents.Reset();
		Context->ComponentToIdx.Reset();
		for (const FSlot& Slot : Context->Slots)
		{
			if (!Slot.Component)
			{
				continue;
			}
			int32& Idx = Context->ComponentToIdx.FindOrAdd(Slot.Component, INDEX_NONE);
			if (Idx != INDEX_NONE)
			{
				continue;
			}
			Idx = Context->UniqueComponents.Num();
			Context->UniqueComponents.Add(Slot.Component);
		}

		Context->SchemaPerComponent.Reset();
		Context->SchemaPerComponent.SetNum(Context->UniqueComponents.Num());

		PCGExMT::ParallelOrSequential(Context->UniqueComponents.Num(), [&](const int32 i)
		{
			Context->SchemaPerComponent[i] = Context->UniqueComponents[i]->BuildResolvedSchema();
		}, /*Threshold=*/8, EParallelForFlags::Unbalanced);
	}

	/** Phase 3b: apply the required-schema filter. Components that fail get nulled out in
	 *  UniqueComponents (so Phase 4 + the per-row write loop ignore them) AND their owning slots
	 *  get bSchemaFiltered=true + Component=nullptr. The row-filter step always drops schema-failed
	 *  rows when RequiredSchemas was non-empty, independent of bOmitUnresolvedEntries -- the user's
	 *  intent in setting RequiredSchemas is a hard "drop everything else" filter. */
	void ApplyPresenceFilter(
		FPCGExGetPropertiesDataContext* Context,
		const EPCGExSchemaPresenceMode Mode)
	{
		if (Context->RequiredSchemaSet.IsEmpty())
		{
			return;
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExGetPropertiesData::ApplyPresenceFilter);

		TArray<int8> CompKeep;
		CompKeep.Init(1, Context->UniqueComponents.Num());

		PCGExMT::ParallelOrSequential(Context->UniqueComponents.Num(), [&](const int32 i)
		{
			if (!ComponentSatisfiesSchemaFilter(Context->UniqueComponents[i], Context->RequiredSchemaSet, Mode))
			{
				CompKeep[i] = 0;
			}
		}, /*Threshold=*/16);

		for (int32 i = 0; i < Context->UniqueComponents.Num(); i++)
		{
			if (CompKeep[i] == 0)
			{
				Context->UniqueComponents[i] = nullptr;
			}
		}

		for (FSlot& Slot : Context->Slots)
		{
			if (!Slot.Component)
			{
				continue;
			}
			const int32 Idx = Context->ComponentToIdx.FindChecked(Slot.Component);
			if (CompKeep[Idx] == 0)
			{
				Slot.Component = nullptr;
				Slot.bSchemaFiltered = true;
			}
		}
	}

	/** Phase 3c: build per-component name -> property pointer lookup tables AND the global
	 *  first-hit prototype map. Single pass over every surviving component's resolved schema.
	 *
	 *  These lookups replace O(SchemaSize) linear scans:
	 *  - WriteInput's per-row source lookup (was O(P*N*SchemaSize), becomes O(P*N))
	 *  - per-output prototype lookup (was O(P*K*SchemaSize), becomes O(P))
	 *
	 *  Skipped properties: anonymous (Name=None) or non-output-supporting. PrototypeByName preserves
	 *  the existing "first wins, encounter order" contract that AllFound mode also relies on. */
	void BuildSchemaLookups(FPCGExGetPropertiesDataContext* Context)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExGetPropertiesData::BuildSchemaLookups);

		const int32 NumComponents = Context->UniqueComponents.Num();
		Context->PropertyLookupPerComponent.Reset();
		Context->PropertyLookupPerComponent.SetNum(NumComponents);
		Context->PrototypeByName.Reset();

		for (int32 c = 0; c < NumComponents; c++)
		{
			if (!Context->UniqueComponents[c])
			{
				continue;
			}
			const TArray<FInstancedStruct>& Schema = Context->SchemaPerComponent[c];
			TMap<FName, const FInstancedStruct*>& Lookup = Context->PropertyLookupPerComponent[c];
			Lookup.Reserve(Schema.Num());

			for (const FInstancedStruct& InstStruct : Schema)
			{
				const FPCGExProperty* Prop = InstStruct.GetPtr<FPCGExProperty>();
				if (!Prop || Prop->PropertyName.IsNone() || !Prop->SupportsOutput())
				{
					continue;
				}
				// Per-component: last-wins on intra-schema name collisions (BuildResolvedSchema is
				// supposed to dedupe, but be defensive). Global: first-wins (matches the encounter
				// contract).
				Lookup.Add(Prop->PropertyName, &InstStruct);
				Context->PrototypeByName.FindOrAdd(Prop->PropertyName, &InstStruct);
			}
		}
	}

	/** Phase 4: build the effective output config list. AllFound enumerates PrototypeByName in its
	 *  insertion order (= component encounter order). Explicit delegates to
	 *  FPCGExPropertyOutputSettings::GetEffectiveConfigs. */
	void BuildEffectiveConfigs(
		const FPCGExGetPropertiesDataContext* Context,
		const UPCGExGetPropertiesDataSettings* Settings,
		TArray<FPCGExPropertyOutputConfig>& OutConfigs)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExGetPropertiesData::BuildEffectiveConfigs);

		if (Settings->OutputMode == EPCGExPropertyOutputMode::Explicit)
		{
			Settings->PropertyOutputSettings.GetEffectiveConfigs(OutConfigs);
			return;
		}

		OutConfigs.Reserve(OutConfigs.Num() + Context->PrototypeByName.Num());
		for (const TPair<FName, const FInstancedStruct*>& Pair : Context->PrototypeByName)
		{
			FPCGExPropertyOutputConfig& Cfg = OutConfigs.AddDefaulted_GetRef();
			Cfg.bEnabled = true;
			Cfg.PropertyName = Pair.Key;
			// OutputAttributeName intentionally empty -- GetEffectiveOutputName falls back to PropertyName.
		}
	}

	/** Per-input write phase. Handles both UPCGBasePointData and UPCGParamData uniformly through
	 *  the accessor-keys + SetRange pattern (same as PCGExCollections::WriteSchemaToElementDomain).
	 *  Tracks per-row outcomes so the optional row-filter step at the end can drop rows that
	 *  failed to resolve or only partially matched.
	 *
	 *  Two duplication strategies, picked by input type:
	 *  - Points: wrap input in FPointIO + InitializeOutput(Duplicate) so we can call Gather on Out
	 *    in place during the filter step. FPointIO owns the duplicate's lifetime via ManagedObjects.
	 *  - Param data: direct ManagedObjects->DuplicateData; filtering goes through GatherParamData
	 *    which builds a fresh UPCGParamData (UPCGMetadata has no entry-removal API).
	 */
	UPCGData* WriteInput(
		FPCGExGetPropertiesDataContext* Context,
		const UPCGExGetPropertiesDataSettings* Settings,
		const TArray<FPCGExPropertyOutputConfig>& EffectiveConfigs,
		const UPCGData* InData,
		const TArray<int32>& SlotIndicesForInput,
		TSharedPtr<PCGExData::FPointIO>& OutPointIO, /* set iff input is point data; caller stages it */
		TArray<FInstancedStruct>* OutSidecarClones /* non-null when the Map sidecar is wanted */)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExGetPropertiesData::WriteInput);

		OutPointIO.Reset();

		const int32 NumRows = SlotIndicesForInput.Num();
		if (NumRows == 0)
		{
			return nullptr;
		}

		UPCGData* DupData = nullptr;

		if (const UPCGBasePointData* PointDataIn = Cast<UPCGBasePointData>(InData))
		{
			OutPointIO = MakeShared<PCGExData::FPointIO>(Context->GetWeakSelfHandle(), PointDataIn);
			if (!OutPointIO->InitializeOutput<UPCGBasePointData>(PCGExData::EIOInit::Duplicate))
			{
				OutPointIO.Reset();
				return nullptr;
			}
			DupData = OutPointIO->GetOut();
		}
		else if (const UPCGParamData* ParamDataIn = Cast<UPCGParamData>(InData))
		{
			UPCGParamData* DupParam = Context->ManagedObjects->DuplicateData<UPCGParamData>(ParamDataIn);
			if (!DupParam)
			{
				return nullptr;
			}
			DupData = DupParam;
		}

		if (!DupData)
		{
			return DupData;
		}

		UPCGMetadata* Metadata = DupData->MutableMetadata();
		if (!Metadata)
		{
			return DupData;
		}

		// Extract per-row metadata entry keys once for direct attribute writes downstream. Each data
		// type carries its keys differently:
		//
		// Points: GetOutKeys(true) ensures metadata entries are allocated AND caches the keys
		// interface on the FPointIO -- the return value goes unused here (we read int64 keys
		// directly from the metadata-entry range below), but the call is the canonical way to
		// trigger entry allocation. Then read int64 keys straight from the duplicated point data.
		// Param data: existing entries already have keys; the accessor-keys API stores them as
		// pointers, so we extract pointers and dereference into a flat int64 array.
		TArray<PCGMetadataEntryKey> EntryKeys;
		EntryKeys.SetNumUninitialized(NumRows);
		if (OutPointIO)
		{
			OutPointIO->GetOutKeys(/*bEnsureValidKeys=*/true);
			const TConstPCGValueRange<int64> KeyRange = OutPointIO->GetOut()->GetConstMetadataEntryValueRange();
			for (int32 r = 0; r < NumRows; r++)
			{
				EntryKeys[r] = KeyRange[r];
			}
		}
		else
		{
			TSharedRef<FPCGAttributeAccessorKeysEntries> ParamKeys = MakeShared<FPCGAttributeAccessorKeysEntries>(Metadata);
			TArray<PCGMetadataEntryKey*> KeyPtrs;
			KeyPtrs.SetNumUninitialized(NumRows);
			ParamKeys->GetKeys<PCGMetadataEntryKey>(0, KeyPtrs);
			for (int32 r = 0; r < NumRows; r++)
			{
				EntryKeys[r] = *KeyPtrs[r];
			}
		}

		// Pre-resolve per-row component index. INDEX_NONE = row has no resolved component
		// (unresolved path OR schema-filtered). The writer's provider uses this index to look up
		// the per-component map built in Phase 3c.
		TArray<int32> RowComponentIdx;
		RowComponentIdx.Init(INDEX_NONE, NumRows);
		for (int32 r = 0; r < NumRows; r++)
		{
			const FSlot& Slot = Context->Slots[SlotIndicesForInput[r]];
			if (!Slot.Component)
			{
				continue;
			}
			// ComponentToIdx is built in Phase 3 and never modified afterward; Phase 3b only nulls
			// SLOT-side Component pointers (and the UniqueComponents entry), not the map. FindChecked
			// is safe -- a non-null Slot.Component implies the component survived Phase 3b.
			RowComponentIdx[r] = Context->ComponentToIdx.FindChecked(Slot.Component);
		}

		// Per-row outcome -- consumed by the filter step at the bottom. FailedSchema is set when
		// Phase 3b nulled out Component as a hard drop; Unresolved covers everything else with no
		// component (no path, path didn't resolve, resolved to a non-component object).
		TArray<ESlotOutcome> Outcomes;
		Outcomes.Init(ESlotOutcome::Complete, NumRows);
		for (int32 r = 0; r < NumRows; r++)
		{
			if (RowComponentIdx[r] != INDEX_NONE)
			{
				continue;
			}
			const FSlot& Slot = Context->Slots[SlotIndicesForInput[r]];
			Outcomes[r] = Slot.bSchemaFiltered ? ESlotOutcome::FailedSchema : ESlotOutcome::Unresolved;
		}

		// Set up the shared metadata writer through the provider. The provider's GetPropertyAt
		// override delivers O(1) per-row per-writer lookups via the Phase 3c maps -- without that
		// override the writer would fall back to a linear scan and we'd lose the optimization.
		// CopyValueFrom + WriteMetadataValue handle per-row write through the property's own
		// virtuals (same path FPCGExCollectionPropertySetWriter uses).
		FContextPropertyProvider Provider(Context);
		FPCGExPropertySetWriter Writer;
		Writer.Initialize(Context, &Provider, EffectiveConfigs, Metadata);

		if (Writer.HasOutputs())
		{
			const int32 NumWriters = Writer.Num();
			for (int32 r = 0; r < NumRows; r++)
			{
				const int32 SourceIdx = RowComponentIdx[r];
				if (SourceIdx == INDEX_NONE)
				{
					continue; // unresolved / schema-filtered row -- nothing to write
				}

				const int32 NumWritten = Writer.WriteEntry(EntryKeys[r], SourceIdx);

				// Partial-match detection: any writer that didn't receive a value for this row
				// means the component is missing one of the requested properties. Downgrade once.
				if (NumWritten < NumWriters && Outcomes[r] == ESlotOutcome::Complete)
				{
					Outcomes[r] = ESlotOutcome::Partial;
				}
			}

			// The writer is local to this (parallel) task; sidecars flush single-threaded in the post-pass.
			if (OutSidecarClones)
			{
				Writer.CopySidecarClones(*OutSidecarClones);
			}
		}

		// Row-filter step. FailedSchema rows are always dropped when the user enabled the
		// RequiredSchemas filter (its mere presence is the activation signal). Unresolved /
		// Partial drops are gated on the explicit toggles. AllFound mode never produces Partial
		// outcomes so the UI gates bOmitPartialMatches on Explicit mode.
		const bool bWantOmitUnresolved = Settings->bOmitUnresolvedEntries;
		const bool bWantOmitPartial = Settings->bOmitPartialMatches && Settings->OutputMode == EPCGExPropertyOutputMode::Explicit;
		const bool bWantOmitFailedSchema = !Context->RequiredSchemaSet.IsEmpty();

		if (!bWantOmitUnresolved && !bWantOmitPartial && !bWantOmitFailedSchema)
		{
			return DupData;
		}

		TArray<int8> KeepMask;
		KeepMask.SetNumUninitialized(NumRows);
		bool bAnyDropped = false;
		for (int32 r = 0; r < NumRows; r++)
		{
			const ESlotOutcome Outcome = Outcomes[r];
			const bool bDrop =
				(bWantOmitFailedSchema && Outcome == ESlotOutcome::FailedSchema) ||
				(bWantOmitUnresolved && Outcome == ESlotOutcome::Unresolved) ||
				(bWantOmitPartial && Outcome == ESlotOutcome::Partial);
			KeepMask[r] = bDrop ? 0 : 1;
			if (bDrop)
			{
				bAnyDropped = true;
			}
		}

		if (!bAnyDropped)
		{
			return DupData;
		}

		// Points: in-place Gather on the FPointIO's Out. DupData remains the same pointer; only the
		// underlying point count shrinks.
		if (OutPointIO)
		{
			OutPointIO->Gather(KeepMask);
			return DupData;
		}

		// Param data: rebuild a fresh UPCGParamData with only kept rows. More expensive than the
		// in-place Gather above but unavoidable -- UPCGMetadata has no entry-removal API.
		if (UPCGParamData* ParamData = Cast<UPCGParamData>(DupData))
		{
			if (UPCGParamData* Rebuilt = GatherParamData(Context, ParamData, KeepMask))
			{
				return Rebuilt;
			}
		}

		return DupData;
	}
}

bool FPCGExGetPropertiesDataElement::Boot(FPCGExContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExGetPropertiesDataElement::Boot);

	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_SETTINGS_C(InContext, GetPropertiesData)
	FPCGExGetPropertiesDataContext* Context = static_cast<FPCGExGetPropertiesDataContext*>(InContext);

	PCGEX_VALIDATE_NAME_C(InContext, Settings->SourceAttribute)

	if (Settings->OutputMode == EPCGExPropertyOutputMode::Explicit)
	{
		TArray<FPCGExPropertyOutputConfig> EffectiveConfigs;
		Settings->PropertyOutputSettings.GetEffectiveConfigs(EffectiveConfigs);
		for (const FPCGExPropertyOutputConfig& Config : EffectiveConfigs)
		{
			if (!Config.bEnabled || Config.PropertyName.IsNone())
			{
				continue;
			}
			// Extract to a local before PCGEX_VALIDATE_NAME_C -- the macro stringifies its arg into
			// the error message, so a method-call expression would surface ugly diagnostic text.
			const FName EffectiveName = Config.GetEffectiveOutputName();
			PCGEX_VALIDATE_NAME_C(InContext, EffectiveName)
		}
	}

	// Pre-resolve the required-schema set for O(1) membership checks during Phase 3b.
	Context->RequiredSchemaSet.Reserve(Settings->RequiredSchemas.Num());
	for (const TObjectPtr<UPCGExPropertySchemaAsset>& AssetPtr : Settings->RequiredSchemas)
	{
		if (AssetPtr)
		{
			Context->RequiredSchemaSet.Add(AssetPtr.Get());
		}
	}

	// Phases 1-3c: build slots, resolve, dedupe components, build schemas, apply presence filter,
	// pre-build name lookup tables (per-component + global prototype map). In Boot (not AdvanceWork) so
	// RegisterAssetDependencies can see the resolved schemas. Resolution never loads anything.
	Context->Inputs = InContext->InputData.GetInputsByPin(PCGExGetPropertiesData::SourcesPin);
	if (!Context->Inputs.IsEmpty())
	{
		PCGExGetPropertiesData::ParseInputsIntoSlots(Context, Settings, Context->Inputs);
		PCGExGetPropertiesData::ResolveSlots(Context);
		PCGExGetPropertiesData::BuildUniqueSchemas(Context);
		PCGExGetPropertiesData::ApplyPresenceFilter(Context, Settings->SchemaPresenceMode);
		PCGExGetPropertiesData::BuildSchemaLookups(Context);
	}

	return true;
}

bool FPCGExGetPropertiesDataElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExGetPropertiesDataElement::AdvanceWork);

	PCGEX_SETTINGS_C(InContext, GetPropertiesData)
	FPCGExGetPropertiesDataContext* Context = static_cast<FPCGExGetPropertiesDataContext*>(InContext);

	const TArray<FPCGTaggedData>& Inputs = Context->Inputs;
	if (Inputs.IsEmpty())
	{
		InContext->Done();
		return InContext->TryComplete();
	}

	// Phase 4: build effective output config list (AllFound = union across components, Explicit = settings).
	TArray<FPCGExPropertyOutputConfig> EffectiveConfigs;
	PCGExGetPropertiesData::BuildEffectiveConfigs(Context, Settings, EffectiveConfigs);

	// Bucket slots by input so the per-input write loop is O(NumRowsInInput) per task instead of
	// O(NumSlots) with skip-filtering inside.
	TArray<TArray<int32>> SlotsPerInput;
	SlotsPerInput.SetNum(Inputs.Num());
	for (int32 i = 0; i < Context->Slots.Num(); i++)
	{
		const int32 InputIdx = Context->Slots[i].SourceInputIndex;
		if (Inputs.IsValidIndex(InputIdx))
		{
			SlotsPerInput[InputIdx].Add(i);
		}
	}

	// Pre-size for worst case (one tagged data per input) so the per-input emission below doesn't
	// pay for TArray growth reallocations.
	InContext->OutputData.TaggedData.Reserve(InContext->OutputData.TaggedData.Num() + Inputs.Num());

	// Phase 5: per-input write -- parallel across inputs. Each task touches only its own duplicated
	// data + its own slot range; the per-task result lands in ParallelResults and we compact + stage
	// in a single-threaded post-pass (OutputData.TaggedData isn't safe to grow in parallel).
	//
	// Point inputs hold a TSharedPtr<FPointIO> in the result so the post-pass can stage via the
	// canonical FPointIO::StageOutput path; param inputs go through a direct TaggedData emplace.
	struct FInputResult
	{
		// Data: the UPCGData* to stage. For points this is also PointIO->GetOut(); for param data
		// it's either the duplicate or the rebuilt UPCGParamData after row filtering.
		// Null only when WriteInput returned null (no rows for this input, or duplication failed).
		UPCGData* Data = nullptr;
		// Set iff input was a UPCGBasePointData -- the stage path goes through FPointIO::StageOutput.
		TSharedPtr<PCGExData::FPointIO> PointIO;
		TSet<FString> Tags;
		TArray<FInstancedStruct> SidecarClones;
	};
	const bool bWantsMap = Settings->WantsOutputMap();
	TArray<FInputResult> ParallelResults;
	ParallelResults.SetNum(Inputs.Num());

	PCGExMT::ParallelOrSequential(Inputs.Num(), [&](const int32 InputIdx)
	{
		const FPCGTaggedData& InputTagged = Inputs[InputIdx];
		if (!InputTagged.Data)
		{
			return;
		}

		FInputResult& Result = ParallelResults[InputIdx];
		UPCGData* Out = PCGExGetPropertiesData::WriteInput(
			Context, Settings, EffectiveConfigs, InputTagged.Data, SlotsPerInput[InputIdx], Result.PointIO,
			bWantsMap ? &Result.SidecarClones : nullptr);
		if (!Out)
		{
			Result.PointIO.Reset();
			return;
		}
		Result.Data = Out;
		if (Settings->bForwardInputTags)
		{
			Result.Tags = InputTagged.Tags;
		}
	}, /*Threshold=*/2, EParallelForFlags::Unbalanced);

	for (int32 InputIdx = 0; InputIdx < ParallelResults.Num(); ++InputIdx)
	{
		FInputResult& Result = ParallelResults[InputIdx];
		if (!Result.Data)
		{
			continue;
		}

		// Points: stage via the FPointIO so output-pin assignment + tag attachment go through the
		// canonical path the rest of the codebase uses.
		//
		// bAllowEmptyOutput=true so a fully-filtered input still emits an output (matching the
		// param-data branch below, which always emits whatever GatherParamData returned even when
		// every row was dropped). Keeps downstream pin connectivity stable -- N inputs in, N outputs
		// out, even when some are empty.
		if (Result.PointIO)
		{
			Result.PointIO->bAllowEmptyOutput = true;
			Result.PointIO->SetInfos(InputIdx, PCGExGetPropertiesData::SourcesPin, &Result.Tags);
			Result.PointIO->StageOutput(InContext);
			continue;
		}

		// Param data: direct TaggedData append.
		FPCGTaggedData& OutTagged = InContext->OutputData.TaggedData.Emplace_GetRef();
		OutTagged.Pin = PCGExGetPropertiesData::SourcesPin;
		OutTagged.Data = Result.Data;
		OutTagged.Tags = MoveTemp(Result.Tags);
	}

	// Sidecars: one attribute set per pin across all inputs, staged to that pin.
	if (bWantsMap)
	{
		TArray<FInstancedStruct> Clones;
		for (FInputResult& Result : ParallelResults)
		{
			Clones.Append(MoveTemp(Result.SidecarClones));
		}
		PCGExProperties::StageSidecars(InContext, Clones);
	}

	InContext->Done();
	return InContext->TryComplete();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
