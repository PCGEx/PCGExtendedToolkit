// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExDispatchSubgraph.h"

#include "PCGCommon.h"
#include "PCGExCoreSettingsCache.h"
#include "PCGGraph.h"
#include "PCGPin.h"
#include "PCGSubgraph.h"
#include "Data/PCGUserParametersData.h"
#include "Graph/PCGStackContext.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Data/PCGExPointElements.h"
#include "Helpers/PCGExBulkAttributeHelpers.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Helpers/PCGExPropertyHelpers.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Types/PCGExTypes.h"
#include "StructUtils/PropertyBag.h"

#define LOCTEXT_NAMESPACE "PCGExDispatchSubgraphElement"
#define PCGEX_NAMESPACE DispatchSubgraph

namespace PCGExDispatchSubgraph
{
	// Type-erased per-entry reader for one source attribute on one driver data. Values are read once in
	// their natural type (BulkReadRows); Hash() buckets the group key, SameValue() confirms equality
	// (the hash alone is never trusted as identity), WriteTo() coerces into a param bag.
	struct IOverrideReader
	{
		virtual ~IOverrideReader() = default;
		virtual uint32 Hash(int32 EntryIndex) const = 0;
		virtual bool SameValue(int32 EntryIndex, const IOverrideReader& Other, int32 OtherEntryIndex) const = 0;
		virtual bool WriteTo(int32 EntryIndex, void* BagMemory, const FProperty* Property) const = 0;

		// Value-type discriminator for SameValue; Unknown identifies the generic (Struct/extended) reader.
		virtual EPCGMetadataTypes GetValueType() const = 0;
	};

	template <typename T>
	struct TOverrideReader final : IOverrideReader
	{
		TArray<T> Values;

		virtual EPCGMetadataTypes GetValueType() const override
		{
			return PCGExTypes::TTraits<T>::Type;
		}

		virtual uint32 Hash(const int32 EntryIndex) const override
		{
			return Values.IsValidIndex(EntryIndex) ? PCGExTypes::ComputeHash(Values[EntryIndex]) : 0;
		}

		virtual bool SameValue(const int32 EntryIndex, const IOverrideReader& Other, const int32 OtherEntryIndex) const override
		{
			if (Other.GetValueType() != PCGExTypes::TTraits<T>::Type)
			{
				return false;
			}
			const TOverrideReader& TypedOther = static_cast<const TOverrideReader&>(Other);
			if (!Values.IsValidIndex(EntryIndex) || !TypedOther.Values.IsValidIndex(OtherEntryIndex))
			{
				return false;
			}
			return PCGExTypes::AreEqual(Values[EntryIndex], TypedOther.Values[OtherEntryIndex]);
		}

		virtual bool WriteTo(const int32 EntryIndex, void* BagMemory, const FProperty* Property) const override
		{
			if (!Values.IsValidIndex(EntryIndex))
			{
				return false;
			}

			// Object-typed targets: only assign already-resident assets. Candidate paths are preloaded in
			// PostLoadAssetsDependencies (game thread); a preload miss below would fall through to
			// LoadBlocking_AnyThread and stall dispatch on a per-value game-thread marshal.
			if (Property->IsA<FObjectPropertyBase>())
			{
				T Value = Values[EntryIndex];
				FSoftObjectPath Path;
				PCGExTypeOps::FTypeOpsRegistry::Get<T>()->ConvertTo(&Value, PCGExTypes::TTraits<FSoftObjectPath>::Type, &Path);
				if (Path.IsNull() || !Path.ResolveObject())
				{
					return false;
				}
			}

			// Engine property accessor with broadcast+construct first, PCGEx TrySetFPropertyValue fallback
			// for targets the accessor doesn't support (e.g. object properties driven by a soft path).
			return PCGExPropertyHelpers::TrySetFPropertyValueCoerced<T>(BagMemory, Property, Values[EntryIndex]);
		}
	};

	// Reader for Struct / extended attribute types (e.g. the Struct attributes emitted by the Tuple
	// node, on points OR param). Reads and writes through the engine's generic accessor: the write is a
	// same-struct copy into the target parameter; the group hash uses PCG's per-value key.
	struct FGenericOverrideReader final : IOverrideReader
	{
		TUniquePtr<const IPCGAttributeAccessor> SrcAccessor;
		TSharedPtr<IPCGAttributeAccessorKeys> SrcKeys;
		const FPCGMetadataAttributeBase* Attr = nullptr;
		const UPCGBasePointData* PointData = nullptr; // non-null when the driver is point data
		int32 DriverIndex = 0;

		virtual EPCGMetadataTypes GetValueType() const override
		{
			return EPCGMetadataTypes::Unknown;
		}

