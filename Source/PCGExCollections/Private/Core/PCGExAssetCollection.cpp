// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExStagingBoundsModifier.h"

#include "PCGExLog.h"
#include "PCGExProperty.h"
#include "PCGExPropertySchemaAsset.h"
#include "StaticMeshResources.h"
#include "Algo/BinarySearch.h"
#include "Algo/RemoveIf.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGExObjectNotifyHelpers.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/IConsoleManager.h"
#include "Hash/Blake3.h"
#include "Helpers/PCGExCollectionStagingPipeline.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/Script.h"
#include "UObject/StructOnScope.h"
#endif

bool FPCGExEntryAccessResult::IsType(PCGExAssetCollection::FTypeId TypeId) const
{
	return Entry ? Entry->IsType(TypeId) : false;
}

#pragma region FPCGExAssetStagingData

bool FPCGExAssetStagingData::FindSocket(FName InName, const FPCGExSocket*& OutSocket) const
{
	for (const FPCGExSocket& Socket : Sockets)
	{
		if (Socket.SocketName == InName)
		{
			OutSocket = &Socket;
			return true;
		}
	}
	return false;
}

bool FPCGExAssetStagingData::FindSocket(FName InName, const FString& Tag, const FPCGExSocket*& OutSocket) const
{
	for (const FPCGExSocket& Socket : Sockets)
	{
		if (Socket.SocketName == InName && Socket.Tag == Tag)
		{
			OutSocket = &Socket;
			return true;
		}
	}
	return false;
}

#pragma endregion

namespace PCGExAssetCollection
{
	// Shared helper: sorts Order by Weights ascending, converts Weights to cumulative sums.
	// Used by both FMicroCache::BuildFromWeights() and FCategory::Compile().
	static double CompileWeightedOrder(TArray<int32>& Weights, TArray<int32>& Order)
	{
		PCGExArrayHelpers::ArrayOfIndices(Order, Weights.Num());

		Order.Sort([&Weights](int32 A, int32 B)
		{
			return Weights[A] < Weights[B];
		});
		Weights.Sort([](int32 A, int32 B)
		{
			return A < B;
		});

		double WeightSum = 0;
		for (int32 i = 0; i < Weights.Num(); i++)
		{
			WeightSum += Weights[i];
			Weights[i] = static_cast<int32>(WeightSum);
		}
		return WeightSum;
	}

#pragma region FEntryIdBank

	void FEntryIdBank::Deposit(const uint32 InExactKey, const uint32 InLooseKey, const int32 InEntryId)
	{
		if (InEntryId == 0 || (InExactKey == 0 && InLooseKey == 0))
		{
			return;
		}

		const int32 DepositIndex = Deposits.Add(FDeposit{InEntryId, false});
		if (InExactKey != 0)
		{
			ExactToDeposits.FindOrAdd(InExactKey).Add(DepositIndex);
		}
		if (InLooseKey != 0)
		{
			LooseToDeposits.FindOrAdd(InLooseKey).Add(DepositIndex);
		}
	}

	void FEntryIdBank::Deposit(const UPCGExAssetCollection* InCollection, TFunctionRef<uint32(const FPCGExAssetCollectionEntry&, int32)> InKeyFunc)
	{
		if (!InCollection)
		{
			return;
		}
		InCollection->ForEachEntry([this, &InKeyFunc](const FPCGExAssetCollectionEntry* Entry, const int32 Index)
		{
			Deposit(InKeyFunc(*Entry, Index), 0, Entry->EntryId);
		});
	}

	int32 FEntryIdBank::ClaimExact(const uint32 InExactKey)
	{
		ensureMsgf(!bLooseClaimStarted, TEXT("FEntryIdBank: exact claims must all happen before the first loose claim."));

		if (InExactKey == 0)
		{
			return 0;
		}

		if (const TArray<int32>* Bucket = ExactToDeposits.Find(InExactKey))
		{
			for (const int32 DepositIndex : *Bucket)
			{
				FDeposit& D = Deposits[DepositIndex];
				if (!D.bClaimed)
				{
					D.bClaimed = true;
					return D.EntryId;
				}
			}
		}
		return 0;
	}

	int32 FEntryIdBank::ClaimLoose(const uint32 InLooseKey)
	{
		bLooseClaimStarted = true;

		if (InLooseKey == 0)
		{
			return 0;
		}

		if (const TArray<int32>* Bucket = LooseToDeposits.Find(InLooseKey))
		{
			for (const int32 DepositIndex : *Bucket)
			{
				FDeposit& D = Deposits[DepositIndex];
				if (!D.bClaimed)
				{
					D.bClaimed = true;
					return D.EntryId;
				}
			}
		}
		return 0;
	}

#pragma endregion

#pragma region FMicroCache

	int32 FMicroCache::GetPick(int32 Index, EPCGExIndexPickMode PickMode) const
	{
		switch (PickMode)
		{
		default:
		case EPCGExIndexPickMode::Ascending:
			return GetPickAscending(Index);
		case EPCGExIndexPickMode::Descending:
			return GetPickDescending(Index);
		case EPCGExIndexPickMode::WeightAscending:
			return GetPickWeightAscending(Index);
		case EPCGExIndexPickMode::WeightDescending:
			return GetPickWeightDescending(Index);
		}
	}

	int32 FMicroCache::GetPickAscending(int32 Index) const
	{
		return Order.IsValidIndex(Index) ? Index : -1;
	}

	int32 FMicroCache::GetPickDescending(int32 Index) const
	{
		return Order.IsValidIndex(Index) ? (Order.Num() - 1) - Index : -1;
	}

	int32 FMicroCache::GetPickWeightAscending(int32 Index) const
	{
		return Order.IsValidIndex(Index) ? Order[Index] : -1;
	}

	int32 FMicroCache::GetPickWeightDescending(int32 Index) const
	{
		return Order.IsValidIndex(Index) ? Order[(Order.Num() - 1) - Index] : -1;
	}

	int32 FMicroCache::GetPickRandom(int32 Seed) const
	{
		if (Order.IsEmpty())
		{
			return -1;
		}
		return Order[FRandomStream(Seed).RandRange(0, Order.Num() - 1)];
	}

	int32 FMicroCache::GetPickRandomWeighted(int32 Seed) const
	{
		if (Order.IsEmpty())
		{
			return -1;
		}

		const int32 Threshold = FRandomStream(Seed).RandRange(0, static_cast<int32>(WeightSum) - 1);
		// Weights is a sorted cumulative array after BuildFromWeights -- first bucket > Threshold.
		const int32 Pick = Algo::UpperBound(Weights, Threshold);
		return Order[FMath::Min(Pick, Order.Num() - 1)];
	}

	void FMicroCache::BuildFromWeights(TConstArrayView<int32> InWeights)
	{
		const int32 NumEntries = InWeights.Num();

		Weights.SetNumUninitialized(NumEntries);
		for (int32 i = 0; i < NumEntries; i++)
		{
			Weights[i] = InWeights[i] + 1; // +1 to ensure non-zero (Weight=0 entries are already excluded by Validate)
		}

		WeightSum = CompileWeightedOrder(Weights, Order);
	}

#pragma endregion

#pragma region FCategory

	int32 FCategory::GetPick(int32 Index, EPCGExIndexPickMode PickMode) const
	{
		switch (PickMode)
		{
		default:
		case EPCGExIndexPickMode::Ascending:
			return GetPickAscending(Index);
		case EPCGExIndexPickMode::Descending:
			return GetPickDescending(Index);
		case EPCGExIndexPickMode::WeightAscending:
			return GetPickWeightAscending(Index);
		case EPCGExIndexPickMode::WeightDescending:
			return GetPickWeightDescending(Index);
		}
	}

	int32 FCategory::GetPickAscending(int32 Index) const
	{
		return Indices.IsValidIndex(Index) ? Indices[Index] : -1;
	}

	int32 FCategory::GetPickDescending(int32 Index) const
	{
		return Indices.IsValidIndex(Index) ? Indices[(Indices.Num() - 1) - Index] : -1;
	}

	int32 FCategory::GetPickWeightAscending(int32 Index) const
	{
		return Order.IsValidIndex(Index) ? Indices[Order[Index]] : -1;
	}

	int32 FCategory::GetPickWeightDescending(int32 Index) const
	{
		return Order.IsValidIndex(Index) ? Indices[Order[(Order.Num() - 1) - Index]] : -1;
	}

	int32 FCategory::GetPickRandom(int32 Seed) const
	{
		if (Order.IsEmpty())
		{
			return -1;
		}
		return Indices[Order[FRandomStream(Seed).RandRange(0, Order.Num() - 1)]];
	}

	int32 FCategory::GetPickRandomWeighted(int32 Seed) const
	{
		if (Order.IsEmpty())
		{
			return -1;
		}
		const int32 Threshold = FRandomStream(Seed).RandRange(0, static_cast<int32>(WeightSum) - 1);
		// Weights is a sorted cumulative array after Compile -- first bucket > Threshold.
		const int32 Pick = Algo::UpperBound(Weights, Threshold);
		return Indices[Order[FMath::Min(Pick, Order.Num() - 1)]];
	}

	void FCategory::Reserve(int32 InNum)
	{
		Indices.Reserve(InNum);
		Weights.Reserve(InNum);
		Order.Reserve(InNum);
	}

	void FCategory::Shrink()
	{
		Indices.Shrink();
		Weights.Shrink();
		Order.Shrink();
	}

	void FCategory::RegisterEntry(int32 Index, const FPCGExAssetCollectionEntry* InEntry)
	{
		Entries.Add(InEntry);
		const_cast<FPCGExAssetCollectionEntry*>(InEntry)->BuildMicroCache();
		Indices.Add(Index);
		Weights.Add(InEntry->Weight + 1);
	}

	void FCategory::Compile()
	{
		Shrink();
		WeightSum = CompileWeightedOrder(Weights, Order);
	}

#pragma endregion

#pragma region FCache

	FCache::FCache()
	{
		Main = MakeShared<FCategory>(NAME_None);
	}

