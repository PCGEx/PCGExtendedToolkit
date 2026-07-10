// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Selectors/PCGExSelectorClosestMatch.h"

#include "PCGExProperty.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Selectors/PCGExSelectorHelpers.h"

namespace PCGExSelectorClosestMatch
{
	// Resolve the property's declared metadata type from the collection's schema. Returns
	// EPCGMetadataTypes::Unknown when no schema with that name exists.
	EPCGMetadataTypes ResolveAxisType(const UPCGExAssetCollection* Collection, FName PropertyName)
	{
		if (!Collection || PropertyName.IsNone())
		{
			return EPCGMetadataTypes::Unknown;
		}
		const FInstancedStruct* PropStruct = Collection->CollectionProperties.GetPropertyByName(PropertyName);
		if (!PropStruct)
		{
			return EPCGMetadataTypes::Unknown;
		}
		const FPCGExProperty* Base = PropStruct->GetPtr<FPCGExProperty>();
		return Base ? Base->GetOutputType() : EPCGMetadataTypes::Unknown;
	}

	// Build the typed axis data for one axis. Scans all entries to populate EntryValues +
	// EntryResolved; when bNormalize is requested AND the type has a meaningful range,
	// also captures EntryMin/EntryMax across resolved entries.
	template <typename T>
	TSharedPtr<FPCGExClosestMatchAxisData> BuildAxisDataT(
		const UPCGExAssetCollection* Collection,
		const PCGExAssetCollection::FCategory* Target,
		const FPCGExSelectorClosestMatchAxis& Axis)
	{
		const int32 N = Target->Entries.Num();
		TSharedPtr<TPCGExClosestMatchAxisData<T>> AxisData = MakeShared<TPCGExClosestMatchAxisData<T>>();
		AxisData->Type = PCGExTypes::TTraits<T>::Type;
		AxisData->EntryValues.SetNum(N);
		AxisData->EntryResolved.Init(false, N);

		// Entry-side min/max is only consumed by the remap path, so gate capture on IsNormalizable
		// -- int32/int64 axes still get bNormalize honored as a UI knob but produce no remap state.
		constexpr bool bTypeIsNormalizable = PCGExClosestMatch::IsNormalizable<T>();
		const bool bTrackRange = bTypeIsNormalizable && Axis.bNormalize;
		bool bFirstResolved = true;

		for (int32 i = 0; i < N; ++i)
		{
			const FPCGExAssetCollectionEntry* Entry = Target->Entries[i];
			if (!Entry)
			{
				continue;
			}
			T Value{};
			if (!Entry->TryGetPropertyValue<T>(Collection, Axis.PropertyName, Value))
			{
				continue;
			}
			AxisData->EntryValues[i] = Value;
			AxisData->EntryResolved[i] = true;
			++AxisData->ResolvedCount;

			if constexpr (bTypeIsNormalizable)
			{
				if (bTrackRange)
				{
					if (bFirstResolved)
					{
						AxisData->EntryMin = Value;
						AxisData->EntryMax = Value;
						bFirstResolved = false;
					}
					else
					{
						PCGExTypeOps::FTypeOps<T>::ExtendRange(AxisData->EntryMin, AxisData->EntryMax, Value, Value);
					}
				}
			}
		}

		AxisData->bHasMinMax = bTrackRange && !bFirstResolved;
		return AxisData;
	}

