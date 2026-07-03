// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExGetCollectionData.h"

#include "PCGGraph.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "Collections/PCGExActorCollection.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExDataHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Metadata/Accessors/PCGCustomAccessor.h"
#include "Elements/Grammar/PCGSubdivisionBase.h"
#include "Helpers/PCGExCollectionPropertySetWriter.h"
#include "Helpers/PCGExGetCollectionDataFlatten.h"
#include "Helpers/PCGExMetaHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExGetCollectionData"
#define PCGEX_NAMESPACE GetCollectionData

// Per-attribute tables driving validation, FUniqueOutput field declarations, attribute creation,
// and the write loop. Row form:
//   _(Type, FieldName, AttrNameMember, ToggleMember, Default, PerEntryValueExpr)
//
// FieldName drives the FUniqueOutput member name via FieldName##Attr.
// PerEntryValueExpr is evaluated inside the write loop where `E` (entry) / `Module` (subdivision
// submodule) are in scope -- LEAF rows reference E, GRAMMAR rows reference Module.
//
// Attributes that don't fit the uniform shape are kept bespoke:
//   - AssetPath/AssetClass: one toggle declares two attributes (FSoftObjectPath/FSoftClassPath)
//   - Weight:               int32 vs float depending on normalization mode
//   - Category:             written always, not gated on bIsSubCollection
//   - Entry:                always created, value derived from Packer
#define PCGEX_GCD_LEAF_ATTRS(_) \
	_(FVector, Extents,      ExtentsAttributeName,      bWriteExtents,      FVector::OneVector, E->Staging.Bounds.GetExtent()) \
	_(FVector, BoundsMin,    BoundsMinAttributeName,    bWriteBoundsMin,    FVector::OneVector, E->Staging.Bounds.Min) \
	_(FVector, BoundsMax,    BoundsMaxAttributeName,    bWriteBoundsMax,    FVector::OneVector, E->Staging.Bounds.Max) \
	_(int32,   NestingDepth, NestingDepthAttributeName, bWriteNestingDepth, -1,                 EH.NestingDepth)

// Per-row attributes (written for EVERY row, including sub-collection placeholders and empty rows).
//   _(Type, FieldName, AttrNameMember, ToggleMember, Default, PerEntryValueExpr)
// PerEntryValueExpr references `EH` (the FFlattenedEntry), which always exists even when EH.Entry
// is null -- that's why these live outside the leaf-only write block.
#define PCGEX_GCD_ROW_ATTRS(_) \
	_(int32, RootCollectionIndex, RootCollectionIndexAttributeName, bWriteRootCollectionIndex, -1, EH.RootCollectionIndex) \
	_(int32, CollectionIndex,     CollectionIndexAttributeName,     bWriteCollectionIndex,     -1, EH.CollectionIndex) \
	_(int32, CollectionHash,      CollectionHashAttributeName,      bWriteCollectionHash,      -1, EH.CollectionHash)

// Per-axis grammar attributes. Each row maps to an array-of-3 in FUniqueOutput (one slot per
// axis bit), with attribute names produced by appending _X/_Y/_Z to the user-configured base name.
//   _(Type, FieldName, AttrNameMember, ToggleMember, Default, PerEntryValueExpr)
// PerEntryValueExpr references `Module` (FPCGSubdivisionSubmodule) populated by FixModuleInfos(Axis).
#define PCGEX_GCD_GRAMMAR_PERAXIS_ATTRS(_) \
	_(double, Size,     SizeAttributeName,     bWriteSize,     0.0,  Module.Size) \
	_(bool,   Scalable, ScalableAttributeName, bWriteScalable, true, Module.bScalable)

// Shared-across-axes grammar attributes. One slot in FUniqueOutput, no suffix.
// Symbol and DebugColor live at the grammar struct level (FPCGExAssetGrammarDetails), not per-axis,
// so they're sourced from Grammar-> directly rather than from a per-axis Module.
#define PCGEX_GCD_GRAMMAR_SHARED_ATTRS(_) \
	_(FName,    Symbol,     SymbolAttributeName,     bWriteSymbol,     NAME_None,            Grammar->Symbol) \
	_(FVector4, DebugColor, DebugColorAttributeName, bWriteDebugColor, FVector4(1, 1, 1, 1), FVector4(Grammar->DebugColor))

#define PCGEX_VALIDATE_TOGGLED(InContext, Settings, Toggle, AttrName) \
	if ((Settings)->Toggle) { PCGEX_VALIDATE_NAME_C(InContext, (Settings)->AttrName) }

// Pin labels (SourcesPin, OutputCollectionDataPin, AnnotatedSourcesPin) and EmptyTag now live in
// Helpers/PCGExGetCollectionDataFlatten.h alongside the flatten code -- both translation units need
// them, so they're `inline const FName` in the header. This main file owns the settings/element
// glue and the write/annotate path.

#pragma region UPCGSettings interface

UPCGExGetCollectionDataSettings::UPCGExGetCollectionDataSettings()
{
	CacheLoadedResources = EPCGExOptionState::Enabled;
}

#if WITH_EDITOR
void UPCGExGetCollectionDataSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	bWriteAssetClass = bWriteAssetPath;
	AssetClassAttributeName = AssetPathAttributeName;
}
#endif

TArray<FPCGPinProperties> UPCGExGetCollectionDataSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	if (SourceMode == EPCGExGetCollectionDataSourceMode::FromInputs)
	{
		PCGEX_PIN_ANY(PCGExGetCollectionData::SourcesPin, TEXT("Input attribute sets and/or point data providing collection references."), Required)
		if (SourceShape == EPCGExGetCollectionDataSourceShape::EntryIdAndMap)
		{
			PCGEX_PIN_PARAM(PCGExCollections::Labels::SourceCollectionMapLabel, TEXT("Upstream Collection Map used to resolve entry hashes back to sub-collection entries."), Required)
		}
	}
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExGetCollectionDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	if (SourceMode == EPCGExGetCollectionDataSourceMode::FromInputs && bAnnotateSources)
	{
		PCGEX_PIN_ANY(PCGExGetCollectionData::AnnotatedSourcesPin, TEXT("One forwarded copy of each Sources input, augmented with identity attributes (RootCollectionIndex / CollectionIndex / CollectionHash where their respective output toggles are on)."), Required)
	}
	PCGEX_PIN_PARAMS(PCGExGetCollectionData::OutputCollectionDataPin, TEXT("One attribute set per resolved source (see Fanout)."), Required)
	PCGEX_PIN_PARAM(PCGExCollections::Labels::OutputCollectionMapLabel, TEXT("Collection map covering every host referenced by the emitted attribute sets."), Required)
	return PinProperties;
}

FPCGElementPtr UPCGExGetCollectionDataSettings::CreateElement() const
{
	return MakeShared<FPCGExGetCollectionDataElement>();
}


#if WITH_EDITOR
TArray<FText> UPCGExGetCollectionDataSettings::GetNodeTitleAliases() const
{
	return {FTEXT("PCGEx | Asset Collection to Set"), FTEXT("PCGEx | Collection to Module Infos")};
}
#endif

#if PCGEX_ENGINE_VERSION < 507
EPCGDataType UPCGExGetCollectionDataSettings::GetCurrentPinTypes(const UPCGPin* InPin) const
{
	// Input pins + the Annotated Sources output mirror their input shape (Any) -- fall through.
	// Data + Map outputs are always Param-typed attribute sets and must not be auto-deduced.
	if (!InPin->IsOutputPin() || InPin->Properties.Label == PCGExGetCollectionData::AnnotatedSourcesPin)
	{
		return Super::GetCurrentPinTypes(InPin);
	}
	return EPCGDataType::Param;
}
#else
FPCGDataTypeIdentifier UPCGExGetCollectionDataSettings::GetCurrentPinTypesID(const UPCGPin* InPin) const
{
	// Input pins + the Annotated Sources output mirror their input shape (Any) -- fall through.
	// Data + Map outputs are always Param-typed attribute sets and must not be auto-deduced.
	if (!InPin->IsOutputPin() || InPin->Properties.Label == PCGExGetCollectionData::AnnotatedSourcesPin)
	{
		return Super::GetCurrentPinTypesID(InPin);
	}
	return FPCGDataTypeInfoParam::AsId();
}
#endif

