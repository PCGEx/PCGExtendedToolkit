// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExCollectionsEditorMenuUtils.h"

#include "Collections/PCGExActorCollection.h"
#include "Collections/PCGExLevelCollection.h"
#include "Collections/PCGExMeshCollection.h"
#include "Details/Collections/PCGExActorCollectionActions.h"
#include "Details/Collections/PCGExLevelCollectionActions.h"
#include "Details/Collections/PCGExMeshCollectionActions.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Misc/ScopedSlowTask.h"

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

		FToolMenuSection& Section = *SectionPtr;
		return Section;
	}

	void CreateOrUpdatePCGExAssetCollectionsFromMenu(UToolMenu* Menu, TArray<FAssetData>& Assets)
	{
		TArray<FAssetData> TempStaticMeshes;
		TArray<TObjectPtr<UPCGExMeshCollection>> TempMeshCollections;

		TArray<FAssetData> TempActorAssets;
		TArray<TObjectPtr<UPCGExActorCollection>> TempActorCollections;

		TArray<FAssetData> TempLevelAssets;
		TArray<TObjectPtr<UPCGExLevelCollection>> TempLevelCollections;

		for (const FAssetData& Asset : Assets)
		{
			if (Asset.IsInstanceOf<UStaticMesh>())
			{
				TempStaticMeshes.Add(Asset);
				continue;
			}

			if (Asset.IsInstanceOf<UPCGExMeshCollection>())
			{
				if (UPCGExMeshCollection* Collection = TSoftObjectPtr<UPCGExMeshCollection>(Asset.GetSoftObjectPath()).LoadSynchronous())
				{
					TempMeshCollections.Add(Collection);
				}

				continue;
			}

			if (Asset.AssetClassPath == UWorld::StaticClass()->GetClassPathName())
			{
				TempLevelAssets.Add(Asset);
				continue;
			}

			if (Asset.IsInstanceOf<UPCGExLevelCollection>())
			{
				if (UPCGExLevelCollection* Collection = TSoftObjectPtr<UPCGExLevelCollection>(Asset.GetSoftObjectPath()).LoadSynchronous())
				{
					TempLevelCollections.Add(Collection);
				}

				continue;
			}

			if (DoesAssetInheritFromAActor(Asset))
			{
				TempActorAssets.Add(Asset);
				continue;
			}

			if (Asset.IsInstanceOf<UPCGExActorCollection>())
			{
				if (UPCGExActorCollection* Collection = TSoftObjectPtr<UPCGExActorCollection>(Asset.GetSoftObjectPath()).LoadSynchronous())
				{
					TempActorCollections.Add(Collection);
				}
			}
		}

		if (TempStaticMeshes.IsEmpty() && TempActorAssets.IsEmpty() && TempLevelAssets.IsEmpty())
		{
			return;
		}

		FToolMenuSection& Section = CreatePCGExSection(Menu);

		if (!TempStaticMeshes.IsEmpty() || !TempActorAssets.IsEmpty() || !TempLevelAssets.IsEmpty())
		{
			FToolUIAction UIAction;
			UIAction.ExecuteAction.BindLambda(
				[
					Meshes = MoveTemp(TempStaticMeshes),
					MeshCollections = MoveTemp(TempMeshCollections),
					Actors = MoveTemp(TempActorAssets),
					ActorCollections = MoveTemp(TempActorCollections),
					Levels = MoveTemp(TempLevelAssets),
					LevelCollections = MoveTemp(TempLevelCollections)](const FToolMenuContext& MenuContext)
				{
					FScopedSlowTask SlowTask(0.0f, LOCTEXT("CreateOrUpdatePCGExMeshCollection", "Create or Update Asset Collection(s) from selection..."));

					if (MeshCollections.IsEmpty()) { PCGExMeshCollectionActions::CreateCollectionFrom(Meshes); }
					else { PCGExMeshCollectionActions::UpdateCollectionsFrom(MeshCollections, Meshes); }

					if (ActorCollections.IsEmpty()) { PCGExActorCollectionActions::CreateCollectionFrom(Actors); }
					else { PCGExActorCollectionActions::UpdateCollectionsFrom(ActorCollections, Actors); }

					if (LevelCollections.IsEmpty()) { PCGExLevelCollectionActions::CreateCollectionFrom(Levels); }
					else { PCGExLevelCollectionActions::UpdateCollectionsFrom(LevelCollections, Levels); }
				});

			Section.AddMenuEntry(
				"CreateOrUpdatePCGExMeshCollectionFromMenu",
				TAttribute<FText>(FText::FromString(TEXT("Create or Update Asset Collection(s) from selection"))),
				TAttribute<FText>(FText::FromString(TEXT("If no Asset collection is part of the selection, will create new Mesh and/or Actor collections. If any collection is part of the selection, the selected mesh and/or actor will be added to the selected collection instead."))),
				FSlateIcon(FName("PCGExStyleSet"), "ClassIcon.PCGExAssetCollection"),
				UIAction);
		}
	}

	bool DoesAssetInheritFromAActor(const FAssetData& AssetData)
	{
		static const FName ParentClassTag = "ParentClass"; // Used to get parent class

		// Check if the asset is a Blueprint
		if (AssetData.AssetClassPath == UBlueprint::StaticClass()->GetClassPathName())
		{
			FString ParentClassPath;
			if (AssetData.GetTagValue(ParentClassTag, ParentClassPath))
			{
				UObject* ParentClassObject = StaticLoadObject(UClass::StaticClass(), nullptr, *ParentClassPath);
				UClass* ParentClass = Cast<UClass>(ParentClassObject);

				return ParentClass && ParentClass->IsChildOf(AActor::StaticClass());
			}
		}
		// Check if the asset is a native class
		else if (AssetData.AssetClassPath == UClass::StaticClass()->GetClassPathName())
		{
			UClass* AssetClass = Cast<UClass>(AssetData.GetAsset());
			return AssetClass && AssetClass->IsChildOf(AActor::StaticClass());
		}

		return false;
	}
}

#undef LOCTEXT_NAMESPACE