	// Construct + bind a typed evaluator for one axis. Returns null if the broadcaster
	// fails to attach (missing/incompatible attribute) -- caller treats null as "axis
	// disabled for this facade".
	template <typename T>
	TSharedPtr<FPCGExClosestMatchAxisEvaluator> BuildEvaluatorT(
		const TSharedRef<PCGExData::FFacade>& Facade,
		const FPCGExSelectorClosestMatchAxis& Axis,
		const TSharedPtr<FPCGExClosestMatchAxisData>& SharedAxisData)
	{
		TSharedPtr<TPCGExClosestMatchAxisData<T>> Typed = StaticCastSharedPtr<TPCGExClosestMatchAxisData<T>>(SharedAxisData);
		if (!Typed.IsValid())
		{
			return nullptr;
		}

		const bool bWantRemap = Axis.bNormalize && PCGExClosestMatch::IsNormalizable<T>();
		// bCaptureMinMax piggybacks on bWantRemap -- the only consumer of the broadcaster's
		// Min/Max is the remap path. Scoped reads are disabled when we need the full scan.
		TSharedPtr<PCGExData::TBuffer<T>> Broadcaster = Facade->GetBroadcaster<T>(Axis.AttributeName, !bWantRemap, bWantRemap, true);
		if (!Broadcaster.IsValid())
		{
			return nullptr;
		}

		TSharedPtr<TPCGExClosestMatchAxisEvaluator<T>> Eval = MakeShared<TPCGExClosestMatchAxisEvaluator<T>>();
		Eval->Weight = Axis.Weight;
		Eval->Broadcaster = Broadcaster;
		Eval->Shared = Typed;
		Eval->EntryValuesPtr = Typed->EntryValues.GetData();

		if constexpr (PCGExClosestMatch::IsNormalizable<T>())
		{
			if (bWantRemap && Typed->bHasMinMax)
			{
				const double PointRangeMag = PCGExTypeOps::FTypeOps<T>::RangeMagnitude(Broadcaster->Min, Broadcaster->Max);
				const double EntryRangeMag = PCGExTypeOps::FTypeOps<T>::RangeMagnitude(Typed->EntryMin, Typed->EntryMax);

				// Both sides need a non-degenerate range. If either side has all values collapsed
				// onto a single point, the per-side remap can't differentiate -- fall back to raw
				// distance (bRemap=false) so the axis still contributes argmin-relevant information.
				if (FMath::IsFinite(PointRangeMag) && PointRangeMag > UE_DOUBLE_SMALL_NUMBER &&
					FMath::IsFinite(EntryRangeMag) && EntryRangeMag > UE_DOUBLE_SMALL_NUMBER)
				{
					Eval->PointMin = Broadcaster->Min;
					Eval->PointInvRange = PCGExTypeOps::FTypeOps<T>::ComputeInvRange(Broadcaster->Min, Broadcaster->Max);
					Eval->bRemap = true;

					// Entry-side remap state is constant per facade -- bake it into a remapped copy
					// once here so the hot path never re-remaps entries. Unresolved entries hold
					// default values; remapping them is harmless (never reached via ValidEntryIndices).
					const T EntryMin = Typed->EntryMin;
					const T EntryInvRange = PCGExTypeOps::FTypeOps<T>::ComputeInvRange(Typed->EntryMin, Typed->EntryMax);
					const int32 NumEntries = Typed->EntryValues.Num();
					Eval->RemappedEntryValues.SetNumUninitialized(NumEntries);
					for (int32 i = 0; i < NumEntries; ++i)
					{
						Eval->RemappedEntryValues[i] = PCGExTypeOps::FTypeOps<T>::ApplyRemap(Typed->EntryValues[i], EntryMin, EntryInvRange);
					}
					Eval->EntryValuesPtr = Eval->RemappedEntryValues.GetData();
				}
			}
		}

		return Eval;
	}
}

#pragma region FPCGExEntryClosestMatchPickerOp

void FPCGExEntryClosestMatchPickerOp::OnSharedDataMissing(FPCGExContext* InContext) const
{
	PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Selector : Closest Match -- no entries resolved every configured axis. Check axis property names / types in the collection."))
}