#pragma endregion

bool FPCGExGetCollectionDataElement::IsCacheable(const UPCGSettings* InSettings) const
{
	const UPCGExGetCollectionDataSettings* Settings = static_cast<const UPCGExGetCollectionDataSettings*>(InSettings);
	PCGEX_GET_OPTION_STATE(Settings->CacheData, bDefaultCacheNodeOutput)
}

bool FPCGExGetCollectionDataElement::Boot(FPCGExContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExGetCollectionDataElement::Boot);

	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_SETTINGS_C(InContext, GetCollectionData)
	FPCGExGetCollectionDataContext* Context = static_cast<FPCGExGetCollectionDataContext*>(InContext);

	// Validate attribute names up-front -- abort early on bad config.
	PCGEX_VALIDATE_TOGGLED(InContext, Settings, bWriteAssetPath, AssetPathAttributeName)
	PCGEX_VALIDATE_TOGGLED(InContext, Settings, bWriteWeight, WeightAttributeName)
	PCGEX_VALIDATE_TOGGLED(InContext, Settings, bWriteCategory, CategoryAttributeName)
#define PCGEX_GCD_VALIDATE(Type, FieldName, AttrName, Toggle, Default, ValueExpr) \
	PCGEX_VALIDATE_TOGGLED(InContext, Settings, Toggle, AttrName)
	PCGEX_GCD_LEAF_ATTRS(PCGEX_GCD_VALIDATE)
	PCGEX_GCD_ROW_ATTRS(PCGEX_GCD_VALIDATE)
	PCGEX_GCD_GRAMMAR_PERAXIS_ATTRS(PCGEX_GCD_VALIDATE)
	PCGEX_GCD_GRAMMAR_SHARED_ATTRS(PCGEX_GCD_VALIDATE)
#undef PCGEX_GCD_VALIDATE

	if (Settings->SourceMode == EPCGExGetCollectionDataSourceMode::Collection)
	{
		// Hard-ref TObjectPtr: asset is already loaded. One slot, one path.
		UPCGExAssetCollection* MainCollection = Settings->AssetCollection;
		if (!MainCollection)
		{
			PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Asset collection is not set."));
			return false;
		}
		FPCGExGetCollectionDataContext::FSlot& Slot = Context->Slots.AddDefaulted_GetRef();
		Slot.Path = FSoftObjectPath(MainCollection);
		Context->ResolvedCollections.Add(Slot.Path, MainCollection);
		return true;
	}

	// FromInputs mode -- delegate the slot parsing + async load orchestration to the helper.
	return PCGExGetCollectionData::ParseSourceInputsIntoSlots(Context, Settings);
}

namespace PCGExGetCollectionData
{
	/** All the writable attribute pointers + property writer + entry list for a single unique output.
	 *  Pre-built single-threaded before the parallel write loop; iteration only touches its own slot.
	 *  In Merged fanout mode a single FUniqueOutput is shared across all unique collections (entries
	 *  from each are appended into Entries); bWantAssetPath/bWantAssetClass are pre-OR'd across the
	 *  set so the right attribute halves are declared. */
	struct FUniqueOutput
	{
		UPCGExAssetCollection* Collection = nullptr; // primary collection (root for PropertyWriter / FixModuleInfos host context)
		UPCGParamData* OutputSet = nullptr;
		TSharedPtr<TArray<FFlattenedEntry>> Entries;
		// Pointer-identity set populated alongside Entries by ProcessEntry. Same scope as Entries so
		// the dedup is correct in Merged fanout (multiple roots feeding one shared FUniqueOutput).
		TSharedPtr<TSet<const FPCGExAssetCollectionEntry*>> SeenEntries;
		bool bWantAssetPath = false;
		bool bWantAssetClass = false;

		// Per-output attribute pointers (set only when corresponding bWriteX is on).
		FPCGMetadataAttribute<FSoftObjectPath>* AssetPathAttr = nullptr;
		FPCGMetadataAttribute<FSoftClassPath>* AssetClassAttr = nullptr;
		FPCGMetadataAttribute<int32>* WeightAttrInt = nullptr;
		FPCGMetadataAttribute<float>* WeightAttrFloat = nullptr;
		FPCGMetadataAttribute<FName>* CategoryAttr = nullptr;
		FPCGMetadataAttribute<int64>* EntryAttr = nullptr;
#define PCGEX_GCD_DECL_FIELD(Type, FieldName, AttrName, Toggle, Default, ValueExpr) \
		FPCGMetadataAttribute<Type>* FieldName##Attr = nullptr;
		PCGEX_GCD_LEAF_ATTRS(PCGEX_GCD_DECL_FIELD)
		PCGEX_GCD_ROW_ATTRS(PCGEX_GCD_DECL_FIELD)
		PCGEX_GCD_GRAMMAR_SHARED_ATTRS(PCGEX_GCD_DECL_FIELD)
#undef PCGEX_GCD_DECL_FIELD
		// Per-axis grammar attributes: arrays indexed by axis (0=X, 1=Y, 2=Z). Each slot is
		// declared only when the corresponding axis bit is set in any entry's effective grammar.
#define PCGEX_GCD_DECL_PERAXIS_FIELD(Type, FieldName, AttrName, Toggle, Default, ValueExpr) \
		FPCGMetadataAttribute<Type>* FieldName##Attr[3] = { nullptr, nullptr, nullptr };
		PCGEX_GCD_GRAMMAR_PERAXIS_ATTRS(PCGEX_GCD_DECL_PERAXIS_FIELD)
#undef PCGEX_GCD_DECL_PERAXIS_FIELD

		PCGExCollections::FPCGExCollectionPropertySetWriter PropertyWriter;
	};

	/** Derived settings + ambient state passed to FlattenInto / WriteFromEntries. Bundled so the
	 *  Collection-mode fast path, the Merged path, and the slot-based FromInputs path can all share
	 *  the same helpers without dragging a dozen positional params. */
	struct FOutputProcessParams
	{
		const UPCGExGetCollectionDataSettings* Settings = nullptr;
		FPCGExContext* InContext = nullptr;
		PCGExCollections::FPickPacker* Packer = nullptr;
		const FProcessEntryContext* FlattenCtx = nullptr;

		bool bOutputWeight = false;
		EPCGExWeightNormalization WeightNorm = EPCGExWeightNormalization::None;
		bool bWeightAsFloat = false;
		bool bOutputCategory = false;
		bool bAnyGrammarField = false;
	};

	/** Inline guarded setter -- write the value if the attribute pointer is non-null. Used by the
	 *  X-macro-driven write block; FORCEINLINE so the call disappears entirely. */
	template <typename T, typename V>
	FORCEINLINE static void SetIf(FPCGMetadataAttribute<T>* Attr, int64 Key, const V& Value)
	{
		if (Attr)
		{
			Attr->SetValue(Key, Value);
		}
	}

	/** Lookup `Slot.Path` in the per-context resolved map. Returns nullptr when the path is empty
	 *  (leaf-failed slot) or when the resolve produced a null cast. */
	FORCEINLINE static UPCGExAssetCollection* ResolveSlotCollection(
		const FPCGExGetCollectionDataContext* Ctx,
		const FPCGExGetCollectionDataContext::FSlot& Slot)
	{
		UPCGExAssetCollection* const* ResolvedPtr = Ctx->ResolvedCollections.Find(Slot.Path);
		return (ResolvedPtr && *ResolvedPtr) ? *ResolvedPtr : nullptr;
	}

	/** Set the AssetPath / AssetClass declaration flags on U based on a single collection's type.
	 *  Used by the non-merged paths where each output is tied to exactly one collection. */
	FORCEINLINE static void SetAssetHalves(FUniqueOutput& U, const UPCGExAssetCollection* Collection)
	{
		const bool bIsActor = Cast<UPCGExActorCollection>(Collection) != nullptr;
		U.bWantAssetPath = !bIsActor;
		U.bWantAssetClass = bIsActor;
	}

