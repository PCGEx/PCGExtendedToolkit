// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExCachedSubSelection.h"

#include "Data/PCGExData.h"
#include "Data/Buffers/PCGExBufferProperty.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Types/PCGExTypeOps.h"

namespace PCGExData
{
	//
	// FCachedSubSelection
	//

	FCachedSubSelection::~FCachedSubSelection()
	{
		for (FProperty* Prop : OwnedProperties)
		{
			delete Prop;
		}
	}

	FCachedSubSelection::FCachedSubSelection(FCachedSubSelection&& Other) noexcept
	{
		*this = MoveTemp(Other);
	}

	FCachedSubSelection& FCachedSubSelection::operator=(FCachedSubSelection&& Other) noexcept
	{
		if (this != &Other)
		{
			for (FProperty* Prop : OwnedProperties)
			{
				delete Prop;
			}

			bIsValid = Other.bIsValid;
			RealType = Other.RealType;
			WorkingType = Other.WorkingType;
			FinalChainType = Other.FinalChainType;
			CompiledChain = MoveTemp(Other.CompiledChain);
			ConvertRealToWorking = Other.ConvertRealToWorking;
			ConvertWorkingToReal = Other.ConvertWorkingToReal;
			ConvertFinalToWorking = Other.ConvertFinalToWorking;
			ConvertWorkingToFinal = Other.ConvertWorkingToFinal;
			RealOps = Other.RealOps;
			WorkingOps = Other.WorkingOps;
			bIsSingleStep = Other.bIsSingleStep;
			SingleStepGetFn = Other.SingleStepGetFn;
			SingleStepSetFn = Other.SingleStepSetFn;
			SingleStepParsed = Other.SingleStepParsed;
			OwnedProperties = MoveTemp(Other.OwnedProperties);
		}
		return *this;
	}

	void FCachedSubSelection::Initialize(
		const FSubSelection& Selection,
		EPCGMetadataTypes InRealType,
		EPCGMetadataTypes InWorkingType,
		const FPCGMetadataAttributeDesc* SourceDesc)
	{
		bIsValid = Selection.HasSelection();
		RealType = InRealType;
		WorkingType = InWorkingType;

		// Start from the parser-produced chain, then run the compiler to
		// drop/promote steps based on RealType. The compiler also fills
		// each remaining step's typed fn pointers, so the hot path has zero
		// per-call lookups.
		// Clean up any previously owned properties from a prior Initialize.
		for (FProperty* Prop : OwnedProperties)
		{
			delete Prop;
		}
		OwnedProperties.Reset();

		CompiledChain = Selection.GetChain();
		CompileChainForSource(CompiledChain, RealType, SourceDesc);

		// Create owned FProperty instances for container-index steps that
		// need property-aware writes (CopyCompleteValue for non-trivially-
		// copyable element types like FString, nested containers, UStructs).
		// Replay the Desc strip logic: start with SourceDesc, strip the
		// outermost ContainerType entry each time a container step appears.
		// The inner-element FProperty is created from the stripped Desc.
		if (SourceDesc && !CompiledChain.Steps.IsEmpty())
		{
			const ISubAccessor* ContainerIndexAccessor = FSubAccessorRegistry::GetContainerIndexAccessor();
			FPCGMetadataAttributeDesc CurrentDesc = *SourceDesc;

			for (FSubSelectionStep& Step : CompiledChain.Steps)
			{
				if (Step.Accessor == ContainerIndexAccessor && !CurrentDesc.IsSingleValue())
				{
					// Build the inner-element property from the stripped Desc.
					FPCGMetadataAttributeDesc InnerDesc = CurrentDesc;
					InnerDesc.ContainerTypes.RemoveAt(0);
					FProperty* ElementProp = FPropertyBuffer::CreateInnerPropertyFromDesc(InnerDesc);
					if (ElementProp)
					{
						OwnedProperties.Add(ElementProp);
						Step.Parsed.ContainerElementProperty = ElementProp;
					}

					// Consume the outer container layer for subsequent steps.
					CurrentDesc = InnerDesc;
				}
				else
				{
					// Non-container step (or container-count, which doesn't
					// need a property). Clear the Desc so subsequent container
					// steps on already-unwrapped values don't get spurious
					// properties.
					CurrentDesc.ContainerTypes.Reset();
				}
			}
		}

		FinalChainType = CompiledChain.Steps.IsEmpty() ? RealType : CompiledChain.Steps.Last().OutType;

		// Type ops for copy/default (callers that rely on these).
		RealOps = PCGExTypeOps::FTypeOpsRegistry::Get(RealType);
		WorkingOps = PCGExTypeOps::FTypeOpsRegistry::Get(WorkingType);

		// Conversions.
		// ConvertReal*Working are used on the identity (no-chain) path and by
		// ConvertGet/ConvertSet. ConvertFinal*Working are used on the
		// chain-active path to bridge the chain's final OutType and the
		// blender's WorkingType.
		ConvertRealToWorking = PCGExTypeOps::FConversionTable::GetConversionFn(RealType, WorkingType);
		ConvertWorkingToReal = PCGExTypeOps::FConversionTable::GetConversionFn(WorkingType, RealType);
		ConvertFinalToWorking = PCGExTypeOps::FConversionTable::GetConversionFn(FinalChainType, WorkingType);
		ConvertWorkingToFinal = PCGExTypeOps::FConversionTable::GetConversionFn(WorkingType, FinalChainType);

		// 1-step fast-path cache: snapshot the step's fn pointers + parsed
		// data so ApplyGet/ApplySet skip the array dereference entirely.
		bIsSingleStep = (CompiledChain.Steps.Num() == 1);
		if (bIsSingleStep)
		{
			const FSubSelectionStep& Only = CompiledChain.Steps[0];
			SingleStepGetFn = Only.StepGetFn;
			SingleStepSetFn = Only.StepSetFn;
			SingleStepParsed = Only.Parsed;
		}
	}

