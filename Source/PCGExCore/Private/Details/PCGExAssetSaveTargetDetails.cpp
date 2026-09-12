// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Details/PCGExAssetSaveTargetDetails.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGGraphExecutionStateInterface.h"
#include "PCGModule.h"

#include "Core/PCGExContext.h"
#include "Core/PCGExMT.h"
#include "CoreGlobals.h"
#include "Helpers/PCGHelpers.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#endif

#define LOCTEXT_NAMESPACE "PCGExAssetSaveTargetDetails"

namespace PCGExAssetSave
{
	bool ResolveTarget(const FPCGExAssetSaveTargetDetails& InTarget, FString& OutPackagePath, FString& OutAssetName, FText& OutError)
	{
		OutPackagePath.Reset();
		OutAssetName.Reset();

		if (!InTarget.IsTargetSet())
		{
			OutError = LOCTEXT("TargetUnset", "Both an Asset Path and an Asset Name are required.");
			return false;
		}

		const FString PackagePath = FPaths::Combine(InTarget.AssetPath, InTarget.AssetName);

		FText Reason;
		if (!FPackageName::IsValidObjectPath(PackagePath, &Reason))
		{
			OutError = FText::Format(LOCTEXT("InvalidPackagePath", "Invalid target path '{0}': {1}"), FText::FromString(PackagePath), Reason);
			return false;
		}

		// An unmounted destination cannot be written; report it rather than redirecting the write.
		if (FPackageName::GetPackageMountPoint(PackagePath).IsNone())
		{
			OutError = FText::Format(LOCTEXT("UnmountedPath", "Target path '{0}' is not under any mounted content root."), FText::FromString(PackagePath));
			return false;
		}

		OutPackagePath = PackagePath;
		OutAssetName = InTarget.AssetName;
		return true;
	}

	bool CanWriteSourceContent(const FPCGExContext* InContext)
	{
#if WITH_EDITOR
		if (!InContext) { return false; }

		// Creating packages and UObjects is illegal mid-SavePackage / GC.
		if (PCGExMT::IsObjectWorkBlocked()) { return false; }

		// A cook must never write source content.
		if (IsRunningCommandlet()) { return false; }

		// IsInPreviewMode below is an editing-mode flag, not a "playing" one: a PIE component clears it.
		if (PCGHelpers::IsRuntimeOrPIE()) { return false; }

		const IPCGGraphExecutionSource* Source = InContext->ExecutionSource.Get();
		if (!Source) { return false; }

		const IPCGGraphExecutionState& State = Source->GetExecutionState();

		// Partitioned generation runs this element once per local component; they would all race for one path.
		if (State.IsLocalSource() || State.IsPartitioned()) { return false; }
		if (State.IsManagedByRuntimeGenSystem()) { return false; }

		// Preview persists nothing. Off the component, not the execution state: 5.7's has no IsInPreviewMode.
		const UPCGComponent* Component = InContext->GetComponent();
		if (Component && Component->IsInPreviewMode()) { return false; }

		return true;
#else
		return false;
#endif
	}

	UObject* FindOrCreateAsset(const FPCGExAssetSaveTargetDetails& InTarget, const UClass* InClass, FPCGExContext* InContext, bool& bOutCreated)
	{
		bOutCreated = false;

#if WITH_EDITOR
		if (!InClass) { return nullptr; }

		FString PackagePath;
		FString AssetName;
		FText ResolveError;
		if (!ResolveTarget(InTarget, PackagePath, AssetName, ResolveError))
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, ResolveError);
			return nullptr;
		}

		UPackage* Package = FPackageName::DoesPackageExist(PackagePath) ? LoadPackage(nullptr, *PackagePath, LOAD_None) : FindPackage(nullptr, *PackagePath);

		if (Package)
		{
			// An unloaded on-disk stub is what SavePackage refuses to overwrite.
			if (!Package->IsFullyLoaded()) { Package->FullyLoad(); }

			if (UObject* Occupant = FindObjectFast<UObject>(Package, *AssetName))
			{
				if (Occupant->GetClass() != InClass)
				{
					PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(
						           LOCTEXT("OccupantWrongClass", "'{0}' already holds a {1}; refusing to replace it."),
						           FText::FromString(PackagePath), FText::FromString(Occupant->GetClass()->GetName())));
					return nullptr;
				}

				// Reuse the object: a fresh one would mint a new identity and unbind external references.
				return Occupant;
			}
		}
		else
		{
			Package = CreatePackage(*PackagePath);
			if (!Package)
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(
					           LOCTEXT("CannotCreatePackage", "Unable to create package '{0}'."), FText::FromString(PackagePath)));
				return nullptr;
			}
		}

		UObject* Asset = NewObject<UObject>(Package, const_cast<UClass*>(InClass), FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		bOutCreated = Asset != nullptr;
		return Asset;
#else
		return nullptr;
#endif
	}

	bool TargetAssetExists(const FPCGExAssetSaveTargetDetails& InTarget)
	{
		FString PackagePath;
		FString AssetName;
		FText ResolveError;
		if (!ResolveTarget(InTarget, PackagePath, AssetName, ResolveError)) { return false; }

		if (FPackageName::DoesPackageExist(PackagePath)) { return true; }

		// An in-memory package counts too: with bSaveToDiskImmediately off, nothing ever reaches disk.
		const UPackage* Package = FindPackage(nullptr, *PackagePath);
		return Package && FindObjectFast<UObject>(const_cast<UPackage*>(Package), *AssetName) != nullptr;
	}

	void FinalizeAsset(const FPCGExAssetSaveTargetDetails& InTarget, UObject* InAsset, const bool bWasCreated, FPCGExContext* InContext)
	{
#if WITH_EDITOR
		if (!InAsset) { return; }

		if (bWasCreated) { FAssetRegistryModule::AssetCreated(InAsset); }
		else { FCoreUObjectDelegates::BroadcastOnObjectModified(InAsset); }

		UPackage* Package = InAsset->GetOutermost();
		if (!Package) { return; }

		Package->MarkPackageDirty();

		if (!InTarget.bSaveToDiskImmediately) { return; }

		// Not FEditorFileUtils: it opens a modal slow-task and force-ticks Slate even unattended, which must
		// not happen inside element execution.
		const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		if (!UPackage::SavePackage(Package, InAsset, *FileName, SaveArgs))
		{
			// Read-only file, no source-control checkout, unwritable mount. The package stays dirty.
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(
				           LOCTEXT("SaveFailed", "Could not write '{0}' to disk (it may be read-only or not checked out). The asset is in memory and left dirty."),
				           FText::FromString(FileName)));
		}
#endif
	}
}

#undef LOCTEXT_NAMESPACE
