// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExSchemaMerging.h"

#include "PCGExLog.h"
#include "PCGExProperty.h"

namespace PCGExProperties
{
	void ApplyMergeResultToSchemas(FPCGExPropertySchemaCollection& InOutCollection, const TArray<FInstancedStruct>& MergedProperties)
	{
		TMap<FName, FPCGExPropertySchema> OldByName;
		OldByName.Reserve(InOutCollection.Schemas.Num());
		for (FPCGExPropertySchema& Old : InOutCollection.Schemas)
		{
			if (!Old.Name.IsNone())
			{
				OldByName.Add(Old.Name, MoveTemp(Old));
			}
		}

		InOutCollection.Schemas.Reset(MergedProperties.Num());
		for (const FInstancedStruct& MergedProp : MergedProperties)
		{
			const FPCGExProperty* Prop = MergedProp.GetPtr<FPCGExProperty>();
			if (!Prop)
			{
				continue;
			}

			FPCGExPropertySchema& NewSchema = InOutCollection.Schemas.AddDefaulted_GetRef();
			if (FPCGExPropertySchema* Existing = OldByName.Find(Prop->PropertyName))
			{
#if WITH_EDITOR
				NewSchema.HeaderId = Existing->HeaderId;
#endif
			}
			NewSchema.Name = Prop->PropertyName;
			NewSchema.Property = MergedProp;
			NewSchema.SyncPropertyName();
		}
	}

	TArray<FInstancedStruct> AggregateAgreedValuesByName(
		TConstArrayView<TConstArrayView<FInstancedStruct>> ValueSets,
		TConstArrayView<TConstArrayView<FInstancedStruct>> FallbackSets)
	{
		// Tracked per name: the first FInstancedStruct seen, and whether every subsequent
		// occurrence compared equal to it. Avoids holding every value array per name.
		TMap<FName, FInstancedStruct> FirstSeen;
		TMap<FName, bool> AllAgree;
		for (const TConstArrayView<FInstancedStruct>& Set : ValueSets)
		{
			for (const FInstancedStruct& Prop : Set)
			{
				const FPCGExProperty* P = Prop.GetPtr<FPCGExProperty>();
				if (!P)
				{
					continue;
				}
				const FName Name = P->PropertyName;
				if (const FInstancedStruct* Existing = FirstSeen.Find(Name))
				{
					if (*Existing != Prop)
					{
						AllAgree[Name] = false;
					}
				}
				else
				{
					FirstSeen.Add(Name, Prop);
					AllAgree.Add(Name, true);
				}
			}
		}

		TMap<FName, FInstancedStruct> FallbackFirst;
		for (const TConstArrayView<FInstancedStruct>& Set : FallbackSets)
		{
			for (const FInstancedStruct& Prop : Set)
			{
				if (const FPCGExProperty* P = Prop.GetPtr<FPCGExProperty>())
				{
					FallbackFirst.FindOrAdd(P->PropertyName, Prop);
				}
			}
		}

		TArray<FInstancedStruct> Out;
		Out.Reserve(FirstSeen.Num());
		for (TPair<FName, FInstancedStruct>& Pair : FirstSeen)
		{
			if (AllAgree.FindChecked(Pair.Key))
			{
				Out.Add(MoveTemp(Pair.Value));
			}
			else if (const FInstancedStruct* Fallback = FallbackFirst.Find(Pair.Key))
			{
				Out.Add(*Fallback);
			}
		}
		return Out;
	}

	void LogSchemaConflicts(const FSchemaMergeResult& MergeResult, const UObject* ContextOwner)
	{
		for (const FSchemaMergeConflict& C : MergeResult.Conflicts)
		{
			UE_LOG(LogPCGEx, Warning,
			       TEXT("[%s] Property schema conflict on '%s' (incoming source %d local %d vs existing source %d): incoming=%s existing=%s -- resolved by policy."),
			       *GetNameSafe(ContextOwner), *C.PropertyName.ToString(),
			       C.IncomingSourceIdx, C.IncomingLocalIdx, C.ExistingSourceIdx,
			       C.IncomingType ? *C.IncomingType->GetName() : TEXT("<null>"),
			       C.ExistingType ? *C.ExistingType->GetName() : TEXT("<null>"));
		}
	}