	// Every valid entry goes into Main. Additionally, entries with a non-None Category
	// are registered to a named sub-category (created on first encounter, assigned a dense
	// index in CategoryNameToIndex). Subsequent entries on the same name reuse that slot.
	void FCache::RegisterEntry(int32 Index, const FPCGExAssetCollectionEntry* InEntry)
	{
		check(InEntry);

		FPCGExAssetCollectionEntry* MutableEntry = const_cast<FPCGExAssetCollectionEntry*>(InEntry);
		FPCGExAssetStagingData& Staging = MutableEntry->Staging;

		if (const FPCGExStagingBoundsModifier* Modifier = Staging.BoundsStagingModifier.GetPtr<FPCGExStagingBoundsModifier>())
		{
			Staging.AlteredBounds = Modifier->ComputeAlteredBounds(Staging.Bounds);
		}
		else
		{
			Staging.AlteredBounds = Staging.Bounds;
		}

		Main->RegisterEntry(Index, InEntry);
		if (const int32* IdxPtr = CategoryNameToIndex.Find(InEntry->Category))
		{
			Categories[*IdxPtr]->RegisterEntry(Index, InEntry);
		}
		else
		{
			TSharedPtr<FCategory> Category = MakeShared<FCategory>(InEntry->Category);
			const int32 NewIdx = Categories.Add(Category);
			CategoryNameToIndex.Add(InEntry->Category, NewIdx);
			Category->RegisterEntry(Index, InEntry);
		}
	}

	void FCache::Compile()
	{
		Main->Compile();
		for (const TSharedPtr<FCategory>& Category : Categories)
		{
			Category->Compile();
		}
	}
#pragma endregion
}

#pragma region FPCGExAssetCollectionEntry

const FPCGExProperty* FPCGExAssetCollectionEntry::GetResolvedPropertyBase(const UPCGExAssetCollection* OwningCollection, FName PropertyName) const
{
	if (const FInstancedStruct* Override = PropertyOverrides.GetOverride(PropertyName))
	{
		if (const FPCGExProperty* Base = Override->GetPtr<FPCGExProperty>())
		{
			return Base;
		}
	}

	if (OwningCollection)
	{
		if (const FInstancedStruct* Default = OwningCollection->CollectionProperties.GetPropertyByName(PropertyName))
		{
			return Default->GetPtr<FPCGExProperty>();
		}
	}

	return nullptr;
}

const FPCGExFittingVariations& FPCGExAssetCollectionEntry::GetVariations(const UPCGExAssetCollection* ParentCollection) const
{
	if (VariationMode == EPCGExEntryVariationMode::Global || ParentCollection->GlobalVariationMode == EPCGExGlobalVariationRule::Overrule)
	{
		return ParentCollection->GlobalVariations;
	}

	if (VariationMode == EPCGExEntryVariationMode::None)
	{
		// Identity ranges, NOT a skip: Apply() must keep drawing from the random stream exactly
		// as a Local entry with default values would, so seed-dependent downstream results are
		// unaffected by the None/Local distinction.
		static const FPCGExFittingVariations IdentityVariations;
		return IdentityVariations;
	}

	return Variations;
}

const FPCGExLeanScaleToFitDetails* FPCGExAssetCollectionEntry::GetScaleToFitOverride(const UPCGExAssetCollection* ParentCollection) const
{
	if (ParentCollection && ParentCollection->GlobalScaleToFitMode == EPCGExGlobalVariationRule::Overrule)
	{
		return &ParentCollection->GlobalScaleToFit;
	}

	switch (ScaleToFitSource)
	{
	case EPCGExEntryVariationMode::Local:
		return &ScaleToFit;
	case EPCGExEntryVariationMode::Global:
		return ParentCollection ? &ParentCollection->GlobalScaleToFit : nullptr;
	default:
		return nullptr;
	}
}

const FPCGExLeanJustificationDetails* FPCGExAssetCollectionEntry::GetJustificationOverride(const UPCGExAssetCollection* ParentCollection) const
{
	if (ParentCollection && ParentCollection->GlobalJustificationMode == EPCGExGlobalVariationRule::Overrule)
	{
		return &ParentCollection->GlobalJustification;
	}

	switch (JustificationSource)
	{
	case EPCGExEntryVariationMode::Local:
		return &Justification;
	case EPCGExEntryVariationMode::Global:
		return ParentCollection ? &ParentCollection->GlobalJustification : nullptr;
	default:
		return nullptr;
	}
}

const FPCGExAssetGrammarDetails* FPCGExAssetCollectionEntry::GetEffectiveGrammar(const UPCGExAssetCollection* Host) const
{
	if (!bIsSubCollection)
	{
		// Leaf: Local vs Global, honoring collection-level Overrule.
		const bool bUseGlobal =
			GrammarSource == EPCGExEntryVariationMode::Global ||
			(Host && Host->GlobalGrammarMode == EPCGExGlobalVariationRule::Overrule);
		return bUseGlobal && Host ? &Host->GlobalAssetGrammar : &AssetGrammar;
	}

	// Subcollection: Inherit / Override / Flatten.
	if (!SubCollection)
	{
		return nullptr;
	}
	switch (SubGrammarMode)
	{
	case EPCGExGrammarSubCollectionMode::Inherit:
		return &SubCollection->SubCollectionGrammar;
	case EPCGExGrammarSubCollectionMode::Override:
		return &AssetGrammar;
	default: // Flatten -- no module emitted for the subcollection itself; leaves contribute directly.
		return nullptr;
	}
}

double FPCGExAssetCollectionEntry::GetGrammarSize(
	const UPCGExAssetCollection* Host,
	const EPCGExGrammarAxes Axis,
	FPCGExGrammarSizeCache* SizeCache) const
{
	const FPCGExGrammarSizeCacheKey CacheKey{this, Axis};
	if (SizeCache)
	{
		if (const double* CachedSize = SizeCache->Find(CacheKey))
		{
			return *CachedSize;
		}
	}

	const FPCGExAssetGrammarDetails* Resolved = GetEffectiveGrammar(Host);
	const double Size = !Resolved
		? 0.0
		: (bIsSubCollection
			? Resolved->GetSubCollectionSize(SubCollection, Axis, SizeCache)
			: Resolved->GetLeafSize(Staging.Bounds, Axis));

	if (SizeCache)
	{
		SizeCache->Add(CacheKey, Size);
	}
	return Size;
}

bool FPCGExAssetCollectionEntry::FixModuleInfos(
	const UPCGExAssetCollection* Host,
	FPCGSubdivisionSubmodule& OutModule,
	const EPCGExGrammarAxes Axis,
	FPCGExGrammarSizeCache* SizeCache) const
{
	const FPCGExAssetGrammarDetails* Resolved = GetEffectiveGrammar(Host);
	if (!Resolved)
	{
		return false;
	}
	return bIsSubCollection
		? Resolved->FixSubCollection(SubCollection, Axis, OutModule, SizeCache)
		: Resolved->FixLeaf(Staging.Bounds, Axis, OutModule);
}

#if WITH_EDITOR
void FPCGExAssetCollectionEntry::EDITOR_Sanitize()
{
	// Base implementation - override in derived classes
}
#endif

bool FPCGExAssetCollectionEntry::Validate(const UPCGExAssetCollection* ParentCollection)
{
	if (Weight <= 0)
	{
		return false;
	}

	if (bIsSubCollection)
	{
		if (!SubCollection)
		{
			return false;
		}
		SubCollection->LoadCache();
	}
	return true;
}

namespace PCGExAssetCollection
{
	// Aggregate child entry extents per the collection's SubcollectionBoundsMode.
	// Children must have their Staging.Bounds already filled (caller ensures this via the
	// recursive pass before this runs). Invalid or zero-volume children are skipped.
	// Returned box is centered at origin -- center offsets are intentionally not aggregated.
	FBox AggregateSubcollectionBounds(const UPCGExAssetCollection* Child, EPCGExSubcollectionBoundsMode Mode)
	{
		if (!Child)
		{
			return FBox(ForceInit);
		}

		FVector UnionMin(FLT_MAX, FLT_MAX, FLT_MAX);
		FVector UnionMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		FVector MaxExt = FVector::ZeroVector;
		FVector SumExt = FVector::ZeroVector;
		int32 Count = 0;
		FVector WeightedSumExt = FVector::ZeroVector;
		int64 TotalWeight = 0;

		Child->ForEachEntry([&](const FPCGExAssetCollectionEntry* Entry, int32 /*Idx*/)
		{
			if (!Entry)
			{
				return;
			}
			const FBox& ChildBox = Entry->Staging.Bounds;
			if (!ChildBox.IsValid)
			{
				return;
			}

			const FVector ChildExt = ChildBox.GetExtent();
			if (ChildExt.IsNearlyZero())
			{
				return;
			}

			UnionMin = UnionMin.ComponentMin(ChildBox.Min);
			UnionMax = UnionMax.ComponentMax(ChildBox.Max);
			MaxExt = MaxExt.ComponentMax(ChildExt);
			SumExt += ChildExt;
			Count++;

			const int64 W = FMath::Max(1, Entry->Weight);
			WeightedSumExt += ChildExt * static_cast<double>(W);
			TotalWeight += W;
		});

		if (Count == 0)
		{
			return FBox(ForceInit);
		}

		FVector Extents;
		switch (Mode)
		{
		default:
		case EPCGExSubcollectionBoundsMode::UnionAABB:
			// Reconstruct extents from the union min/max, centered at origin.
			Extents = (UnionMax - UnionMin) * 0.5;
			break;
		case EPCGExSubcollectionBoundsMode::MeanExtents:
			Extents = SumExt / static_cast<double>(Count);
			break;
		case EPCGExSubcollectionBoundsMode::WeightedMean:
			Extents = (TotalWeight > 0) ? WeightedSumExt / static_cast<double>(TotalWeight) : (SumExt / static_cast<double>(Count));
			break;
		case EPCGExSubcollectionBoundsMode::MaxExtents:
			Extents = MaxExt;
			break;
		}

		return FBox(-Extents, Extents);
	}
}

void FPCGExAssetCollectionEntry::UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive)
{
	Staging.InternalIndex = InInternalIndex;

	if (bIsSubCollection)
	{
		Staging.Bounds = FBox(ForceInit);
		if (SubCollection)
		{
			Staging.Path = FSoftObjectPath(SubCollection.GetPathName());
			if (bRecursive)
			{
				SubCollection->RebuildStagingData(true);
			}

			// Aggregate child bounds per the owning collection's policy. Children are now staged
			// (either because bRecursive ran them, or because they were already up-to-date).
			const EPCGExSubcollectionBoundsMode Mode = OwningCollection
				? OwningCollection->SubcollectionBoundsMode
				: EPCGExSubcollectionBoundsMode::UnionAABB;
			Staging.Bounds = PCGExAssetCollection::AggregateSubcollectionBounds(SubCollection, Mode);
		}
		else
		{
			Staging.Path = FSoftObjectPath{};
		}
	}
}