		PCGMetadataValueKey GetValueKeyAt(const int32 EntryIndex) const
		{
			const PCGMetadataEntryKey EntryKey = PointData ? PointData->GetMetadataEntry(EntryIndex) : static_cast<PCGMetadataEntryKey>(EntryIndex);
			return Attr->GetValueKey(EntryKey);
		}

		virtual uint32 Hash(const int32 EntryIndex) const override
		{
			// PCG value keys are unique per distinct value but only WITHIN one attribute instance, so we
			// fold the driver index in. Consequence: Struct/extended-valued dedup is PER DRIVER DATA --
			// identical values arriving on two separate driver inputs dispatch separately (an
			// over-dispatch, never a wrong result). [DOC: surface this in the user-facing docs.]
			return HashCombineFast(static_cast<uint32>(DriverIndex), static_cast<uint32>(GetValueKeyAt(EntryIndex)));
		}

		virtual bool SameValue(const int32 EntryIndex, const IOverrideReader& Other, const int32 OtherEntryIndex) const override
		{
			if (Other.GetValueType() != EPCGMetadataTypes::Unknown)
			{
				return false;
			}
			const FGenericOverrideReader& GenericOther = static_cast<const FGenericOverrideReader&>(Other);

			// Value keys are only comparable within one attribute instance (see Hash).
			if (Attr != GenericOther.Attr || DriverIndex != GenericOther.DriverIndex)
			{
				return false;
			}
			return GetValueKeyAt(EntryIndex) == GenericOther.GetValueKeyAt(OtherEntryIndex);
		}

		virtual bool WriteTo(const int32 EntryIndex, void* BagMemory, const FProperty* Property) const override
		{
			if (!SrcAccessor.IsValid() || !SrcKeys.IsValid())
			{
				return false;
			}

			// bUseGenericAccessor=true builds an FPCGPropertyGenericAccessor, which handles arbitrary
			// structs (and other extended types) that the typed accessor / TrySetFPropertyValue can't.
			const TUniquePtr<IPCGAttributeAccessor> TargetAccessor = PCGAttributeAccessorHelpers::CreatePropertyAccessor(Property, /*bUseGenericAccessor=*/true);
			if (!TargetAccessor)
			{
				return false;
			}

			void* Containers[1] = {BagMemory};
			FPCGAttributeAccessorKeysGenericPtrs TargetKeys(Containers);
			return SrcAccessor->CopyTo(*SrcKeys, *TargetAccessor, TargetKeys, /*InputIndex=*/EntryIndex, /*OutputIndex=*/0, /*Count=*/1, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);
		}
	};

	// Builds a generic reader for a Struct/extended attribute, or null if the engine cannot build a
	// const accessor for it.
	TSharedPtr<IOverrideReader> MakeGenericOverrideReader(const UPCGData* InData, const FName InSourceAttr, const FPCGMetadataAttributeBase* InAttr, const int32 InDriverIndex, const TSharedPtr<IPCGAttributeAccessorKeys>& InKeys)
	{
		FPCGAttributePropertyInputSelector Selector;
		Selector.Update(InSourceAttr.ToString());
		Selector = Selector.CopyAndFixLast(InData);

		TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(InData, Selector);
		if (!Accessor || !InKeys)
		{
			return nullptr;
		}

		TSharedPtr<FGenericOverrideReader> Reader = MakeShared<FGenericOverrideReader>();
		Reader->SrcAccessor = MoveTemp(Accessor);
		Reader->SrcKeys = InKeys;
		Reader->Attr = InAttr;
		Reader->PointData = Cast<UPCGBasePointData>(InData);
		Reader->DriverIndex = InDriverIndex;
		return Reader;
	}