bool FPCGExEntryClosestMatchPickerOp::OnInitForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade)
{
	const int32 AxisCount = Axes.Num();
	if (AxisCount == 0 || AxisCount != Shared->AxisCount)
	{
		PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Selector : Closest Match -- axis configuration mismatch between op and shared data."))
		return false;
	}

	ActiveEvaluators.Reset();
	ActiveEvaluators.Reserve(AxisCount);
	for (int32 A = 0; A < AxisCount; ++A)
	{
		const TSharedPtr<FPCGExClosestMatchAxisData>& SharedAxisData = Shared->AxisData[A];
		if (!SharedAxisData.IsValid())
		{
			continue; // Axis was disabled at BuildSharedData (unsupported type / unresolved).
		}

		TSharedPtr<FPCGExClosestMatchAxisEvaluator> Eval;

#define PCGEX_CLOSEST_BIND_EVAL(_TYPE, _NAME) \
		if constexpr (PCGExClosestMatch::IsSupported<_TYPE>()) { Eval = PCGExSelectorClosestMatch::BuildEvaluatorT<_TYPE>(InDataFacade, Axes[A], SharedAxisData); }

		PCGEX_EXECUTEWITHRIGHTTYPE(SharedAxisData->Type, PCGEX_CLOSEST_BIND_EVAL)
#undef PCGEX_CLOSEST_BIND_EVAL

		if (!Eval.IsValid())
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext,
			           FText::Format(FTEXT("Selector : Closest Match -- axis {0} could not bind attribute '{1}' as the expected type; this axis will be skipped."),
				           FText::AsNumber(A), FText::FromName(Axes[A].AttributeName)));
			continue;
		}

		ActiveEvaluators.Add(Eval);
	}

	if (ActiveEvaluators.IsEmpty())
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Selector : Closest Match -- no axes could bind their query attribute; nothing to pick from."));
		return false;
	}

	return true;
}

TSharedPtr<FPCGExPickerScratchBase> FPCGExEntryClosestMatchPickerOp::CreateScratchForScope(int32 MaxPointsInScope) const
{
	return MakeShared<FPCGExClosestMatchScratch>();
}

int32 FPCGExEntryClosestMatchPickerOp::Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());

	const TArray<int32>& ValidEntryIndices = Shared->ValidEntryIndices;
	if (ValidEntryIndices.IsEmpty())
	{
		return -1;
	}

	// Axis-major accumulation: one virtual call per axis (not per axis per entry), with the
	// query attribute read + remapped once per axis per pick inside Accumulate. Distances is
	// compact over ValidEntryIndices.
	FPCGExClosestMatchScratch LocalScratch;
	FPCGExClosestMatchScratch& S = Scratch ? *static_cast<FPCGExClosestMatchScratch*>(Scratch) : LocalScratch;
	S.Distances.Reset();
	S.Distances.SetNumZeroed(ValidEntryIndices.Num());

	for (const TSharedPtr<FPCGExClosestMatchAxisEvaluator>& Eval : ActiveEvaluators)
	{
		Eval->Accumulate(PointIndex, ValidEntryIndices, S.Distances);
	}

	// First encountered minimum wins -- same tie behavior as the previous entry-major loop.
	double BestDist = TNumericLimits<double>::Max();
	int32 BestLocal = -1;
	for (int32 j = 0; j < S.Distances.Num(); ++j)
	{
		if (S.Distances[j] < BestDist)
		{
			BestDist = S.Distances[j];
			BestLocal = j;
		}
	}

	return BestLocal == -1 ? -1 : Target->Indices[ValidEntryIndices[BestLocal]];
}

int32 FPCGExEntryClosestMatchPickerOp::PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch) const
{
	checkSlow(Target && !Target->IsEmpty());

	const TArray<int32>& ValidEntryIndices = Shared->ValidEntryIndices;
	if (ValidEntryIndices.IsEmpty())
	{
		return -1;
	}

	// Quota-only path: distances are accumulated for the full valid set (the batched evaluators
	// are cheaper unsegmented), then availability filters the argmin.
	FPCGExClosestMatchScratch LocalScratch;
	FPCGExClosestMatchScratch& S = Scratch ? *static_cast<FPCGExClosestMatchScratch*>(Scratch) : LocalScratch;
	S.Distances.Reset();
	S.Distances.SetNumZeroed(ValidEntryIndices.Num());

	for (const TSharedPtr<FPCGExClosestMatchAxisEvaluator>& Eval : ActiveEvaluators)
	{
		Eval->Accumulate(PointIndex, ValidEntryIndices, S.Distances);
	}

	double BestDist = TNumericLimits<double>::Max();
	int32 BestLocal = -1;
	for (int32 j = 0; j < S.Distances.Num(); ++j)
	{
		if (!InAvailability.IsAvailable(ValidEntryIndices[j]))
		{
			continue;
		}
		if (S.Distances[j] < BestDist)
		{
			BestDist = S.Distances[j];
			BestLocal = j;
		}
	}

	return BestLocal == -1 ? -1 : Target->Indices[ValidEntryIndices[BestLocal]];
}

