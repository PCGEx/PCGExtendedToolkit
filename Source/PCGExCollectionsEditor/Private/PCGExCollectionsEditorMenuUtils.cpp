// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExCollectionsEditorMenuUtils.h"

#include "ContentBrowserMenuContexts.h"
#include "Core/PCGExAssetCollection.h"
#include "Details/Collections/PCGExCollectionEditorHelpers.h"
#include "Details/Collections/PCGExCollectionEditorTypeRegistry.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopedSlowTask.h"
#include "Styling/AppStyle.h"
#include "UObject/SoftObjectPath.h"

#define LOCTEXT_NAMESPACE "PCGEditorMenuUtils"

namespace PCGExCollectionsEditorMenuUtils
{
	FToolMenuSection& CreatePCGExSection(UToolMenu* Menu)
	{
		const FName LevelSectionName = TEXT("PCGEx");
		FToolMenuSection* SectionPtr = Menu->FindSection(LevelSectionName);
		if (!SectionPtr)
		{
			SectionPtr = &(Menu->AddSection(LevelSectionName, LOCTEXT("PCGExSectionLabel", "PCGEx")));
		}
		return *SectionPtr;
	}

	void AddRebuildEntries(UToolMenu* Menu, const TArray<FAssetData>& CollectionAssets)
	{
		FToolMenuSection& Section = CreatePCGExSection(Menu);

		const auto AddRebuildEntry = [&Section, &CollectionAssets](
			const FName EntryName, const FText& Label, const FText& ToolTip, const FName IconName, const bool bIncludeSubCollections)
		{
			FToolUIAction RebuildAction;
			RebuildAction.ExecuteAction.BindLambda(
				[CollectionAssets, bIncludeSubCollections](const FToolMenuContext&)
				{
					PCGExCollectionEditorHelpers::RebuildCollections(CollectionAssets, bIncludeSubCollections);
				});

			Section.AddMenuEntry(EntryName, Label, ToolTip, FSlateIcon(FAppStyle::GetAppStyleSetName(), IconName), RebuildAction);
		};

		const int32 NumSelected = CollectionAssets.Num();

		AddRebuildEntry(
			"RebuildPCGExCollectionsStaging",
			LOCTEXT("RebuildStaging", "Rebuild Staging"),
			FText::Format(
				LOCTEXT("RebuildStagingTooltip", "Rebuild staging for the {0} selected {0}|plural(one=collection,other=collections). A selected subcollection is rebuilt before the collections holding it."),
				NumSelected),
			"PCGEx.ActionIcon.RebuildStaging",
			/*bIncludeSubCollections=*/ false);

		AddRebuildEntry(
			"RebuildPCGExCollectionsStagingRecursive",
			LOCTEXT("RebuildStagingRecursive", "Rebuild Staging (Recursive)"),
			FText::Format(
				LOCTEXT("RebuildStagingRecursiveTooltip", "Rebuild staging for the {0} selected {0}|plural(one=collection,other=collections) and every subcollection they reach, each once, subcollections first."),
				NumSelected),
			"PCGEx.ActionIcon.RebuildStagingRecursive",
			/*bIncludeSubCollections=*/ true);
	}

	void AddMergeIntoOmniEntry(UToolMenu* Menu, const TArray<FAssetData>& CollectionAssets)
	{
		FToolMenuSection& Section = CreatePCGExSection(Menu);

		FToolUIAction MergeAction;
		MergeAction.ExecuteAction.BindLambda(
			[CollectionAssets](const FToolMenuContext&)
			{
				// Progress/result feedback is handled inside MergeCollectionsIntoOmni
				// (a slow-task dialog here would fight the modal save dialog it opens).
				PCGExCollectionEditorHelpers::MergeCollectionsIntoOmni(CollectionAssets);
			});

		Section.AddMenuEntry(
			"MergePCGExCollectionsIntoOmni",
			TAttribute<FText>(FText::FromString(TEXT("Merge Collection(s) into Omni Collection"))),
			TAttribute<FText>(FText::FromString(TEXT("Create (or pick) an Omni collection and append a copy of every entry from the selected collections. Collection-level globals carry over; sources are untouched."))),
			FSlateIcon(FName("PCGExStyleSet"), "ClassIcon.PCGExAssetCollection"),
			MergeAction);
	}