	// Builds a reader for InSourceAttr on InData, or null if the attribute is missing. Basic single-value
	// types use the fast typed path (content hash, cross-data dedup); Struct/extended types fall back to
	// the generic accessor path (per-driver-data dedup -- see FGenericOverrideReader).
	// InKeys is the shared per-driver accessor keys (built once, reused across every reader on that data).
	TSharedPtr<IOverrideReader> MakeOverrideReader(const UPCGData* InData, const FName InSourceAttr, const int32 InDriverIndex, const TSharedPtr<IPCGAttributeAccessorKeys>& InKeys)
	{
		if (!InData)
		{
			return nullptr;
		}

		TSharedPtr<IOverrideReader> Result;
		auto MakeTyped = [&](auto Dummy)
		{
			using T = decltype(Dummy);
			TSharedPtr<TOverrideReader<T>> Reader = MakeShared<TOverrideReader<T>>();
			PCGExData::Helpers::BulkReadRows<T>(InData, InSourceAttr, Reader->Values, InKeys);
			if (!Reader->Values.IsEmpty())
			{
				Result = Reader;
			}
		};

		FPCGAttributePropertyInputSelector Selector;
		Selector.Update(InSourceAttr.ToString());
		Selector = Selector.CopyAndFixLast(InData);

		// Point / extra property source ($Transform, $Density, $Index, ...). The value is read into a
		// concrete type resolved from the accessor (robust for sub-selectors like $Transform.X) and hashed
		// by content, so property-valued dedup is cross-data. Properties only exist on point data; on data
		// that lacks the property (e.g. param) no accessor is built and the override is skipped.
		if (Selector.GetSelection() != EPCGAttributePropertySelection::Attribute)
		{
			const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(InData, Selector);
			if (!Accessor)
			{
				return nullptr;
			}

			PCGExMetaHelpers::ExecuteWithRightType(Accessor->GetUnderlyingType(), MakeTyped);
			return Result;
		}

		// Attribute source.
		const UPCGMetadata* Metadata = InData->ConstMetadata();
		if (!Metadata)
		{
			return nullptr;
		}

		const FPCGMetadataAttributeBase* Attr = Metadata->GetConstAttribute(InSourceAttr);
		if (!Attr)
		{
			return nullptr;
		}

		// Basic single-value types take the typed branch; Struct/extended fall back to the generic reader.
		PCGExMetaHelpers::ExecuteWithRightType(
			Attr, MakeTyped,
			[&]()
			{
				Result = MakeGenericOverrideReader(InData, InSourceAttr, Attr, InDriverIndex, InKeys);
			});

		return Result;
	}

	// One override applied to a (graph, driver data) pair: a target param that exists on the graph, fed
	// by a source that resolved to a reader on the driver data. Lists are sorted by param name so group
	// comparison is order-stable.
	struct FApplied
	{
		FName Param;
		const FPropertyBagPropertyDesc* Desc = nullptr;
		TSharedPtr<IOverrideReader> Reader;
	};

	// One unique (graph + override values) combination, pending scheduling. First contributing entry
	// wins: its values feed the param writes and its tags stamp the dispatch outputs.
	struct FGroup
	{
		const UPCGGraphInterface* Interface = nullptr;
		UPCGGraph* Graph = nullptr;
		int32 DriverIndex = INDEX_NONE;
		int32 EntryIndex = INDEX_NONE;
		TSharedPtr<TArray<FApplied>> Applied;
		TSet<FString> Tags;
	};

	// Exact group identity: same graph interface, same applied param list, and every applied override
	// value equal (reader-confirmed). Never trusts the bucket hash.
	bool SameGroup(const FGroup& Group, const UPCGGraphInterface* Interface, const TArray<FApplied>& Applied, const int32 EntryIndex)
	{
		if (Group.Interface != Interface || Group.Applied->Num() != Applied.Num())
		{
			return false;
		}

		const TArray<FApplied>& GroupApplied = *Group.Applied;
		for (int32 i = 0; i < Applied.Num(); i++)
		{
			if (Applied[i].Param != GroupApplied[i].Param)
			{
				return false;
			}
			if (!Applied[i].Reader->SameValue(EntryIndex, *GroupApplied[i].Reader, Group.EntryIndex))
			{
				return false;
			}
		}
		return true;
	}

	// True if Graph declares a Required input pin whose label is not among DeclaredInputLabels -- i.e. the
	// dispatcher (Model C forwards only the declared custom input pins) would leave a required input unfed.
	bool GraphHasUndeclaredRequiredInput(const UPCGGraph* Graph, const TSet<FName>& DeclaredInputLabels)
	{
		const UPCGNode* InputNode = Graph ? Graph->GetInputNode() : nullptr;
		if (!InputNode)
		{
			return false;
		}

		for (const FPCGPinProperties& Pin : InputNode->InputPinProperties())
		{
			if (Pin.IsRequiredPin() && !DeclaredInputLabels.Contains(Pin.Label))
			{
				return true;
			}
		}
		return false;
	}
}

#pragma region Settings

#if WITH_EDITOR
FLinearColor UPCGExDispatchSubgraphSettings::GetNodeTitleColor() const
{
	return PCGEX_NODE_COLOR_OPTIN_NAME(Action);
}
#endif

TArray<FPCGPinProperties> UPCGExDispatchSubgraphSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	// Points-or-param via Any (a literal Point|Param union renders gray); Boot ignores unusable rows.
	PCGEX_PIN_ANY(DriversPinLabel, TEXT("Points or attribute sets carrying the subgraph reference and override sources. One dispatch per unique (graph + overrides)."), Required)
	PinProperties.Append(CustomInputPins);
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExDispatchSubgraphSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Append(CustomOutputPins);
	PCGEX_PIN_ANY(OutputPinLabel, TEXT("Subgraph outputs that did not match a custom output pin by name."), Normal)
	return PinProperties;
}

FPCGElementPtr UPCGExDispatchSubgraphSettings::CreateElement() const
{
	return MakeShared<FPCGExDispatchSubgraphElement>();
}