#pragma endregion

#pragma region UPCGExSelectorClosestMatchFactoryData

TSharedPtr<FPCGExEntryPickerOperation> UPCGExSelectorClosestMatchFactoryData::CreateEntryOperation(FPCGExContext* InContext) const
{
	TSharedPtr<FPCGExEntryClosestMatchPickerOp> NewOp = MakeShared<FPCGExEntryClosestMatchPickerOp>();
	NewOp->Axes = Config.Axes;
	return NewOp;
}

TSharedPtr<PCGExCollections::FSelectorSharedData> UPCGExSelectorClosestMatchFactoryData::BuildSharedData(
	const UPCGExAssetCollection* Collection,
	const PCGExAssetCollection::FCategory* Target) const
{
	if (!Collection || !Target)
	{
		return nullptr;
	}

	const int32 AxisCount = Config.Axes.Num();
	if (AxisCount == 0)
	{
		return nullptr;
	}

	const int32 N = Target->Entries.Num();
	if (N == 0)
	{
		return nullptr;
	}

	TSharedPtr<FPCGExClosestMatchSharedData> NewShared = MakeShared<FPCGExClosestMatchSharedData>();
	NewShared->AxisCount = AxisCount;
	NewShared->EntryCount = N;
	NewShared->AxisData.SetNum(AxisCount);

	int32 ResolvedAxisCount = 0;
	for (int32 A = 0; A < AxisCount; ++A)
	{
		const FPCGExSelectorClosestMatchAxis& Axis = Config.Axes[A];
		const EPCGMetadataTypes Type = PCGExSelectorClosestMatch::ResolveAxisType(Collection, Axis.PropertyName);

		TSharedPtr<FPCGExClosestMatchAxisData> AxisData;

#define PCGEX_CLOSEST_BUILD_AXIS(_TYPE, _NAME) \
		if constexpr (PCGExClosestMatch::IsSupported<_TYPE>()) { AxisData = PCGExSelectorClosestMatch::BuildAxisDataT<_TYPE>(Collection, Target, Axis); }

		PCGEX_EXECUTEWITHRIGHTTYPE(Type, PCGEX_CLOSEST_BUILD_AXIS)
#undef PCGEX_CLOSEST_BUILD_AXIS

		if (!AxisData.IsValid() || AxisData->ResolvedCount == 0)
		{
			// Axis property is unsupported, missing from the schema, or no entry resolved a value.
			NewShared->AxisData[A] = nullptr;
			continue;
		}

		NewShared->AxisData[A] = AxisData;
		++ResolvedAxisCount;
	}

	if (ResolvedAxisCount == 0)
	{
		return nullptr;
	}

	// ValidEntryIndices = entries resolved on EVERY non-null axis. An axis disabled at the
	// shared-data level doesn't gate inclusion; only configured-and-resolved axes do.
	NewShared->ValidEntryIndices.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		bool bAllResolved = true;
		for (const TSharedPtr<FPCGExClosestMatchAxisData>& AxisData : NewShared->AxisData)
		{
			if (!AxisData.IsValid())
			{
				continue;
			}
			if (!AxisData->EntryResolved[i])
			{
				bAllResolved = false;
				break;
			}
		}
		if (bAllResolved)
		{
			NewShared->ValidEntryIndices.Add(i);
		}
	}

	if (NewShared->ValidEntryIndices.IsEmpty())
	{
		return nullptr;
	}

	return NewShared;
}

#pragma endregion

#pragma region UPCGExSelectorClosestMatchFactoryProviderSettings

UPCGExFactoryData* UPCGExSelectorClosestMatchFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExSelectorClosestMatchFactoryData* NewFactory = InContext->ManagedObjects->New<UPCGExSelectorClosestMatchFactoryData>();
	NewFactory->BaseConfig = BaseConfig;
	NewFactory->Config = Config;
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExSelectorClosestMatchFactoryProviderSettings::GetDisplayName() const
{
	return TEXT("Select : Closest Match");
}
#endif


#pragma endregion