void FPCGExAssetCollectionEntry::PostUpdateStaging()
{
	// TODO : Update grammar values where relevant
}

void FPCGExAssetCollectionEntry::SetAssetPath(const FSoftObjectPath& InPath)
{
	// A new asset invalidates externally-authored staging content -- the authoring system
	// re-pins bAuthored on its next rebuild if it is still authoritative.
	Staging.bAuthored = false;
	Staging.Path = InPath;
}

void FPCGExAssetCollectionEntry::GetAssetPaths(TSet<FSoftObjectPath>& OutPaths) const
{
	// Skip empty/unset slots: a null staged path is never a loadable/cookable asset, and it would
	// otherwise pollute preload sets and the cook-dependency walk (mirrors EDITOR_GetSourceAssetPaths).
	if (Staging.Path.IsValid())
	{
		OutPaths.Emplace(Staging.Path);
	}
}

#if WITH_EDITOR
void FPCGExAssetCollectionEntry::EDITOR_GetSourceAssetPaths(TSet<FSoftObjectPath>& OutPaths) const
{
	// Default: the staged path is the source path for most entry types.
	// Types that bake into an embedded asset (e.g. level → exported data asset)
	// must override to advertise their external source reference instead.
	if (Staging.Path.IsValid())
	{
		OutPaths.Emplace(Staging.Path);
	}
}

FSoftObjectPath FPCGExAssetCollectionEntry::EDITOR_GetThumbnailAssetPath() const
{
	// Live SubCollection ref, not Staging.Path: staging can lag a just-assigned reference.
	if (bIsSubCollection)
	{
		return SubCollection ? FSoftObjectPath(SubCollection.GetPathName()) : FSoftObjectPath();
	}
	return Staging.Path;
}
#endif

void FPCGExAssetCollectionEntry::BuildMicroCache()
{
	MicroCache = nullptr;
}

void FPCGExAssetCollectionEntry::ClearManagedSockets()
{
	Staging.Sockets.SetNum(Algo::RemoveIf(Staging.Sockets, [](const FPCGExSocket& Socket)
	{
		return Socket.bManaged;
	}));
}

#pragma endregion

// All API methods follow the same pattern: pick from cache → if subcollection, recurse
// into it (using weighted random for the nested pick) → otherwise return entry + host.
// Tag-inheriting variants accumulate tags from the hierarchy as they recurse.
#pragma region API

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntryAt(int32 Index) const
{
	FPCGExEntryAccessResult Result;

	const int32 Pick = const_cast<UPCGExAssetCollection*>(this)->LoadCache()->Main->GetPick(Index, EPCGExIndexPickMode::Ascending);
	if (const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(Pick))
	{
		Result.Entry = Entry;
		Result.Host = this;
	}
	return Result;
}

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntryRaw(int32 RawIndex) const
{
	FPCGExEntryAccessResult Result;

	if (const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(RawIndex))
	{
		Result.Entry = Entry;
		Result.Host = this;
	}
	return Result;
}

void UPCGExAssetCollection::GetTypeGlobalsStructs(TArray<const UScriptStruct*>& OutStructs) const
{
	// Lineage-resolved: a derived type without its own registered block still answers its
	// ancestor's struct through the seam (GetTypeGlobalsInternal chains to Super), so that
	// is the block it provides.
	PCGExAssetCollection::FTypeInfo TypeInfo;
	if (PCGExAssetCollection::FTypeRegistry::Get().GetInfoResolved(GetTypeId(), TypeInfo) && TypeInfo.GlobalsStruct)
	{
		OutStructs.AddUnique(TypeInfo.GlobalsStruct);
	}
}

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntry(int32 Index, int32 Seed, EPCGExIndexPickMode PickMode) const
{
	FPCGExEntryAccessResult Result;

	const int32 PickedIndex = const_cast<UPCGExAssetCollection*>(this)->LoadCache()->Main->GetPick(Index, PickMode);
	const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(PickedIndex);

	if (!Entry)
	{
		return Result;
	}

	if (Entry->HasValidSubCollection())
	{
		return Entry->GetSubCollectionPtr()->GetEntryWeightedRandom(Seed);
	}

	Result.Entry = Entry;
	Result.Host = this;
	return Result;
}

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntryRandom(int32 Seed) const
{
	FPCGExEntryAccessResult Result;

	const int32 PickedIndex = const_cast<UPCGExAssetCollection*>(this)->LoadCache()->Main->GetPickRandom(Seed);
	const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(PickedIndex);

	if (!Entry)
	{
		return Result;
	}

	if (Entry->HasValidSubCollection())
	{
		return Entry->GetSubCollectionPtr()->GetEntryRandom(Seed * 2);
	}

	Result.Entry = Entry;
	Result.Host = this;
	return Result;
}

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntryWeightedRandom(int32 Seed) const
{
	FPCGExEntryAccessResult Result;

	const int32 PickedIndex = const_cast<UPCGExAssetCollection*>(this)->LoadCache()->Main->GetPickRandomWeighted(Seed);
	const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(PickedIndex);

	if (!Entry)
	{
		return Result;
	}

	if (Entry->HasValidSubCollection())
	{
		return Entry->GetSubCollectionPtr()->GetEntryWeightedRandom(Seed * 2);
	}

	Result.Entry = Entry;
	Result.Host = this;
	return Result;
}

// With tag inheritance

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntryAt(int32 Index, uint8 TagInheritance, TSet<FName>& OutTags) const
{
	FPCGExEntryAccessResult Result;

	const int32 PickedIndex = const_cast<UPCGExAssetCollection*>(this)->LoadCache()->Main->GetPick(Index, EPCGExIndexPickMode::Ascending);
	const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(PickedIndex);

	if (!Entry)
	{
		return Result;
	}

	if (Entry->HasValidSubCollection())
	{
		if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Collection))
		{
			OutTags.Append(Entry->GetSubCollectionPtr()->CollectionTags);
		}
	}
	if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Asset))
	{
		OutTags.Append(Entry->Tags);
	}

	Result.Entry = Entry;
	Result.Host = this;
	return Result;
}

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntryRaw(int32 RawIndex, uint8 TagInheritance, TSet<FName>& OutTags) const
{
	FPCGExEntryAccessResult Result;

	const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(RawIndex);

	if (!Entry)
	{
		return Result;
	}

	if (Entry->HasValidSubCollection())
	{
		if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Collection))
		{
			OutTags.Append(Entry->GetSubCollectionPtr()->CollectionTags);
		}
	}
	if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Asset))
	{
		OutTags.Append(Entry->Tags);
	}

	Result.Entry = Entry;
	Result.Host = this;
	return Result;
}

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntry(int32 Index, int32 Seed, EPCGExIndexPickMode PickMode, uint8 TagInheritance, TSet<FName>& OutTags) const
{
	FPCGExEntryAccessResult Result;

	const int32 PickedIndex = const_cast<UPCGExAssetCollection*>(this)->LoadCache()->Main->GetPick(Index, PickMode);
	const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(PickedIndex);

	if (!Entry)
	{
		return Result;
	}

	if (Entry->HasValidSubCollection())
	{
		if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Hierarchy))
		{
			OutTags.Append(Entry->Tags);
		}
		if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Collection))
		{
			OutTags.Append(Entry->GetSubCollectionPtr()->CollectionTags);
		}
		return Entry->GetSubCollectionPtr()->GetEntryWeightedRandom(Seed, TagInheritance, OutTags);
	}

	if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Asset))
	{
		OutTags.Append(Entry->Tags);
	}

	Result.Entry = Entry;
	Result.Host = this;
	return Result;
}

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntryRandom(int32 Seed, uint8 TagInheritance, TSet<FName>& OutTags) const
{
	FPCGExEntryAccessResult Result;

	const int32 PickedIndex = const_cast<UPCGExAssetCollection*>(this)->LoadCache()->Main->GetPickRandom(Seed);
	const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(PickedIndex);

	if (!Entry)
	{
		return Result;
	}

	if (Entry->HasValidSubCollection())
	{
		if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Hierarchy))
		{
			OutTags.Append(Entry->Tags);
		}
		if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Collection))
		{
			OutTags.Append(Entry->GetSubCollectionPtr()->CollectionTags);
		}
		return Entry->GetSubCollectionPtr()->GetEntryRandom(Seed * 2, TagInheritance, OutTags);
	}

	if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Asset))
	{
		OutTags.Append(Entry->Tags);
	}

	Result.Entry = Entry;
	Result.Host = this;
	return Result;
}

FPCGExEntryAccessResult UPCGExAssetCollection::GetEntryWeightedRandom(int32 Seed, uint8 TagInheritance, TSet<FName>& OutTags) const
{
	FPCGExEntryAccessResult Result;

	const int32 PickedIndex = const_cast<UPCGExAssetCollection*>(this)->LoadCache()->Main->GetPickRandomWeighted(Seed);
	const FPCGExAssetCollectionEntry* Entry = GetEntryAtRawIndex(PickedIndex);

	if (!Entry)
	{
		return Result;
	}

	if (Entry->HasValidSubCollection())
	{
		if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Hierarchy))
		{
			OutTags.Append(Entry->Tags);
		}
		if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Collection))
		{
			OutTags.Append(Entry->GetSubCollectionPtr()->CollectionTags);
		}
		return Entry->GetSubCollectionPtr()->GetEntryWeightedRandom(Seed * 2, TagInheritance, OutTags);
	}

	if (TagInheritance & static_cast<uint8>(EPCGExAssetTagInheritance::Asset))
	{
		OutTags.Append(Entry->Tags);
	}

	Result.Entry = Entry;
	Result.Host = this;
	return Result;
}

#pragma endregion

#pragma region Cache

