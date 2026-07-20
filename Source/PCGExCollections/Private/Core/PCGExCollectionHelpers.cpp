// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExCollectionHelpers.h"

#include "PCGParamData.h"
#include "Core/PCGExAssetCollection.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Details/PCGExStagingDetails.h"
#include "UObject/UnrealType.h"

namespace PCGExCollectionHelpers
{
	bool BuildFromAttributeSet(
		UPCGExAssetCollection* InCollection,
		FPCGExContext* InContext,
		const UPCGParamData* InAttributeSet,
		const FPCGExAssetAttributeSetDetails& Details,
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

		// Get path attribute
		const FPCGMetadataAttributeBase* PathAttribute = Metadata->GetConstAttribute(Details.AssetPathSourceAttribute);
		if (!PathAttribute)
		{
			PCGEX_LOG_INVALID_ATTR_C(InContext, Asset Path, Details.AssetPathSourceAttribute)
			return false;
		}

		// Optional weight attribute
		const FPCGMetadataAttribute<int32>* WeightAttribute = nullptr;
		if (Details.WeightSourceAttribute != NAME_None)
		{
			WeightAttribute = Metadata->GetConstTypedAttribute<int32>(Details.WeightSourceAttribute);
		}

		// Optional category attribute
		const FPCGMetadataAttribute<FName>* CategoryAttribute = nullptr;
		if (Details.CategorySourceAttribute != NAME_None)
		{
			CategoryAttribute = Metadata->GetConstTypedAttribute<FName>(Details.CategorySourceAttribute);
		}

		// Get entry count
		const int32 NumEntries = Metadata->GetLocalItemCount();
		if (NumEntries == 0)
		{
			return false;
		}

		// Initialize collection entries
		InCollection->InitNumEntries(NumEntries);

		// Populate entries
		int32 ValidEntries = 0;
		for (int64 ItemKey = 0; ItemKey < NumEntries; ItemKey++)
		{
			FSoftObjectPath Path;

			// Extract path based on attribute type
			if (const FPCGMetadataAttribute<FSoftObjectPath>* SoftPathAttr = static_cast<const FPCGMetadataAttribute<FSoftObjectPath>*>(PathAttribute);
				SoftPathAttr && PathAttribute->GetTypeId() == PCG::Private::MetadataTypes<FSoftObjectPath>::Id)
			{
				Path = SoftPathAttr->GetValueFromItemKey(ItemKey);
			}
			else if (const FPCGMetadataAttribute<FString>* StringAttr = static_cast<const FPCGMetadataAttribute<FString>*>(PathAttribute);
				StringAttr && PathAttribute->GetTypeId() == PCG::Private::MetadataTypes<FString>::Id)
			{
				Path = FSoftObjectPath(StringAttr->GetValueFromItemKey(ItemKey));
			}
			else
			{
				continue; // Skip unsupported attribute types
			}

			if (!Path.IsValid())
			{
				continue;
			}

			// Get mutable entry via ForEach (a bit awkward but maintains abstraction)
			FPCGExAssetCollectionEntry* Entry = nullptr;
			int32 CurrentIndex = ValidEntries;
			InCollection->ForEachEntry([&](FPCGExAssetCollectionEntry* E, int32 Idx)
			{
				if (Idx == CurrentIndex)
				{
					Entry = E;
				}
			});

			if (!Entry)
			{
				continue;
			}

			Entry->SetAssetPath(Path);

			if (WeightAttribute)
			{
				Entry->Weight = FMath::Max(1, WeightAttribute->GetValueFromItemKey(ItemKey));
			}

			if (CategoryAttribute)
			{
				Entry->Category = CategoryAttribute->GetValueFromItemKey(ItemKey);
			}

			ValidEntries++;
		}

		// Trim to valid entries
		if (ValidEntries < NumEntries)
		{
			InCollection->InitNumEntries(ValidEntries);
		}

		if (bBuildStaging)
		{
			InCollection->RebuildStagingData(false);
		}

		return ValidEntries > 0;
	}

	bool BuildFromAttributeSet(
		UPCGExAssetCollection* InCollection,
		FPCGExContext* InContext,
		FName InputPin,
		const FPCGExAssetAttributeSetDetails& Details,
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
					// Get target entry
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
#endif
}
