// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExCollectionEditorHelpers.h"

#include "ContentBrowserModule.h"
#include "FileHelpers.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Collections/PCGExOmniCollection.h"
#include "Core/PCGExAssetCollection.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Package.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace PCGExCollectionEditorHelpers
{
	UPCGExAssetCollection* CreateCollectionAsset(
		const FString& AnchorPackagePath,
		UClass* CollectionClass,
		const TCHAR* DefaultAssetName,
		bool bOpenSaveDialog)
	{
		if (!CollectionClass)
		{
			return nullptr;
		}

		FString CollectionAssetName = DefaultAssetName;
		FString PackageName = FPaths::Combine(AnchorPackagePath, CollectionAssetName);

		if (bOpenSaveDialog)
		{
			FSaveAssetDialogConfig SaveAssetDialogConfig;
			SaveAssetDialogConfig.DefaultPath = AnchorPackagePath;
			SaveAssetDialogConfig.DefaultAssetName = CollectionAssetName;
			SaveAssetDialogConfig.AssetClassNames.Add(CollectionClass->GetClassPathName());
			SaveAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
			SaveAssetDialogConfig.DialogTitleOverride = NSLOCTEXT("PCGExCollections", "SaveCollectionDialogTitle", "Create Asset Collection");

			FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			const FString SaveObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);

			if (SaveObjectPath.IsEmpty())
			{
				// User cancelled the dialog -- create nothing.
				return nullptr;
			}

			CollectionAssetName = FPackageName::ObjectPathToObjectName(SaveObjectPath);
			PackageName = FPackageName::ObjectPathToPackageName(SaveObjectPath);
		}

		FText Reason;
		if (!FPackageName::IsValidObjectPath(PackageName, &Reason))
		{
			UE_LOG(LogTemp, Error, TEXT("Invalid package path '%s': %s."), *PackageName, *Reason.ToString());
			return nullptr;
		}

		UPackage* Package = FPackageName::DoesPackageExist(PackageName) ? LoadPackage(nullptr, *PackageName, LOAD_None) : nullptr;

		UPCGExAssetCollection* TargetCollection = nullptr;
		bool bIsNewCollection = false;

		if (Package)
		{
			UObject* Object = FindObjectFast<UObject>(Package, *CollectionAssetName);
			if (Object && Object->GetClass() != CollectionClass)
			{
				// Existing asset under that name isn't our collection type -- evict it.
				Object->SetFlags(RF_Transient);
				Object->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
				bIsNewCollection = true;
			}
			else
			{
				TargetCollection = Cast<UPCGExAssetCollection>(Object);
			}
		}
		else
		{
			Package = CreatePackage(*PackageName);
			if (!Package)
			{
				UE_LOG(LogTemp, Error, TEXT("Unable to create package with name '%s'."), *PackageName);
				return nullptr;
			}
			bIsNewCollection = true;
		}

		if (!TargetCollection)
		{
			constexpr EObjectFlags Flags = RF_Public | RF_Standalone | RF_Transactional;
			TargetCollection = NewObject<UPCGExAssetCollection>(Package, CollectionClass, FName(*CollectionAssetName), Flags);
		}

		if (TargetCollection && bIsNewCollection)
		{
			FAssetRegistryModule::AssetCreated(TargetCollection);
		}

		return TargetCollection;
	}

	void CreateCollectionFromTyped(
		const TArray<FAssetData>& SelectedAssets,
		UClass* CollectionClass,
		const TCHAR* DefaultAssetName,
		bool bOpenSaveDialog)
	{
		if (SelectedAssets.IsEmpty() || !CollectionClass)
		{
			return;
		}

		UPCGExAssetCollection* TargetCollection = CreateCollectionAsset(
			SelectedAssets[0].PackagePath.ToString(), CollectionClass, DefaultAssetName, bOpenSaveDialog);
		if (!TargetCollection)
		{
			return;
		}

		TArray<TObjectPtr<UPCGExAssetCollection>> Collections;
		Collections.Add(TargetCollection);
		UpdateCollectionsFromTyped(Collections, SelectedAssets);

		FEditorFileUtils::PromptForCheckoutAndSave({TargetCollection->GetOutermost()}, /*bCheckDirty=*/false, /*bPromptToSave=*/false);
	}

	void MergeCollectionsIntoOmni(const TArray<FAssetData>& SelectedCollections)
	{
		TArray<UPCGExAssetCollection*> Sources;
		FString AnchorPath;
		Sources.Reserve(SelectedCollections.Num());

		for (const FAssetData& Asset : SelectedCollections)
		{
			if (UPCGExAssetCollection* Collection = Cast<UPCGExAssetCollection>(Asset.GetAsset()))
			{
				if (AnchorPath.IsEmpty())
				{
					AnchorPath = Asset.PackagePath.ToString();
				}
				Sources.Add(Collection);
			}
		}

		if (Sources.IsEmpty())
		{
			return;
		}

		UPCGExOmniCollection* Target = Cast<UPCGExOmniCollection>(
			CreateCollectionAsset(AnchorPath, UPCGExOmniCollection::StaticClass(), TEXT("SMC_NewOmniCollection")));
		if (!Target)
		{
			return;
		}

		int32 AppendedCount = 0;
		{
			// After the modal save dialog, so the progress dialog doesn't fight it.
			FScopedSlowTask SlowTask(1.0f, NSLOCTEXT("PCGEx", "MergingIntoOmni", "Merging collection(s) into Omni collection..."));
			SlowTask.MakeDialog();
			SlowTask.EnterProgressFrame(1.0f);
			AppendedCount = Target->EDITOR_AppendCollections(Sources);
		}

		// Surface the outcome (per-source detail is in the log) -- a partial or empty merge
		// must never be silent.
		{
			FNotificationInfo Info(AppendedCount > 0
				                       ? FText::Format(NSLOCTEXT("PCGEx", "MergedIntoOmni", "Appended {0} {0}|plural(one=entry,other=entries) from {1} {1}|plural(one=collection,other=collections) into '{2}'."),
				                                       AppendedCount, Sources.Num(), FText::FromString(Target->GetName()))
				                       : NSLOCTEXT("PCGEx", "MergedIntoOmniNothing", "No entries could be appended from the selected collections (see log)."));
			Info.ExpireDuration = 5.0f;
			Info.bUseSuccessFailIcons = true;
			Info.bFireAndForget = true;
			if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
			{
				Item->SetCompletionState(AppendedCount > 0 ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
			}
		}

		if (AppendedCount > 0)
		{
			FEditorFileUtils::PromptForCheckoutAndSave({Target->GetOutermost()}, /*bCheckDirty=*/false, /*bPromptToSave=*/false);
		}
	}

	void UpdateCollectionsFromTyped(
		const TArray<TObjectPtr<UPCGExAssetCollection>>& SelectedCollections,
		const TArray<FAssetData>& SelectedAssets)
	{
		if (SelectedCollections.IsEmpty() || SelectedAssets.IsEmpty())
		{
			return;
		}

		for (const TObjectPtr<UPCGExAssetCollection>& Collection : SelectedCollections)
		{
			Collection->EDITOR_AddBrowserSelectionTyped(SelectedAssets);
		}
	}
}
