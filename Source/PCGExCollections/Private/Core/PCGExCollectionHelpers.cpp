// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExCollectionHelpers.h"

#include "PCGParamData.h"
#include "PCGExProperty.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Core/PCGExAssetCollection.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Details/PCGExStagingDetails.h"
#include "Helpers/PCGExMetaHelpersMacros.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace PCGExAttributeSetBuild
{
	struct FPropertyColumn
	{
		const FPCGMetadataAttributeBase* Attribute = nullptr;
		EPCGMetadataTypes Type = EPCGMetadataTypes::Unknown;
		int32 SchemaIndex = INDEX_NONE; // position in the built schema (== override slot index)
	};

	/**
	 * Declare one schema entry per mapped attribute on the host, then resolve each column's slot index
	 * from the built schema. Schema is built BEFORE any row exists: per-entry values are written by
	 * parallel index afterwards, never re-derived from rows (the runtime SyncToSchema resets entries
	 * to schema defaults).
	 */
	void CollectPropertyColumns(
		UPCGExAssetCollection* InCollection, FPCGExContext* InContext, const UPCGMetadata* Metadata,
		const FPCGExRoamingAssetCollectionDetails& Details, TArray<FPropertyColumn>& OutColumns, TArray<FInstancedStruct>& OutSchema)
	{
		FPCGExAttributeGatherDetails Filter = Details.PropertyAttributes;
		Filter.Init();

		TArray<FName> Names;
		TArray<EPCGMetadataTypes> Types;
		Metadata->GetAttributes(Names, Types);

		TArray<FName> ColumnNames;
		for (int32 i = 0; i < Names.Num(); i++)
		{
			const FName Name = Names[i];
			if (Name == Details.AssetPathSourceAttribute || Name == Details.WeightSourceAttribute || Name == Details.CategorySourceAttribute)
			{
				continue;
			}

			if (!Filter.Test(Name.ToString()))
			{
				continue;
			}

			FInstancedStruct Property;
			if (!PCGExProperties::MakePropertyForMetadataType(Types[i], Name, Property))
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("Attribute '{0}' has no custom property counterpart and was skipped."), FText::FromName(Name)));
				continue;
			}

			FPCGExPropertySchema& Schema = InCollection->CollectionProperties.Schemas.Emplace_GetRef();
			Schema.Name = Name;
			Schema.Property = MoveTemp(Property);
			Schema.SyncPropertyName();

			FPropertyColumn& Column = OutColumns.Emplace_GetRef();
			Column.Attribute = Metadata->GetConstAttribute(Name);
			Column.Type = Types[i];
			ColumnNames.Add(Name);
		}

		if (OutColumns.IsEmpty())
		{
			return;
		}

		TArray<FPCGExHeaderIdRemap> Remaps;
		InCollection->CollectionProperties.SyncAllSchemas(Remaps);
		OutSchema = InCollection->CollectionProperties.BuildSchema();

		// Slot index by name rather than insertion order: BuildSchema is the authority on layout.
		for (int32 c = 0; c < OutColumns.Num(); c++)
		{
			OutColumns[c].SchemaIndex = OutSchema.IndexOfByPredicate([&ColumnNames, c](const FInstancedStruct& Slot)
			{
				const FPCGExProperty* Prop = Slot.GetPtr<FPCGExProperty>();
				return Prop && Prop->PropertyName == ColumnNames[c];
			});
		}
	}

	/** Row value -> override slot, converted through the property's TryReadValue. */
	bool WriteColumn(const FPropertyColumn& Column, const int64 ItemKey, FPCGExProperty* Prop)
	{
		switch (Column.Type)
		{
#define PCGEX_WRITE_COLUMN(_TYPE, _NAME, ...) \
		case EPCGMetadataTypes::_NAME: \
			return Prop->TrySetValue<_TYPE>(static_cast<const FPCGMetadataAttribute<_TYPE>*>(Column.Attribute)->GetValueFromItemKey(ItemKey));
		PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_WRITE_COLUMN)
#undef PCGEX_WRITE_COLUMN
		default:
			return false;
		}
	}
}

namespace PCGExCollectionHelpers
{
#pragma region FSourceAssetResolver

	FSourceAssetResolver::FSourceAssetResolver()
	{
		PCGExAssetCollection::FTypeRegistry::Get().ForEach([this](const PCGExAssetCollection::FTypeInfo& Info)
		{
			if (Info.DetectSourceAsset && Info.EntryStruct)
			{
				Detectors.Add(Info);
			}
		});

		Detectors.Sort([](const PCGExAssetCollection::FTypeInfo& A, const PCGExAssetCollection::FTypeInfo& B)
		{
			return A.SourceDetectPriority < B.SourceDetectPriority;
		});
	}

