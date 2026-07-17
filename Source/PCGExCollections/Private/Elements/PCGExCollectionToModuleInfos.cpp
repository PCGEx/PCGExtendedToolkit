// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExCollectionToModuleInfos.h"


#include "PCGExProperty.h"
#include "PCGGraph.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "Core/PCGExAssetCollection.h"
#include "Elements/Grammar/PCGSubdivisionBase.h"
#include "Helpers/PCGExCollectionPropertySetWriter.h"
#include "Helpers/PCGExMetaHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExGraphSettings"
#define PCGEX_NAMESPACE CollectionToModuleInfos

#pragma region UPCGSettings interface

#if WITH_EDITOR
TOptional<FPCGNodeThumbnailProxy> UPCGExCollectionToModuleInfosSettings::GetNodeThumbnail() const
{
	if (!AssetCollection.IsNull())
	{
		return FPCGNodeThumbnailProxy::FromAssetPath(AssetCollection.ToSoftObjectPath());
	}

	return {};
}
#endif

TArray<FPCGPinProperties> UPCGExCollectionToModuleInfosSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExCollectionToModuleInfosSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_PARAM(FName("ModuleInfos"), TEXT("Module infos generated from the selected collection"), Normal)
	PCGEX_PIN_PARAM(PCGExCollections::Labels::OutputCollectionMapLabel, "Collection map", Normal)
	return PinProperties;
}

FPCGElementPtr UPCGExCollectionToModuleInfosSettings::CreateElement() const
{
	return MakeShared<FPCGExCollectionToModuleInfosElement>();
}

#pragma endregion

#define PCGEX_FOREACH_MODULE_FIELD(MACRO)\
MACRO(Symbol, FName, Infos.Symbol, NAME_None)\
MACRO(Size, double, Infos.Size, 0)\
MACRO(Scalable, bool, Infos.bScalable, true)\
MACRO(DebugColor, FVector4, Infos.DebugColor, FVector4(1,1,1,1))\
MACRO(Entry, int64, Idx, 0)\
MACRO(Category, FName, Entry->Category, NAME_None)

bool FPCGExCollectionToModuleInfosElement::IsCacheable(const UPCGSettings* InSettings) const
{
	const UPCGExCollectionToModuleInfosSettings* Settings = static_cast<const UPCGExCollectionToModuleInfosSettings*>(InSettings);
	PCGEX_GET_OPTION_STATE(Settings->CacheData, bDefaultCacheNodeOutput)
}

bool FPCGExCollectionToModuleInfosElement::Boot(FPCGExContext* InContext) const
{
	PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Collection to Module Infos is DEPRECATED -- Use Get Collection Data instead."));
	return IPCGExElement::Boot(InContext);
}

bool FPCGExCollectionToModuleInfosElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_SETTINGS_C(InContext, CollectionToModuleInfos)

	PCGExHelpers::LoadBlocking_AnyThreadTpl(Settings->AssetCollection, InContext);
	UPCGExAssetCollection* MainCollection = Settings->AssetCollection.Get();

	if (!MainCollection)
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Mesh collection failed to load."));
		return true;
	}

	TSharedPtr<PCGExCollections::FPickPacker> Packer = MakeShared<PCGExCollections::FPickPacker>(InContext);
	Packer->RegisterCollection(MainCollection);

	MainCollection->EDITOR_RegisterTrackingKeys(InContext);

	UPCGParamData* OutputModules = NewObject<UPCGParamData>();
	UPCGMetadata* Metadata = OutputModules->Metadata;

