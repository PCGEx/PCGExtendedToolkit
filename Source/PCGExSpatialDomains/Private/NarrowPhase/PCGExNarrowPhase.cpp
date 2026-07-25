// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "NarrowPhase/PCGExNarrowPhase.h"

#include "Containers/Map.h"
#include "NarrowPhase/PCGExNarrowPhaseRegistrations.h"

namespace PCGExSpatial::NarrowPhase
{
	namespace
	{
		/**
		 * Single dispatch slot. For an entry at Matrix[StoredTag][CandidateTag]:
		 *   - bSwapArgs = false means call Overlap(Candidate, Stored)
		 *   - bSwapArgs = true  means call Overlap(Stored, Candidate)
		 *
		 * The mirrored slot at [CandidateTag][StoredTag] holds the same fn ptrs
		 * with the swap flag inverted, so any direction of lookup resolves in
		 * a single 2D index.
		 */
		struct FPairSlot
		{
			FPairOverlapFn Overlap = nullptr;
			FPairPenetrationFn Penetration = nullptr;
			bool bSwapArgs = false;
		};

		/**
		 * Module-scoped singleton registry. Function-local static so it lives
		 * in the .obj of this TU regardless of unity-build mergers. Populated
		 * during module init; read-only during query phase, so no synchronization
		 * is needed past registration.
		 *
		 * Storage:
		 *   - StructToTag / TagToStruct: dense bidirectional tag table.
		 *   - Matrix[a][b]: dispatch slot for "stored kind a vs candidate kind b".
		 *     Sized to NumKinds x NumKinds; grown as new kinds are registered.
		 */
		struct FRegistryState
		{
			TArray<TArray<FPairSlot>> Matrix;
			TMap<const UScriptStruct*, FShapeKindTag> StructToTag;
			TArray<const UScriptStruct*> TagToStruct;

			/**
			 * Single-axis table for signed-distance dispatch, indexed by
			 * FShapeKindTag. Parallel to the pair-test Matrix but one-
			 * dimensional -- QueryPoint takes a single stored shape, not a
			 * pair. Grown alongside the matrix in EnsureMatrixSize so the
			 * two stay in lockstep.
			 */
			TArray<FQueryPointFn> QueryPointFns;
		};

		FRegistryState& State()
		{
			static FRegistryState S;
			return S;
		}

		void EnsureMatrixSize(int32 NumKinds)
		{
			FRegistryState& S = State();
			if (S.Matrix.Num() >= NumKinds)
			{
				if (S.QueryPointFns.Num() < NumKinds)
				{
					S.QueryPointFns.SetNumZeroed(NumKinds);
				}
				return;
			}
			S.Matrix.SetNum(NumKinds);
			for (TArray<FPairSlot>& Row : S.Matrix)
			{
				Row.SetNum(NumKinds);
			}
			S.QueryPointFns.SetNumZeroed(NumKinds);
		}
	}

	FShapeKindTag RegisterShapeKind(UScriptStruct* Struct)
	{
		if (!Struct)
		{
			return InvalidKindTag;
		}

		FRegistryState& S = State();
		if (const FShapeKindTag* Existing = S.StructToTag.Find(Struct))
		{
			return *Existing;
		}

		const FShapeKindTag Tag = S.TagToStruct.Add(Struct);
		S.StructToTag.Add(Struct, Tag);
		EnsureMatrixSize(S.TagToStruct.Num());
		return Tag;
	}

	FShapeKindTag FindShapeKindTag(const UScriptStruct* Struct)
	{
		if (!Struct)
		{
			return InvalidKindTag;
		}
		const FShapeKindTag* Found = State().StructToTag.Find(Struct);
		return Found ? *Found : InvalidKindTag;
	}

	void Register(UScriptStruct* StructA, UScriptStruct* StructB, FPairFns Fns)
	{
		if (!ensureMsgf(StructA && StructB, TEXT("PCGExSpatial::NarrowPhase::Register: null UScriptStruct*")))
		{
			return;
		}

		const FShapeKindTag TagA = RegisterShapeKind(StructA);
		const FShapeKindTag TagB = RegisterShapeKind(StructB);

		FRegistryState& S = State();
		FPairSlot& Slot = S.Matrix[TagA][TagB];

		if (Slot.Overlap != nullptr || Slot.Penetration != nullptr)
		{
			ensureMsgf(false,
			           TEXT("PCGExSpatial::NarrowPhase: duplicate registration for (%s, %s) -- last write wins."),
			           *StructA->GetName(), *StructB->GetName());
		}

		Slot.Overlap = Fns.Overlap;
		Slot.Penetration = Fns.Penetration;
		Slot.bSwapArgs = false;

		if (TagA != TagB)
		{
			// Mirror: a query coming in as (B, A) resolves to the same fns
			// with the swap flag set, so the impl is always called with the
			// arg order the registration specified.
			FPairSlot& Mirror = S.Matrix[TagB][TagA];
			ensureMsgf(Mirror.Overlap == nullptr,
			           TEXT("PCGExSpatial::NarrowPhase: pair (%s, %s) is already registered in the reverse direction. "
				           "Register only one orientation; lookups in the other direction resolve via arg swap."),
			           *StructA->GetName(), *StructB->GetName());
			Mirror.Overlap = Fns.Overlap;
			Mirror.Penetration = Fns.Penetration;
			Mirror.bSwapArgs = true;
		}
	}