	FSourceAssetResolver::~FSourceAssetResolver()
	{
		PCGExHelpers::SafeReleaseHandles(Handles);
	}

	bool FSourceAssetResolver::Resolve(const FAssetData& InAsset, FInstancedStruct& OutPayload) const
	{
		for (const PCGExAssetCollection::FTypeInfo& Info : Detectors)
		{
			if (!Info.DetectSourceAsset(InAsset))
			{
				continue;
			}

			FInstancedStruct Payload;
			if (Info.MakeEntryFromSourceAsset)
			{
				// Factory rejected on closer inspection: fall through to lower-priority detectors.
				if (!Info.MakeEntryFromSourceAsset(InAsset, Payload))
				{
					continue;
				}
			}
			else
			{
				Payload.InitializeAs(Info.EntryStruct);
				Payload.GetMutablePtr<FPCGExAssetCollectionEntry>()->SetAssetPath(InAsset.ToSoftObjectPath());
			}

			if (!Payload.GetPtr<FPCGExAssetCollectionEntry>())
			{
				continue;
			}

			OutPayload = MoveTemp(Payload);
			return true;
		}

		return false;
	}

	bool FSourceAssetResolver::ResolvePath(const FSoftObjectPath& InPath, FInstancedStruct& OutPayload, FPCGExContext* InContext)
	{
		if (!InPath.IsValid())
		{
			return false;
		}

		FAssetData Asset;
		if (const IAssetRegistry* Registry = IAssetRegistry::Get())
		{
			Asset = Registry->GetAssetByObjectPath(InPath);
		}

		if (!Asset.IsValid())
		{
			// No registry row (class paths, unscanned assets): resolve through the loaded object. A class
			// must yield its own row, not its Blueprint's (AllowBlueprintClass).
			Handles.Add(PCGExHelpers::LoadBlocking_AnyThread(InPath, InContext));
			const UObject* Object = InPath.ResolveObject();
			if (!Object)
			{
				return false;
			}
			Asset = FAssetData(Object, FAssetData::ECreationFlags::AllowBlueprintClass);
		}

		return Resolve(Asset, OutPayload);
	}

#pragma endregion

	bool BuildFromAttributeSet(
		UPCGExAssetCollection* InCollection,
		FPCGExContext* InContext,
		const UPCGParamData* InAttributeSet,
		const FPCGExRoamingAssetCollectionDetails& Details,
		bool bBuildStaging)
	{
		if (!InCollection || !InAttributeSet)
		{
			return false;
		}

		const UPCGMetadata* Metadata = InAttributeSet->Metadata;
		if (!Metadata)
		{
			return false;
		}

		const FPCGMetadataAttributeBase* PathAttribute = Metadata->GetConstAttribute(Details.AssetPathSourceAttribute);
		if (!PathAttribute)
		{
			PCGEX_LOG_INVALID_ATTR_C(InContext, Asset Path, Details.AssetPathSourceAttribute)
			return false;
		}

		const FPCGMetadataAttribute<FSoftObjectPath>* SoftPathAttribute = PathAttribute->GetTypeId() == PCG::Private::MetadataTypes<FSoftObjectPath>::Id
			? static_cast<const FPCGMetadataAttribute<FSoftObjectPath>*>(PathAttribute) : nullptr;
		const FPCGMetadataAttribute<FString>* StringPathAttribute = PathAttribute->GetTypeId() == PCG::Private::MetadataTypes<FString>::Id
			? static_cast<const FPCGMetadataAttribute<FString>*>(PathAttribute) : nullptr;
		if (!SoftPathAttribute && !StringPathAttribute)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(FTEXT("Asset path attribute '{0}' must be a Soft Object Path or a String."), FText::FromName(Details.AssetPathSourceAttribute)));
			return false;
		}

		const FPCGMetadataAttribute<int32>* WeightAttribute = nullptr;
		if (Details.WeightSourceAttribute != NAME_None)
		{
			WeightAttribute = Metadata->GetConstTypedAttribute<int32>(Details.WeightSourceAttribute);
		}

		const FPCGMetadataAttribute<FName>* CategoryAttribute = nullptr;
		if (Details.CategorySourceAttribute != NAME_None)
		{
			CategoryAttribute = Metadata->GetConstTypedAttribute<FName>(Details.CategorySourceAttribute);
		}

		const int32 NumRows = Metadata->GetLocalItemCount();
		if (NumRows == 0)
		{
			return false;
		}

		InCollection->DefaultStagingBounds = Details.GetDefaultStagingBounds();

		TArray<PCGExAttributeSetBuild::FPropertyColumn> Columns;
		TArray<FInstancedStruct> Schema;
		PCGExAttributeSetBuild::CollectPropertyColumns(InCollection, InContext, Metadata, Details, Columns, Schema);

		FSourceAssetResolver Resolver;
		TSet<const UScriptStruct*> RejectedTypes;
		int32 NumAppended = 0;

