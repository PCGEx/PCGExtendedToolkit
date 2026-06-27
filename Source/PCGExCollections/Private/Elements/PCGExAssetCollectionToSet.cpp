// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExAssetCollectionToSet.h"

#include "PCGGraph.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "Collections/PCGExActorCollection.h"
#include "Helpers/PCGExCollectionPropertySetWriter.h"
#include "Helpers/PCGExMetaHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExGraphSettings"
#define PCGEX_NAMESPACE AssetCollectionToSet

#pragma region UPCGSettings interface

UPCGExAssetCollectionToSetSettings::UPCGExAssetCollectionToSetSettings()
{
	CacheLoadedResources = EPCGExOptionState::Enabled;
}

#if WITH_EDITOR
void UPCGExAssetCollectionToSetSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	bWriteAssetClass = bWriteAssetPath;
	AssetClassAttributeName = AssetPathAttributeName;
}
#endif


TArray<FPCGPinProperties> UPCGExAssetCollectionToSetSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExAssetCollectionToSetSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_PARAM(FName("AttributeSet"), TEXT("Attribute set generated from collection"), Required)
	return PinProperties;
}

FPCGElementPtr UPCGExAssetCollectionToSetSettings::CreateElement() const
{
	return MakeShared<FPCGExAssetCollectionToSetElement>();
}

#pragma endregion

// Note: Weight and Category are intentionally NOT in this list -- they're handled explicitly.
// Weight: attribute type (int32 vs float) and value (raw vs normalized) depend on WeightNormalization.
// Category: value is read from FEntryWithHost::Category (resolved during recursion via CategoryInheritance),
// not from E->Category.
#define PCGEX_FOREACH_COL_FIELD(MACRO)\
MACRO(AssetPath, FSoftObjectPath, FSoftObjectPath(), E->Staging.Path)\
MACRO(AssetClass, FSoftClassPath, FSoftClassPath(), E->Staging.Path.ToString())\
MACRO(Extents, FVector, FVector::OneVector, E->Staging.Bounds.GetExtent())\
MACRO(BoundsMin, FVector, FVector::OneVector, E->Staging.Bounds.Min)\
MACRO(BoundsMax, FVector, FVector::OneVector, E->Staging.Bounds.Max)\
MACRO(NestingDepth, int32, -1, -1)

bool FPCGExAssetCollectionToSetElement::IsCacheable(const UPCGSettings* InSettings) const
{
	const UPCGExAssetCollectionToSetSettings* Settings = static_cast<const UPCGExAssetCollectionToSetSettings*>(InSettings);
	PCGEX_GET_OPTION_STATE(Settings->CacheData, bDefaultCacheNodeOutput)
}

bool FPCGExAssetCollectionToSetElement::Boot(FPCGExContext* InContext) const
{
	PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Asset Collection to Set is DEPRECATED -- Use Get Collection Data instead."));
	return IPCGExElement::Boot(InContext);
}

bool FPCGExAssetCollectionToSetElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_SETTINGS_C(InContext, AssetCollectionToSet)

	UPCGParamData* OutputSet = NewObject<UPCGParamData>();

	auto OutputToPin = [InContext, OutputSet]()
	{
		FPCGTaggedData& OutData = InContext->OutputData.TaggedData.Emplace_GetRef();
		OutData.Pin = FName("AttributeSet");
		OutData.Data = OutputSet;

		InContext->Done();
		return InContext->TryComplete();
	};

	PCGExHelpers::LoadBlockingTracked_AnyThread(Settings->AssetCollection.ToSoftObjectPath(), InContext);
	UPCGExAssetCollection* MainCollection = Settings->AssetCollection.Get();

	if (!MainCollection)
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Asset collection failed to load."));
		return OutputToPin();
	}

	MainCollection->EDITOR_RegisterTrackingKeys(InContext);

#define PCGEX_DECLARE_ATT(_NAME, _TYPE, _DEFAULT, _VALUE) bool bOutput##_NAME = Settings->bWrite##_NAME;
	PCGEX_FOREACH_COL_FIELD(PCGEX_DECLARE_ATT);