// Thread-safe lazy cache initialization. First checks under read lock (fast path),
// then builds under write lock (inside BuildCacheFromEntries) if needed.
PCGExAssetCollection::FCache* UPCGExAssetCollection::LoadCache()
{
	{
		FReadScopeLock ReadScopeLock(CacheLock);
		if (bCacheNeedsRebuild)
		{
			InvalidateCache();
		}
		if (Cache)
		{
			return Cache.Get();
		}
	}

	BuildCache();
	return Cache.Get();
}

void UPCGExAssetCollection::InvalidateCache()
{
	Cache.Reset();
	bCacheNeedsRebuild = true;
}

void UPCGExAssetCollection::BuildCache()
{
	bCacheNeedsRebuild = false;
	// Per-class implementation calls BuildCacheFromEntries(Entries)
}

#pragma endregion

void UPCGExAssetCollection::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	// Duplicates get a new identity; PIE copies keep the same GUID
	if (!bDuplicateForPIE)
	{
		CollectionGUID = GenerateNewGUID();
	}

#if WITH_EDITOR
	// PostLoad doesn't fire on duplicates, so any stale ImportOverrides on the source would
	// carry into the duplicate's first paint. PIE copies are transient -- skip the heal.
	if (!bDuplicateForPIE)
	{
		CollectionProperties.ReconcileImportOverrides();
	}

	EDITOR_SetDirty();
#endif
}

void UPCGExAssetCollection::PostEditImport()
{
	Super::PostEditImport();

	// Paste/import gets a new identity
	CollectionGUID = GenerateNewGUID();

#if WITH_EDITOR
	EDITOR_SetDirty();
#endif
}

#if WITH_EDITOR
namespace PCGExAssetCollectionMigration
{
	static constexpr int32 CurrentGrammarSchemaVersion = 1;
	static constexpr int32 CurrentFittingSchemaVersion = 1;

	/**
	 * Migrate one entry's variation mode from the always-on era (v0) to opt-in (v1).
	 *
	 * 'Local' was both the old default and the old delta-serialization baseline, so entries
	 * authored as Local never wrote the byte to disk and now load as None (the new default).
	 * Intent is recovered from the data itself: authored ranges mean Local, untouched ranges
	 * mean None. Global entries always serialized their byte and are left alone (their parked
	 * local values are kept too). Behaviorally lossless either way: GetVariations resolves
	 * None to identity ranges, which is exactly what a Local entry with default values was.
	 *
	 * Returns true if the entry was modified.
	 */
	static bool MigrateEntryVariationsV0ToV1(FPCGExAssetCollectionEntry* Entry)
	{
		if (!Entry || Entry->VariationMode == EPCGExEntryVariationMode::Global)
		{
			return false;
		}

		static const FPCGExFittingVariations Defaults;
		const bool bIsDefault = FPCGExFittingVariations::StaticStruct()->CompareScriptStruct(&Entry->Variations, &Defaults, 0);

		const EPCGExEntryVariationMode Desired = bIsDefault ? EPCGExEntryVariationMode::None : EPCGExEntryVariationMode::Local;
		if (Entry->VariationMode == Desired)
		{
			return false;
		}

		Entry->VariationMode = Desired;
		return true;
	}

	// -- Future (option B): true sparse storage for entry variations ---------------------------
	// v1 (above) makes variations semantically opt-in but keeps the FPCGExFittingVariations
	// payload inline on every entry. If per-entry memory ever matters, the v2 pass is:
	//   1. Add sparse storage (e.g. an FInstancedStruct payload populated only for Local
	//      entries -- after v1 normalization that is exactly the entries with authored ranges).
	//   2. Rename the inline member Variations -> Variations_DEPRECATED (UE matches the
	//      serialized 'Variations' tag onto the _DEPRECATED member automatically) and add a
	//      FittingSchemaVersion < 2 PostLoad pass that moves the payload, then bump to 2.
	//   3. Fleet-resave production collections (ResavePackages commandlet) so disk data migrates.
	//   4. Only then delete Variations_DEPRECATED in a later release: a deprecated UPROPERTY
	//      still occupies its bytes in RAM, so the win only materializes at deletion -- which is
	//      only safe once no un-resaved asset remains.
	// GetVariations() is the single read funnel, so consumers won't notice the storage change.
	// -------------------------------------------------------------------------------------------

	/** Migrate one entry's grammar data from v0 to v1. Returns true if a downgrade warning
	 *  should be emitted for this entry (legacy Min/Max/Average on a leaf). */
	static bool MigrateEntryGrammarV0ToV1(FPCGExAssetCollectionEntry* Entry)
	{
		if (!Entry)
		{
			return false;
		}

		bool bWarn = false;
		if (Entry->bIsSubCollection && Entry->SubGrammarMode == EPCGExGrammarSubCollectionMode::Override)
		{
			// Old Override stored its data in CollectionGrammar_DEPRECATED. Hoist it into AssetGrammar.
			Entry->AssetGrammar.MigrateFromLegacyCollectionGrammar(Entry->CollectionGrammar_DEPRECATED);
		}
		else if (!Entry->bIsSubCollection)
		{
			// Leaf: migrate AssetGrammar's internal _DEPRECATED fields.
			bWarn = Entry->AssetGrammar.MigrateFromV0Internal();
		}
		// Subcollection entries with Inherit/Flatten: nothing to migrate at the entry level --
		// the source data lives on the subcollection's own SubCollectionGrammar (migrated by
		// that collection's own PostLoad).
		return bWarn;
	}
}
#endif

void UPCGExAssetCollection::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	// Single-pipeline slot migration: the legacy StagingPipeline pointer becomes the first
	// element of the composable StagingPipelines array. Runs once; subsequent loads no-op.
	if (StagingPipeline_DEPRECATED)
	{
		StagingPipelines.Add(StagingPipeline_DEPRECATED);
		StagingPipeline_DEPRECATED = nullptr;
	}
#endif

#if WITH_EDITOR
	// Grammar schema migration. Runs once per collection; subsequent loads no-op.
	if (GrammarSchemaVersion < PCGExAssetCollectionMigration::CurrentGrammarSchemaVersion)
	{
		int32 DowngradedEntries = 0;
		int32 DisabledEntries = 0;

		if (GlobalAssetGrammar.MigrateFromV0Internal())
		{
			DowngradedEntries++;
		}

		// SubCollectionGrammar is a new v1 field; its source data lives on the legacy CollectionGrammar slot.
		SubCollectionGrammar.MigrateFromLegacyCollectionGrammar(CollectionGrammar_DEPRECATED);

		ForEachEntry([&DowngradedEntries, &DisabledEntries](FPCGExAssetCollectionEntry* Entry, int32 /*Index*/)
		{
			if (PCGExAssetCollectionMigration::MigrateEntryGrammarV0ToV1(Entry))
			{
				DowngradedEntries++;
			}
			if (Entry && !Entry->bIsSubCollection && Entry->AssetGrammar.Axes == static_cast<uint8>(EPCGExGrammarAxes::None))
			{
				DisabledEntries++;
			}
		});

		if (DowngradedEntries > 0)
		{
			UE_LOG(LogTemp, Warning,
			       TEXT("[PCGEx] Grammar migration: %d entr%s in '%s' had legacy Min/Max/Average size mode -- downgraded to X-bounds. Review and reconfigure axes if needed."),
			       DowngradedEntries, DowngradedEntries == 1 ? TEXT("y") : TEXT("ies"), *GetName());
		}
		if (DisabledEntries > 0)
		{
			UE_LOG(LogTemp, Log,
			       TEXT("[PCGEx] Grammar migration: %d entr%s in '%s' had empty Symbol -- grammar disabled (Axes=None)."),
			       DisabledEntries, DisabledEntries == 1 ? TEXT("y") : TEXT("ies"), *GetName());
		}

		GrammarSchemaVersion = PCGExAssetCollectionMigration::CurrentGrammarSchemaVersion;

		(void)MarkPackageDirty();
	}

	// Entry variations opt-in migration (v0 -> v1). VariationMode's default flipped from Local
	// to None; this pass recovers authored intent from the serialized data (see
	// MigrateEntryVariationsV0ToV1). Runs once per collection; subsequent loads no-op.
	if (FittingSchemaVersion < PCGExAssetCollectionMigration::CurrentFittingSchemaVersion)
	{
		ForEachEntry([](FPCGExAssetCollectionEntry* Entry, int32 /*Index*/)
		{
			PCGExAssetCollectionMigration::MigrateEntryVariationsV0ToV1(Entry);
		});

		FittingSchemaVersion = PCGExAssetCollectionMigration::CurrentFittingSchemaVersion;

		(void)MarkPackageDirty();
	}
#endif

#if WITH_EDITOR
	// Self-heal HeaderId collisions saved to disk before the dedup pass landed (or introduced
	// later via copy-paste in a build that lacked the runtime fix). MarkPackageDirty only when
	// something actually changed, so clean assets stay clean on load.
	if (SyncPropertySchemaAndRemapEntries())
	{
		(void)MarkPackageDirty();
	}

	// Heal stale CollectionProperties.ImportOverrides at load time so the customization's
	// drift path doesn't have to fire mid-layout (which would leave the property tree's
	// child count stale until a refresh). ConditionalPostLoad each import first -- UE
	// doesn't guarantee referenced-asset PostLoad ordering, and the reconcile reads each
	// asset's HeaderIds, which the asset's own PostLoad canonicalizes.
	for (const TObjectPtr<UPCGExPropertySchemaAsset>& AssetPtr : CollectionProperties.ImportedSchemas)
	{
		if (UPCGExPropertySchemaAsset* Asset = AssetPtr.Get())
		{
			Asset->ConditionalPostLoad();
		}
	}
	if (CollectionProperties.ReconcileImportOverrides())
	{
		(void)MarkPackageDirty();
	}

	// Load-time staleness refresh lives in FPCGExCollectionsEditorModule::OnAssetLoaded, with its
	// sibling triggers -- PostLoad stays pure data migration.
#endif
}

void UPCGExAssetCollection::BeginDestroy()
{
	InvalidateCache();
	Super::BeginDestroy();
}

void UPCGExAssetCollection::RebuildStagingData(bool bRecursive)
{
	SyncEntryIds();

	ForEachEntry([this, bRecursive](FPCGExAssetCollectionEntry* InEntry, int32 i)
	{
		InEntry->UpdateStaging(this, i, bRecursive);
		InEntry->PostUpdateStaging();
	});
	InvalidateCache();
}