#if !WITH_EDITOR
		TMap<const UScriptStruct*, bool> StageableByType;
		int32 NumAuthored = 0;
#endif

		for (int64 ItemKey = 0; ItemKey < NumRows; ItemKey++)
		{
			const FSoftObjectPath Path = SoftPathAttribute
				? SoftPathAttribute->GetValueFromItemKey(ItemKey)
				: FSoftObjectPath(StringPathAttribute->GetValueFromItemKey(ItemKey));
			if (!Path.IsValid())
			{
				continue;
			}

			FInstancedStruct Payload;
			if (!Resolver.ResolvePath(Path, Payload, InContext))
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("Asset '{0}' could not be resolved and was skipped."), FText::FromString(Path.ToString())));
				continue;
			}

			const UScriptStruct* PayloadStruct = Payload.GetScriptStruct();
			FPCGExAssetCollectionEntry* Entry = InCollection->AddEntryOfType(PayloadStruct);
			if (!Entry)
			{
				bool bAlreadyRejected = false;
				RejectedTypes.Add(PayloadStruct, &bAlreadyRejected);
				if (!bAlreadyRejected)
				{
					PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("'{0}' entries are not supported by this collection type; matching rows were skipped."), FText::FromString(PayloadStruct->GetName())));
				}
				continue;
			}

			// The host created the row as its own type; the payload is that type or a base of it.
			PayloadStruct->CopyScriptStruct(Entry, Payload.GetMemory());

			if (WeightAttribute)
			{
				Entry->Weight = FMath::Max(1, WeightAttribute->GetValueFromItemKey(ItemKey));
			}

			if (CategoryAttribute)
			{
				Entry->Category = CategoryAttribute->GetValueFromItemKey(ItemKey);
			}

			if (!Columns.IsEmpty())
			{
				Entry->PropertyOverrides.SyncToSchema(Schema);
				for (const PCGExAttributeSetBuild::FPropertyColumn& Column : Columns)
				{
					if (!Entry->PropertyOverrides.Overrides.IsValidIndex(Column.SchemaIndex))
					{
						continue;
					}
					FPCGExPropertyOverrideEntry& Slot = Entry->PropertyOverrides.Overrides[Column.SchemaIndex];
					FPCGExProperty* Prop = Slot.GetPropertyMutable();
					Slot.bEnabled = Prop && PCGExAttributeSetBuild::WriteColumn(Column, ItemKey, Prop);
				}
			}

#if !WITH_EDITOR
			// Types whose UpdateStaging can only measure in the editor take the default box instead of
			// an empty one (after SetAssetPath, which clears bAuthored).
			bool* bStageable = StageableByType.Find(PayloadStruct);
			if (!bStageable)
			{
				PCGExAssetCollection::FTypeInfo Info;
				const bool bResolved = PCGExAssetCollection::FTypeRegistry::Get().GetInfoByEntryStruct(PayloadStruct, Info);
				bStageable = &StageableByType.Add(PayloadStruct, !bResolved || Info.bRuntimeStageable);
			}
			if (!*bStageable)
			{
				Entry->Staging.Bounds = Details.GetDefaultStagingBounds();
				Entry->Staging.bAuthored = true;
				NumAuthored++;
			}
#endif

			NumAppended++;
		}

#if !WITH_EDITOR
		if (NumAuthored > 0 && !Details.bQuietRuntimeStagingWarning)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("{0} entries (actors, levels) cannot be staged outside the editor and use Default Staging Bounds."), NumAuthored));
		}