#define PCGEX_DECLARE_ATT(_NAME, _TYPE, _GETTER, _DEFAULT) \
	FPCGMetadataAttribute<_TYPE>* _NAME##Attribute = nullptr; \
	PCGEX_VALIDATE_NAME(Settings->_NAME##AttributeName) _NAME##Attribute = Metadata->FindOrCreateAttribute<_TYPE>(PCGExMetaHelpers::GetAttributeIdentifier(Settings->_NAME##AttributeName, OutputModules), _DEFAULT, false, true);
	PCGEX_FOREACH_MODULE_FIELD(PCGEX_DECLARE_ATT);
#undef PCGEX_DECLARE_ATT

	TSet<FName> UniqueSymbols;
	UniqueSymbols.Reserve(100);

	FPCGExGrammarSizeCache SizeCache;
	SizeCache.Reserve(100);

	TArray<PCGExCollectionToGrammar::FModule> Modules;
	Modules.Reserve(100);

	FPCGExNameFiltersDetails CategoryFilters = Settings->CategoryFilters;
	CategoryFilters.Init();

	FlattenCollection(Packer, MainCollection, Settings, CategoryFilters, Modules, UniqueSymbols, SizeCache);

	// Prepare custom property outputs. Fallback hosts = unique per-module hosts (for heterogeneous
	// nested collections where a property is only declared on a sub-host).
	TArray<const UPCGExAssetCollection*> FallbackHosts;
	{
		TSet<const UPCGExAssetCollection*> Seen;
		for (const PCGExCollectionToGrammar::FModule& Module : Modules)
		{
			if (!Module.Host || Module.Host == MainCollection)
			{
				continue;
			}
			bool bAlreadyIn = false;
			Seen.Add(Module.Host, &bAlreadyIn);
			if (!bAlreadyIn)
			{
				FallbackHosts.Add(Module.Host);
			}
		}
	}

	PCGExCollections::FPCGExCollectionPropertySetWriter PropertyWriter;
	PropertyWriter.Initialize(InContext, Settings->PropertyOutputSettings, MainCollection, FallbackHosts, Metadata);

	for (const PCGExCollectionToGrammar::FModule& Module : Modules)
	{
		const int64 Key = Metadata->AddEntry();
#define PCGEX_MODULE_OUT(_NAME, _TYPE, _GETTER, _DEFAULT) _NAME##Attribute->SetValue(Key, Module._GETTER);
		PCGEX_FOREACH_MODULE_FIELD(PCGEX_MODULE_OUT)
#undef PCGEX_MODULE_OUT

		PropertyWriter.WriteEntry(Key, Module.Entry, Module.Host);
	}

	FPCGTaggedData& ModulesData = InContext->OutputData.TaggedData.Emplace_GetRef();
	ModulesData.Pin = FName("ModuleInfos");
	ModulesData.Data = OutputModules;

	UPCGParamData* OutputMap = NewObject<UPCGParamData>();
	Packer->PackToDataset(OutputMap);

	FPCGTaggedData& CollectionMapData = InContext->OutputData.TaggedData.Emplace_GetRef();
	CollectionMapData.Pin = PCGExCollections::Labels::OutputCollectionMapLabel;
	CollectionMapData.Data = OutputMap;

	InContext->Done();
	return InContext->TryComplete();
}

void FPCGExCollectionToModuleInfosElement::FlattenCollection(
	const TSharedPtr<PCGExCollections::FPickPacker>& Packer,
	const UPCGExAssetCollection* Collection,
	const UPCGExCollectionToModuleInfosSettings* Settings,
	const FPCGExNameFiltersDetails& CategoryFilters,
	TArray<PCGExCollectionToGrammar::FModule>& OutModules,
	TSet<FName>& UniqueSymbols,
	FPCGExGrammarSizeCache& SizeCache) const
{
	if (!Collection)
	{
		return;
	}

	const PCGExAssetCollection::FCache* Cache = const_cast<UPCGExAssetCollection*>(Collection)->LoadCache();
	const int32 NumEntries = Cache->Main->Order.Num();

	const FPCGExAssetCollectionEntry* Entry = nullptr;

	for (int i = 0; i < NumEntries; i++)
	{
		FPCGExEntryAccessResult Result = Collection->GetEntryAt(i);
		Entry = Result.Entry;

		if (!Entry)
		{
			continue;
		}

		if (!CategoryFilters.Test(Entry->Category.ToString()))
		{
			continue;
		}

		if (Entry->bIsSubCollection && Entry->SubGrammarMode == EPCGExGrammarSubCollectionMode::Flatten)
		{
			FlattenCollection(Packer, Entry->GetSubCollection<UPCGExAssetCollection>(), Settings, CategoryFilters, OutModules, UniqueSymbols, SizeCache);
			continue;
		}

		PCGExCollectionToGrammar::FModule& Module = OutModules.Emplace_GetRef();
		// Deprecated node: always queries the X axis (legacy single-axis behavior). Per-axis
		// output is exclusive to the new PCGExGetCollectionData node.
		if (!Entry->FixModuleInfos(Collection, Module.Infos, EPCGExGrammarAxes::X) || (Settings->bSkipEmptySymbol && Module.Infos.Symbol.IsNone()))
		{
			OutModules.Pop(EAllowShrinking::No);
			continue;
		}

		bool bIsAlreadyInSet = false;
		UniqueSymbols.Add(Module.Infos.Symbol, &bIsAlreadyInSet);

		if (bIsAlreadyInSet && !Settings->bAllowDuplicates)
		{
			OutModules.Pop(EAllowShrinking::No);
			continue;
		}

		Module.Entry = Entry;
		Module.Host = Result.Host;
		Module.Idx = Packer->GetPickIdx(Result.Host, Entry->Staging.InternalIndex, 0);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
#undef PCGEX_FOREACH_MODULE_FIELD