#pragma endregion

#pragma region Element

void FPCGExDispatchSubgraphContext::RegisterAssetDependencies()
{
	PCGEX_SETTINGS_LOCAL(DispatchSubgraph)

	const TArray<FPCGTaggedData> Drivers = InputData.GetInputsByPin(Settings->DriversPinLabel);
	DriverPaths.Reserve(Drivers.Num());

	for (const FPCGTaggedData& TaggedData : Drivers)
	{
		TArray<FSoftObjectPath>& Paths = DriverPaths.Emplace_GetRef();
		if (!TaggedData.Data)
		{
			continue;
		}

		PCGExData::Helpers::BulkReadSoftPaths(TaggedData.Data, Settings->GraphPathAttribute, Paths);

		for (const FSoftObjectPath& Path : Paths)
		{
			if (Path.IsNull())
			{
				continue;
			} // empty reference -> ignored, no warning

			bool bAlreadyRegistered = false;
			UniqueGraphPaths.Add(Path, &bAlreadyRegistered);
			if (!bAlreadyRegistered)
			{
				AddAssetDependency(Path);
			}
		}
	}

	FPCGExContext::RegisterAssetDependencies();
}

void FPCGExDispatchSubgraphContext::AddToReferencedObjects(const FPCGDataCollection& InCollection)
{
	for (const FPCGTaggedData& TaggedData : InCollection.TaggedData)
	{
		if (TaggedData.Data)
		{
			ReferencedObjects.Add(TaggedData.Data);
		}
	}
}

void FPCGExDispatchSubgraphContext::AddExtraStructReferencedObjects(FReferenceCollector& Collector)
{
	FPCGExContext::AddExtraStructReferencedObjects(Collector);
	Collector.AddReferencedObjects(ReferencedObjects);
}

bool FPCGExDispatchSubgraphElement::Boot(FPCGExContext* InContext) const
{
	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(DispatchSubgraph)
	PCGEX_VALIDATE_NAME(Settings->GraphPathAttribute)
	PCGEX_VALIDATE_NAME(Settings->DriversPinLabel)
	PCGEX_VALIDATE_NAME(Settings->OutputPinLabel)

	return true;
}

void FPCGExDispatchSubgraphElement::PostLoadAssetsDependencies(FPCGExContext* InContext) const
{
	PCGEX_CONTEXT_AND_SETTINGS(DispatchSubgraph)

	// Runs on the game thread: PCGEX_ELEMENT_MAIN_THREAD_ONLY_IN_PREPARE gates the whole PrepareData
	// phase (which hosts the preparation state machine) to the main thread.

	Context->ResolvedGraphs.Reserve(Context->UniqueGraphPaths.Num());
	for (const FSoftObjectPath& Path : Context->UniqueGraphPaths)
	{
		// Interface-typed resolve accepts both graphs and graph instances (an instance's bag carries its
		// parameter presets, which the dispatch then overrides per entry).
		UPCGGraphInterface* Interface = TSoftObjectPtr<UPCGGraphInterface>(Path).Get();
		if (Interface && !Interface->GetGraph())
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::FromString(TEXT("Graph instance has no graph set : ") + Path.ToString()));
			Interface = nullptr;
		}
		else if (!Interface)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::FromString(TEXT("Subgraph could not be loaded (missing, or not a PCG graph / graph instance) : ") + Path.ToString()));
		}

		Context->ResolvedGraphs.Add(Path, Interface);

#if WITH_EDITOR
		// Dynamic tracking: the dispatched graphs are attribute-resolved, invisible to static tracking.
		// Track the reference itself (regen when the asset changes) and the graph's own tracked-selection
		// keys (regen when actors/landscape its nodes depend on change). Gated by CanDynamicallyTrackKeys.
		Context->EDITOR_TrackPath(Path);
		if (Interface && Context->ExecutionSource.IsValid())
		{
			if (const UPCGGraph* Graph = Interface->GetGraph())
			{
				Context->ExecutionSource->GetExecutionState().RegisterDynamicTracking(Graph->GetTrackedSelectionKeysToSettings());
			}
		}