	FSchemaMergeResult MergeSchemas(
		TConstArrayView<TArray<FInstancedStruct>> Sources,
		EPCGExSchemaMergePolicy Policy)
	{
		FSchemaMergeResult Result;
		Result.SourceToMergedIdx.SetNum(Sources.Num());

		for (int32 i = 0; i < Sources.Num(); ++i)
		{
			Result.SourceToMergedIdx[i].Init(INDEX_NONE, Sources[i].Num());
		}

		// Tracks merged-slot index by property name, plus the source that contributed it
		// (kept for conflict diagnostics).
		struct FSlot
		{
			int32 MergedIdx = INDEX_NONE;
			int32 SourceIdx = INDEX_NONE;
		};

		TMap<FName, FSlot> NameToSlot;

		const bool bRejectMode = (Policy == EPCGExSchemaMergePolicy::Reject);
		bool bAnyConflict = false;

		for (int32 SourceIdx = 0; SourceIdx < Sources.Num(); ++SourceIdx)
		{
			const TArray<FInstancedStruct>& Source = Sources[SourceIdx];

			for (int32 LocalIdx = 0; LocalIdx < Source.Num(); ++LocalIdx)
			{
				const FInstancedStruct& Incoming = Source[LocalIdx];
				const FPCGExProperty* IncomingProp = Incoming.GetPtr<FPCGExProperty>();
				if (!IncomingProp || IncomingProp->PropertyName.IsNone())
				{
					continue;
				}

				const FName Name = IncomingProp->PropertyName;
				const UScriptStruct* IncomingType = Incoming.GetScriptStruct();

				FSlot* ExistingSlot = NameToSlot.Find(Name);

				if (!ExistingSlot)
				{
					// First time this name is seen: add to merged set and record slot.
					const int32 NewIdx = Result.Merged.Add(Incoming);
					FSlot& Added = NameToSlot.Add(Name);
					Added.MergedIdx = NewIdx;
					Added.SourceIdx = SourceIdx;
					Result.SourceToMergedIdx[SourceIdx][LocalIdx] = NewIdx;
					continue;
				}

				// Name collision -- resolve via policy.
				const UScriptStruct* ExistingType = Result.Merged[ExistingSlot->MergedIdx].GetScriptStruct();
				const bool bSameType = (IncomingType == ExistingType);

				auto RecordConflict = [&]()
				{
					FSchemaMergeConflict& Conflict = Result.Conflicts.AddDefaulted_GetRef();
					Conflict.PropertyName = Name;
					Conflict.IncomingSourceIdx = SourceIdx;
					Conflict.IncomingLocalIdx = LocalIdx;
					Conflict.ExistingSourceIdx = ExistingSlot->SourceIdx;
					Conflict.ExistingMergedIdx = ExistingSlot->MergedIdx;
					Conflict.IncomingType = IncomingType;
					Conflict.ExistingType = ExistingType;
					bAnyConflict = true;
				};

				switch (Policy)
				{
				case EPCGExSchemaMergePolicy::FirstWins:
					// Always drop the incoming; record conflict for diagnostics.
					RecordConflict();
					// Same-named entries can still alias to the existing merged slot so per-source
					// overrides can target it -- but only when the types match (otherwise writing
					// would type-punch). Mismatched types leave the slot at INDEX_NONE.
					if (bSameType)
					{
						Result.SourceToMergedIdx[SourceIdx][LocalIdx] = ExistingSlot->MergedIdx;
					}
					break;

				case EPCGExSchemaMergePolicy::LastWins:
					// Replace existing with incoming; record displacement.
					RecordConflict();
					Result.Merged[ExistingSlot->MergedIdx] = Incoming;
					ExistingSlot->SourceIdx = SourceIdx;
					Result.SourceToMergedIdx[SourceIdx][LocalIdx] = ExistingSlot->MergedIdx;
					break;

				case EPCGExSchemaMergePolicy::StrictTypeMatch:
					if (bSameType)
					{
						// Silent dedupe: alias to existing slot, no conflict reported.
						Result.SourceToMergedIdx[SourceIdx][LocalIdx] = ExistingSlot->MergedIdx;
					}
					else
					{
						RecordConflict();
						// Type mismatch: drop incoming, no alias (would type-punch).
					}
					break;

				case EPCGExSchemaMergePolicy::Reject:
					RecordConflict();
					// Don't update aliases either -- Reject mode produces an empty merged set
					// at the end, callers should abort.
					break;
				}
			}
		}

		if (bRejectMode && bAnyConflict)
		{
			Result.Merged.Reset();
			for (TArray<int32>& Map : Result.SourceToMergedIdx)
			{
				for (int32& V : Map)
				{
					V = INDEX_NONE;
				}
			}
		}

		return Result;
	}
}