bool UPCGExAssetCollection::BuildCacheFromEntryPtrs(TConstArrayView<FPCGExAssetCollectionEntry*> InEntries)
{
	FWriteScopeLock WriteScopeLock(CacheLock);

	if (Cache)
	{
		return true;
	}

	// Rebuild property registry from collection properties
	RebuildPropertyRegistry();

	Cache = MakeShared<PCGExAssetCollection::FCache>();
	bCacheNeedsRebuild = false;

	const int32 NumEntriesCount = InEntries.Num();
	Cache->Main->Reserve(NumEntriesCount);

	// Collect direct subcollection children while iterating entries. Recursion into their
	// FlatHosts is deferred to after the loop because LoadCache() on a sub-collection takes
	// its own CacheLock and we want to release the write path here before that happens.
	TSet<UPCGExAssetCollection*> DirectSubs;

	for (int32 i = 0; i < NumEntriesCount; i++)
	{
		FPCGExAssetCollectionEntry* Entry = InEntries[i];

		// Null = unset row in a heterogeneous collection. Skipped, but still consumes
		// raw index i so pointer-array order stays aligned with raw entry indices.
		if (!Entry || !Entry->Validate(this))
		{
			continue;
		}

		Cache->RegisterEntry(i, Entry);

		if (Entry->HasValidSubCollection())
		{
			if (UPCGExAssetCollection* Sub = const_cast<UPCGExAssetCollection*>(Entry->GetSubCollectionPtr()))
			{
				if (Sub != this)
				{
					DirectSubs.Add(Sub);
				}
			}
		}
	}

	Cache->Compile();

	// Materialize FlatHosts: self + every transitively reachable subcollection, deduplicated.
	// Walks sub-collections via ForEachEntry (direct Entries array read -- no lock on the
	// sub-collection's cache). This avoids calling LoadCache() on sub-collections, which
	// could re-enter the cache build on a cycle (A→B→A) and deadlock on our own CacheLock.
	// Cycles are handled by the Visited set.
	TSet<UPCGExAssetCollection*> Visited;
	Visited.Add(this);
	Cache->FlatHosts.Add(this);

	TArray<UPCGExAssetCollection*> Stack;
	for (UPCGExAssetCollection* Sub : DirectSubs)
	{
		bool bAlreadyVisited = false;
		Visited.Add(Sub, &bAlreadyVisited);
		if (!bAlreadyVisited)
		{
			Stack.Add(Sub);
		}
	}

	while (!Stack.IsEmpty())
	{
		UPCGExAssetCollection* Current = Stack.Pop(EAllowShrinking::No);
		Cache->FlatHosts.Add(Current);

		Current->ForEachEntry([&Visited, &Stack](const FPCGExAssetCollectionEntry* E, int32 /*Idx*/)
		{
			if (!E || !E->HasValidSubCollection())
			{
				return;
			}
			if (UPCGExAssetCollection* Sub = const_cast<UPCGExAssetCollection*>(E->GetSubCollectionPtr()))
			{
				bool bAlreadyVisited = false;
				Visited.Add(Sub, &bAlreadyVisited);
				if (!bAlreadyVisited)
				{
					Stack.Add(Sub);
				}
			}
		});
	}

	return true;
}

void UPCGExAssetCollection::SyncEntryIds()
{
	TSet<int32> SeenIds;
	SeenIds.Reserve(NumEntries());

	ForEachEntry([&SeenIds](FPCGExAssetCollectionEntry* InEntry, int32 i)
	{
		// Copy-paste preserves the source's EntryId; without this catch, external references
		// (variant collections) would alias the duplicates. First-seen keeps its id.
		bool bAlreadySeen = false;
		if (InEntry->EntryId != 0)
		{
			SeenIds.Add(InEntry->EntryId, &bAlreadySeen);
		}
		if (InEntry->EntryId == 0 || bAlreadySeen)
		{
			do
			{
				InEntry->EntryId = GetTypeHash(FGuid::NewGuid());
			}
			while (InEntry->EntryId == 0 || SeenIds.Contains(InEntry->EntryId));
			SeenIds.Add(InEntry->EntryId);
		}
	});
}

void UPCGExAssetCollection::EDITOR_RegisterTrackingKeys(FPCGExContext* Context) const
{
	Context->EDITOR_TrackPath(this);
	ForEachEntry([Context](const FPCGExAssetCollectionEntry* InEntry, int32 i)
	{
		if (!InEntry->bIsSubCollection)
		{
			return;
		}
		if (const UPCGExAssetCollection* SubCollection = InEntry->GetSubCollectionPtr())
		{
			SubCollection->EDITOR_RegisterTrackingKeys(Context);
		}
	});
}

bool UPCGExAssetCollection::HasCircularDependency(const UPCGExAssetCollection* OtherCollection) const
{
	if (!OtherCollection)
	{
		return false;
	}
	if (OtherCollection == this)
	{
		return true;
	}

	TSet<const UPCGExAssetCollection*> References;
	return OtherCollection->HasCircularDependency(References);
}

bool UPCGExAssetCollection::HasCircularDependency(TSet<const UPCGExAssetCollection*>& InReferences) const
{
	// InReferences is the active recursion stack, not "ever visited." Pop on the way back
	// up so DAG diamonds (two siblings pointing to the same descendant) don't false-positive
	// as cycles -- caller's ClearSubCollection() on a false positive wipes valid entries.
	bool bAlreadyOnStack = false;
	InReferences.Add(this, &bAlreadyOnStack);
	if (bAlreadyOnStack)
	{
		return true;
	}

	bool bCircularDependency = false;
	ForEachEntry([&](const FPCGExAssetCollectionEntry* InEntry, int32 i)
	{
		if (bCircularDependency)
		{
			return;
		}
		// Skip leaf entries: a parked SubCollection ref is not a cycle edge.
		if (!InEntry->bIsSubCollection)
		{
			return;
		}
		if (const UPCGExAssetCollection* Other = InEntry->GetSubCollectionPtr())
		{
			bCircularDependency = Other->HasCircularDependency(InReferences);
		}
	});

	InReferences.Remove(this);
	return bCircularDependency;
}

void UPCGExAssetCollection::GetAssetPaths(TSet<FSoftObjectPath>& OutPaths, PCGExAssetCollection::ELoadingFlags Flags) const
{
	const bool bCollectionOnly = Flags == PCGExAssetCollection::ELoadingFlags::RecursiveCollectionsOnly;
	const bool bRecursive = bCollectionOnly || Flags == PCGExAssetCollection::ELoadingFlags::Recursive;

	ForEachEntry([&](const FPCGExAssetCollectionEntry* InEntry, int32 i)
	{
		if (InEntry->bIsSubCollection)
		{
			if (bRecursive || bCollectionOnly)
			{
				if (InEntry->SubCollection)
				{
					InEntry->SubCollection->GetAssetPaths(OutPaths, Flags);
				}
			}
			return;
		}
		if (bCollectionOnly)
		{
			return;
		}

		InEntry->GetAssetPaths(OutPaths);
	});
}

void UPCGExAssetCollection::GatherPropertySoftObjectPaths(TSet<FSoftObjectPath>& OutPaths) const
{
	// Collection-level defaults, then every entry's overrides. Shared by the editor
	// cook-dependency walk and runtime preloading.
	PCGExProperties::GatherSoftObjectPaths(CollectionProperties, OutPaths);

	ForEachEntry([&OutPaths](const FPCGExAssetCollectionEntry* Entry, int32 /*Idx*/)
	{
		PCGExProperties::GatherSoftObjectPaths(Entry->PropertyOverrides, OutPaths);
	});
}

#if WITH_EDITOR
void UPCGExAssetCollection::GetCookDependencyAssetPaths(TSet<FSoftObjectPath>& OutPaths) const
{
	// Identical cook output to before: recursive assets + the same custom-property soft paths,
	// now sourced from the shared (un-gated) GatherPropertySoftObjectPaths.
	GetAssetPaths(OutPaths, PCGExAssetCollection::ELoadingFlags::Recursive);
	GatherPropertySoftObjectPaths(OutPaths);
}
#endif

void UPCGExAssetCollection::RefreshCollectionPropertiesFromEntries(
	EPCGExSchemaMergePolicy Policy,
	TConstArrayView<FInstancedStruct> InheritedDefaults)
{
	// Source ordering (see header for full reasoning):
	//   1. InheritedDefaults (caller-computed common-ancestor view) -- wins under FirstWins.
	//   2. Per-entry contributors (each entry's enabled override slots).
	//   3. Existing CollectionProperties -- loses to both above; survives only for
	//      manual-only schema entries.
	SyncPropertySchemaAndRemapEntries();

	TArray<TArray<FInstancedStruct>> Sources;
	Sources.Reserve(2 + NumEntries());

	// Source #0: caller-supplied inherited defaults. Empty when the caller has no chain context
	// (manual collection rebuilds, etc.) -- in that case the merge falls through to contributors.
	if (!InheritedDefaults.IsEmpty())
	{
		Sources.Add(TArray<FInstancedStruct>(InheritedDefaults));
	}

	// One source per entry whose enabled overrides carry self-contained (name+type+value)
	// FInstancedStructs -- the same shape BuildSchema produces on the collection side, so
	// they double as schema declarations for the union.
	ForEachEntry([&Sources](const FPCGExAssetCollectionEntry* InEntry, int32 /*Idx*/)
	{
		if (!InEntry)
		{
			return;
		}
		TArray<FInstancedStruct> EntrySource;
		for (const FPCGExPropertyOverrideEntry& Slot : InEntry->PropertyOverrides.Overrides)
		{
			if (Slot.bEnabled && Slot.Value.IsValid())
			{
				EntrySource.Add(Slot.Value);
			}
		}
		if (!EntrySource.IsEmpty())
		{
			Sources.Add(MoveTemp(EntrySource));
		}
	});

	// Existing manual schema appended LAST -- loses on name collision under FirstWins, survives as
	// the sole source for properties no entry contributes.
	Sources.Add(CollectionProperties.BuildSchema());

	// Nothing authored anywhere: leave existing state untouched (avoids no-op churn).
	if (Sources.Num() == 1 && Sources.Last().IsEmpty())
	{
		return;
	}

	const PCGExProperties::FSchemaMergeResult MergeResult = PCGExProperties::MergeSchemas(Sources, Policy);
	PCGExProperties::LogSchemaConflicts(MergeResult, this);
	PCGExProperties::ApplyMergeResultToSchemas(CollectionProperties, MergeResult.Merged);

	// Overrides may have arrived from heterogenous sources (e.g. per-actor components whose
	// schemas had their own HeaderIds), so SyncToSchema's HeaderId match would miss and
	// reset values to defaults. Realign HeaderIds by name first.
	TArray<FInstancedStruct> CanonicalSchema = CollectionProperties.BuildSchema();

#if WITH_EDITOR
	TMap<FName, int32> CanonicalHeaderIdsByName;
	CanonicalHeaderIdsByName.Reserve(CanonicalSchema.Num());
	for (const FInstancedStruct& SchemaProp : CanonicalSchema)
	{
		if (const FPCGExProperty* P = SchemaProp.GetPtr<FPCGExProperty>())
		{
			if (P->HeaderId != 0 && !P->PropertyName.IsNone())
			{
				CanonicalHeaderIdsByName.Add(P->PropertyName, P->HeaderId);
			}
		}
	}
#endif

	ForEachEntry([&CanonicalSchema
#if WITH_EDITOR
			, &CanonicalHeaderIdsByName
#endif
		](FPCGExAssetCollectionEntry* InEntry, int32 /*Idx*/)
		{
			if (!InEntry)
			{
				return;
			}
#if WITH_EDITOR
			for (FPCGExPropertyOverrideEntry& Slot : InEntry->PropertyOverrides.Overrides)
			{
				if (FPCGExProperty* P = Slot.GetPropertyMutable())
				{
					if (const int32* CanonicalId = CanonicalHeaderIdsByName.Find(P->PropertyName))
					{
						P->HeaderId = *CanonicalId;
					}
				}
			}
#endif
			InEntry->PropertyOverrides.SyncToSchema(CanonicalSchema);
		});

	RebuildPropertyRegistry();
}