	void FCachedSubSelection::ApplyGet(const void* Source, void* OutValue) const
	{
		if (CompiledChain.Steps.IsEmpty())
		{
			if (ConvertRealToWorking)
			{
				ConvertRealToWorking(Source, OutValue);
			}
			return;
		}

		const int32 NumSteps = CompiledChain.Steps.Num();

		// Fast path: single-step chain (the common case -- .X, .Position,
		// .Forward, etc.). Uses cached fn pointer + parsed data -- zero
		// array dereference, zero bounds check.
		if (bIsSingleStep)
		{
			if (FinalChainType == WorkingType)
			{
				SingleStepGetFn(Source, OutValue, SingleStepParsed);
			}
			else
			{
				alignas(16) uint8 Tmp[96];
				SingleStepGetFn(Source, Tmp, SingleStepParsed);
				if (ConvertFinalToWorking)
				{
					ConvertFinalToWorking(Tmp, OutValue);
				}
			}
			return;
		}

		// Multi-step path: double-buffered intermediates.
		alignas(16) uint8 BufA[96];
		alignas(16) uint8 BufB[96];
		void* Bufs[2] = {BufA, BufB};

		const void* CurrentIn = Source;
		int32 BufIdx = 0;
		const int32 LastIdx = NumSteps - 1;

		for (int32 i = 0; i < LastIdx; ++i)
		{
			const FSubSelectionStep& Step = CompiledChain.Steps[i];
			void* StepOut = Bufs[BufIdx];
			Step.StepGetFn(CurrentIn, StepOut, Step.Parsed);
			CurrentIn = StepOut;
			BufIdx = 1 - BufIdx;
		}

		const FSubSelectionStep& Last = CompiledChain.Steps[LastIdx];

		if (FinalChainType == WorkingType)
		{
			Last.StepGetFn(CurrentIn, OutValue, Last.Parsed);
		}
		else
		{
			void* FinalBuf = Bufs[BufIdx];
			Last.StepGetFn(CurrentIn, FinalBuf, Last.Parsed);
			if (ConvertFinalToWorking)
			{
				ConvertFinalToWorking(FinalBuf, OutValue);
			}
		}
	}

	void FCachedSubSelection::ApplySet(void* Target, const void* Source) const
	{
		// Single Num() read: the analyzer can't correlate separate IsEmpty()/Num() calls
		// and otherwise assumes a negative LastIdx below (C6385)
		const int32 NumSteps = CompiledChain.Steps.Num();

		if (NumSteps == 0)
		{
			if (ConvertWorkingToReal)
			{
				ConvertWorkingToReal(Source, Target);
			}
			return;
		}

		// Any step without a SetFn (e.g., axis) disqualifies the whole chain
		// for inject. Defensive check -- AppliesToTargetWrite also guards.
		for (const FSubSelectionStep& Step : CompiledChain.Steps)
		{
			if (!Step.StepSetFn)
			{
				return;
			}
		}

		// Convert Source (WorkingType) to FinalChainType so it matches
		// the last step's expected NewChild type.
		alignas(16) uint8 NewChildBuf[96];
		const void* NewChild = Source;
		if (WorkingType != FinalChainType)
		{
			if (ConvertWorkingToFinal)
			{
				ConvertWorkingToFinal(Source, NewChildBuf);
			}
			else
			{
				return;
			}
			NewChild = NewChildBuf;
		}

		// Fast path: single-step chain. Uses cached fn pointer -- inject
		// directly into Target with zero array dereference.
		if (bIsSingleStep)
		{
			SingleStepSetFn(Target, NewChild, SingleStepParsed);
			return;
		}

		// Multi-step path: extract-modify-inject.
		constexpr int32 MaxSteps = 4;
		checkf(NumSteps <= MaxSteps,
		       TEXT("FCachedSubSelection chain exceeded MaxSteps (%d > %d)"),
		       NumSteps, MaxSteps);

		const int32 LastIdx = NumSteps - 1;
		alignas(16) uint8 Buffers[MaxSteps][96];

		// Extract phase: walk forward to populate intermediates.
		{
			const void* CurrentIn = Target;
			for (int32 i = 0; i < LastIdx; ++i)
			{
				const FSubSelectionStep& Step = CompiledChain.Steps[i];
				Step.StepGetFn(CurrentIn, Buffers[i], Step.Parsed);
				CurrentIn = Buffers[i];
			}
		}

		// Inject at last step, walk backward propagating mutations.
		void* LastParent = (LastIdx == 0) ? Target : Buffers[LastIdx - 1];
		CompiledChain.Steps[LastIdx].StepSetFn(LastParent, NewChild, CompiledChain.Steps[LastIdx].Parsed);

		for (int32 i = LastIdx - 1; i >= 0; --i)
		{
			void* Parent = (i == 0) ? Target : Buffers[i - 1];
			CompiledChain.Steps[i].StepSetFn(Parent, Buffers[i], CompiledChain.Steps[i].Parsed);
		}
	}
}
