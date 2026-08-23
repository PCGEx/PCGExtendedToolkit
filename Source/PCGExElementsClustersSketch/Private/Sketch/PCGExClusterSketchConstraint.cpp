// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExClusterSketchConstraint.h"

#include "Sketch/PCGExClusterSketchModel.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "PCGExSketchConstraint"

void PCGExSketch::GatherConstraintTypes(TArray<const UScriptStruct*>& OutTypes)
{
	OutTypes.Reset();
	const UScriptStruct* Base = FPCGExSketchConstraint::StaticStruct();
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		if (*It != Base && It->IsChildOf(Base))
		{
			OutTypes.Add(*It);
		}
	}
	OutTypes.Sort([](const UScriptStruct& A, const UScriptStruct& B)
	{
		return A.GetName() < B.GetName();
	});
}

#pragma region FPCGExSketchSolveContext

void FPCGExSketchSolveContext::Build(const FPCGExClusterSketchModel& InModel, const FPCGExLatticeBasis* InBasis, const TConstArrayView<uint32> InPinnedIds)
{
	Model = &InModel;
	Basis = InBasis;

	const int32 NumVtx = InModel.Vertices.Num();
	Positions.SetNumUninitialized(NumVtx);
	bMovable.Init(false, NumVtx);
	VertexIndexById.Reset();
	VertexIndexById.Reserve(NumVtx);
	for (int32 i = 0; i < NumVtx; ++i)
	{
		Positions[i] = FPCGExClusterSketchModel::ResolvedLocation(InModel.Vertices[i], InBasis);
		VertexIndexById.Add(InModel.Vertices[i].Id, i);
	}

	EdgeIndexById.Reset();
	EdgeIndexById.Reserve(InModel.Edges.Num());
	for (int32 e = 0; e < InModel.Edges.Num(); ++e)
	{
		EdgeIndexById.Add(InModel.Edges[e].Id, e);
	}

	PinnedIds.Reset();
	PinnedIds.Append(InPinnedIds);

#if WITH_EDITORONLY_DATA
	// Infinite mass by default: only a subject of some enabled constraint ever moves.
	TArray<int32> Movable;
	for (const FInstancedStruct& Entry : InModel.Constraints)
	{
		const FPCGExSketchConstraint* Constraint = Entry.GetPtr<FPCGExSketchConstraint>();
		if (!Constraint || !Constraint->bEnabled || !Constraint->ResolvesIn(*this))
		{
			continue;
		}
		Movable.Reset();
		Constraint->GatherMovableVertices(*this, Movable);
		for (const int32 Index : Movable)
		{
			bMovable[Index] = true;
		}
	}
#endif
}

bool FPCGExSketchSolveContext::CanMove(const int32 VertexIdx) const
{
	return bMovable.IsValidIndex(VertexIdx) && bMovable[VertexIdx] && Model && !PinnedIds.Contains(Model->Vertices[VertexIdx].Id);
}

#pragma endregion

#pragma region FPCGExSketchConstraint

bool FPCGExSketchConstraint::ResolvesIn(const FPCGExSketchSolveContext& Ctx) const
{
	const int32 Expected = GetNumSubjects();
	if (Expected != PCGExSketch::VariadicSubjects && Subjects.Num() != Expected)
	{
		return false;
	}
	for (int32 Slot = 0; Slot < Subjects.Num(); ++Slot)
	{
		const int32 KindSlot = Expected == PCGExSketch::VariadicSubjects ? 0 : Slot;
		const bool bResolves = GetSubjectKind(KindSlot) == EPCGExSketchSubjectKind::Vertex
			? Ctx.VertexIndex(Subjects[Slot]) != INDEX_NONE
			: Ctx.EdgeIndex(Subjects[Slot]) != INDEX_NONE;
		if (!bResolves)
		{
			return false;
		}
	}
	return true;
}