	void UnregisterAll()
	{
		FRegistryState& S = State();
		S.Matrix.Reset();
		S.StructToTag.Reset();
		S.TagToStruct.Reset();
		S.QueryPointFns.Reset();
	}

	void RegisterBuiltInPairTests()
	{
		// The one list -- tests restore through it, so a kind cannot be half-registered.
		RegisterOBBPairTests();
		RegisterPolygonPairTests();
		RegisterVolumePrimitivePairTests();
	}

	void RegisterQueryPoint(UScriptStruct* Struct, FQueryPointFn Fn)
	{
		if (!ensureMsgf(Struct, TEXT("PCGExSpatial::NarrowPhase::RegisterQueryPoint: null UScriptStruct*")))
		{
			return;
		}
		if (!ensureMsgf(Fn, TEXT("PCGExSpatial::NarrowPhase::RegisterQueryPoint: null FQueryPointFn for %s"), *Struct->GetName()))
		{
			return;
		}

		const FShapeKindTag Tag = RegisterShapeKind(Struct);
		FRegistryState& S = State();

		if (S.QueryPointFns[Tag] != nullptr)
		{
			ensureMsgf(false,
			           TEXT("PCGExSpatial::NarrowPhase: duplicate QueryPoint registration for %s -- last write wins."),
			           *Struct->GetName());
		}

		S.QueryPointFns[Tag] = Fn;
	}

	bool HasQueryPoint(const UScriptStruct* Struct)
	{
		const FShapeKindTag Tag = FindShapeKindTag(Struct);
		if (Tag == InvalidKindTag)
		{
			return false;
		}
		const FRegistryState& S = State();
		return S.QueryPointFns.IsValidIndex(Tag) && S.QueryPointFns[Tag] != nullptr;
	}

	float QueryPoint(FShapeKindTag StoredKind, const FVector& Point, const FPCGExFootprintShape& Stored)
	{
		const FRegistryState& S = State();
		check(StoredKind >= 0 && StoredKind < S.QueryPointFns.Num());

		const FQueryPointFn Fn = S.QueryPointFns[StoredKind];
		if (!Fn)
		{
			return TNumericLimits<float>::Max();
		}
		return Fn(Point, Stored);
	}

	float QueryPoint(const FVector& Point, const FPCGExFootprintShape& Stored)
	{
		const FShapeKindTag Kind = FindShapeKindTag(Stored.GetScriptStruct());
		if (Kind == InvalidKindTag)
		{
			return TNumericLimits<float>::Max();
		}
		return QueryPoint(Kind, Point, Stored);
	}

	bool TestOverlap(
		FShapeKindTag AKind, const FPCGExFootprintShape& A,
		FShapeKindTag BKind, const FPCGExFootprintShape& B)
	{
		const FRegistryState& S = State();
		check(AKind >= 0 && AKind < S.Matrix.Num());
		check(BKind >= 0 && BKind < S.Matrix.Num());

		const FPairSlot& Slot = S.Matrix[AKind][BKind];
		if (!Slot.Overlap)
		{
			return false;
		}
		return Slot.bSwapArgs ? Slot.Overlap(B, A) : Slot.Overlap(A, B);
	}

	float QueryPenetration(
		FShapeKindTag AKind, const FPCGExFootprintShape& A,
		FShapeKindTag BKind, const FPCGExFootprintShape& B)
	{
		const FRegistryState& S = State();
		check(AKind >= 0 && AKind < S.Matrix.Num());
		check(BKind >= 0 && BKind < S.Matrix.Num());

		const FPairSlot& Slot = S.Matrix[AKind][BKind];

		if (Slot.Penetration)
		{
			return Slot.bSwapArgs ? Slot.Penetration(B, A) : Slot.Penetration(A, B);
		}

		// No penetration fn: degenerate to "any overlap is infinite penetration".
		// Callers comparing against MaxAllowedPenetration get the conservative
		// reject, matching the FSpatialDomain default-base semantic.
		if (Slot.Overlap)
		{
			const bool bOverlaps = Slot.bSwapArgs ? Slot.Overlap(B, A) : Slot.Overlap(A, B);
			return bOverlaps ? TNumericLimits<float>::Max() : 0.0f;
		}

		return TNumericLimits<float>::Max();
	}

	bool TestOverlap(const FPCGExFootprintShape& A, const FPCGExFootprintShape& B)
	{
		const FShapeKindTag AKind = FindShapeKindTag(A.GetScriptStruct());
		const FShapeKindTag BKind = FindShapeKindTag(B.GetScriptStruct());
		if (AKind == InvalidKindTag || BKind == InvalidKindTag)
		{
			return false;
		}
		return TestOverlap(AKind, A, BKind, B);
	}

	float QueryPenetration(const FPCGExFootprintShape& A, const FPCGExFootprintShape& B)
	{
		const FShapeKindTag AKind = FindShapeKindTag(A.GetScriptStruct());
		const FShapeKindTag BKind = FindShapeKindTag(B.GetScriptStruct());
		if (AKind == InvalidKindTag || BKind == InvalidKindTag)
		{
			return TNumericLimits<float>::Max();
		}
		return QueryPenetration(AKind, A, BKind, B);
	}
}