#endif

		if (NumAppended == 0)
		{
			return false;
		}

		if (bBuildStaging)
		{
			InCollection->RebuildStagingData(false);
		}

		return true;
	}

	bool BuildFromAttributeSet(
		UPCGExAssetCollection* InCollection,
		FPCGExContext* InContext,
		FName InputPin,
		const FPCGExRoamingAssetCollectionDetails& Details,
		bool bBuildStaging)
	{
		TArray<FPCGTaggedData> Inputs = InContext->InputData.GetInputsByPin(InputPin);
		for (const FPCGTaggedData& TaggedData : Inputs)
		{
			if (const UPCGParamData* ParamData = Cast<UPCGParamData>(TaggedData.Data))
			{
				return BuildFromAttributeSet(InCollection, InContext, ParamData, Details, bBuildStaging);
			}
		}

		PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(FTEXT("No attribute set found on pin: {0}"), FText::FromName(InputPin)));
		return false;
	}

	void AccumulateTags(
		const FPCGExAssetCollectionEntry* Entry,
		uint8 TagInheritance,
		TSet<FName>& OutTags)
	{
		if (!Entry)
		{
			return;
		}

		if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Asset))
		{
			OutTags.Append(Entry->Tags);
		}

		if (Entry->HasValidSubCollection())
		{
			if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Collection))
			{
				OutTags.Append(Entry->GetSubCollectionPtr()->CollectionTags);
			}
		}
	}

	void GetAllAssetPaths(
		const UPCGExAssetCollection* Collection,
		TSet<FSoftObjectPath>& OutPaths,
		bool bRecursive)
	{
		if (!Collection)
		{
			return;
		}

		Collection->GetAssetPaths(OutPaths,
		                          bRecursive ? PCGExAssetCollection::ELoadingFlags::Recursive : PCGExAssetCollection::ELoadingFlags::Default);
	}

	bool ContainsAsset(
		const UPCGExAssetCollection* Collection,
		const FSoftObjectPath& AssetPath)
	{
		if (!Collection || !AssetPath.IsValid())
		{
			return false;
		}

		bool bFound = false;

		Collection->ForEachEntry([&](const FPCGExAssetCollectionEntry* Entry, int32 Index)
		{
			if (bFound)
			{
				return;
			}

			if (Entry->bIsSubCollection)
			{
				if (const UPCGExAssetCollection* SubCollection = Entry->GetSubCollectionPtr())
				{
					bFound = ContainsAsset(SubCollection, AssetPath);
				}
			}
			else if (Entry->Staging.Path == AssetPath)
			{
				bFound = true;
			}
		});

		return bFound;
	}

	int32 CountTotalEntries(const UPCGExAssetCollection* Collection)
	{
		if (!Collection)
		{
			return 0;
		}

		int32 Count = 0;

		Collection->ForEachEntry([&](const FPCGExAssetCollectionEntry* Entry, int32 Index)
		{
			if (Entry->bIsSubCollection)
			{
				if (const UPCGExAssetCollection* SubCollection = Entry->GetSubCollectionPtr())
				{
					Count += CountTotalEntries(SubCollection);
				}
			}
			else
			{
				Count++;
			}
		});

		return Count;
	}

	bool FlattenCollection(
		const UPCGExAssetCollection* Source,
		UPCGExAssetCollection* Target)
	{
		if (!Source || !Target)
		{
			return false;
		}

		// Must be same type
		if (Source->GetTypeId() != Target->GetTypeId())
		{
			return false;
		}

		// Count total entries first
		const int32 TotalEntries = CountTotalEntries(Source);
		if (TotalEntries == 0)
		{
			return false;
		}

		Target->InitNumEntries(TotalEntries);

		// Flatten recursively
		int32 WriteIndex = 0;

		TFunction<void(const UPCGExAssetCollection*, const TSet<FName>&)> FlattenRecursive;
		FlattenRecursive = [&](const UPCGExAssetCollection* Current, const TSet<FName>& InheritedTags)
		{
			Current->ForEachEntry([&](const FPCGExAssetCollectionEntry* Entry, int32 Index)
			{
				if (Entry->bIsSubCollection)
				{
					if (const UPCGExAssetCollection* SubCollection = Entry->GetSubCollectionPtr())
					{
						TSet<FName> CombinedTags = InheritedTags;
						CombinedTags.Append(Entry->Tags);
						CombinedTags.Append(SubCollection->CollectionTags);
						FlattenRecursive(SubCollection, CombinedTags);
					}
				}
				else
				{
					FPCGExAssetCollectionEntry* TargetEntry = nullptr;
					Target->ForEachEntry([&](FPCGExAssetCollectionEntry* E, int32 Idx)
					{
						if (Idx == WriteIndex)
						{
							TargetEntry = E;
						}
					});

					if (TargetEntry)
					{
						// Copy base properties
						TargetEntry->Weight = Entry->Weight;
						TargetEntry->Category = Entry->Category;
						TargetEntry->bIsSubCollection = false;
						TargetEntry->VariationMode = Entry->VariationMode;
						TargetEntry->Variations = Entry->Variations;
						TargetEntry->ScaleToFitSource = Entry->ScaleToFitSource;
						TargetEntry->ScaleToFit = Entry->ScaleToFit;
						TargetEntry->JustificationSource = Entry->JustificationSource;
						TargetEntry->Justification = Entry->Justification;
						TargetEntry->GrammarSource = Entry->GrammarSource;
						TargetEntry->AssetGrammar = Entry->AssetGrammar;
						TargetEntry->Staging = Entry->Staging;

						// Combine tags
						TargetEntry->Tags = InheritedTags;
						TargetEntry->Tags.Append(Entry->Tags);

						// Set asset path (triggers type-specific setup in derived entries)
						TargetEntry->SetAssetPath(Entry->Staging.Path);

						WriteIndex++;
					}
				}
			});
		};

		FlattenRecursive(Source, Source->CollectionTags);

		// Trim if we didn't fill all slots (shouldn't happen, but safety)
		if (WriteIndex < TotalEntries)
		{
			Target->InitNumEntries(WriteIndex);
		}

		return WriteIndex > 0;
	}

	void GetEntryAssetHalves(const UPCGExAssetCollection* Collection, bool& bOutAnyActor, bool& bOutAnyNonActor)
	{
		bOutAnyActor = false;
		bOutAnyNonActor = false;

		if (!Collection)
		{
			return;
		}

		// Typed Actor collections keep their legacy classification even when empty.
		if (Collection->IsType(PCGExAssetCollection::TypeIds::Actor))
		{
			bOutAnyActor = true;
		}

		Collection->ForEachEntry([&bOutAnyActor, &bOutAnyNonActor](const FPCGExAssetCollectionEntry* Entry, int32)
		{
			if (Entry->bIsSubCollection)
			{
				return;
			}

			if (Entry->IsType(PCGExAssetCollection::TypeIds::Actor))
			{
				bOutAnyActor = true;
			}
			else
			{
				bOutAnyNonActor = true;
			}
		});
	}