bool UPCGExAssetCollection::SyncPropertySchemaAndRemapEntries()
{
	bool bRemapped = false;
	CollectionProperties.SyncAllSchemasAndRemap([this, &bRemapped](TConstArrayView<FPCGExHeaderIdRemap> Remaps)
	{
		bRemapped = true;
		ForEachEntry([&Remaps](FPCGExAssetCollectionEntry* InEntry, int32 /*i*/)
		{
			InEntry->PropertyOverrides.ApplyHeaderIdRemap(Remaps);
		});
	});
	return bRemapped;
}

#if WITH_EDITOR
void UPCGExAssetCollection::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Skip Interactive ticks: the sync + staging-rebuild chain below tears down the dragged
	// widget (breaking undo) and floods PCG with per-tick re-cooks. Super still propagates
	// the Interactive event for live preview.
	const EPropertyChangeType::Type ChangeType = PropertyChangedEvent.ChangeType;
	if (ChangeType == EPropertyChangeType::Interactive)
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);
		return;
	}

	// Default-to-structural is the safe bias; only a positive "leaf is an FPCGExProperty value
	// field" identification opts out, and array shape changes always force structural anyway.
	bool bIsValueOnlyLeafEdit = false;
	bool bIsStructuralSchemaChange = false;

	if (PropertyChangedEvent.MemberProperty)
	{
		const FName PropName = PropertyChangedEvent.MemberProperty->GetFName();
		if (PropName == GET_MEMBER_NAME_CHECKED(UPCGExAssetCollection, CollectionProperties))
		{
			const bool bArrayShapeChange =
				ChangeType == EPropertyChangeType::ArrayAdd ||
				ChangeType == EPropertyChangeType::ArrayRemove ||
				ChangeType == EPropertyChangeType::ArrayClear ||
				ChangeType == EPropertyChangeType::ArrayMove ||
				ChangeType == EPropertyChangeType::Duplicate;

			// IsChildOf, not equality: plugin-registered FPCGExProperty subtypes must classify
			// the same way as built-in ones.
			const FProperty* LeafProperty = PropertyChangedEvent.Property;
			const UScriptStruct* LeafOwner = LeafProperty ? Cast<UScriptStruct>(LeafProperty->GetOwnerStruct()) : nullptr;
			const bool bLeafIsFPCGExPropertyValue = LeafOwner && LeafOwner->IsChildOf(FPCGExProperty::StaticStruct());

			bIsValueOnlyLeafEdit = !bArrayShapeChange && bLeafIsFPCGExPropertyValue;
			bIsStructuralSchemaChange = !bIsValueOnlyLeafEdit;
		}
		// Programmatic / reflection edits that bypass the outer CollectionProperties UPROPERTY.
		else if (const UStruct* OwnerStruct = PropertyChangedEvent.MemberProperty->GetOwnerStruct();
			OwnerStruct == FPCGExPropertySchema::StaticStruct() || OwnerStruct == FPCGExPropertySchemaCollection::StaticStruct())
		{
			bIsStructuralSchemaChange = true;
		}
	}

	// Heavy O(N) work -- grammar context recovery, structural rebuild, circular-dep walk -- is
	// guarded on !bIsValueOnlyLeafEdit so a 1000-entry collection doesn't pay it for a single
	// property tweak. Cache invalidation + optional staging rebuild always run so downstream
	// consumers see fresh data regardless of which path we took.
	if (!bIsValueOnlyLeafEdit)
	{
		// Grammar context recovery: FPCGExAssetGrammarDetails is shared across leaf/subcollection
		// contexts; flipping bIsSubCollection can leave Size in the now-invalid enum subset -- snap back.
		GlobalAssetGrammar.ValidateContext(/*bIsSubCollection=*/false);
		SubCollectionGrammar.ValidateContext(/*bIsSubCollection=*/true);
		ForEachEntry([](FPCGExAssetCollectionEntry* Entry, int32 /*Index*/)
		{
			if (Entry)
			{
				Entry->AssetGrammar.ValidateContext(Entry->bIsSubCollection);
			}
		});
	}

	if (bIsStructuralSchemaChange)
	{
		RebuildPropertyRegistry();
		SyncPropertyOverridesToEntries();
	}

	(void)MarkPackageDirty();
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!bIsValueOnlyLeafEdit)
	{
		// Sub-collection refs can only change on structural edits -- value-only edits can't create cycles.
		// Skip leaf entries: a parked ref (survives bIsSubCollection toggles) must not be cleared here.
		ForEachEntry([this](FPCGExAssetCollectionEntry* InEntry, int32 i)
		{
			if (!InEntry->bIsSubCollection)
			{
				return;
			}
			const UPCGExAssetCollection* Other = InEntry->GetSubCollectionPtr();
			if (Other && HasCircularDependency(Other))
			{
				UE_LOG(LogTemp, Error, TEXT("Prevented circular dependency trying to nest \"%s\" inside \"%s\""), *GetNameSafe(Other), *GetNameSafe(this));
				InEntry->ClearSubCollection();
			}
		});
	}

	EDITOR_SetDirty();

	if (!bSuppressStagingRebuild)
	{
		EDITOR_RebuildStagingData();
	}
}

void UPCGExAssetCollection::SyncPropertyOverridesToEntries()
{
	// Remap must happen before the SyncToSchema loop below -- SyncToSchema's HeaderId index
	// aliases collided entries otherwise, and one side's authored values fall through to
	// schema defaults.
	SyncPropertySchemaAndRemapEntries();

	TArray<FInstancedStruct> Schema = CollectionProperties.BuildSchema();
	ForEachEntry([&Schema](FPCGExAssetCollectionEntry* InEntry, int32 i)
	{
		InEntry->PropertyOverrides.SyncToSchema(Schema);
	});
}

bool UPCGExAssetCollection::EDITOR_HasAnyStagingPipeline() const
{
	for (const TObjectPtr<UPCGExCollectionStagingPipeline>& Pipeline : StagingPipelines)
	{
		if (Pipeline)
		{
			return true;
		}
	}
	return false;
}

namespace PCGExAssetCollectionDiag
{
	/** Turns "the rebuild dirties my collection and I changed nothing" into a named field. */
	bool bLogStagingChanges = false;
	FAutoConsoleVariableRef CVarLogStagingChanges(
		TEXT("pcgex.LogStagingChanges"),
		bLogStagingChanges,
		TEXT("Log which entry/property caused a collection staging rebuild to mark the package dirty."));

	/** Placeholder result means the difference lives in a nested or non-reflected member. */
	FString FindFirstDifferingProperty(const UScriptStruct* Struct, const void* A, const void* B)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (const FProperty* Prop = *It; !Prop->Identical_InContainer(A, B, 0))
			{
				return Prop->GetName();
			}
		}
		return TEXT("<none reflected>");
	}
}

void UPCGExAssetCollection::EDITOR_DispatchPipelinePreRebuild()
{
	if (bEDITOR_PipelineDispatchGuard || IsRunningCookCommandlet() || !EDITOR_HasAnyStagingPipeline())
	{
		return;
	}

	// Hooks may mutate entries before some session paths take their own snapshot (the
	// stale-entry batch only Modifies per entry, inside EDITOR_RebuildEntryStaging) --
	// snapshot for undo up front. Redundant Modify calls within one transaction are no-ops.
	//
	// Modify(false), not (true): dirtying up front churns every pipeline-bearing collection on
	// every rebuild. EDITOR_RebuildStagingDataInternal diffs whole-object state instead.
	Modify(false);

	TGuardValue<bool> DispatchGuard(bEDITOR_PipelineDispatchGuard, true);
	FEditorScriptExecutionGuard ScriptGuard;

	for (UPCGExCollectionStagingPipeline* Pipeline : StagingPipelines)
	{
		if (!Pipeline)
		{
			continue;
		}
		TGuardValue<TObjectPtr<UPCGExAssetCollection>> TargetCollectionGuard(Pipeline->TargetCollection, this);
		TGuardValue<int32> TargetIndexGuard(Pipeline->TargetEntryIndex, INDEX_NONE);
		Pipeline->OnPreRebuild(this);
	}
}

