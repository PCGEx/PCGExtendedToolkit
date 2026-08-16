// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExPropertySchemaAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#if WITH_EDITOR
FOnPCGExSchemaAssetChanged UPCGExPropertySchemaAsset::OnAnySchemaAssetChanged;

void UPCGExPropertySchemaAsset::PostLoad()
{
	Super::PostLoad();

	// Self-heal: closes the "asset saved with stale caches because a referenced asset drifted
	// after this asset's last save" gap at the source. Order matters -- ReconcileImportOverrides
	// reads each Source schema's outer Name + HeaderId, which SyncAllSchemas canonicalizes first.
	Collection.SyncAllSchemas();
	Collection.ReconcileImportOverrides();
}

void UPCGExPropertySchemaAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Keep HeaderId / PropertyName in sync for any newly added or edited local schemas.
	// Imported assets sync themselves through their own PostEditChangeProperty.
	Collection.SyncAllSchemas();

	OnSchemaAssetChanged.Broadcast(this);
	OnAnySchemaAssetChanged.Broadcast(this);
}

EDataValidationResult UPCGExPropertySchemaAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// Only surface cycles that reach back to this asset -- other cycles in the graph are
	// reported by their own participants' IsDataValid calls.
	TSet<const UPCGExPropertySchemaAsset*> Visited;
	Visited.Add(this);

	TArray<const UPCGExPropertySchemaAsset*> Stack;

	auto TryEnqueue = [&](UPCGExPropertySchemaAsset* Asset)
	{
		if (!Asset)
		{
			return;
		}
		if (Asset == this)
		{
			Context.AddWarning(FText::FromString(FString::Printf(
				TEXT("Schema asset '%s' is part of a cyclic import chain and will be skipped during resolution."),
				*GetPathName())));
			Result = CombineDataValidationResults(Result, EDataValidationResult::Invalid);
			return;
		}
		bool bAlreadyVisited = false;
		Visited.Add(Asset, &bAlreadyVisited);
		if (!bAlreadyVisited)
		{
			Stack.Add(Asset);
		}
	};

	for (const TObjectPtr<UPCGExPropertySchemaAsset>& AssetPtr : Collection.ImportedSchemas)
	{
		TryEnqueue(AssetPtr.Get());
	}

	while (!Stack.IsEmpty())
	{
		const UPCGExPropertySchemaAsset* Current = Stack.Pop(EAllowShrinking::No);
		for (const TObjectPtr<UPCGExPropertySchemaAsset>& AssetPtr : Current->Collection.ImportedSchemas)
		{
			TryEnqueue(AssetPtr.Get());
		}
	}

	return Result;
}
#endif