#undef PCGEX_DECLARE_ATT

	const bool bOutputWeight = Settings->bWriteWeight;
	const EPCGExWeightNormalization WeightNorm = bOutputWeight ? Settings->WeightNormalization : EPCGExWeightNormalization::None;
	const bool bWeightAsFloat = bOutputWeight && WeightNorm != EPCGExWeightNormalization::None;

	const bool bOutputCategory = Settings->bWriteCategory;
	const EPCGExCategoryInheritance CategoryInheritance = bOutputCategory ? Settings->CategoryInheritance : EPCGExCategoryInheritance::None;

	// Output actor as FSoftClassPath
	if (Cast<UPCGExActorCollection>(MainCollection))
	{
		bOutputAssetPath = false;
	}
	else
	{
		bOutputAssetClass = false;
	}

#define PCGEX_DECLARE_ATT(_NAME, _TYPE, _DEFAULT, _VALUE) \
	FPCGMetadataAttribute<_TYPE>* _NAME##Attribute = nullptr; \
	if(bOutput##_NAME){PCGEX_VALIDATE_NAME(Settings->_NAME##AttributeName) _NAME##Attribute = OutputSet->Metadata->FindOrCreateAttribute<_TYPE>(PCGExMetaHelpers::GetAttributeIdentifier(Settings->_NAME##AttributeName, OutputSet), _DEFAULT, false, true);}
	PCGEX_FOREACH_COL_FIELD(PCGEX_DECLARE_ATT);
#undef PCGEX_DECLARE_ATT

	FPCGMetadataAttribute<int32>* WeightAttributeInt = nullptr;
	FPCGMetadataAttribute<float>* WeightAttributeFloat = nullptr;
	if (bOutputWeight)
	{
		PCGEX_VALIDATE_NAME(Settings->WeightAttributeName)
		const FPCGAttributeIdentifier WeightId = PCGExMetaHelpers::GetAttributeIdentifier(Settings->WeightAttributeName, OutputSet);
		if (bWeightAsFloat)
		{
			WeightAttributeFloat = OutputSet->Metadata->FindOrCreateAttribute<float>(WeightId, 0.0f, false, true);
		}
		else
		{
			WeightAttributeInt = OutputSet->Metadata->FindOrCreateAttribute<int32>(WeightId, 0, false, true);
		}
	}

	FPCGMetadataAttribute<FName>* CategoryAttribute = nullptr;
	if (bOutputCategory)
	{
		PCGEX_VALIDATE_NAME(Settings->CategoryAttributeName)
		CategoryAttribute = OutputSet->Metadata->FindOrCreateAttribute<FName>(PCGExMetaHelpers::GetAttributeIdentifier(Settings->CategoryAttributeName, OutputSet), NAME_None, false, true);
	}

	const PCGExAssetCollection::FCache* MainCache = MainCollection->LoadCache();
	TArray<FEntryWithHost> Entries;

	TSet<uint64> GUIDS;

	FPCGExNameFiltersDetails CategoryFilters = Settings->CategoryFilters;
	CategoryFilters.Init();

	EPCGExSubCollectionToSet EffectiveSubHandling = Settings->SubCollectionHandling;
	if (EffectiveSubHandling == EPCGExSubCollectionToSet::Grammar)
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Grammar sub-collection mode is only supported on the Get Collection Data node. Falling back to Ignore."));
		EffectiveSubHandling = EPCGExSubCollectionToSet::Ignore;
	}

	FProcessEntryContext Ctx;
	Ctx.Context = InContext;
	Ctx.CategoryFilters = &CategoryFilters;
	Ctx.SubHandling = EffectiveSubHandling;
	Ctx.CategoryInheritance = CategoryInheritance;
	Ctx.bOmitInvalidAndEmpty = Settings->bOmitInvalidAndEmpty;
	Ctx.bNoDuplicates = !Settings->bAllowDuplicates;

	for (int i = 0; i < MainCache->Main->Order.Num(); i++)
	{
		GUIDS.Empty();
		FPCGExEntryAccessResult Result = MainCollection->GetEntryAt(i);
		ProcessEntry(Ctx, Result.Entry, Result.Host, Entries, NAME_None, GUIDS);
	}

	if (Entries.IsEmpty())
	{
		return OutputToPin();
	}

	// Custom property outputs. Fallback hosts = unique collected hosts (for heterogeneous nested
	// collections where a property is only declared on a sub-host).
	TArray<const UPCGExAssetCollection*> FallbackHosts;
	{
		TSet<const UPCGExAssetCollection*> Seen;
		for (const FEntryWithHost& EH : Entries)
		{
			if (!EH.Host || EH.Host == MainCollection)
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
	}

	PCGExCollections::FPCGExCollectionPropertySetWriter PropertyWriter;
	PropertyWriter.Initialize(InContext, Settings->PropertyOutputSettings, MainCollection, FallbackHosts, OutputSet->Metadata);

	// Weight normalization pre-pass. Only real (non-placeholder) entries contribute to the sums.
	double GlobalWeightSum = 0.0;
	TMap<FName, double> CategoryWeightSums;
	TMap<const UPCGExAssetCollection*, double> CollectionWeightSums;
	if (bWeightAsFloat)
	{
		for (const FEntryWithHost& EH : Entries)
		{
			const FPCGExAssetCollectionEntry* E = EH.Entry;
			if (!E || E->bIsSubCollection)
			{
				continue;
			}
			const double W = E->Weight;
			switch (WeightNorm)
			{
			case EPCGExWeightNormalization::Global:
				GlobalWeightSum += W;
				break;
			case EPCGExWeightNormalization::PerCategory:
				CategoryWeightSums.FindOrAdd(EH.Category) += W;
				break;
			case EPCGExWeightNormalization::PerCollection:
				CollectionWeightSums.FindOrAdd(EH.Host) += W;
				break;
			default: ;
			}
		}
	}

	for (const FEntryWithHost& EH : Entries)
	{
		const FPCGExAssetCollectionEntry* E = EH.Entry;
		const int64 Key = OutputSet->Metadata->AddEntry();

		if (!E || E->bIsSubCollection)
		{
#define PCGEX_SET_ATT(_NAME, _TYPE, _DEFAULT, _VALUE) if(_NAME##Attribute){ _NAME##Attribute->SetValue(Key, _DEFAULT); }
			PCGEX_FOREACH_COL_FIELD(PCGEX_SET_ATT)
#undef PCGEX_SET_ATT
			if (CategoryAttribute)
			{
				CategoryAttribute->SetValue(Key, NAME_None);
			}
			if (WeightAttributeInt)
			{
				WeightAttributeInt->SetValue(Key, 0);
			}
			else if (WeightAttributeFloat)
			{
				WeightAttributeFloat->SetValue(Key, 0.0f);
			}
			continue;
		}

#define PCGEX_SET_ATT(_NAME, _TYPE, _DEFAULT, _VALUE) if(_NAME##Attribute){ _NAME##Attribute->SetValue(Key, _VALUE); }
		PCGEX_FOREACH_COL_FIELD(PCGEX_SET_ATT)
#undef PCGEX_SET_ATT

		if (CategoryAttribute)
		{
			CategoryAttribute->SetValue(Key, EH.Category);
		}

		if (WeightAttributeInt)
		{
			WeightAttributeInt->SetValue(Key, E->Weight);
		}
		else if (WeightAttributeFloat)
		{
			double Denom = 0.0;
			switch (WeightNorm)
			{
			case EPCGExWeightNormalization::Global:
				Denom = GlobalWeightSum;
				break;
			case EPCGExWeightNormalization::PerCategory:
				Denom = CategoryWeightSums.FindRef(EH.Category);
				break;
			case EPCGExWeightNormalization::PerCollection:
				Denom = CollectionWeightSums.FindRef(EH.Host);
				break;
			default: ;
			}
			WeightAttributeFloat->SetValue(Key, Denom > 0.0 ? static_cast<float>(static_cast<double>(E->Weight) / Denom) : 0.0f);
		}

		PropertyWriter.WriteEntry(Key, E, EH.Host);
	}

	return OutputToPin();
}