	void AddCreateOrUpdateEntry(UToolMenu* Menu, const TArray<FAssetData>& Assets)
	{
		TArray<const FCollectionEditorTypeInfo*> AllInfos;
		FCollectionEditorTypeRegistry::Get().GetAll(AllInfos);

		TArray<const FCollectionEditorTypeInfo*> MenuInfos;
		MenuInfos.Reserve(AllInfos.Num());
		for (const FCollectionEditorTypeInfo* Info : AllInfos)
		{
			if (Info && Info->bSupportsMenuCreation && Info->DetectSourceAsset && Info->DetectCollectionAsset)
			{
				MenuInfos.Add(Info);
			}
		}

		if (MenuInfos.IsEmpty())
		{
			return;
		}

		TMap<PCGExAssetCollection::FTypeId, TArray<FAssetData>> SourceAssetsByType;
		// Defer LoadSynchronous: store soft paths during menu construction; resolve only when
		// the user actually clicks the menu item. Avoids loading every selected collection asset
		// just to populate a menu the user may never invoke.
		TMap<PCGExAssetCollection::FTypeId, TArray<FSoftObjectPath>> CollectionPathsByType;

		for (const FAssetData& Asset : Assets)
		{
			bool bMatched = false;

			for (const FCollectionEditorTypeInfo* Info : MenuInfos)
			{
				if (Info->DetectSourceAsset(Asset))
				{
					SourceAssetsByType.FindOrAdd(Info->Id).Add(Asset);
					bMatched = true;
					break;
				}
			}

			if (bMatched)
			{
				continue;
			}

			for (const FCollectionEditorTypeInfo* Info : MenuInfos)
			{
				if (Info->DetectCollectionAsset(Asset))
				{
					CollectionPathsByType.FindOrAdd(Info->Id).Add(Asset.GetSoftObjectPath());
					break;
				}
			}
		}

		if (SourceAssetsByType.IsEmpty())
		{
			return;
		}

		FToolMenuSection& Section = CreatePCGExSection(Menu);

		FToolUIAction UIAction;
		UIAction.ExecuteAction.BindLambda(
			[SourceAssetsByType = MoveTemp(SourceAssetsByType),
				CollectionPathsByType = MoveTemp(CollectionPathsByType)](const FToolMenuContext& MenuContext)
			{
				FScopedSlowTask SlowTask(0.0f, LOCTEXT("CreateOrUpdatePCGExCollection", "Create or Update Asset Collection(s) from selection..."));

				for (const TPair<PCGExAssetCollection::FTypeId, TArray<FAssetData>>& Pair : SourceAssetsByType)
				{
					const FCollectionEditorTypeInfo* Info = FCollectionEditorTypeRegistry::Get().Find(Pair.Key);
					if (!Info)
					{
						continue;
					}

					if (const TArray<FSoftObjectPath>* ExistingPaths = CollectionPathsByType.Find(Pair.Key))
					{
						TArray<TObjectPtr<UPCGExAssetCollection>> Resolved;
						Resolved.Reserve(ExistingPaths->Num());
						for (const FSoftObjectPath& Path : *ExistingPaths)
						{
							if (UPCGExAssetCollection* Collection = Cast<UPCGExAssetCollection>(TSoftObjectPtr<UObject>(Path).LoadSynchronous()))
							{
								Resolved.Add(Collection);
							}
						}

						if (Info->UpdateCollections && !Resolved.IsEmpty())
						{
							Info->UpdateCollections(Resolved, Pair.Value);
						}
					}
					else if (Info->CreateCollection)
					{
						Info->CreateCollection(Pair.Value);
					}
				}
			});

		Section.AddMenuEntry(
			"CreateOrUpdatePCGExCollectionFromMenu",
			TAttribute<FText>(FText::FromString(TEXT("Create or Update Asset Collection(s) from selection"))),
			TAttribute<FText>(FText::FromString(TEXT("If no Asset collection is part of the selection, will create new collections from the selected source assets. If any collection is part of the selection, the matching source assets will be added to it instead."))),
			FSlateIcon(FName("PCGExStyleSet"), "ClassIcon.PCGExAssetCollection"),
			UIAction);
	}

	void ExtendAssetActionsMenu(UToolMenu* Menu, const UContentBrowserAssetContextMenuContext& Context)
	{
		TArray<FAssetData> CollectionAssets;
		for (const FAssetData& Asset : Context.SelectedAssets)
		{
			if (Asset.IsInstanceOf<UPCGExAssetCollection>())
			{
				CollectionAssets.Add(Asset);
			}
		}

		if (!CollectionAssets.IsEmpty())
		{
			// Rebuilding writes to the collections; cooked or read-only selections can't take it.
			if (Context.bCanBeModified)
			{
				AddRebuildEntries(Menu, CollectionAssets);
			}

			AddMergeIntoOmniEntry(Menu, CollectionAssets);
		}

		AddCreateOrUpdateEntry(Menu, Context.SelectedAssets);
	}

	bool DoesAssetInheritFromAActor(const FAssetData& AssetData)
	{
		static const FName ParentClassTag = "ParentClass";

		if (AssetData.AssetClassPath == UBlueprint::StaticClass()->GetClassPathName())
		{
			FString ParentClassPath;
			if (!AssetData.GetTagValue(ParentClassTag, ParentClassPath))
			{
				return false;
			}

			// Try the in-memory class first; only fall back to load if the parent isn't already
			// resident. AActor and most common BP parents are native and always loaded, so this
			// avoids forcing a package load during right-click menu construction.
			UClass* ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
			if (!ParentClass)
			{
				ParentClass = LoadObject<UClass>(nullptr, *ParentClassPath);
			}
			return ParentClass && ParentClass->IsChildOf(AActor::StaticClass());
		}

		if (AssetData.AssetClassPath == UClass::StaticClass()->GetClassPathName())
		{
			// Native class assets are always loaded -- FindObject suffices.
			UClass* AssetClass = FindObject<UClass>(nullptr, *AssetData.GetObjectPathString());
			return AssetClass && AssetClass->IsChildOf(AActor::StaticClass());
		}

		return false;
	}
}

#undef LOCTEXT_NAMESPACE