bool FPCGExSketchConstraint::Concerns(const FPCGExSketchSolveContext& Ctx, const TConstArrayView<uint32> InVertexIds, const TConstArrayView<uint32> InEdgeIds) const
{
	if (!InEdgeIds.IsEmpty())
	{
		const int32 Expected = GetNumSubjects();
		for (int32 Slot = 0; Slot < Subjects.Num(); ++Slot)
		{
			const int32 KindSlot = Expected == PCGExSketch::VariadicSubjects ? 0 : Slot;
			if (GetSubjectKind(KindSlot) == EPCGExSketchSubjectKind::Edge && InEdgeIds.Contains(Subjects[Slot]))
			{
				return true;
			}
		}
	}
	if (!InVertexIds.IsEmpty())
	{
		TArray<int32> Moved;
		GatherMovableVertices(Ctx, Moved);
		for (const int32 Index : Moved)
		{
			if (Ctx.Model->Vertices.IsValidIndex(Index) && InVertexIds.Contains(Ctx.Model->Vertices[Index].Id))
			{
				return true;
			}
		}
	}
	return false;
}

void FPCGExSketchConstraint::GatherMovableVertices(const FPCGExSketchSolveContext& Ctx, TArray<int32>& OutVertexIndices) const
{
	const int32 Expected = GetNumSubjects();
	for (int32 Slot = 0; Slot < Subjects.Num(); ++Slot)
	{
		const int32 KindSlot = Expected == PCGExSketch::VariadicSubjects ? 0 : Slot;
		if (GetSubjectKind(KindSlot) == EPCGExSketchSubjectKind::Vertex)
		{
			OutVertexIndices.AddUnique(Ctx.VertexIndex(Subjects[Slot]));
		}
		else
		{
			const int32 EdgeIdx = Ctx.EdgeIndex(Subjects[Slot]);
			if (EdgeIdx != INDEX_NONE)
			{
				OutVertexIndices.AddUnique(Ctx.Model->Edges[EdgeIdx].A);
				OutVertexIndices.AddUnique(Ctx.Model->Edges[EdgeIdx].B);
			}
		}
	}
	OutVertexIndices.Remove(INDEX_NONE);
}

#pragma endregion

#pragma region FPCGExSketchConstraint_Along

FText FPCGExSketchConstraint_Along::GetRoleName(const int32 Slot) const
{
	switch (Slot)
	{
	case PCGExSketch::AlongRole::Subject: return LOCTEXT("AlongSubject", "Vertex");
	case PCGExSketch::AlongRole::AnchorA: return LOCTEXT("AlongAnchorA", "Anchor A");
	case PCGExSketch::AlongRole::AnchorB: return LOCTEXT("AlongAnchorB", "Anchor B");
	default: return FText::GetEmpty();
	}
}

FText FPCGExSketchConstraint_Along::GetDisplayName() const
{
	return LOCTEXT("AlongName", "Along");
}

bool FPCGExSketchConstraint_Along::Target(const FPCGExSketchSolveContext& Ctx, FVector& OutTarget) const
{
	if (Subjects.Num() != 3)
	{
		return false;
	}
	const int32 A = Ctx.VertexIndex(Subjects[PCGExSketch::AlongRole::AnchorA]);
	const int32 B = Ctx.VertexIndex(Subjects[PCGExSketch::AlongRole::AnchorB]);
	if (A == INDEX_NONE || B == INDEX_NONE)
	{
		return false;
	}

	const FVector& PA = Ctx.Positions[A];
	const FVector& PB = Ctx.Positions[B];
	if (bByFraction)
	{
		OutTarget = FMath::Lerp(PA, PB, Fraction);
		return true;
	}

	const FVector Span = PB - PA;
	const double SpanLength = Span.Size();
	if (SpanLength <= UE_DOUBLE_SMALL_NUMBER)
	{
		OutTarget = PA;
		return true;
	}
	// Clamped to the span: a distance past the far anchor is the anchors' problem, not a licence to overshoot.
	const double Along = FMath::Min(Distance, SpanLength);
	OutTarget = bFromB ? PB - Span / SpanLength * Along : PA + Span / SpanLength * Along;
	return true;
}