	/** Append flattened entries from `Collection` into U.Entries. Never clobbers, so it's safe
	 *  to call multiple times on the same U (Merged fanout walks every unique collection into
	 *  one shared FUniqueOutput). Every appended entry is stamped with RootCollectionIndex, and
	 *  its CollectionIndex is assigned from the shared map on FProcessEntryContext. */
	static void FlattenInto(FUniqueOutput& U, UPCGExAssetCollection* Collection, const int32 RootCollectionIndex, const FOutputProcessParams& P)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetCollectionData_Flatten);
		// Per-call Ctx copy so the shared FlattenCtx isn't mutated -- SeenEntries is per-U, not global.
		FProcessEntryContext Ctx = *P.FlattenCtx;
		Ctx.SeenEntries = U.SeenEntries.Get();
		FlattenCollection(Ctx, Collection, RootCollectionIndex, *U.Entries);
	}

	// =============================================================================================
	// WriteFromEntries pre-passes
	// =============================================================================================

	/** Pre-pass: resolve each entry's effective grammar once and cache pointers for the write loop.
	 *  Returns the union of per-entry Axes (intersected with the user-requested OutputAxes) -- this
	 *  drives which per-axis attribute slots get declared. No-op when no grammar field is requested
	 *  or OutputAxes is empty (saves the GetEffectiveGrammar call entirely). */
	static uint8 ResolveGrammars(
		const TArray<FFlattenedEntry>& Entries,
		const UPCGExGetCollectionDataSettings* Settings,
		const bool bAnyGrammarField,
		TArray<const FPCGExAssetGrammarDetails*>& OutResolvedGrammars)
	{
		if (!bAnyGrammarField || Settings->OutputAxes == 0)
		{
			return 0;
		}
		OutResolvedGrammars.SetNumZeroed(Entries.Num());
		uint8 UsedAxes = 0;
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			const FFlattenedEntry& EH = Entries[i];
			if (!EH.Entry)
			{
				continue;
			}
			const FPCGExAssetGrammarDetails* G = EH.Entry->GetEffectiveGrammar(EH.Host);
			OutResolvedGrammars[i] = G;
			if (G)
			{
				UsedAxes |= (G->Axes & Settings->OutputAxes);
			}
		}
		return UsedAxes;
	}

	/** Bundle of weight-sum denominators for the three normalization scopes, used by the write loop
	 *  to divide each entry's raw weight into a [0..1] float. Empty when Mode == None or bWeightAsFloat
	 *  is false (the write loop emits raw int32 weights in those cases). */
	struct FWeightNormSums
	{
		double Global = 0.0;
		TMap<FName, double> PerCategory;
		TMap<const UPCGExAssetCollection*, double> PerCollection;
	};

	/** Pre-pass: sum entry weights by the chosen normalization scope. Skips the walk when bWeightAsFloat
	 *  is false -- raw int32 weights don't need a denominator. */
	static void ComputeWeightNorms(
		const TArray<FFlattenedEntry>& Entries,
		const EPCGExWeightNormalization Mode,
		const bool bWeightAsFloat,
		FWeightNormSums& OutSums)
	{
		if (!bWeightAsFloat)
		{
			return;
		}
		for (const FFlattenedEntry& EH : Entries)
		{
			const FPCGExAssetCollectionEntry* E = EH.Entry;
			if (!E || E->bIsSubCollection)
			{
				continue;
			}
			const double W = E->Weight;
			switch (Mode)
			{
			case EPCGExWeightNormalization::Global:
				OutSums.Global += W;
				break;
			case EPCGExWeightNormalization::PerCategory:
				OutSums.PerCategory.FindOrAdd(EH.Category) += W;
				break;
			case EPCGExWeightNormalization::PerCollection:
				OutSums.PerCollection.FindOrAdd(EH.Host) += W;
				break;
			default: ;
			}
		}
	}

	/** Initialize the U.PropertyWriter when properties are configured. Pulls "fallback hosts"
	 *  (every distinct host that isn't the primary U.Collection) so per-entry property lookups can
	 *  resolve against the sub-collection that actually owns the entry. Skipped when no property
	 *  output is configured -- saves a non-trivial allocation per output. */
	static void InitPropertyWriter(
		FUniqueOutput& U,
		const FOutputProcessParams& P,
		const UPCGExGetCollectionDataSettings* Settings,
		UPCGMetadata* Metadata)
	{
		if (!Settings->PropertyOutputSettings.HasOutputs())
		{
			return;
		}
		TRACE_CPUPROFILER_EVENT_SCOPE(GetCollectionData_PropertyWriterInit);
		TArray<const UPCGExAssetCollection*> FallbackHosts;
		TSet<const UPCGExAssetCollection*> Seen;
		for (const FFlattenedEntry& EH : *U.Entries)
		{
			if (!EH.Host || EH.Host == U.Collection)
			{
				continue;
			}
			bool bAlreadyIn = false;
			Seen.Add(EH.Host, &bAlreadyIn);
			if (!bAlreadyIn)
			{
				FallbackHosts.Add(EH.Host);
			}
		}
		U.PropertyWriter.Initialize(P.InContext, Settings->PropertyOutputSettings, U.Collection, FallbackHosts, Metadata);
	}

	/** Declares attributes on U.OutputSet, initializes the property writer, computes weight sums,
	 *  and writes every row in U.Entries. Call exactly once per output. */
	static void WriteFromEntries(FUniqueOutput& U, const FOutputProcessParams& P)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetCollectionData_WriteFromEntries);

		const UPCGExGetCollectionDataSettings* Settings = P.Settings;
		TArray<FFlattenedEntry>& Entries = *U.Entries;
		UPCGMetadata* Metadata = U.OutputSet->Metadata;

		// Choose which asset halves to declare. bWantAssetPath/bWantAssetClass are pre-computed
		// per-mode (single-collection: based on actor-vs-mesh type; merged: OR'd across all
		// contributing collections so heterogeneous mixes get both halves).
		const bool bOutputAssetPath = Settings->bWriteAssetPath && U.bWantAssetPath;
		const bool bOutputAssetClass = Settings->bWriteAssetPath && U.bWantAssetClass;

		// Resolve grammars once, accumulate the union of axes used across all entries -- drives
		// the per-axis attribute declarations below and the per-row Fix dispatch in the write loop.
		TArray<const FPCGExAssetGrammarDetails*> ResolvedGrammars;
		const uint8 LocalUsedAxes = ResolveGrammars(Entries, Settings, P.bAnyGrammarField, ResolvedGrammars);

		// Declare attributes. CreateAttribute (vs FindOrCreate) skips the find lookup -- safe
		// because we just allocated a fresh empty Metadata.
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GetCollectionData_DeclareAttrs);
			if (bOutputAssetPath)
			{
				U.AssetPathAttr = Metadata->CreateAttribute<FSoftObjectPath>(PCGExMetaHelpers::GetAttributeIdentifier(Settings->AssetPathAttributeName, U.OutputSet), FSoftObjectPath(), false, true);
			}
			if (bOutputAssetClass)
			{
				U.AssetClassAttr = Metadata->CreateAttribute<FSoftClassPath>(PCGExMetaHelpers::GetAttributeIdentifier(Settings->AssetPathAttributeName, U.OutputSet), FSoftClassPath(), false, true);
			}
			if (P.bOutputWeight)
			{
				const FPCGAttributeIdentifier WeightId = PCGExMetaHelpers::GetAttributeIdentifier(Settings->WeightAttributeName, U.OutputSet);
				if (P.bWeightAsFloat)
				{
					U.WeightAttrFloat = Metadata->CreateAttribute<float>(WeightId, 0.0f, false, true);
				}
				else
				{
					U.WeightAttrInt = Metadata->CreateAttribute<int32>(WeightId, 0, false, true);
				}
			}
			if (P.bOutputCategory)
			{
				U.CategoryAttr = Metadata->CreateAttribute<FName>(PCGExMetaHelpers::GetAttributeIdentifier(Settings->CategoryAttributeName, U.OutputSet), NAME_None, false, true);
			}