void UPCGExAssetCollection::EDITOR_DispatchPipelineEntry(int32 EntryIndex, bool bIsSubCollection)
{
	if (bEDITOR_PipelineDispatchGuard || IsRunningCookCommandlet() || !EDITOR_HasAnyStagingPipeline())
	{
		return;
	}

	TGuardValue<bool> DispatchGuard(bEDITOR_PipelineDispatchGuard, true);
	FEditorScriptExecutionGuard ScriptGuard;

	for (UPCGExCollectionStagingPipeline* Pipeline : StagingPipelines)
	{
		if (!Pipeline)
		{
			continue;
		}
		TGuardValue<TObjectPtr<UPCGExAssetCollection>> TargetCollectionGuard(Pipeline->TargetCollection, this);
		TGuardValue<int32> TargetIndexGuard(Pipeline->TargetEntryIndex, EntryIndex);
		Pipeline->OnProcessEntry(this, EntryIndex, bIsSubCollection);
	}
}

void UPCGExAssetCollection::EDITOR_DispatchPipelinePostRebuild()
{
	if (bEDITOR_PipelineDispatchGuard || IsRunningCookCommandlet() || !EDITOR_HasAnyStagingPipeline())
	{
		return;
	}

	TGuardValue<bool> DispatchGuard(bEDITOR_PipelineDispatchGuard, true);
	FEditorScriptExecutionGuard ScriptGuard;

	for (UPCGExCollectionStagingPipeline* Pipeline : StagingPipelines)
	{
		if (!Pipeline)
		{
			continue;
		}
		TGuardValue<TObjectPtr<UPCGExAssetCollection>> TargetCollectionGuard(Pipeline->TargetCollection, this);
		TGuardValue<int32> TargetIndexGuard(Pipeline->TargetEntryIndex, INDEX_NONE);
		Pipeline->OnPostRebuild(this);
	}
}

void UPCGExAssetCollection::EDITOR_FinalizeStagingRebuild()
{
	// Native extension point first (actor component schema merges, shared-collection
	// compaction), then the pipeline so its OnPostRebuild operates on final state.
	EDITOR_OnPostStagingRebuild();
	EDITOR_DispatchPipelinePostRebuild();

	// Content is final here -- persist the mosaic so it survives editor restarts.
	EDITOR_BakeThumbnailToPackage();
}

void UPCGExAssetCollection::EDITOR_BakeThumbnailToPackage()
{
	// Needs the editor engine + thumbnail manager, so no-op in commandlets/cooks; never render mid-GC.
	if (!GEditor || IsRunningCommandlet() || IsGarbageCollecting())
	{
		return;
	}

	// Empty collections render nothing (CanVisualizeAsset == false) -- leave the class icon.
	if (NumEntries() <= 0)
	{
		return;
	}

	// Cells resolve child paths through the asset registry; skip while its initial scan is in flight
	// so a load-time rebuild doesn't bake a half-resolved mosaic.
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	if (AssetRegistryModule.Get().IsLoadingAssets())
	{
		return;
	}

	UPackage* Package = GetOutermost();
	if (!Package || Package == GetTransientPackage())
	{
		return;
	}

	// Render via our renderer + CacheThumbnail into the package's (in-memory) thumbnail map.
	// This alone does NOT dirty the package, so without the MarkPackageDirty below a normal Save
	// never serializes the map (SaveThumbnails only runs for packages that actually get saved) --
	// the thumbnail would stay session-only. Only dirty when a thumbnail was actually produced.
	if (ThumbnailTools::GenerateThumbnailForObjectToSaveToDisk(this))
	{
		(void)MarkPackageDirty();
	}
}

void UPCGExAssetCollection::EDITOR_SnapshotForComparison(TArray<uint8>& OutBytes)
{
	OutBytes.Reset();

	FMemoryWriter Writer(OutBytes, /*bIsPersistent=*/true);

	// Refs as path strings: a reallocated pointer must not read as a content change. ArNoDelta
	// forces absolute state -- delta-vs-defaults elides the fields a hook is likeliest to reset.
	FObjectAndNameAsStringProxyArchive Ar(Writer, /*bInLoadIfFindFails=*/false);
	Ar.ArNoDelta = true;

	Serialize(Ar);
}

void UPCGExAssetCollection::EDITOR_RebuildStagingDataInternal(bool bRecursive)
{
	InvalidateCache();

	// Pipeline hooks can mutate collection-level state the per-entry diff can't see. Snapshot and
	// diff instead of assuming -- gated on having a pipeline, so the common case pays nothing.
	const bool bDiffWholeObject = EDITOR_HasAnyStagingPipeline();
	TArray<uint8> PreState;
	if (bDiffWholeObject)
	{
		EDITOR_SnapshotForComparison(PreState);
	}

	if (EDITOR_PostStagingRebuildSuppressDepth == 0)
	{
		EDITOR_DispatchPipelinePreRebuild();
	}

	// Dirty on actual change, not on "a rebuild ran" -- else every project-wide rebuild rewrites
	// every collection on disk. Undo snapshots happen per entry, so bailing here is safe.
	int32 NumChanged = EDITOR_SanitizeAndRebuildStagingData(bRecursive);

	if (NumChanged == 0 && bDiffWholeObject)
	{
		TArray<uint8> PostState;
		EDITOR_SnapshotForComparison(PostState);
		if (PreState != PostState)
		{
			NumChanged = 1;
		}
	}

	if (NumChanged == 0)
	{
		return;
	}

	Modify(true);
	LastRebuiltUtc = FDateTime::UtcNow();
	(void)MarkPackageDirty();
	PCGExEditor::NotifyObjectChanged(this);
	if (EDITOR_PostStagingRebuildSuppressDepth == 0)
	{
		EDITOR_FinalizeStagingRebuild();
	}
}

void UPCGExAssetCollection::EDITOR_RebuildStagingData()
{
	EDITOR_RebuildStagingDataInternal(false);
}

void UPCGExAssetCollection::EDITOR_RebuildStagingData_Recursive()
{
	EDITOR_RebuildStagingDataInternal(true);
}

#pragma region Staleness

uint64 UPCGExAssetCollection::EDITOR_ComputeEntrySourceFingerprint(const FPCGExAssetCollectionEntry* InEntry)
{
	IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
	if (!InEntry || InEntry->bIsSubCollection || !AssetRegistry)
	{
		return 0;
	}

	// Not Staging.Path -- for entries that bake in-place it points at the collection's own package.
	TSet<FSoftObjectPath> SourcePaths;
	InEntry->EDITOR_GetSourceAssetPaths(SourcePaths);

	// Package granularity: paths can share a package (Blueprint "_C") and the hash covers the file.
	TArray<FName> PackageNames;
	PackageNames.Reserve(SourcePaths.Num());
	for (const FSoftObjectPath& Path : SourcePaths)
	{
		const FName PackageName = Path.GetLongPackageFName();
		if (!PackageName.IsNone())
		{
			PackageNames.AddUnique(PackageName);
		}
	}

	// TSet order isn't stable between runs.
	PackageNames.Sort(FNameLexicalLess());

	FBlake3 Digest;
	int32 NumResolved = 0;

	for (const FName& PackageName : PackageNames)
	{
		// Registry-cached from the package header, so this loads nothing. Zero = can't answer yet.
		const TOptional<FAssetPackageData> PackageData = AssetRegistry->GetAssetPackageDataCopy(PackageName);
		if (!PackageData.IsSet() || PackageData->GetPackageSavedHash().IsZero())
		{
			continue;
		}

		// Name in the digest so dropping a source reads as a change, not "the rest still match".
		// UTF8, not TCHAR: sizeof(TCHAR) varies by platform and would churn the digest cross-OS.
		const FTCHARToUTF8 PackageNameUtf8(*PackageName.ToString());
		Digest.Update(MakeMemoryView(PackageNameUtf8.Get(), PackageNameUtf8.Length()));
		Digest.Update(MakeMemoryView(PackageData->GetPackageSavedHash().GetBytes(), sizeof(FIoHash::ByteArray)));
		NumResolved++;
	}

	// All-or-nothing: a digest over a partially resolved set is indistinguishable from a real
	// change once the registry catches up. 0 = "cannot determine", never "unchanged".
	if (NumResolved != PackageNames.Num())
	{
		return 0;
	}

	uint64 Fingerprint = 0;
	const FBlake3Hash Result = Digest.Finalize();
	static_assert(sizeof(Fingerprint) <= sizeof(FBlake3Hash::ByteArray), "Digest too small to fold into a fingerprint.");
	FMemory::Memcpy(&Fingerprint, Result.GetBytes(), sizeof(Fingerprint));

	// 0 is reserved for "no baseline".
	return Fingerprint != 0 ? Fingerprint : 1;
}

int32 UPCGExAssetCollection::EDITOR_RebuildStaleEntries()
{
	if (bSuppressStagingRebuild)
	{
		return 0;
	}

	TArray<int32> StaleIndices;
	ForEachEntry([&StaleIndices](const FPCGExAssetCollectionEntry* InEntry, int32 i)
	{
		if (InEntry->bIsSubCollection)
		{
			return;
		}

		// No baseline -- treating it as stale would mass-rebuild the project on first load.
		if (InEntry->StagingSourceFingerprint == 0)
		{
			return;
		}

		const uint64 Current = EDITOR_ComputeEntrySourceFingerprint(InEntry);

		// Not knowing isn't knowing it changed.
		if (Current == 0)
		{
			return;
		}

		if (Current != InEntry->StagingSourceFingerprint)
		{
			StaleIndices.Add(i);
		}
	});

	if (StaleIndices.IsEmpty())
	{
		return 0;
	}

	EDITOR_DispatchPipelinePreRebuild();

	int32 NumChanged = 0;
	{
		// Suppress per-entry post-rebuild hook firings; emit one tail call after the batch.
		TGuardValue<int32> SuppressGuard(EDITOR_PostStagingRebuildSuppressDepth, EDITOR_PostStagingRebuildSuppressDepth + 1);
		for (int32 Index : StaleIndices)
		{
			if (EDITOR_RebuildEntryStaging(Index))
			{
				NumChanged++;
			}
		}
	}

	// Skip the finalize tail: it bakes a thumbnail and would dirty the package for nothing.
	if (NumChanged == 0)
	{
		return 0;
	}

	EDITOR_FinalizeStagingRebuild();
	return NumChanged;
}

#pragma endregion

