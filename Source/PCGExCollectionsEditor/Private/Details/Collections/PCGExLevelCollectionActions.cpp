// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExLevelCollectionActions.h"

#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "FileHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Collections/PCGExLevelCollection.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "ToolMenuSection.h"
#include "Details/Collections/PCGExLevelCollectionEditor.h"
#include "Details/Collections/PCGExAssetCollectionEditor.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Views/SListView.h"

namespace PCGExLevelCollectionActions
{
	void CreateCollectionFrom(const TArray<FAssetData>& SelectedAssets)
	{
		if (SelectedAssets.IsEmpty()) { return; }

		if (SelectedAssets.Num() > 1)
		{
		}

		FString CollectionAssetName = TEXT("SMC_NewLevelCollection");
		FString CollectionAssetPath = SelectedAssets[0].PackagePath.ToString();
		FString PackageName = FPaths::Combine(CollectionAssetPath, CollectionAssetName);

		FText Reason;
		if (!FPackageName::IsValidObjectPath(PackageName, &Reason))
		{
			UE_LOG(LogTemp, Error, TEXT("Invalid package path '%s': %s."), *PackageName, *Reason.ToString());
			return;
		}

		UPackage* Package = FPackageName::DoesPackageExist(PackageName) ? LoadPackage(nullptr, *PackageName, LOAD_None) : nullptr;

		UPCGExLevelCollection* TargetCollection = nullptr;
		bool bIsNewCollection = false;

		if (Package)
		{
			UObject* Object = FindObjectFast<UObject>(Package, *CollectionAssetName);
			if (Object && Object->GetClass() != UPCGExLevelCollection::StaticClass())
			{
				Object->SetFlags(RF_Transient);
				Object->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
				bIsNewCollection = true;
			}
			else
			{
				TargetCollection = Cast<UPCGExLevelCollection>(Object);
			}
		}
		else
		{
			Package = CreatePackage(*PackageName);

			if (Package)
			{
				bIsNewCollection = true;
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Unable to create package with name '%s'."), *PackageName);
				return;
			}
		}

		if (!TargetCollection)
		{
			constexpr EObjectFlags Flags = RF_Public | RF_Standalone | RF_Transactional;
			TargetCollection = NewObject<UPCGExLevelCollection>(Package, UPCGExLevelCollection::StaticClass(), FName(*CollectionAssetName), Flags);
		}

		if (TargetCollection)
		{
			if (bIsNewCollection)
			{
				// Notify the asset registry
				FAssetRegistryModule::AssetCreated(TargetCollection);
			}

			TArray<TObjectPtr<UPCGExLevelCollection>> SelectedCollections;
			SelectedCollections.Add(TargetCollection);

			UpdateCollectionsFrom(SelectedCollections, SelectedAssets, bIsNewCollection);
		}

		// Save the file
		if (Package)
		{
			FEditorFileUtils::PromptForCheckoutAndSave({Package}, /*bCheckDirty=*/false, /*bPromptToSave=*/false);
		}
	}

	void UpdateCollectionsFrom(
		const TArray<TObjectPtr<UPCGExLevelCollection>>& SelectedCollections,
		const TArray<FAssetData>& SelectedAssets,
		bool bIsNewCollection)
	{
		if (SelectedCollections.IsEmpty() || SelectedAssets.IsEmpty()) { return; }

		for (const TObjectPtr<UPCGExLevelCollection>& Collection : SelectedCollections)
		{
			Collection->EDITOR_AddBrowserSelectionTyped(SelectedAssets);
		}
	}
}

FText FPCGExLevelCollectionActions::GetName() const
{
	return INVTEXT("PCGEx Collection | Level");
}

FString FPCGExLevelCollectionActions::GetObjectDisplayName(UObject* Object) const
{
	return Object->GetName();
}

UClass* FPCGExLevelCollectionActions::GetSupportedClass() const
{
	return UPCGExLevelCollection::StaticClass();
}

FColor FPCGExLevelCollectionActions::GetTypeColor() const
{
	return FColor(255, 156, 0);
}

uint32 FPCGExLevelCollectionActions::GetCategories()
{
	return EAssetTypeCategories::Misc;
}

bool FPCGExLevelCollectionActions::HasActions(const TArray<UObject*>& InObjects) const
{
	return false;
}

void FPCGExLevelCollectionActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	for (UObject* Obj : InObjects)
	{
		if (UPCGExLevelCollection* Collection = Cast<UPCGExLevelCollection>(Obj))
		{
			TSharedRef<FPCGExLevelCollectionEditor> Editor = MakeShared<FPCGExLevelCollectionEditor>();
			Editor->InitEditor(Collection, EToolkitMode::Standalone, EditWithinLevelEditor);
		}
	}
}