#endif
	}

	// Preload assets referenced by override values that target object-typed user parameters, so the
	// override writes (worker thread, Execute phase) only ever assign already-resident objects.
	TSet<FName> ObjectParams;
	for (const TPair<FSoftObjectPath, UPCGGraphInterface*>& Pair : Context->ResolvedGraphs)
	{
		if (!Pair.Value)
		{
			continue;
		}
		const FInstancedPropertyBag* Bag = Pair.Value->GetUserParametersStruct();
		if (!Bag || !Bag->GetPropertyBagStruct())
		{
			continue;
		}
		for (const FPropertyBagPropertyDesc& Desc : Bag->GetPropertyBagStruct()->GetPropertyDescs())
		{
			if (Desc.CachedProperty && Desc.CachedProperty->IsA<FObjectPropertyBase>())
			{
				ObjectParams.Add(Desc.Name);
			}
		}
	}

	if (ObjectParams.IsEmpty())
	{
		return;
	}

	TSharedPtr<TSet<FSoftObjectPath>> ObjectPaths = MakeShared<TSet<FSoftObjectPath>>();
	const TArray<FPCGTaggedData> Drivers = Context->InputData.GetInputsByPin(Settings->DriversPinLabel);
	for (const FPCGTaggedData& TaggedData : Drivers)
	{
		if (!TaggedData.Data)
		{
			continue;
		}

		// Sources that can feed an object-typed param on this driver: remapped sources first, then
		// auto-matched attribute names. Non-path-typed sources simply read as empty paths.
		TSet<FName> Sources;
		for (const TPair<FName, FName>& Remap : Settings->OverrideRemap)
		{
			if (ObjectParams.Contains(Remap.Value))
			{
				Sources.Add(Remap.Key);
			}
		}
		if (Settings->bAutoMatchByName && TaggedData.Data->ConstMetadata())
		{
			TArray<FName> AttrNames;
			TArray<EPCGMetadataTypes> AttrTypes;
			TaggedData.Data->ConstMetadata()->GetAttributes(AttrNames, AttrTypes);
			for (const FName AttrName : AttrNames)
			{
				if (ObjectParams.Contains(AttrName))
				{
					Sources.Add(AttrName);
				}
			}
		}

		for (const FName Source : Sources)
		{
			TArray<FSoftObjectPath> Values;
			PCGExData::Helpers::BulkReadSoftPaths(TaggedData.Data, Source, Values);
			for (const FSoftObjectPath& Value : Values)
			{
				if (!Value.IsNull())
				{
					ObjectPaths->Add(Value);
				}
			}
		}
	}

	if (!ObjectPaths->IsEmpty())
	{
		PCGExHelpers::LoadBlocking_AnyThread(ObjectPaths, Context);
	}
}

bool FPCGExDispatchSubgraphElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExDispatchSubgraphElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(DispatchSubgraph)
	PCGEX_EXECUTION_CHECK

	if (!Context->bDispatched)
	{
		Context->bDispatched = true;

		// Schedule the dynamic subgraphs. If any were scheduled, park until they complete: the PCG
		// executor clears bIsPaused and re-ticks us once every DynamicDependencies task is done.
		// (SupportsDetachedExecute is false: this must run inside ExecuteInternal's synchronous call
		// chain or the executor never sees the dependencies.)
		if (ScheduleDispatches(Context, Settings))
		{
			Context->bIsPaused = true;
			return false;
		}
	}
	else
	{
		GatherDispatchOutputs(Context, Settings);
	}

	Context->Done();
	return Context->TryComplete();
}

