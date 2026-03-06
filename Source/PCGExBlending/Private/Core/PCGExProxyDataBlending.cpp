// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExProxyDataBlending.h"

#include "PCGExBlendingCommon.h"
#include "Containers/PCGExIndexLookup.h"
#include "Types/PCGExTypes.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExProxyData.h"
#include "Data/PCGExProxyDataHelpers.h"
#include "Core/PCGExUnionData.h"
#include "Core/PCGExBlendOperations.h"
#include "Core/PCGExOpStats.h"
#include "Data/PCGExData.h"
#include "Math/PCGExMathDistances.h"

namespace PCGExBlending
{
	// FDummyUnionBlender implementation

	void FDummyUnionBlender::Init(const TSharedPtr<PCGExData::FFacade>& TargetData, const TArray<TSharedRef<PCGExData::FFacade>>& InSources)
	{
		CurrentTargetData = TargetData;

		int32 MaxIndex = 0;
		for (const TSharedRef<PCGExData::FFacade>& Src : InSources) { MaxIndex = FMath::Max(Src->Source->IOIndex, MaxIndex); }
		IOLookup = MakeShared<PCGEx::FIndexLookup>(MaxIndex + 1);
		for (const TSharedRef<PCGExData::FFacade>& Src : InSources) { IOLookup->Set(Src->Source->IOIndex, SourcesData.Add(Src->GetIn())); }

		Distances = PCGExMath::GetDistances();
	}

	int32 FDummyUnionBlender::ComputeWeights(const int32 WriteIndex, const TSharedPtr<PCGExData::IUnionData>& InUnionData, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints) const
	{
		const PCGExData::FConstPoint Target = CurrentTargetData->Source->GetOutPoint(WriteIndex);
		return InUnionData->ComputeWeights(SourcesData, IOLookup, Target, Distances, OutWeightedPoints);
	}

	// FProxyDataBlender implementation

	void FProxyDataBlender::Blend(const int32 SourceIndexA, const int32 SourceIndexB, const int32 TargetIndex, const double Weight) const
	{
		if (!Operation || !A || !C) { return; }

		// Use FScopedTypedValue for safe working buffers
		PCGExTypes::FScopedTypedValue ValA(UnderlyingType);
		PCGExTypes::FScopedTypedValue ValB(UnderlyingType);
		PCGExTypes::FScopedTypedValue ValC(UnderlyingType);

		A->GetVoid(SourceIndexA, ValA.GetRaw());
		B->GetVoid(SourceIndexB, ValB.GetRaw());

		Operation->Blend(ValA.GetRaw(), ValB.GetRaw(), Weight, ValC.GetRaw());
		C->SetVoid(TargetIndex, ValC.GetRaw());
	}

	void FProxyDataBlender::BlendScope(const PCGExMT::FScope& Scope, const double Weight) const
	{
		if (!Operation || !A || !C) { return; }

		// Use FScopedTypedValue for safe working buffers
		PCGExTypes::FScopedTypedValue ValA(UnderlyingType);
		PCGExTypes::FScopedTypedValue ValB(UnderlyingType);
		PCGExTypes::FScopedTypedValue ValC(UnderlyingType);

		PCGEX_SCOPE_LOOP(Index)
		{
			A->GetVoid(Index, ValA.GetRaw());
			B->GetVoid(Index, ValB.GetRaw());

			Operation->Blend(ValA.GetRaw(), ValB.GetRaw(), Weight, ValC.GetRaw());
			C->SetVoid(Index, ValC.GetRaw());
		}
	}

	void FProxyDataBlender::BlendScope(const PCGExMT::FScope& Scope, TArrayView<const double> Weights) const
	{
		if (!Operation || !A || !C) { return; }

		// Use FScopedTypedValue for safe working buffers
		PCGExTypes::FScopedTypedValue ValA(UnderlyingType);
		PCGExTypes::FScopedTypedValue ValB(UnderlyingType);
		PCGExTypes::FScopedTypedValue ValC(UnderlyingType);

		PCGEX_SCOPE_LOOP(Index)
		{
			A->GetVoid(Index, ValA.GetRaw());
			B->GetVoid(Index, ValB.GetRaw());

			Operation->Blend(ValA.GetRaw(), ValB.GetRaw(), Weights[Index - Scope.Start], ValC.GetRaw());
			C->SetVoid(Index, ValC.GetRaw());
		}
	}