void FPCGExAssetCollectionToSetElement::ProcessEntry(
	const FProcessEntryContext& Ctx,
	const FPCGExAssetCollectionEntry* InEntry,
	const UPCGExAssetCollection* InHost,
	TArray<FEntryWithHost>& OutEntries,
	const FName EffectiveParentCategory,
	TSet<uint64>& GUIDS)
{
	if (Ctx.bNoDuplicates)
	{
		for (const FEntryWithHost& Existing : OutEntries)
		{
			if (Existing.Entry == InEntry)
			{
				return;
			}
		}
	}

	auto AddNone = [&]()
	{
		if (Ctx.bOmitInvalidAndEmpty)
		{
			return;
		}
		OutEntries.Add({nullptr, nullptr, NAME_None});
	};

	if (!InEntry)
	{
		AddNone();
		return;
	}

	// Filters always run on the authored category, never on the resolved one -- inheritance is an
	// output concern.
	if (!Ctx.CategoryFilters->Test(InEntry->Category.ToString()))
	{
		return;
	}

	// Resolve the category for THIS entry against the inherited chain.
	auto ResolveCategory = [&](const FName Authored) -> FName
	{
		switch (Ctx.CategoryInheritance)
		{
		case EPCGExCategoryInheritance::FillEmpty:
			return Authored.IsNone() ? EffectiveParentCategory : Authored;
		case EPCGExCategoryInheritance::Replace:
			return EffectiveParentCategory.IsNone() ? Authored : EffectiveParentCategory;
		default:
			return Authored;
		}
	};

	auto AddEmpty = [&](const FPCGExAssetCollectionEntry* S)
	{
		if (Ctx.bOmitInvalidAndEmpty)
		{
			return;
		}
		OutEntries.Add({S, InHost, NAME_None});
	};

	if (InEntry->bIsSubCollection)
	{
		if (Ctx.SubHandling == EPCGExSubCollectionToSet::Ignore)
		{
			return;
		}

		UPCGExAssetCollection* SubCollection = InEntry->Staging.LoadSync<UPCGExAssetCollection>(Ctx.Context);
		const PCGExAssetCollection::FCache* SubCache = SubCollection ? SubCollection->LoadCache() : nullptr;

		if (!SubCache)
		{
			AddEmpty(InEntry);
			return;
		}

		bool bVisited = false;
		GUIDS.Add(SubCollection->GetUniqueID(), &bVisited);
		if (bVisited)
		{
			return;
		} // !! Circular dependency !!

		// Cascade: a None-stub doesn't reset the chain -- the closest non-None ancestor wins.
		const FName NextParent = InEntry->Category.IsNone() ? EffectiveParentCategory : InEntry->Category;

		FPCGExEntryAccessResult SubResult;
		switch (Ctx.SubHandling)
		{
		default: ;
		case EPCGExSubCollectionToSet::Expand:
			for (int i = 0; i < SubCache->Main->Order.Num(); i++)
			{
				SubResult = SubCollection->GetEntryAt(i);
				ProcessEntry(Ctx, SubResult.Entry, SubResult.Host, OutEntries, NextParent, GUIDS);
			}
			return;
		case EPCGExSubCollectionToSet::PickRandom:
			SubResult = SubCollection->GetEntryRandom(0);
			break;
		case EPCGExSubCollectionToSet::PickRandomWeighted:
			SubResult = SubCollection->GetEntryWeightedRandom(0);
			break;
		case EPCGExSubCollectionToSet::PickFirstItem:
			SubResult = SubCollection->GetEntryAt(0);
			break;
		case EPCGExSubCollectionToSet::PickLastItem:
			SubResult = SubCollection->GetEntryAt(SubCache->Main->Indices.Num() - 1);
			break;
		}

		ProcessEntry(Ctx, SubResult.Entry, SubResult.Host, OutEntries, NextParent, GUIDS);
	}
	else
	{
		OutEntries.Add({InEntry, InHost, ResolveCategory(InEntry->Category)});
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
#undef PCGEX_FOREACH_COL_FIELD