void FPCGExSketchConstraint_Along::Project(FPCGExSketchSolveContext& Ctx) const
{
	FVector Target;
	const int32 S = Ctx.VertexIndex(Subjects[PCGExSketch::AlongRole::Subject]);
	if (S != INDEX_NONE && Ctx.CanMove(S) && this->Target(Ctx, Target))
	{
		Ctx.Positions[S] = Target;
	}
}

double FPCGExSketchConstraint_Along::Residual(const FPCGExSketchSolveContext& Ctx) const
{
	FVector Target;
	const int32 S = Ctx.VertexIndex(Subjects[PCGExSketch::AlongRole::Subject]);
	return (S != INDEX_NONE && this->Target(Ctx, Target)) ? FVector::Dist(Ctx.Positions[S], Target) : 0.0;
}

void FPCGExSketchConstraint_Along::InitializeFromGeometry(const FPCGExSketchSolveContext& Ctx)
{
	// Seed from where the vertex already sits: attaching the constraint must not move anything on its own.
	if (Subjects.Num() == 3)
	{
		const int32 S = Ctx.VertexIndex(Subjects[PCGExSketch::AlongRole::Subject]);
		if (S != INDEX_NONE)
		{
			AbsorbProposal(Ctx, Subjects[PCGExSketch::AlongRole::Subject], Ctx.Positions[S]);
		}
	}
}

bool FPCGExSketchConstraint_Along::AbsorbProposal(const FPCGExSketchSolveContext& Ctx, const uint32 InVertexId, const FVector& InProposed)
{
	if (Subjects.Num() != 3 || Subjects[PCGExSketch::AlongRole::Subject] != InVertexId)
	{
		return false;
	}
	const int32 A = Ctx.VertexIndex(Subjects[PCGExSketch::AlongRole::AnchorA]);
	const int32 B = Ctx.VertexIndex(Subjects[PCGExSketch::AlongRole::AnchorB]);
	if (A == INDEX_NONE || B == INDEX_NONE)
	{
		return false;
	}

	// The proposal projected onto the span IS the new parameter; the solve then lands the vertex there.
	const FVector Span = Ctx.Positions[B] - Ctx.Positions[A];
	const double SpanLengthSq = Span.SizeSquared();
	const double T = SpanLengthSq > UE_DOUBLE_SMALL_NUMBER
		? FMath::Clamp(FVector::DotProduct(InProposed - Ctx.Positions[A], Span) / SpanLengthSq, 0.0, 1.0)
		: 0.0;
	Fraction = T;
	Distance = bFromB ? (1.0 - T) * FMath::Sqrt(SpanLengthSq) : T * FMath::Sqrt(SpanLengthSq);
	return true;
}

void FPCGExSketchConstraint_Along::GatherMovableVertices(const FPCGExSketchSolveContext& Ctx, TArray<int32>& OutVertexIndices) const
{
	if (Subjects.Num() == 3)
	{
		const int32 S = Ctx.VertexIndex(Subjects[PCGExSketch::AlongRole::Subject]);
		if (S != INDEX_NONE)
		{
			OutVertexIndices.AddUnique(S);
		}
	}
}

bool FPCGExSketchConstraint_Along::BuildSubjectsFromSelection(const FPCGExClusterSketchModel& InModel, const TConstArrayView<uint32> InSelectedVertexIds, const TConstArrayView<uint32> InSelectedEdgeIds)
{
#if WITH_EDITORONLY_DATA
	if (InSelectedVertexIds.Num() != 1 || !InSelectedEdgeIds.IsEmpty())
	{
		return false;
	}
	uint32 A = 0;
	uint32 B = 0;
	if (!InModel.InferAlongAnchors(InSelectedVertexIds[0], A, B))
	{
		return false;
	}
	Subjects = {InSelectedVertexIds[0], A, B};
	return true;
#else
	return false;
#endif
}