	void FProxyDataBlender::BlendScope(const PCGExMT::FScope& Scope, TArrayView<const int8> Mask, const double Weight) const
	{
		if (!Operation || !A || !C) { return; }

		// Use FScopedTypedValue for safe working buffers
		PCGExTypes::FScopedTypedValue ValA(UnderlyingType);
		PCGExTypes::FScopedTypedValue ValB(UnderlyingType);
		PCGExTypes::FScopedTypedValue ValC(UnderlyingType);

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!Mask[Index - Scope.Start]) { continue; }

			A->GetVoid(Index, ValA.GetRaw());
			B->GetVoid(Index, ValB.GetRaw());

			Operation->Blend(ValA.GetRaw(), ValB.GetRaw(), Weight, ValC.GetRaw());
			C->SetVoid(Index, ValC.GetRaw());
		}
	}

	void FProxyDataBlender::BlendScope(const PCGExMT::FScope& Scope, TArrayView<const int8> Mask, TArrayView<const double> Weights) const
	{
		if (!Operation || !A || !C) { return; }

		// Use FScopedTypedValue for safe working buffers
		PCGExTypes::FScopedTypedValue ValA(UnderlyingType);
		PCGExTypes::FScopedTypedValue ValB(UnderlyingType);
		PCGExTypes::FScopedTypedValue ValC(UnderlyingType);

		PCGEX_SCOPE_LOOP(Index)
		{
			const int32 i = Index - Scope.Start;
			if (!Mask[i]) { continue; }

			A->GetVoid(Index, ValA.GetRaw());
			B->GetVoid(Index, ValB.GetRaw());

			Operation->Blend(ValA.GetRaw(), ValB.GetRaw(), Weights[i], ValC.GetRaw());
			C->SetVoid(Index, ValC.GetRaw());
		}
	}

	PCGEx::FOpStats FProxyDataBlender::BeginMultiBlend(const int32 TargetIndex)
	{
		PCGEx::FOpStats Tracker{};

		check(Operation)
		check(C)

		PCGExTypes::FScopedTypedValue Current(UnderlyingType);
		C->GetVoid(TargetIndex, Current.GetRaw());
		Operation->BeginMulti(Current.GetRaw(), nullptr, Tracker);
		C->SetVoid(TargetIndex, Current.GetRaw());

		return Tracker;
	}

	void FProxyDataBlender::MultiBlend(const int32 SourceIndex, const int32 TargetIndex, const double Weight, PCGEx::FOpStats& Tracker)
	{
		check(Operation)
		check(A)
		check(B)
		check(C)

		PCGExTypes::FScopedTypedValue Source(UnderlyingType);

		// Read source value
		A->GetVoid(SourceIndex, Source.GetRaw());

		if (Tracker.Count < 0)
		{
			Tracker.Count = 0;
			C->SetVoid(TargetIndex, Source.GetRaw()); // Copy source to current
		}
		else
		{
			PCGExTypes::FScopedTypedValue Current(UnderlyingType);

			C->GetCurrentVoid(TargetIndex, Current.GetRaw());                 // Read current accumulated value			
			Operation->Accumulate(Source.GetRaw(), Current.GetRaw(), Weight); // Accumulate			
			C->SetVoid(TargetIndex, Current.GetRaw());                        // Write back
		}

		Tracker.Count++;
		Tracker.TotalWeight += Weight;
	}

	void FProxyDataBlender::EndMultiBlend(const int32 TargetIndex, PCGEx::FOpStats& Tracker)
	{
		check(Operation)
		check(C)

		if (!Tracker.Count) { return; }

		PCGExTypes::FScopedTypedValue Current(UnderlyingType);


		C->GetCurrentVoid(TargetIndex, Current.GetRaw());                          // Read accumulated value		
		Operation->EndMulti(Current.GetRaw(), Tracker.TotalWeight, Tracker.Count); // Finalize (e.g., divide by count for average)		
		C->SetVoid(TargetIndex, Current.GetRaw());                                 // Write final result
	}

	void FProxyDataBlender::Div(const int32 TargetIndex, const double Divider)
	{
		if (!Operation || !C || Divider == 0.0) { return; }

		PCGExTypes::FScopedTypedValue Value(UnderlyingType);

		C->GetVoid(TargetIndex, Value.GetRaw()); // Read current value
		Operation->Div(Value.GetRaw(), Divider); // Divide
		C->SetVoid(TargetIndex, Value.GetRaw()); // Write back
	}

	TSharedPtr<PCGExData::IBuffer> FProxyDataBlender::GetOutputBuffer() const
	{
		return C ? C->GetBuffer() : nullptr;
	}

	bool FProxyDataBlender::InitFromParam(
		FPCGExContext* InContext,
		const FBlendingParam& InParam,
		const TSharedPtr<PCGExData::FFacade> InTargetFacade,
		const TSharedPtr<PCGExData::FFacade> InSourceFacade,
		const PCGExData::EIOSide InSide,
		const PCGExData::EProxyFlags InProxyFlags)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FProxyDataBlender::InitFromParam)

		// Setup proxy descriptors
		PCGExData::FProxyDescriptor Desc_A = PCGExData::FProxyDescriptor(InSourceFacade, PCGExData::EProxyRole::Read);
		PCGExData::FProxyDescriptor Desc_B = PCGExData::FProxyDescriptor(InTargetFacade, PCGExData::EProxyRole::Read);

		if (!Desc_A.Capture(InContext, InParam.Selector, InSide)) { return false; }

		if (InParam.bIsNewAttribute)
		{
			// Capturing B will fail as it does not exist yet.
			// Simply copy A
			Desc_B = Desc_A;

			// Swap B side for Out so the buffer will be initialized
			Desc_B.Side = PCGExData::EIOSide::Out;
			Desc_B.DataFacade = InTargetFacade;
		}
		else
		{
			// Strict capture may fail here, TBD
			if (!Desc_B.CaptureStrict(InContext, InParam.Selector, PCGExData::EIOSide::Out)) { return false; }
		}

		PCGExData::FProxyDescriptor Desc_C = Desc_B;
		Desc_C.Side = PCGExData::EIOSide::Out;
		Desc_C.Role = PCGExData::EProxyRole::Write;

		Desc_A.AddFlags(InProxyFlags | PCGExData::EProxyFlags::Shared);
		Desc_B.AddFlags(InProxyFlags | PCGExData::EProxyFlags::Shared);

		// Remove direct write
		PCGExData::EProxyFlags WriteFlags = InProxyFlags;
		EnumRemoveFlags(WriteFlags, PCGExData::EProxyFlags::Direct);
		Desc_C.AddFlags(WriteFlags);

		// Set type info
		UnderlyingType = Desc_A.WorkingType;
		bNeedsLifecycleManagement = PCGExTypes::FScopedTypedValue::NeedsLifecycleManagement(UnderlyingType);

		// Create output first so we may read from it
		C = PCGExData::GetProxyBuffer(InContext, Desc_C);
		A = PCGExData::GetProxyBuffer(InContext, Desc_A);
		B = PCGExData::GetProxyBuffer(InContext, Desc_B);

		// Ensure C is readable for MultiBlend, as those will use GetCurrent
		if (C && !C->EnsureReadable())
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Failed to ensure target buffer is readable."));
			return false;
		}

		return A && B && C;
	}

	// Factory functions

	TSharedPtr<FProxyDataBlender> CreateProxyBlender(
		EPCGMetadataTypes WorkingType,
		EPCGExABBlendingType BlendMode,
		bool bResetValueForMultiBlend)
	{
		TSharedPtr<FProxyDataBlender> Blender = MakeShared<FProxyDataBlender>();

		Blender->UnderlyingType = WorkingType;

		// For unknown/generic types, get size from the type utility so FCopyOnlyBlendOperation can be created
		const int32 ValueSize = PCGExTypes::GetElementSizeFromType(WorkingType);
		Blender->Operation = FBlendOperationFactory::Create(WorkingType, BlendMode, bResetValueForMultiBlend, ValueSize);

		if (!Blender->Operation) { return nullptr; }

		return Blender;
	}

	TSharedPtr<FProxyDataBlender> CreateProxyBlender(
		FPCGExContext* InContext,
		const EPCGExABBlendingType BlendMode,
		const PCGExData::FProxyDescriptor& A,
		const PCGExData::FProxyDescriptor& B,
		const PCGExData::FProxyDescriptor& C,
		const bool bResetValueForMultiBlend)
	{
		if (A.WorkingType != B.WorkingType || A.WorkingType != C.WorkingType)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("ProxyBlender: WorkingType mismatch between A, B, and C."));
			return nullptr;
		}

		TSharedPtr<FProxyDataBlender> Blender = MakeShared<FProxyDataBlender>();

		// Set type info
		Blender->UnderlyingType = A.WorkingType;

		// Create blend operation — forward size from descriptor for generic types
		const int32 ValueSize = A.ValueSize > 0 ? A.ValueSize : PCGExTypes::GetElementSizeFromType(A.WorkingType);
		const int32 ValueAlign = A.ValueAlignment > 1 ? A.ValueAlignment : PCGExTypes::GetElementAlignmentFromType(A.WorkingType);
		Blender->Operation = FBlendOperationFactory::Create(A.WorkingType, BlendMode, bResetValueForMultiBlend, ValueSize, ValueAlign);
		if (!Blender->Operation)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("ProxyBlender: Failed to create blend operation."));
			return nullptr;
		}

		// Create output first so we may read from it
		Blender->C = PCGExData::GetProxyBuffer(InContext, C);
		Blender->A = PCGExData::GetProxyBuffer(InContext, A);
		Blender->B = PCGExData::GetProxyBuffer(InContext, B);

		if (!Blender->A)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("ProxyBlender: Failed to generate buffer for Operand A."));
			return nullptr;
		}

		if (!Blender->B)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("ProxyBlender: Failed to generate buffer for Operand B."));
			return nullptr;
		}

		if (!Blender->C)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("ProxyBlender: Failed to generate buffer for Output."));
			return nullptr;
		}

		// Ensure C is readable for MultiBlend, as those will use GetCurrent
		if (!Blender->C->EnsureReadable())
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("ProxyBlender: Failed to ensure target write buffer is also readable."));
			return nullptr;
		}

		return Blender;
	}

	TSharedPtr<FProxyDataBlender> CreateProxyBlender(
		FPCGExContext* InContext,
		const EPCGExABBlendingType BlendMode,
		const PCGExData::FProxyDescriptor& A,
		const PCGExData::FProxyDescriptor& C,
		const bool bResetValueForMultiBlend)
	{
		if (A.WorkingType != C.WorkingType)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("ProxyBlender: WorkingType mismatch between A and C."));
			return nullptr;
		}

		TSharedPtr<FProxyDataBlender> Blender = MakeShared<FProxyDataBlender>();

		// Set type info
		Blender->UnderlyingType = A.WorkingType;

		// Create blend operation — forward size from descriptor for generic types
		const int32 ValueSize = A.ValueSize > 0 ? A.ValueSize : PCGExTypes::GetElementSizeFromType(A.WorkingType);
		const int32 ValueAlign = A.ValueAlignment > 1 ? A.ValueAlignment : PCGExTypes::GetElementAlignmentFromType(A.WorkingType);
		Blender->Operation = FBlendOperationFactory::Create(A.WorkingType, BlendMode, bResetValueForMultiBlend, ValueSize, ValueAlign);
		if (!Blender->Operation)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("ProxyBlender: Failed to create blend operation."));
			return nullptr;
		}

		// Create output first so we may read from it
		Blender->C = PCGExData::GetProxyBuffer(InContext, C);
		Blender->A = PCGExData::GetProxyBuffer(InContext, A);
		Blender->B = Blender->A; // Use B as fallback -- important for intrinsic properties such as $Index

		if (!Blender->A)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("ProxyBlender: Failed to generate buffer for Operand A."));
			return nullptr;
		}

		if (!Blender->C)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("ProxyBlender: Failed to generate buffer for Output."));
			return nullptr;
		}

		// Ensure C is readable for MultiBlend, as those will use GetCurrent
		if (!Blender->C->EnsureReadable())
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("ProxyBlender: Failed to ensure target write buffer is also readable."));
			return nullptr;
		}

		return Blender;
	}
}