bool FPCGExDispatchSubgraphElement::ScheduleDispatches(FPCGExDispatchSubgraphContext* Context, const UPCGExDispatchSubgraphSettings* Settings) const
{
	using namespace PCGExDispatchSubgraph;

	if (!Context->ExecutionSource.IsValid())
	{
		PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("No execution source is available; cannot dispatch subgraphs."));
		return false;
	}

	const TArray<FPCGTaggedData> Drivers = Context->InputData.GetInputsByPin(Settings->DriversPinLabel);

	const FPCGStack* Stack = Context->GetStack();
	const UPCGGraph* CurrentGraph = Stack ? Stack->GetGraphForCurrentFrame() : nullptr;
	// Engine-parity fallback (PCGSubgraph.cpp): a null stack (e.g. behind a Proxy node) must not fail
	// the recursion guard open -- fall back to the execution source's graph.
	const UPCGGraph* SourceGraph = Stack ? nullptr : Context->ExecutionSource->GetExecutionState().GetGraph();

	// Custom input pin labels the user declared. A graph whose Required inputs aren't all covered is
	// skipped. A custom pin colliding with the Drivers label is excluded: it is never forwarded (the
	// node dedupes the pin away), so it cannot satisfy a required input either.
	TSet<FName> DeclaredInputLabels;
	DeclaredInputLabels.Reserve(Settings->CustomInputPins.Num());
	for (const FPCGPinProperties& Pin : Settings->CustomInputPins)
	{
		if (Pin.Label != Settings->DriversPinLabel)
		{
			DeclaredInputLabels.Add(Pin.Label);
		}
	}

	const bool bWantsTags = Settings->AttributesToTags.bAddIndexTag
		|| !Settings->AttributesToTags.AttributeMappings.IsEmpty()
		|| !Settings->AttributesToTags.CommaSeparatedAttributeSelectors.IsEmpty();

	// Phase 1 -- resolve unique (graph + override values) groups across all drivers.

	TArray<FGroup> Groups;
	TMap<uint32, TArray<int32, TInlineAllocator<1>>> GroupBuckets; // hash -> group indices; SameGroup confirms
	TMap<const UPCGGraph*, bool> GraphValidity;                    // recursion + required-input, checked & warned once per graph

	for (int32 DriverIndex = 0; DriverIndex < Drivers.Num(); ++DriverIndex)
	{
		const UPCGData* Data = Drivers[DriverIndex].Data;
		if (!Data || !Context->DriverPaths.IsValidIndex(DriverIndex))
		{
			continue;
		}

		const TArray<FSoftObjectPath>& Paths = Context->DriverPaths[DriverIndex];

		// Shared accessor keys for every read on this driver data (FPCGAttributeAccessorKeysEntries
		// walks all metadata entries on construction -- build it once).
		const TSharedPtr<IPCGAttributeAccessorKeys> DriverKeys = PCGExData::Helpers::GetKeys(Data);

		// Driver attribute names, for auto-match-by-name.
		TArray<FName> DriverAttrNames;
		if (Settings->bAutoMatchByName && Data->ConstMetadata())
		{
			TArray<EPCGMetadataTypes> DriverAttrTypes;
			Data->ConstMetadata()->GetAttributes(DriverAttrNames, DriverAttrTypes);
		}

		// Lazily built, cached per-attribute readers on this driver data.
		TMap<FName, TSharedPtr<IOverrideReader>> Readers;
		auto GetReader = [&](const FName Attr) -> TSharedPtr<IOverrideReader>
		{
			if (const TSharedPtr<IOverrideReader>* Found = Readers.Find(Attr))
			{
				return *Found;
			}
			TSharedPtr<IOverrideReader> Reader = MakeOverrideReader(Data, Attr, DriverIndex, DriverKeys);
			Readers.Add(Attr, Reader);
			return Reader;
		};

		// Applied-override lists depend only on (graph interface, driver data) -- resolve once per pair.
		TMap<const UPCGGraphInterface*, TSharedPtr<TArray<FApplied>>> AppliedCache;
		auto GetApplied = [&](const UPCGGraphInterface* Interface) -> TSharedPtr<TArray<FApplied>>
		{
			if (const TSharedPtr<TArray<FApplied>>* Found = AppliedCache.Find(Interface))
			{
				return *Found;
			}

			TSharedPtr<TArray<FApplied>> Applied = MakeShared<TArray<FApplied>>();
			AppliedCache.Add(Interface, Applied);

			const FInstancedPropertyBag* GraphBag = Interface->GetUserParametersStruct();
			if (GraphBag)
			{
				TSet<FName> AppliedParams;
				auto TryAddOverride = [&](const FName Param, const FName Src)
				{
					if (AppliedParams.Contains(Param))
					{
						return;
					}
					const FPropertyBagPropertyDesc* Desc = GraphBag->FindPropertyDescByName(Param);
					if (!Desc || !Desc->CachedProperty)
					{
						return;
					}
					TSharedPtr<IOverrideReader> Reader = GetReader(Src);
					if (!Reader)
					{
						return;
					}
					AppliedParams.Add(Param);
					Applied->Add({Param, Desc, Reader});
				};

				// Remap wins over auto-match.
				for (const TPair<FName, FName>& Remap : Settings->OverrideRemap)
				{
					TryAddOverride(Remap.Value, Remap.Key);
				}
				if (Settings->bAutoMatchByName)
				{
					for (const FName Attr : DriverAttrNames)
					{
						TryAddOverride(Attr, Attr);
					}
				}

				Applied->Sort([](const FApplied& A, const FApplied& B)
				{
					return A.Param.LexicalLess(B.Param);
				});
			}

			return Applied;
		};

		// Driver tags, resolved against this driver data (raw-data init: points or attribute sets).
		FPCGExAttributeToTagDetails AttributesToTagsDetails;
		bool bTagsReady = false;
		if (bWantsTags)
		{
			AttributesToTagsDetails = Settings->AttributesToTags;
			bTagsReady = AttributesToTagsDetails.Init(Context, Data);
		}

		for (int32 EntryIndex = 0; EntryIndex < Paths.Num(); ++EntryIndex)
		{
			const FSoftObjectPath& Path = Paths[EntryIndex];
			if (Path.IsNull())
			{
				continue;
			}

			const UPCGGraphInterface* Interface = Context->ResolvedGraphs.FindRef(Path);
			if (!Interface)
			{
				continue;
			}

			UPCGGraph* Graph = const_cast<UPCGGraph*>(Interface->GetGraph());

			// Validate once per underlying graph (warn once), regardless of how many entries or
			// interfaces reference it: recursion guard, then required-input guard.
			bool bValidGraph;
			if (const bool* Known = GraphValidity.Find(Graph))
			{
				bValidGraph = *Known;
			}
			else
			{
				bValidGraph = true;

				// Never dispatch a graph that is, or contains, the graph currently executing.
				if ((Stack && Stack->HasObject(Graph)) || (CurrentGraph && Graph->Contains(CurrentGraph)) ||
					(SourceGraph && (Graph == SourceGraph || Graph->Contains(SourceGraph))))
				{
					bValidGraph = false;
					PCGE_LOG_C(Warning, GraphAndLog, Context, FText::FromString(TEXT("Skipped a recursive subgraph reference : ") + Graph->GetName()));
				}
				// A graph with a Required input pin not declared in Custom Input Pins would run with that
				// input unfed, so skip it rather than execute it starved.
				else if (GraphHasUndeclaredRequiredInput(Graph, DeclaredInputLabels))
				{
					bValidGraph = false;
					PCGE_LOG_C(Warning, GraphAndLog, Context, FText::FromString(TEXT("Skipped subgraph with an undeclared required input pin : ") + Graph->GetName()));
				}

				GraphValidity.Add(Graph, bValidGraph);
			}

			if (!bValidGraph)
			{
				continue;
			}

			const TSharedPtr<TArray<FApplied>> Applied = GetApplied(Interface);

			// Group bucket = graph identity + each applied (param name, per-entry source value).
			uint32 Key = GetTypeHash(Interface);
			for (const FApplied& A : *Applied)
			{
				Key = HashCombineFast(Key, GetTypeHash(A.Param));
				Key = HashCombineFast(Key, A.Reader->Hash(EntryIndex));
			}

			// Exact dedup: the hash only buckets, SameGroup confirms value equality -- a hash collision
			// can never silently swallow a distinct dispatch.
			TArray<int32, TInlineAllocator<1>>& Bucket = GroupBuckets.FindOrAdd(Key);
			bool bAlreadyGrouped = false;
			for (const int32 GroupIndex : Bucket)
			{
				if (SameGroup(Groups[GroupIndex], Interface, *Applied, EntryIndex))
				{
					bAlreadyGrouped = true;
					break;
				}
			}
			if (bAlreadyGrouped)
			{
				continue;
			}

			FGroup& Group = Groups.Emplace_GetRef();
			Group.Interface = Interface;
			Group.Graph = Graph;
			Group.DriverIndex = DriverIndex;
			Group.EntryIndex = EntryIndex;
			Group.Applied = Applied;
			if (bTagsReady)
			{
				AttributesToTagsDetails.Tag(PCGExData::FElement(EntryIndex, DriverIndex), Group.Tags);
			}

			Bucket.Add(Groups.Num() - 1);
		}
	}

	if (Groups.IsEmpty())
	{
		return false;
	}

	// Phase 2 -- schedule one dynamic subgraph per group.

	// Forwarded input is shared across dispatches: mark it multi-use so in-subgraph steal paths never
	// mutate data another dispatch (or the parent graph) still references (engine parity: FPCGLoopElement).
	const bool bMarkUsedMultipleTimes = Groups.Num() > 1;
	TMap<const UPCGGraph*, FPCGDataCollection> DispatchInputCache; // depends only on the graph's input pins
	int32 LoopIndex = 0;

	for (FGroup& Group : Groups)
	{
		// Pre-graph user parameters: the interface's defaults (instance presets included) with this
		// entry's overrides written in -- a single struct copy, edited in place.
		FPCGDataCollection PreGraphData;
		{
			UPCGUserParametersData* UserParamData = FPCGContext::NewObject_AnyThread<UPCGUserParametersData>(Context);
			if (const FInstancedPropertyBag* GraphBag = Group.Interface->GetUserParametersStruct();
				GraphBag && GraphBag->GetPropertyBagStruct())
			{
				UserParamData->UserParameters.InitializeAs(GraphBag->GetPropertyBagStruct(), GraphBag->GetValue().GetMemory());
				void* BagMemory = UserParamData->UserParameters.GetMutableMemory();
				for (const FApplied& A : *Group.Applied)
				{
					A.Reader->WriteTo(Group.EntryIndex, BagMemory, A.Desc->CachedProperty);
				}
			}

			FPCGTaggedData& Tagged = PreGraphData.TaggedData.Emplace_GetRef();
			Tagged.Data = UserParamData;
			Tagged.Tags.Add(PCG::Private::UserParameterTagData);
			Tagged.bPinlessData = true;
		}
		Context->AddToReferencedObjects(PreGraphData);

		const FPCGDataCollection* DispatchInput = DispatchInputCache.Find(Group.Graph);
		if (!DispatchInput)
		{
			FPCGDataCollection& NewInput = DispatchInputCache.Add(Group.Graph);
			BuildDispatchInput(Context, Settings, Group.Graph, NewInput, bMarkUsedMultipleTimes);
			Context->AddToReferencedObjects(NewInput);
			DispatchInput = &NewInput;
		}

		// Distinct invocation stack per dispatch (this node + a not-a-loop index), so the executor
		// keeps each scheduled subgraph's tasks/outputs distinct.
		FPCGStack InvocationStack = Stack ? *Stack : FPCGStack();
		InvocationStack.GetStackFramesMutable().Emplace(Context->Node);
		InvocationStack.GetStackFramesMutable().Emplace(LoopIndex++);

		const FPCGTaskId TaskId = Context->ScheduleGraph(FPCGScheduleGraphParams(
			Group.Graph,
			Context->ExecutionSource.Get(),
			MakeShared<FPCGInputForwardingElement>(PreGraphData),
			MakeShared<FPCGInputForwardingElement>(*DispatchInput),
			/*Dependencies=*/{},
			&InvocationStack,
			/*bAllowHierarchicalGeneration=*/false));

		if (TaskId == InvalidPCGTaskId)
		{
			continue;
		}

		FPCGExDispatchSubgraphContext::FDispatch& Dispatch = Context->Dispatches.Emplace_GetRef();
		Dispatch.TaskId = TaskId;
		Dispatch.Tags = MoveTemp(Group.Tags);
		Context->DynamicDependencies.Add(TaskId);
	}

	return !Context->Dispatches.IsEmpty();
}