#pragma endregion

#pragma region FPCGExSketchConstraint_Length

FText FPCGExSketchConstraint_Length::GetRoleName(const int32 Slot) const
{
	return LOCTEXT("LengthSubject", "Edge");
}

FText FPCGExSketchConstraint_Length::GetDisplayName() const
{
	return LOCTEXT("LengthName", "Length");
}

void FPCGExSketchConstraint_Length::Project(FPCGExSketchSolveContext& Ctx) const
{
	const int32 EdgeIdx = Ctx.EdgeIndex(Subjects[0]);
	if (EdgeIdx == INDEX_NONE)
	{
		return;
	}
	const FPCGExClusterSketchEdge& E = Ctx.Model->Edges[EdgeIdx];
	if (!Ctx.Positions.IsValidIndex(E.A) || !Ctx.Positions.IsValidIndex(E.B))
	{
		return;
	}

	const bool bMoveA = Ctx.CanMove(E.A);
	const bool bMoveB = Ctx.CanMove(E.B);
	if (!bMoveA && !bMoveB)
	{
		return;
	}

	FVector& PA = Ctx.Positions[E.A];
	FVector& PB = Ctx.Positions[E.B];
	const FVector Delta = PB - PA;
	const double Current = Delta.Size();
	// A collapsed edge has no direction to grow along; leave it for the collocation validation to report.
	if (Current <= UE_DOUBLE_SMALL_NUMBER)
	{
		return;
	}
	const FVector Correction = Delta / Current * (Length - Current);

	if (bMoveA && bMoveB)
	{
		PA -= Correction * 0.5;
		PB += Correction * 0.5;
	}
	else if (bMoveA)
	{
		PA -= Correction;
	}
	else
	{
		PB += Correction;
	}
}

double FPCGExSketchConstraint_Length::Residual(const FPCGExSketchSolveContext& Ctx) const
{
	const int32 EdgeIdx = Ctx.EdgeIndex(Subjects[0]);
	if (EdgeIdx == INDEX_NONE)
	{
		return 0.0;
	}
	const FPCGExClusterSketchEdge& E = Ctx.Model->Edges[EdgeIdx];
	if (!Ctx.Positions.IsValidIndex(E.A) || !Ctx.Positions.IsValidIndex(E.B))
	{
		return 0.0;
	}
	return FMath::Abs(FVector::Dist(Ctx.Positions[E.A], Ctx.Positions[E.B]) - Length);
}

void FPCGExSketchConstraint_Length::InitializeFromGeometry(const FPCGExSketchSolveContext& Ctx)
{
	const int32 EdgeIdx = Subjects.IsEmpty() ? INDEX_NONE : Ctx.EdgeIndex(Subjects[0]);
	if (EdgeIdx == INDEX_NONE)
	{
		return;
	}
	const FPCGExClusterSketchEdge& E = Ctx.Model->Edges[EdgeIdx];
	if (Ctx.Positions.IsValidIndex(E.A) && Ctx.Positions.IsValidIndex(E.B))
	{
		Length = FVector::Dist(Ctx.Positions[E.A], Ctx.Positions[E.B]);
	}
}

bool FPCGExSketchConstraint_Length::BuildSubjectsFromSelection(const FPCGExClusterSketchModel& InModel, const TConstArrayView<uint32> InSelectedVertexIds, const TConstArrayView<uint32> InSelectedEdgeIds)
{
	if (InSelectedEdgeIds.Num() != 1 || !InSelectedVertexIds.IsEmpty())
	{
		return false;
	}
	Subjects = {InSelectedEdgeIds[0]};
	return true;
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
