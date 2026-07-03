// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExCollectionEditorUtils.h"

#include "ContentBrowserModule.h"
#include "FileHelpers.h"
#include "IContentBrowserSingleton.h"
#include "ScopedTransaction.h"
#include "Core/PCGExAssetCollection.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace PCGExCollectionEditorUtils
{
#define PCGEX_IF_TYPE(_NAME, _BODY) { if (UPCGEx##_NAME##Collection* Collection = Cast<UPCGEx##_NAME##Collection>(InCollection)) { _BODY; return; }}
#define PCGEX_PER_COLLECTION(_BODY)	PCGEX_FOREACH_COLLECTION_TYPE(PCGEX_IF_TYPE, _BODY)

	FAssetData ResolveEntryAssetData(const FSoftObjectPath& AssetPath)
	{
		const IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(AssetPath);

		// Actor entries store the generated class path ("_C"); retry against the Blueprint asset.
		if (!AssetData.IsValid())
		{
			FString PathString = AssetPath.ToString();
			if (PathString.EndsWith(TEXT("_C")))
			{
				PathString.LeftChopInline(2);
				AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(PathString));
			}
		}

		return AssetData;
	}

	// Notify listeners that the collection was modified (for grid view refresh, etc.)
	static void NotifyModified(UPCGExAssetCollection* InCollection)
	{
		(void)InCollection->MarkPackageDirty();
		FCoreUObjectDelegates::BroadcastOnObjectModified(InCollection);
	}

	void AddBrowserSelection(UPCGExAssetCollection* InCollection)
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		TArray<FAssetData> SelectedAssets;
		ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

		if (SelectedAssets.IsEmpty())
		{
			return;
		}

		InCollection->EDITOR_AddBrowserSelectionTyped(SelectedAssets);
	}

	UPCGExAssetCollection* CreateCollectionFromSelection(
		TSubclassOf<UPCGExAssetCollection> InCollectionClass,
		const FString& InDefaultAssetName,
		const TArray<FAssetData>& InSelectedAssets,
		bool bOpenSaveDialog)
	{
		UClass* CollectionClass = InCollectionClass.Get();
		if (InSelectedAssets.IsEmpty() || !CollectionClass)
		{
			return nullptr;
		}

		FString AssetName = InDefaultAssetName;
		FString AssetPath = InSelectedAssets[0].PackagePath.ToString();
		FString PackageName = FPaths::Combine(AssetPath, AssetName);

		if (bOpenSaveDialog)
		{
			FSaveAssetDialogConfig SaveAssetDialogConfig;
			SaveAssetDialogConfig.DefaultPath = AssetPath;
			SaveAssetDialogConfig.DefaultAssetName = AssetName;
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

			AssetName = FPackageName::ObjectPathToObjectName(SaveObjectPath);
			PackageName = FPackageName::ObjectPathToPackageName(SaveObjectPath);
		}

		// Perform some validation on the package name, so we can prevent crashes downstream when trying to create or save the package.
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
			UObject* Object = FindObjectFast<UObject>(Package, *AssetName);
			if (Object && Object->GetClass() != CollectionClass)
			{
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

			if (Package)
			{
				bIsNewCollection = true;
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Unable to create package with name '%s'."), *PackageName);
				return nullptr;
			}
		}

		if (!TargetCollection)
		{
			constexpr EObjectFlags Flags = RF_Public | RF_Standalone | RF_Transactional;
			TargetCollection = NewObject<UPCGExAssetCollection>(Package, CollectionClass, FName(*AssetName), Flags);
		}

		if (TargetCollection)
		{
			if (bIsNewCollection)
			{
				// Notify the asset registry
				FAssetRegistryModule::AssetCreated(TargetCollection);
			}

			TargetCollection->EDITOR_AddBrowserSelectionTyped(InSelectedAssets);
		}

		// Save the file
		if (Package)
		{
			FEditorFileUtils::PromptForCheckoutAndSave({Package}, /*bCheckDirty=*/false, /*bPromptToSave=*/false);
		}

		return TargetCollection;
	}

	void SortByWeightAscending(UPCGExAssetCollection* InCollection)
	{
		FScopedTransaction Transaction(INVTEXT("Sort Collection by Weight (Ascending)"));
		InCollection->Modify();
		InCollection->Sort([&](const FPCGExAssetCollectionEntry* A, const FPCGExAssetCollectionEntry* B)
		{
			return A->Weight < B->Weight;
		});
		NotifyModified(InCollection);
	}

	void SortByWeightDescending(UPCGExAssetCollection* InCollection)
	{
		FScopedTransaction Transaction(INVTEXT("Sort Collection by Weight (Descending)"));
		InCollection->Modify();
		InCollection->Sort([&](const FPCGExAssetCollectionEntry* A, const FPCGExAssetCollectionEntry* B)
		{
			return A->Weight > B->Weight;
		});
		NotifyModified(InCollection);
	}

	void SetWeightIndex(UPCGExAssetCollection* InCollection)
	{
		FScopedTransaction Transaction(INVTEXT("Set Weights to Index"));
		InCollection->Modify();
		InCollection->ForEachEntry([&](FPCGExAssetCollectionEntry* Entry, int32 i)
		{
			Entry->Weight = i + 1;
		});
		NotifyModified(InCollection);
	}

	void PadWeight(UPCGExAssetCollection* InCollection)
	{
		FScopedTransaction Transaction(INVTEXT("Pad Weights (+1)"));
		InCollection->Modify();
		InCollection->ForEachEntry([&](FPCGExAssetCollectionEntry* Entry, int32 i)
		{
			Entry->Weight += 1;
		});
		NotifyModified(InCollection);
	}

	void MultWeight(UPCGExAssetCollection* InCollection, int32 Mult)
	{
		FScopedTransaction Transaction(FText::Format(INVTEXT("Multiply Weights (x{0})"), FText::AsNumber(Mult)));
		InCollection->Modify();
		InCollection->ForEachEntry([&](FPCGExAssetCollectionEntry* Entry, int32 i)
		{
			Entry->Weight *= Mult;
		});
		NotifyModified(InCollection);
	}

	void WeightOne(UPCGExAssetCollection* InCollection)
	{
		FScopedTransaction Transaction(INVTEXT("Reset Weights to 100"));
		InCollection->Modify();
		InCollection->ForEachEntry([&](FPCGExAssetCollectionEntry* Entry, int32 i)
		{
			Entry->Weight = 100;
		});
		NotifyModified(InCollection);
	}

	void WeightRandom(UPCGExAssetCollection* InCollection)
	{
		FScopedTransaction Transaction(INVTEXT("Randomize Weights"));
		InCollection->Modify();
		FRandomStream RandomSource(FMath::Rand());
		const int32 NumEntries = InCollection->NumEntries();
		InCollection->ForEachEntry(
			[&](FPCGExAssetCollectionEntry* Entry, int32 i)
			{
				Entry->Weight = RandomSource.RandRange(1, NumEntries * 100);
			});
		NotifyModified(InCollection);
	}

	void NormalizedWeightToSum(UPCGExAssetCollection* InCollection)
	{
		FScopedTransaction Transaction(INVTEXT("Normalize Weights to 100"));
		InCollection->Modify();
		double Sum = 0;

		InCollection->ForEachEntry([&](const FPCGExAssetCollectionEntry* Entry, int32 i)
		{
			Sum += Entry->Weight;
		});
		InCollection->ForEachEntry([&](FPCGExAssetCollectionEntry* Entry, int32 i)
		{
			int32& W = Entry->Weight;

			if (W <= 0)
			{
				W = 0;
				return;
			}

			const double Weight = (static_cast<double>(W) / Sum) * 100;
			W = Weight;
		});
		NotifyModified(InCollection);
	}
}