void FPCGExDispatchSubgraphElement::GatherDispatchOutputs(FPCGExDispatchSubgraphContext* Context, const UPCGExDispatchSubgraphSettings* Settings) const
{
	TSet<FName> CustomOutputLabels;
	CustomOutputLabels.Reserve(Settings->CustomOutputPins.Num());
	for (const FPCGPinProperties& Pin : Settings->CustomOutputPins)
	{
		CustomOutputLabels.Add(Pin.Label);
	}

	for (const FPCGExDispatchSubgraphContext::FDispatch& Dispatch : Context->Dispatches)
	{
		FPCGDataCollection SubgraphOutput;
		if (!Context->GetOutputData(Dispatch.TaskId, SubgraphOutput))
		{
			continue;
		}

		// Root the gathered data before clearing the executor's copy: StageOutput(None) records it for
		// the OnComplete flush but does not GC-root it, and ClearOutputData releases the executor's hold.
		Context->AddToReferencedObjects(SubgraphOutput);
		Context->IncreaseStagedOutputReserve(SubgraphOutput.TaggedData.Num());

		for (const FPCGTaggedData& TaggedData : SubgraphOutput.TaggedData)
		{
			if (!TaggedData.Data)
			{
				continue;
			}
			const FName Pin = CustomOutputLabels.Contains(TaggedData.Pin) ? TaggedData.Pin : Settings->OutputPinLabel;

			if (Dispatch.Tags.IsEmpty())
			{
				Context->StageOutput(const_cast<UPCGData*>(TaggedData.Data.Get()), Pin, PCGExData::EStaging::None, TaggedData.Tags);
			}
			else
			{
				TSet<FString> Tags = TaggedData.Tags;
				Tags.Append(Dispatch.Tags);
				Context->StageOutput(const_cast<UPCGData*>(TaggedData.Data.Get()), Pin, PCGExData::EStaging::None, Tags);
			}
		}

		Context->ClearOutputData(Dispatch.TaskId);
	}
}