#if WITH_EDITOR
	void DuplicateInstancedSubobjects(const UScriptStruct* Struct, void* StructMemory, UObject* NewOuter)
	{
		if (!Struct || !StructMemory || !NewOuter)
		{
			return;
		}

		for (TFieldIterator<FObjectPropertyBase> It(Struct); It; ++It)
		{
			const FObjectPropertyBase* ObjProp = *It;
			if (!ObjProp->HasAnyPropertyFlags(CPF_InstancedReference))
			{
				continue;
			}

			UObject* Current = ObjProp->GetObjectPropertyValue_InContainer(StructMemory);
			if (Current && Current->GetOuter() != NewOuter)
			{
				ObjProp->SetObjectPropertyValue_InContainer(StructMemory, DuplicateObject(Current, NewOuter));
			}
		}
	}

	void RetireInstancedSubobjects(const UScriptStruct* Struct, void* StructMemory)
	{
		if (!Struct || !StructMemory)
		{
			return;
		}

		for (TFieldIterator<FObjectPropertyBase> It(Struct); It; ++It)
		{
			const FObjectPropertyBase* ObjProp = *It;
			if (!ObjProp->HasAnyPropertyFlags(CPF_InstancedReference))
			{
				continue;
			}

			if (UObject* Current = ObjProp->GetObjectPropertyValue_InContainer(StructMemory))
			{
				Current->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
				ObjProp->SetObjectPropertyValue_InContainer(StructMemory, nullptr);
			}
		}
	}

	void ReparentInstancedSubobjects(const UScriptStruct* Struct, void* StructMemory, UObject* NewOuter)
	{
		if (!Struct || !StructMemory || !NewOuter)
		{
			return;
		}

		for (TFieldIterator<FObjectPropertyBase> It(Struct); It; ++It)
		{
			const FObjectPropertyBase* ObjProp = *It;
			if (!ObjProp->HasAnyPropertyFlags(CPF_InstancedReference))
			{
				continue;
			}

			UObject* Current = ObjProp->GetObjectPropertyValue_InContainer(StructMemory);
			if (Current && Current->GetOuter() != NewOuter)
			{
				Current->Rename(nullptr, NewOuter, REN_DontCreateRedirectors | REN_NonTransactional);
			}
		}
	}
#endif
}