bool UPCGExAssetCollection::EDITOR_RestageEntryIfChanged(FPCGExAssetCollectionEntry* InEntry, int32 EntryIndex, bool bRecursive)
{
	if (!InEntry)
	{
		return false;
	}

	// Modify(false) snapshots for undo without dirtying -- dirty stays conditional on the diff.
	Modify(false);

	// Null for payload-less rows -- undiffable, so they report changed rather than skip.
	const UScriptStruct* EntryStruct = EDITOR_GetEntryScriptStruct(EntryIndex);

	FStructOnScope PreState;
	if (EntryStruct)
	{
		PreState.Initialize(EntryStruct);
		EntryStruct->CopyScriptStruct(PreState.GetStructMemory(), InEntry);
	}

	InEntry->EDITOR_Sanitize();
	InEntry->UpdateStaging(this, EntryIndex, bRecursive);
	InEntry->PostUpdateStaging();

	// Refresh even when staging is identical, or the entry re-reports stale on every load -- but
	// never clobber a good baseline with 0. The registry answers differently at different moments,
	// so 0 now and a real digest next pass makes the entry oscillate and dirty every rebuild.
	if (const uint64 Fingerprint = EDITOR_ComputeEntrySourceFingerprint(InEntry); Fingerprint != 0)
	{
		InEntry->StagingSourceFingerprint = Fingerprint;
	}

	EDITOR_DispatchPipelineEntry(EntryIndex, InEntry->bIsSubCollection);

	// Undiffable row: can't prove it unchanged, so report changed.
	if (!EntryStruct)
	{
		return true;
	}

	// PortFlags 0: exact comparison.
	if (EntryStruct->CompareScriptStruct(InEntry, PreState.GetStructMemory(), 0))
	{
		return false;
	}

	UE_CLOG(PCGExAssetCollectionDiag::bLogStagingChanges, LogPCGEx, Warning,
	        TEXT("[PCGEx] Rebuild changed '%s' entry %d -- first differing property: '%s'."),
	        *GetName(), EntryIndex,
	        *PCGExAssetCollectionDiag::FindFirstDifferingProperty(EntryStruct, InEntry, PreState.GetStructMemory()));

	return true;
}

bool UPCGExAssetCollection::EDITOR_RebuildEntryStaging(int32 EntryIndex)
{
	if (bSuppressStagingRebuild)
	{
		return false;
	}

	if (!IsValidIndex(EntryIndex))
	{
		return false;
	}

	// Hook-initiated restages (e.g. Blueprint RestageEntry called from a StagingPipeline hook)
	// must be finalize-quiet: the owning session fires EDITOR_FinalizeStagingRebuild once at
	// its own tail. Standalone calls keep full session semantics (pre-dispatch + finalize).
	TGuardValue<int32> HookSuppressGuard(
		EDITOR_PostStagingRebuildSuppressDepth,
		EDITOR_PostStagingRebuildSuppressDepth + (bEDITOR_PipelineDispatchGuard ? 1 : 0));

	// Direct single-entry sessions fire the pre hook themselves; batch loops (stale entries)
	// already fired it before suppressing.
	if (EDITOR_PostStagingRebuildSuppressDepth == 0)
	{
		EDITOR_DispatchPipelinePreRebuild();
	}

	bool bChanged = false;
	ForEachEntry([this, EntryIndex, &bChanged](FPCGExAssetCollectionEntry* InEntry, int32 i)
	{
		if (i != EntryIndex)
		{
			return;
		}
		bChanged = EDITOR_RestageEntryIfChanged(InEntry, i, false);
	});

	if (bChanged)
	{
		InvalidateCache();
		(void)MarkPackageDirty();
		PCGExEditor::NotifyObjectChanged(this);
		if (EDITOR_PostStagingRebuildSuppressDepth == 0)
		{
			EDITOR_FinalizeStagingRebuild();
		}
	}
	return bChanged;
}

void UPCGExAssetCollection::EDITOR_RebuildStagingData_Project()
{
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	const IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	for (const FAssetData& AssetData : AssetDataList)
	{
		if (UPCGExAssetCollection* Collection = Cast<UPCGExAssetCollection>(AssetData.GetAsset()))
		{
			Collection->EDITOR_RebuildStagingData();
		}
	}
}

int32 UPCGExAssetCollection::EDITOR_SanitizeAndRebuildStagingData(bool bRecursive)
{
	int32 NumChanged = 0;
	ForEachEntry([this, bRecursive, &NumChanged](FPCGExAssetCollectionEntry* InEntry, int32 i)
	{
		if (EDITOR_RestageEntryIfChanged(InEntry, i, bRecursive))
		{
			NumChanged++;
		}
	});
	return NumChanged;
}

void UPCGExAssetCollection::EDITOR_AddBrowserSelectionTyped(const TArray<FAssetData>& InAssetData)
{
	FScopedTransaction Transaction(INVTEXT("Add Browser Selection to Collection"));
	Modify(true);

	// Partition: any collection asset (any type, not just the host's class) becomes a subcollection
	// entry; everything else goes to the type-specific EDITOR_AddBrowserSelectionInternal. GetAsset()
	// loads the package, fine for a user-driven editor action.
	TArray<FAssetData> RegularAssets;
	TArray<UPCGExAssetCollection*> SubCollectionAssets;
	RegularAssets.Reserve(InAssetData.Num());

	for (const FAssetData& AssetData : InAssetData)
	{
		UClass* AssetClass = AssetData.GetClass();
		if (!AssetClass || !AssetClass->IsChildOf(StaticClass()))
		{
			RegularAssets.Add(AssetData);
			continue;
		}

		if (UPCGExAssetCollection* Sub = Cast<UPCGExAssetCollection>(AssetData.GetAsset()))
		{
			SubCollectionAssets.Add(Sub);
			continue;
		}

		RegularAssets.Add(AssetData);
	}

	if (!SubCollectionAssets.IsEmpty())
	{
		EDITOR_AddSubCollectionEntries(SubCollectionAssets);
	}

	if (!RegularAssets.IsEmpty())
	{
		EDITOR_AddBrowserSelectionInternal(RegularAssets);
	}

	SyncPropertyOverridesToEntries();
	(void)MarkPackageDirty();
	FCoreUObjectDelegates::BroadcastOnObjectModified(this);
}

void UPCGExAssetCollection::EDITOR_AddSubCollectionEntries(const TArray<UPCGExAssetCollection*>& InSubCollections)
{
	if (InSubCollections.IsEmpty())
	{
		return;
	}

	// Reflection only grows the per-class Entries array; bIsSubCollection/SubCollection live on the
	// base struct, so the new element is written through a base pointer. Any collection type is accepted.
	FArrayProperty* ArrayProp = CastField<FArrayProperty>(GetClass()->FindPropertyByName(FName("Entries")));
	if (!ArrayProp)
	{
		return;
	}

	const FStructProperty* InnerProp = CastField<FStructProperty>(ArrayProp->Inner);
	if (!InnerProp || !InnerProp->Struct || !InnerProp->Struct->IsChildOf(FPCGExAssetCollectionEntry::StaticStruct()))
	{
		return;
	}

	void* ArrayData = ArrayProp->ContainerPtrToValuePtr<void>(this);
	FScriptArrayHelper ArrayHelper(ArrayProp, ArrayData);

	// Build a set of subcollections already referenced by existing subcollection entries
	// so drag-dropping the same asset twice doesn't create duplicates -- matches the
	// dedupe behavior of MeshCollection / ActorCollection / PCGDataAssetCollection's
	// EDITOR_AddBrowserSelectionInternal implementations.
	TSet<const UPCGExAssetCollection*> AlreadyReferenced;
	ForEachEntry([&AlreadyReferenced](const FPCGExAssetCollectionEntry* Entry, int32 /*Idx*/)
	{
		if (Entry->HasValidSubCollection())
		{
			AlreadyReferenced.Add(Entry->GetSubCollectionPtr());
		}
	});

	for (UPCGExAssetCollection* Sub : InSubCollections)
	{
		if (!Sub || Sub == this)
		{
			continue;
		}
		if (AlreadyReferenced.Contains(Sub))
		{
			continue;
		}
		if (HasCircularDependency(Sub))
		{
			continue;
		}

		const int32 NewIdx = ArrayHelper.AddValue();
		FPCGExAssetCollectionEntry* NewEntry = reinterpret_cast<FPCGExAssetCollectionEntry*>(ArrayHelper.GetRawPtr(NewIdx));
		NewEntry->bIsSubCollection = true;
		NewEntry->SubCollection = Sub;
		AlreadyReferenced.Add(Sub);
	}
}

void UPCGExAssetCollection::EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData)
{
	// Override in derived classes
}

const UScriptStruct* UPCGExAssetCollection::EDITOR_GetEntryScriptStruct(int32 RawIndex) const
{
	const FArrayProperty* ArrayProp = CastField<FArrayProperty>(GetClass()->FindPropertyByName(FName("Entries")));
	const FStructProperty* InnerProp = ArrayProp ? CastField<FStructProperty>(ArrayProp->Inner) : nullptr;

	if (InnerProp && InnerProp->Struct && InnerProp->Struct->IsChildOf(FPCGExAssetCollectionEntry::StaticStruct()))
	{
		return InnerProp->Struct;
	}

	return nullptr;
}

FPCGExAssetCollectionEntry* UPCGExAssetCollection::EDITOR_AddEntry(const UScriptStruct* EntryStruct)
{
	FArrayProperty* ArrayProp = CastField<FArrayProperty>(GetClass()->FindPropertyByName(FName("Entries")));
	const FStructProperty* InnerProp = ArrayProp ? CastField<FStructProperty>(ArrayProp->Inner) : nullptr;

	if (!InnerProp || !InnerProp->Struct || !InnerProp->Struct->IsChildOf(FPCGExAssetCollectionEntry::StaticStruct()))
	{
		return nullptr;
	}

	// Requests for a BASE of the native type are honored: the element is created native and
	// the caller copies the base portion (how subcollection rows transfer in).
	if (EntryStruct && !InnerProp->Struct->IsChildOf(EntryStruct))
	{
		return nullptr;
	}

	void* ArrayData = ArrayProp->ContainerPtrToValuePtr<void>(this);
	FScriptArrayHelper ArrayHelper(ArrayProp, ArrayData);
	const int32 NewIndex = ArrayHelper.AddValue();
	return reinterpret_cast<FPCGExAssetCollectionEntry*>(ArrayHelper.GetRawPtr(NewIndex));
}
#endif