void FPCGExDispatchSubgraphElement::BuildDispatchInput(FPCGExDispatchSubgraphContext* Context, const UPCGExDispatchSubgraphSettings* Settings, const UPCGGraph* Graph, FPCGDataCollection& OutData, const bool bMarkUsedMultipleTimes) const
{
	// Model C: forward each declared custom input pin whose label matches one of the graph's input pins.
	// The Drivers pin is never forwarded -- including through a custom pin that shadows its label.
	const UPCGNode* InputNode = Graph->GetInputNode();
	if (!InputNode)
	{
		return;
	}

	TSet<FName> GraphInputLabels;
	for (const FPCGPinProperties& Pin : InputNode->InputPinProperties())
	{
		GraphInputLabels.Add(Pin.Label);
	}

	for (const FPCGPinProperties& CustomPin : Settings->CustomInputPins)
	{
		if (CustomPin.Label == Settings->DriversPinLabel || !GraphInputLabels.Contains(CustomPin.Label))
		{
			continue;
		}
		OutData.TaggedData.Append(Context->InputData.GetInputsByPin(CustomPin.Label));
	}

	if (bMarkUsedMultipleTimes)
	{
		// The same data objects feed every dispatch; the flag can only be repaired by the compiler within
		// one compiled graph, so cross-scheduled-graph sharing must be declared here (see FPCGLoopElement).
		for (FPCGTaggedData& TaggedData : OutData.TaggedData)
		{
			TaggedData.bIsUsedMultipleTimes = true;
		}
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
