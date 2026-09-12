// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExAssetSaveTargetDetails.generated.h"

struct FPCGExContext;

/** Where a node writes a generated asset: FPCGAssetExporterParameters minus its save dialog, which would be
 *  a modal opened mid-execution. Editor-only in effect -- the helpers below no-op elsewhere. */
USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExAssetSaveTargetDetails
{
	GENERATED_BODY()

	/** Target asset path to write the generated asset to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FString AssetPath;

	/** Name of the generated asset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FString AssetName;

	/** Write the package to disk now. Off leaves it dirty for the next Save All -- a timing switch, not a safety one. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bSaveToDiskImmediately = true;

	/** A destination is authored well enough to attempt a save. */
	bool IsTargetSet() const { return !AssetName.IsEmpty() && !AssetPath.IsEmpty(); }
};

/** Writing a generated UObject into a content package. Free functions, not members: fab-preflight's
 *  editor-guard-free check only parses namespace scope. */
namespace PCGExAssetSave
{
	/** AssetPath / AssetName. False + OutError on an invalid or unmounted path. No mount healing by design:
	 *  a destination is reported, never redirected. */
	PCGEXCORE_API bool ResolveTarget(const FPCGExAssetSaveTargetDetails& InTarget, FString& OutPackagePath, FString& OutAssetName, FText& OutError);

	/** This session may write source content: editor, not preview / PIE / RuntimeGen / cook, not a
	 *  partitioned local source, object work unblocked. */
	PCGEXCORE_API bool CanWriteSourceContent(const FPCGExContext* InContext);

	/** Find-or-create the target asset, reusing an occupant of the same class so its identity survives a
	 *  re-save. Null on refusal; an occupant of a different class is refused. */
	PCGEXCORE_API UObject* FindOrCreateAsset(const FPCGExAssetSaveTargetDetails& InTarget, const UClass* InClass, FPCGExContext* InContext, bool& bOutCreated);

	/** True when the target already resolves to an asset, on disk or created in memory this session. */
	PCGEXCORE_API bool TargetAssetExists(const FPCGExAssetSaveTargetDetails& InTarget);

	/** Registry-notify a newly created asset, dirty the package, and write it when the target asks. */
	PCGEXCORE_API void FinalizeAsset(const FPCGExAssetSaveTargetDetails& InTarget, UObject* InAsset, bool bWasCreated, FPCGExContext* InContext);
}