#define PCGEX_GCD_DECLARE(Type, FieldName, AttrName, Toggle, Default, ValueExpr) \
			if (Settings->Toggle) \
			{ \
				U.FieldName##Attr = Metadata->CreateAttribute<Type>(PCGExMetaHelpers::GetAttributeIdentifier(Settings->AttrName, U.OutputSet), Default, false, true); \
			}
			PCGEX_GCD_LEAF_ATTRS(PCGEX_GCD_DECLARE)
			PCGEX_GCD_ROW_ATTRS(PCGEX_GCD_DECLARE)
			PCGEX_GCD_GRAMMAR_SHARED_ATTRS(PCGEX_GCD_DECLARE)
#undef PCGEX_GCD_DECLARE

			// Per-axis declarations. When exactly one axis ends up in UsedAxes, drop the
			// _X/_Y/_Z suffix so single-axis outputs match the legacy attribute shape (Size,
			// Scalable). When 2+ axes are used, suffix the user-configured base name with the
			// matching axis tag for unambiguous downstream lookup.
			{
				const bool bSuppressSuffix = (PCGExGrammarAxes::CountAxes(LocalUsedAxes) <= 1) && !Settings->bAlwaysSuffixAxes;
#define PCGEX_GCD_DECLARE_PERAXIS(Type, FieldName, AttrName, Toggle, Default, ValueExpr) \
				if (Settings->Toggle) \
				{ \
					for (int32 _a = 0; _a < 3; _a++) \
					{ \
						if (!(LocalUsedAxes & static_cast<uint8>(PCGExGrammarAxes::Bits[_a]))) { continue; } \
						const FName _AttrName = bSuppressSuffix \
							? Settings->AttrName \
							: FName(*FString::Printf(TEXT("%s%s"), *Settings->AttrName.ToString(), PCGExGrammarAxes::Suffixes[_a])); \
						U.FieldName##Attr[_a] = Metadata->CreateAttribute<Type>(PCGExMetaHelpers::GetAttributeIdentifier(_AttrName, U.OutputSet), Default, false, true); \
					} \
				}
				PCGEX_GCD_GRAMMAR_PERAXIS_ATTRS(PCGEX_GCD_DECLARE_PERAXIS)
#undef PCGEX_GCD_DECLARE_PERAXIS
			}

			U.EntryAttr = Metadata->CreateAttribute<int64>(PCGExMetaHelpers::GetAttributeIdentifier(Settings->EntryAttributeName, U.OutputSet), 0, false, true);
		} // end DeclareAttrs scope

		// Property writer (gated -- skips an allocation per output when no properties configured).
		InitPropertyWriter(U, P, Settings, Metadata);

		// Weight normalization pre-pass (scoped to this output).
		FWeightNormSums WeightSums;
		ComputeWeightNorms(Entries, P.WeightNorm, P.bWeightAsFloat, WeightSums);

		FPCGExGrammarSizeCache SizeCache;
		if (Settings->bWriteSize)
		{
			SizeCache.Reserve(Entries.Num() * PCGExGrammarAxes::CountAxes(LocalUsedAxes));
		}

		TSet<FName> UniqueSymbols;
		if (Settings->bWriteSymbol)
		{
			UniqueSymbols.Reserve(Entries.Num());
		}

		const uint8 SkipFlags = Settings->SkipFlags;
		const bool bSkipEmptySymbol = (SkipFlags & static_cast<uint8>(EPCGExGetCollectionDataSkipFlags::EmptySymbol)) != 0;
		const bool bSkipEmptyAxes = (SkipFlags & static_cast<uint8>(EPCGExGetCollectionDataSkipFlags::EmptyAxes)) != 0;
		const bool bSkipDuplicates = (SkipFlags & static_cast<uint8>(EPCGExGetCollectionDataSkipFlags::Duplicates)) != 0;

		TRACE_CPUPROFILER_EVENT_SCOPE(GetCollectionData_WriteRows);
		for (int32 EntryIdx = 0; EntryIdx < Entries.Num(); ++EntryIdx)
		{
			const FFlattenedEntry& EH = Entries[EntryIdx];
			const FPCGExAssetCollectionEntry* E = EH.Entry;

			// Cached from the pre-pass (skips a second GetEffectiveGrammar per row).
			// Symbol / DebugColor / Axes come straight off the cached pointer; per-axis Size /
			// bScalable go through Grammar->FixLeaf/FixSubCollection directly, which avoids a third
			// resolve that Entry::FixModuleInfos would otherwise do internally per axis.
			const FPCGExAssetGrammarDetails* Grammar = ResolvedGrammars.IsValidIndex(EntryIdx) ? ResolvedGrammars[EntryIdx] : nullptr;
			const uint8 RowAxes = Grammar ? (Grammar->Axes & Settings->OutputAxes) : 0;
			const bool bHasGrammar = Grammar && Grammar->Axes != 0;
			const FName SharedSymbol = Grammar ? Grammar->Symbol : NAME_None;

			// EmptyAxes: drop rows that contribute nothing to the requested output axes (includes
			// Flatten-mode subcollections and entries with Axes & OutputAxes == 0).
			if (bSkipEmptyAxes && RowAxes == 0)
			{
				continue;
			}
			// EmptySymbol: drop rows whose grammar is enabled but Symbol is unset.
			if (bSkipEmptySymbol && bHasGrammar && SharedSymbol.IsNone())
			{
				continue;
			}
			// Duplicates (symbol side): drop rows whose Symbol was already emitted earlier in this output.
			// The pointer-side dedupe lives in ProcessEntry (Ctx.bNoDuplicates).
			if (bSkipDuplicates && bHasGrammar && !SharedSymbol.IsNone())
			{
				bool bAlreadyInSet = false;
				UniqueSymbols.Add(SharedSymbol, &bAlreadyInSet);
				if (bAlreadyInSet)
				{
					continue;
				}
			}

			const int64 Key = Metadata->AddEntry();

			// ROW_ATTRS (root + collection identity) are written for EVERY row -- including null/empty
			// rows where E is nullptr -- so downstream consumers can always read "which root tried to
			// resolve at this slot". Hence we write them before the !E early-out below.
#define PCGEX_GCD_WRITE_ROW(Type, FieldName, AttrName, Toggle, Default, ValueExpr) \
			SetIf(U.FieldName##Attr, Key, ValueExpr);
			PCGEX_GCD_ROW_ATTRS(PCGEX_GCD_WRITE_ROW)
#undef PCGEX_GCD_WRITE_ROW

			if (!E)
			{
				continue;
			}

			// AssetPath is written for both leaf and sub-collection entries. UpdateStaging
			// populates Staging.Path with the sub-collection's own asset path when bIsSubCollection,
			// so downstream consumers can resolve either kind through the same attribute.
			// AssetClass and the other leaf-only fields (weights, bounds, nesting depth) stay
			// gated -- sub-collections aren't UClass references and don't have meaningful bounds.
			SetIf(U.AssetPathAttr, Key, E->Staging.Path);

			if (!E->bIsSubCollection)
			{
				SetIf(U.AssetClassAttr, Key, FSoftClassPath(E->Staging.Path.ToString()));
				if (U.WeightAttrInt)
				{
					U.WeightAttrInt->SetValue(Key, E->Weight);
				}
				else if (U.WeightAttrFloat)
				{
					double Denom = 0.0;
					switch (P.WeightNorm)
					{
					case EPCGExWeightNormalization::Global:
						Denom = WeightSums.Global;
						break;
					case EPCGExWeightNormalization::PerCategory:
						Denom = WeightSums.PerCategory.FindRef(EH.Category);
						break;
					case EPCGExWeightNormalization::PerCollection:
						Denom = WeightSums.PerCollection.FindRef(EH.Host);
						break;
					default: ;
					}
					U.WeightAttrFloat->SetValue(Key, Denom > 0.0 ? static_cast<float>(static_cast<double>(E->Weight) / Denom) : 0.0f);
				}
#define PCGEX_GCD_WRITE_LEAF(Type, FieldName, AttrName, Toggle, Default, ValueExpr) \
				SetIf(U.FieldName##Attr, Key, ValueExpr);
				PCGEX_GCD_LEAF_ATTRS(PCGEX_GCD_WRITE_LEAF)
#undef PCGEX_GCD_WRITE_LEAF
			}

			SetIf(U.CategoryAttr, Key, EH.Category);

			if (bHasGrammar)
			{
				// Iterate the bits this entry contributes (already filtered through OutputAxes above).
				// Dispatch directly through Grammar-> to skip the resolve that Entry::FixModuleInfos
				// would otherwise repeat on every call.
				const bool bIsSub = E->bIsSubCollection;
				for (int32 a = 0; a < 3; a++)
				{
					if (!(RowAxes & static_cast<uint8>(PCGExGrammarAxes::Bits[a])))
					{
						continue;
					}

					FPCGSubdivisionSubmodule Module;
					const bool bFixed = bIsSub
						? Grammar->FixSubCollection(E->SubCollection, PCGExGrammarAxes::Bits[a], Module, Settings->bWriteSize ? &SizeCache : nullptr)
						: Grammar->FixLeaf(E->Staging.Bounds, PCGExGrammarAxes::Bits[a], Module);
					if (!bFixed)
					{
						continue;
					}

#define PCGEX_GCD_WRITE_PERAXIS(Type, FieldName, AttrName, Toggle, Default, ValueExpr) \
					SetIf(U.FieldName##Attr[a], Key, ValueExpr);
					PCGEX_GCD_GRAMMAR_PERAXIS_ATTRS(PCGEX_GCD_WRITE_PERAXIS)
#undef PCGEX_GCD_WRITE_PERAXIS
				}

				// Shared grammar attributes (DebugColor) -- sourced from Grammar->, not the per-axis Module.
#define PCGEX_GCD_WRITE_SHARED(Type, FieldName, AttrName, Toggle, Default, ValueExpr) \
				SetIf(U.FieldName##Attr, Key, ValueExpr);
				PCGEX_GCD_GRAMMAR_SHARED_ATTRS(PCGEX_GCD_WRITE_SHARED)
#undef PCGEX_GCD_WRITE_SHARED
			}

			const uint64 Hash = P.Packer->GetPickIdx(EH.Host, E->Staging.InternalIndex, 0);
			U.EntryAttr->SetValue(Key, static_cast<int64>(Hash));

			U.PropertyWriter.WriteEntry(Key, E, EH.Host);
		}
	}

	/** Per-slot precomputed identity for the AnnotateSources pass. Built once per node invocation
	 *  from the flatten-time identity maps + the resolved collection ptr; read O(1) per row.
	 *  Carries Collection* so collection-level reads (schema defaults, collection metadata) don't
	 *  have to re-walk ResolvedCollections during annotation. */
	struct FSlotIdentity
	{
		int32 Root = -1;
		int32 Coll = -1;
		int32 Hash = -1;
		UPCGExAssetCollection* Collection = nullptr;
	};

	/** Forward each input from SourcesPin to AnnotatedSourcesPin with identity attributes added,
	 *  based on whichever of bWriteRootCollectionIndex / bWriteCollectionIndex / bWriteCollectionHash
	 *  are enabled. Domain matches Fanout (PerInputData -> @Data, else @Element).
	 *
	 *  When no identity toggle is on, falls back to a straight pass-through (no duplicate, no
	 *  attribute writes) so the pin still emits its sources -- gives the user a useful default
	 *  before they pick which identity field(s) to annotate with.
	 *
	 *  Must be called AFTER flatten so the per-collection identity maps on FlattenCtx are populated.
	 *
	 *  Per-slot identity (Root/Coll/Hash + resolved Collection*) is precomputed once into SlotIdentity
	 *  so the per-row inner loop is a single array read instead of 3-4 map probes. The per-input
	 *  duplicate + attribute write is dispatched via PCGExMT::ParallelOrSequential -- each task owns
	 *  its DupData and writes into its disjoint slot in ParallelResults; a single-threaded compact
	 *  appends the successes to TaggedData. SlotIdentity carries Collection* so future schema-output
	 *  paths can read collection-level data without re-resolving. */
	static void AnnotateSources(
		FPCGExGetCollectionDataContext* Context,
		const UPCGExGetCollectionDataSettings* Settings,
		const FProcessEntryContext& FlattenCtx,
		const TMap<UPCGExAssetCollection*, int32>& CollectionToRootIdx)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetCollectionData_AnnotateSources);

		TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(SourcesPin);
		if (Inputs.IsEmpty())
		{
			return;
		}

		const bool bWantRoot = Settings->bWriteRootCollectionIndex;
		const bool bWantColl = Settings->bWriteCollectionIndex;
		const bool bWantHash = Settings->bWriteCollectionHash;
		const bool bWantSchema = Settings->SchemaPropertyAnnotations.HasOutputs();

		// Pass-through path: no annotation needed, just forward each input under the new pin name
		// without duplicating. Same UPCGData pointer; downstream sees the original attribute set
		// or point data, plus whatever tags were already on it.
		if (!bWantRoot && !bWantColl && !bWantHash && !bWantSchema)
		{
			Context->OutputData.TaggedData.Reserve(Context->OutputData.TaggedData.Num() + Inputs.Num());
			for (const FPCGTaggedData& InputTagged : Inputs)
			{
				if (!InputTagged.Data)
				{
					continue;
				}
				FPCGTaggedData& OutTagged = Context->OutputData.TaggedData.Emplace_GetRef();
				OutTagged.Pin = AnnotatedSourcesPin;
				OutTagged.Data = InputTagged.Data;
				OutTagged.Tags = InputTagged.Tags;
			}
			return;
		}

		const bool bDataDomain = Settings->Fanout == EPCGExGetCollectionDataFanout::PerInputData;

		// Precompute per-slot identity once. Single pass over Slots replaces 3-4 map probes per row
		// inside the inner write loop. Carries the resolved Collection pointer so future schema-output
		// code paths can read collection-level data without a second resolve.
		TArray<FSlotIdentity> SlotIdentity;
		SlotIdentity.SetNum(Context->Slots.Num());
		for (int32 k = 0; k < Context->Slots.Num(); k++)
		{
			const FPCGExGetCollectionDataContext::FSlot& Slot = Context->Slots[k];
			UPCGExAssetCollection* const* CollectionPtr = Context->ResolvedCollections.Find(Slot.Path);
			if (!CollectionPtr || !*CollectionPtr)
			{
				continue;
			}
			UPCGExAssetCollection* Collection = *CollectionPtr;
			FSlotIdentity& Id = SlotIdentity[k];
			Id.Collection = Collection;
			if (const int32* R = CollectionToRootIdx.Find(Collection))
			{
				Id.Root = *R;
			}
			if (FlattenCtx.CollectionIndexMap)
			{
				if (const int32* C = FlattenCtx.CollectionIndexMap->Find(Collection))
				{
					Id.Coll = *C;
				}
			}
			// Input rows always reference a root collection -> Depth is 0 in the hash tuple.
			if (FlattenCtx.CollectionHashMap && Id.Root != -1 && Id.Coll != -1)
			{
				const TTuple<int32, int32, int32> Key(Id.Root, Id.Coll, 0);
				if (const int32* H = FlattenCtx.CollectionHashMap->Find(Key))
				{
					Id.Hash = *H;
				}
			}
		}

		// Bucket slots by input. Slots are stored input-major, row-minor today, but explicit
		// bucketing makes the per-input iteration robust to that ordering and is O(NumSlots) anyway.
		TArray<TArray<int32>> SlotsPerInput;
		SlotsPerInput.SetNum(Inputs.Num());
		for (int32 k = 0; k < Context->Slots.Num(); k++)
		{
			const int32 InputIdx = Context->Slots[k].SourceInputIndex;
			if (Inputs.IsValidIndex(InputIdx))
			{
				SlotsPerInput[InputIdx].Add(k);
			}
		}

		// Filter to valid inputs upfront so the parallel task list is dense (no skip-and-no-op tasks).
		TArray<int32> ValidInputIndices;
		ValidInputIndices.Reserve(Inputs.Num());
		for (int32 i = 0; i < Inputs.Num(); i++)
		{
			if (Inputs[i].Data)
			{
				ValidInputIndices.Add(i);
			}
		}
		if (ValidInputIndices.IsEmpty())
		{
			return;
		}

		// Per-task scratch: each task writes its own disjoint slot. Sequential post-pass appends
		// the successes into Context->OutputData.TaggedData (the latter isn't thread-safe to grow).
		TArray<FPCGTaggedData> ParallelResults;
		ParallelResults.SetNum(ValidInputIndices.Num());

		PCGExMT::ParallelOrSequential(ValidInputIndices.Num(), [&](const int32 ValidIdx)
		{
			const int32 InputIdx = ValidInputIndices[ValidIdx];
			const FPCGTaggedData& InputTagged = Inputs[InputIdx];

			UPCGData* DupData = Context->ManagedObjects->DuplicateData<UPCGData>(InputTagged.Data);
			if (!DupData)
			{
				return; // leaves ParallelResults[ValidIdx].Data == nullptr; compact pass drops it
			}

			const TArray<int32>& SlotIndices = SlotsPerInput[InputIdx];

			if (bDataDomain)
			{
				// PerInputData: one slot per input. Read identity once and stamp as @Data.
				const FSlotIdentity Id = SlotIndices.Num() > 0 ? SlotIdentity[SlotIndices[0]] : FSlotIdentity{};
				if (bWantRoot)
				{
					PCGExData::Helpers::SetDataValue<int32>(DupData, Settings->RootCollectionIndexAttributeName, Id.Root);
				}
				if (bWantColl)
				{
					PCGExData::Helpers::SetDataValue<int32>(DupData, Settings->CollectionIndexAttributeName, Id.Coll);
				}
				if (bWantHash)
				{
					PCGExData::Helpers::SetDataValue<int32>(DupData, Settings->CollectionHashAttributeName, Id.Hash);
				}

				// Schema-property annotation (@Data): single value per input, taken from the slot's
				// resolved collection schema. Null host -> no schema attribute (see helper docs).
				if (bWantSchema)
				{
					PCGExCollections::WriteSchemaToDataDomain(
						Context, Settings->SchemaPropertyAnnotations, Id.Collection, DupData);
				}
			}
			else
			{
				// Element domain: build per-row value arrays in slot order (= row order in the
				// source), then push them through a single SetRange per attribute. Same accessor
				// pattern as the Boot-time read path; works uniformly for point and attribute-set inputs.
				const int32 NumRows = SlotIndices.Num();
				TArray<int32> RootValues, CollValues, HashValues;
				if (bWantRoot)
				{
					RootValues.SetNumUninitialized(NumRows);
				}
				if (bWantColl)
				{
					CollValues.SetNumUninitialized(NumRows);
				}
				if (bWantHash)
				{
					HashValues.SetNumUninitialized(NumRows);
				}
				for (int32 r = 0; r < NumRows; r++)
				{
					const FSlotIdentity& Id = SlotIdentity[SlotIndices[r]];
					if (bWantRoot)
					{
						RootValues[r] = Id.Root;
					}
					if (bWantColl)
					{
						CollValues[r] = Id.Coll;
					}
					if (bWantHash)
					{
						HashValues[r] = Id.Hash;
					}
				}

				UPCGMetadata* M = DupData->MutableMetadata();
				if (!M)
				{
					return;
				}
				
				TSharedPtr<IPCGAttributeAccessorKeys> Keys = PCGExMetaHelpers::MakeMutableKeys(DupData);

				auto WriteAttr = [&](const bool bWant, const FName AttrName, const TArray<int32>& Values)
				{
					if (!bWant)
					{
						return;
					}
					// Always declare the attribute (so the column exists downstream) even when there's
					// nothing to write -- keeps the schema stable for consumers that branch on presence.
					M->FindOrCreateAttribute<int32>(PCGExMetaHelpers::GetAttributeIdentifier(AttrName, DupData), -1, false, true);
					if (!Keys || Values.Num() == 0)
					{
						return;
					}
					FPCGAttributePropertyInputSelector Selector;
					Selector.Update(AttrName.ToString());
					Selector = Selector.CopyAndFixLast(DupData);
					TUniquePtr<IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateAccessor(DupData, Selector);
					if (Accessor)
					{
						Accessor->SetRange<int32>(Values, 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);
					}
				};

				WriteAttr(bWantRoot, Settings->RootCollectionIndexAttributeName, RootValues);
				WriteAttr(bWantColl, Settings->CollectionIndexAttributeName, CollValues);
				WriteAttr(bWantHash, Settings->CollectionHashAttributeName, HashValues);

				// Schema-property annotation (@Element): per-row schema values, one row per slot.
				// PerRowHosts[r] = the collection that row r's slot resolved to (or null). The helper
				// finds prototypes across the union and falls back to prototype default for null rows.
				if (bWantSchema)
				{
					TArray<const UPCGExAssetCollection*> PerRowHosts;
					PerRowHosts.SetNumUninitialized(NumRows);
					for (int32 r = 0; r < NumRows; r++)
					{
						PerRowHosts[r] = SlotIdentity[SlotIndices[r]].Collection;
					}
					PCGExCollections::WriteSchemaToElementDomain(
						Context, Settings->SchemaPropertyAnnotations, DupData, PerRowHosts);
				}
			}

			FPCGTaggedData& OutTagged = ParallelResults[ValidIdx];
			OutTagged.Pin = AnnotatedSourcesPin;
			OutTagged.Data = DupData;
			OutTagged.Tags = InputTagged.Tags;
		}, /*Threshold=*/2, EParallelForFlags::Unbalanced);

		// Compact + append. Skips entries where DuplicateData returned nullptr (rare; only on
		// node-wide handle-invalid). Worst-case Reserve overestimate is OK.
		Context->OutputData.TaggedData.Reserve(Context->OutputData.TaggedData.Num() + ParallelResults.Num());
		for (FPCGTaggedData& R : ParallelResults)
		{
			if (R.Data)
			{
				Context->OutputData.TaggedData.Emplace(MoveTemp(R));
			}
		}
	}

	// =============================================================================================
	// Per-mode AdvanceWork bodies
	// =============================================================================================
	// Each helper handles its own flatten+write+emission. AdvanceWork shares only the upfront
	// FProcessEntryContext / FOutputProcessParams setup, dispatches to one of these based on mode,
	// then emits the shared Map output. Map emission stays in AdvanceWork because the Packer lifetime
	// is owned there.

	/** Collection-mode fast path. Skips Slots / UniqueOutputs / CollectionToIndex / ResolvedCollections
	 *  / ParallelOrSequential dispatch entirely. Single FUniqueOutput, single flatten + write.
	 *  Hard-ref TObjectPtr means the asset is already loaded -- no resolve, no map lookup. */
	static void Run_CollectionFastPath(FPCGExGetCollectionDataContext* Context, const FOutputProcessParams& P)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetCollectionData_AdvanceWork_CollectionFastPath);

		FPCGExContext* InContext = P.InContext;
		const UPCGExGetCollectionDataSettings* Settings = P.Settings;
		UPCGExAssetCollection* MainCollection = Settings->AssetCollection;
		InContext->OutputData.TaggedData.Reserve(InContext->OutputData.TaggedData.Num() + 2);

		FUniqueOutput U;
		U.Collection = MainCollection;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GetCollectionData_AllocOutputSet);
			U.OutputSet = InContext->ManagedObjects->New<UPCGParamData>();
		}
		U.Entries = MakeShared<TArray<FFlattenedEntry>>();
		U.SeenEntries = MakeShared<TSet<const FPCGExAssetCollectionEntry*>>();
		SetAssetHalves(U, MainCollection);

		if (MainCollection)
		{
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(GetCollectionData_RegisterTrackingKeys);
				MainCollection->EDITOR_RegisterTrackingKeys(InContext);
			}
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(GetCollectionData_RegisterPacker);
				P.Packer->RegisterCollection(MainCollection);
			}
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(GetCollectionData_Flatten);
				FlattenInto(U, MainCollection, /*RootCollectionIndex=*/0, P);
			}
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(GetCollectionData_Write);
				WriteFromEntries(U, P);
			}
			// Schema-property annotation (@Data): mirrors the FromInputs PerInputData branch -- one
			// value per attribute, stamped onto the output param data.
			if (Settings->SchemaPropertyAnnotations.HasOutputs())
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(GetCollectionData_WriteSchemaToDataDomain);
				PCGExCollections::WriteSchemaToDataDomain(
					InContext, Settings->SchemaPropertyAnnotations, MainCollection, U.OutputSet);
			}
		}

		FPCGTaggedData& OutData = InContext->OutputData.TaggedData.Emplace_GetRef();
		OutData.Pin = OutputCollectionDataPin;
		OutData.Data = U.OutputSet;
		if (U.Entries->IsEmpty())
		{
			OutData.Tags.Add(EmptyTag.ToString());
		}
	}

	/** FromInputs - Merged fanout. One shared FUniqueOutput receives entries from every unique
	 *  collection (append in encounter order). One FPCGTaggedData emitted. AssetPath/AssetClass
	 *  attributes are declared based on the union of contributing collection types so heterogeneous
	 *  mixes get both halves. */
	static void Run_MergedFanout(FPCGExGetCollectionDataContext* Context, const FOutputProcessParams& P)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetCollectionData_AdvanceWork_Merged);

		FPCGExContext* InContext = P.InContext;
		const UPCGExGetCollectionDataSettings* Settings = P.Settings;

		// Walk slots to discover unique collections (dedupe flatten work), build the Collection ->
		// RootIdx map (used both by the flatten loop below and by AnnotateSources for per-row
		// identity lookup), and pre-OR the asset-half flags.
		TArray<UPCGExAssetCollection*> UniqueCollections;
		TMap<UPCGExAssetCollection*, int32> CollectionToRootIdx;
		bool bAnyActor = false;
		bool bAnyNonActor = false;
		for (const FPCGExGetCollectionDataContext::FSlot& Slot : Context->Slots)
		{
			UPCGExAssetCollection* Collection = ResolveSlotCollection(Context, Slot);
			if (!Collection)
			{
				continue;
			}
			int32& RootIdx = CollectionToRootIdx.FindOrAdd(Collection, INDEX_NONE);
			if (RootIdx != INDEX_NONE)
			{
				continue;
			}

			RootIdx = UniqueCollections.Num();
			UniqueCollections.Add(Collection);
			Collection->EDITOR_RegisterTrackingKeys(InContext);
			if (Cast<UPCGExActorCollection>(Collection))
			{
				bAnyActor = true;
			}
			else
			{
				bAnyNonActor = true;
			}
		}

		// Allocate the single shared output. Packer registration covers every contributing host.
		FUniqueOutput Merged;
		Merged.Collection = UniqueCollections.IsEmpty() ? nullptr : UniqueCollections[0];
		Merged.OutputSet = InContext->ManagedObjects->New<UPCGParamData>();
		Merged.Entries = MakeShared<TArray<FFlattenedEntry>>();
		Merged.SeenEntries = MakeShared<TSet<const FPCGExAssetCollectionEntry*>>();
		Merged.bWantAssetPath = bAnyNonActor;
		Merged.bWantAssetClass = bAnyActor;

		for (int32 RootIdx = 0; RootIdx < UniqueCollections.Num(); ++RootIdx)
		{
			UPCGExAssetCollection* Collection = UniqueCollections[RootIdx];
			P.Packer->RegisterCollection(Collection);
			FlattenInto(Merged, Collection, /*RootCollectionIndex=*/RootIdx, P);
		}

		// Single-shot declare + write.
		if (!Merged.Entries->IsEmpty())
		{
			WriteFromEntries(Merged, P);
		}

		// Emit one FPCGTaggedData. Tag forwarding (if on) unions tags from every contributing
		// source input -- per-slot identity is lost in Merged mode by design.
		InContext->OutputData.TaggedData.Reserve(InContext->OutputData.TaggedData.Num() + 2);

		FPCGTaggedData& OutData = InContext->OutputData.TaggedData.Emplace_GetRef();
		OutData.Pin = OutputCollectionDataPin;
		OutData.Data = Merged.OutputSet;
		if (Merged.Entries->IsEmpty())
		{
			OutData.Tags.Add(EmptyTag.ToString());
		}
		if (Settings->bForwardInputTags)
		{
			TArray<FPCGTaggedData> Inputs = InContext->InputData.GetInputsByPin(SourcesPin);
			TSet<FString> TagUnion;
			for (const FPCGExGetCollectionDataContext::FSlot& Slot : Context->Slots)
			{
				if (Inputs.IsValidIndex(Slot.SourceInputIndex))
				{
					TagUnion.Append(Inputs[Slot.SourceInputIndex].Tags);
				}
			}
			OutData.Tags.Append(TagUnion);
		}

		if (Settings->bAnnotateSources)
		{
			AnnotateSources(Context, Settings, *P.FlattenCtx, CollectionToRootIdx);
		}
	}

	/** FromInputs slot-based path (PerEntry / PerInputData). One FPCGTaggedData per slot. Multiple
	 *  slots pointing at the same collection share a single UPCGParamData (tags live on the
	 *  per-slot wrapper, not the data). Flatten serial, write parallel. */
	static void Run_SlotBasedFanout(FPCGExGetCollectionDataContext* Context, const FOutputProcessParams& P)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetCollectionData_AdvanceWork_SlotBased);

		FPCGExContext* InContext = P.InContext;
		const UPCGExGetCollectionDataSettings* Settings = P.Settings;

		// Phase 1 (single-threaded): pre-allocate one UPCGParamData per unique collection. Cache the
		// per-slot resolved collection in SlotCollections so Phase 4 can skip the second resolve pass.
		TArray<FUniqueOutput> UniqueOutputs;
		TMap<UPCGExAssetCollection*, int32> CollectionToIndex;
		TArray<UPCGExAssetCollection*> SlotCollections;
		SlotCollections.Reserve(Context->Slots.Num());

		for (const FPCGExGetCollectionDataContext::FSlot& Slot : Context->Slots)
		{
			UPCGExAssetCollection* Collection = ResolveSlotCollection(Context, Slot);
			SlotCollections.Add(Collection); // nullptr stays nullptr; Phase 4 reads by index
			if (!Collection)
			{
				continue;
			}
			int32& Idx = CollectionToIndex.FindOrAdd(Collection, INDEX_NONE);
			if (Idx != INDEX_NONE)
			{
				continue;
			}
			Idx = UniqueOutputs.Num();

			FUniqueOutput& U = UniqueOutputs.AddDefaulted_GetRef();
			U.Collection = Collection;
			U.OutputSet = InContext->ManagedObjects->New<UPCGParamData>();
			U.Entries = MakeShared<TArray<FFlattenedEntry>>();
			U.SeenEntries = MakeShared<TSet<const FPCGExAssetCollectionEntry*>>();
			SetAssetHalves(U, Collection);

			Collection->EDITOR_RegisterTrackingKeys(InContext);
		}

		// Reserve TaggedData up-front (one entry per slot + one for the map) so the slot emission
		// loop doesn't pay for TArray growth reallocations.
		InContext->OutputData.TaggedData.Reserve(InContext->OutputData.TaggedData.Num() + Context->Slots.Num() + 1);

		// Phase 2 (single-threaded): packer registration.
		for (FUniqueOutput& U : UniqueOutputs)
		{
			P.Packer->RegisterCollection(U.Collection);
		}

		// Phase 3a (single-threaded flatten): assigns RootCollectionIndex (= position in UniqueOutputs)
		// and CollectionIndex (= position in CollectionIndexMap, allocated lazily as new hosts are
		// encountered, including recursed sub-collections). Must stay serial so the index space is
		// deterministic across runs regardless of how Phase 3b parallelizes.
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GetCollectionData_FlattenAll);
			for (int32 i = 0; i < UniqueOutputs.Num(); ++i)
			{
				FlattenInto(UniqueOutputs[i], UniqueOutputs[i].Collection, /*RootCollectionIndex=*/i, P);
			}
		}

		// Phase 3b (parallel over unique collections): attribute declaration + write of pre-flattened
		// entries. Threshold=2 (default 512 would never trigger here); Unbalanced because per-iteration
		// cost varies dramatically (small leaf-only collection vs deeply nested grammar tree).
		PCGExMT::ParallelOrSequential(UniqueOutputs.Num(), [&](const int32 i)
		{
			WriteFromEntries(UniqueOutputs[i], P);
		}, /*Threshold=*/2, EParallelForFlags::Unbalanced);

		// Phase 4 (single-threaded): emit one FPCGTaggedData per slot. Re-fetch input list only if
		// we'll actually forward tags.
		TArray<FPCGTaggedData> InputsForTags;
		const bool bWillForwardTags = Settings->bForwardInputTags && Settings->SourceMode == EPCGExGetCollectionDataSourceMode::FromInputs;
		if (bWillForwardTags)
		{
			InputsForTags = InContext->InputData.GetInputsByPin(SourcesPin);
		}

		UPCGParamData* EmptySentinel = nullptr;
		auto GetOrCreateEmpty = [&]() -> UPCGParamData*
		{
			if (!EmptySentinel)
			{
				EmptySentinel = InContext->ManagedObjects->New<UPCGParamData>();
			}
			return EmptySentinel;
		};

		for (int32 SlotIdx = 0; SlotIdx < Context->Slots.Num(); SlotIdx++)
		{
			const FPCGExGetCollectionDataContext::FSlot& Slot = Context->Slots[SlotIdx];
			UPCGExAssetCollection* Collection = SlotCollections[SlotIdx];

			FPCGTaggedData& OutData = InContext->OutputData.TaggedData.Emplace_GetRef();
			OutData.Pin = OutputCollectionDataPin;

			if (!Collection)
			{
				OutData.Data = GetOrCreateEmpty();
				OutData.Tags.Add(EmptyTag.ToString());
			}
			else
			{
				const int32 Idx = CollectionToIndex.FindChecked(Collection);
				const FUniqueOutput& U = UniqueOutputs[Idx];
				if (U.Entries->IsEmpty())
				{
					OutData.Data = GetOrCreateEmpty();
					OutData.Tags.Add(EmptyTag.ToString());
				}
				else
				{
					OutData.Data = U.OutputSet;
				}
			}

			if (bWillForwardTags && InputsForTags.IsValidIndex(Slot.SourceInputIndex))
			{
				OutData.Tags.Append(InputsForTags[Slot.SourceInputIndex].Tags);
			}
		}

		// Phase 5: optional annotated-source forwarding. Uses CollectionToIndex directly (it already
		// maps Collection -> RootCollectionIndex), so no extra prep is needed in this path.
		if (Settings->bAnnotateSources)
		{
			AnnotateSources(Context, Settings, *P.FlattenCtx, CollectionToIndex);
		}
	}

}

bool FPCGExGetCollectionDataElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExGetCollectionDataElement::AdvanceWork);

	PCGEX_SETTINGS_C(InContext, GetCollectionData)
	FPCGExGetCollectionDataContext* Context = static_cast<FPCGExGetCollectionDataContext*>(InContext);

	const bool bOutputWeight = Settings->bWriteWeight;
	const EPCGExWeightNormalization WeightNorm = bOutputWeight ? Settings->WeightNormalization : EPCGExWeightNormalization::None;
	const bool bWeightAsFloat = bOutputWeight && WeightNorm != EPCGExWeightNormalization::None;
	const bool bOutputCategory = Settings->bWriteCategory;
	const EPCGExCategoryInheritance CategoryInheritance = bOutputCategory ? Settings->CategoryInheritance : EPCGExCategoryInheritance::None;
	const bool bAnyGrammarField = Settings->bWriteSymbol || Settings->bWriteSize || Settings->bWriteScalable || Settings->bWriteDebugColor;

	FPCGExNameFiltersDetails CategoryFilters = Settings->CategoryFilters;
	CategoryFilters.Init();

	// Shared host -> CollectionIndex map + counter, and (Root, Coll, Depth) -> CollectionHash map
	// + counter. Lifetime tied to this AdvanceWork call. Both are populated during the
	// (single-threaded) flatten phase so the index space is deterministic regardless of how the
	// write phase is parallelized.
	TMap<const UPCGExAssetCollection*, int32> CollectionIndexMap;
	int32 NextCollectionIndex = 0;
	TMap<TTuple<int32, int32, int32>, int32> CollectionHashMap;
	int32 NextCollectionHash = 0;

	PCGExGetCollectionData::FProcessEntryContext Ctx;
	Ctx.Context = InContext;
	Ctx.CategoryFilters = &CategoryFilters;
	Ctx.SubHandling = Settings->SubCollectionHandling;
	Ctx.CategoryInheritance = CategoryInheritance;
	Ctx.bOmitInvalidAndEmpty = Settings->bOmitInvalidAndEmpty;
	Ctx.bNoDuplicates = (Settings->SkipFlags & static_cast<uint8>(EPCGExGetCollectionDataSkipFlags::Duplicates)) != 0;
	Ctx.CollectionIndexMap = &CollectionIndexMap;
	Ctx.NextCollectionIndex = &NextCollectionIndex;
	Ctx.CollectionHashMap = &CollectionHashMap;
	Ctx.NextCollectionHash = &NextCollectionHash;

	// Shared FPickPacker (covers every fanout path).
	TSharedPtr<PCGExCollections::FPickPacker> Packer = MakeShared<PCGExCollections::FPickPacker>(InContext);

	PCGExGetCollectionData::FOutputProcessParams ProcessParams;
	ProcessParams.Settings = Settings;
	ProcessParams.InContext = InContext;
	ProcessParams.Packer = Packer.Get();
	ProcessParams.FlattenCtx = &Ctx;
	ProcessParams.bOutputWeight = bOutputWeight;
	ProcessParams.WeightNorm = WeightNorm;
	ProcessParams.bWeightAsFloat = bWeightAsFloat;
	ProcessParams.bOutputCategory = bOutputCategory;
	ProcessParams.bAnyGrammarField = bAnyGrammarField;

	// Dispatch to the per-mode helper. Each helper owns its own flatten+write+emission; the shared
	// Map output is emitted here afterward so the Packer lifetime stays in this scope.
	if (Settings->SourceMode == EPCGExGetCollectionDataSourceMode::Collection)
	{
		PCGExGetCollectionData::Run_CollectionFastPath(Context, ProcessParams);
	}
	else if (Settings->Fanout == EPCGExGetCollectionDataFanout::Merged)
	{
		PCGExGetCollectionData::Run_MergedFanout(Context, ProcessParams);
	}
	else
	{
		PCGExGetCollectionData::Run_SlotBasedFanout(Context, ProcessParams);
	}

	// Shared Map output -- packed from everything the Packer has accumulated across the helpers.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetCollectionData_EmitMap);
		UPCGParamData* OutputMap = InContext->ManagedObjects->New<UPCGParamData>();
		Packer->PackToDataset(OutputMap);
		FPCGTaggedData& MapData = InContext->OutputData.TaggedData.Emplace_GetRef();
		MapData.Pin = PCGExCollections::Labels::OutputCollectionMapLabel;
		MapData.Data = OutputMap;
	}

	InContext->Done();
	return InContext->TryComplete();
}

#undef PCGEX_GCD_LEAF_ATTRS
#undef PCGEX_GCD_ROW_ATTRS
#undef PCGEX_GCD_GRAMMAR_PERAXIS_ATTRS
#undef PCGEX_GCD_GRAMMAR_SHARED_ATTRS
#undef PCGEX_VALIDATE_TOGGLED

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
