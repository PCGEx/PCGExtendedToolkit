// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExSubAccessor.h"

#include "Data/Accessors/PCGExAxisAccessor.h"
#include "Data/Accessors/PCGExContainerCountAccessor.h"
#include "Data/Accessors/PCGExContainerIndexAccessor.h"
#include "Data/Accessors/PCGExSingleFieldAccessor.h"
#include "Data/Accessors/PCGExSwizzleAccessor.h"
#include "Data/Accessors/PCGExTransformPartAccessor.h"
#include "Metadata/PCGMetadataCommon.h"

namespace PCGExData
{
	//
	// FSubSelectionChain
	//

	void FSubSelectionChain::Reset()
	{
		Steps.Reset();
		FinalType = EPCGMetadataTypes::Unknown;
		SourceTypeHint = EPCGMetadataTypes::Unknown;
		bIsValid = false;
	}

	//
	// FSubAccessorRegistry
	//

	TArray<TUniquePtr<ISubAccessor>> FSubAccessorRegistry::OwnedAccessors;
	TArray<const ISubAccessor*> FSubAccessorRegistry::OrderedView;
	bool FSubAccessorRegistry::bInitialized = false;

	namespace PCGExSubAccessor
	{
		// Cached pointers for typed getters. Set by Initialize().
		const ISubAccessor* GAxisAccessor = nullptr;
		const ISubAccessor* GTransformPartAccessor = nullptr;
		const ISubAccessor* GSingleFieldAccessor = nullptr;
		const ISubAccessor* GContainerIndexAccessor = nullptr;
		const ISubAccessor* GContainerCountAccessor = nullptr;
	}

	void FSubAccessorRegistry::Initialize()
	{
		if (bInitialized)
		{
			return;
		}

		// Registration order = priority order: when a token could match
		// multiple accessors, the earlier-registered one wins. Order is
		// Axis -> TransformPart -> SingleField -> Swizzle ->
		// ContainerCount -> ContainerIndex.
		OwnedAccessors.Add(MakeUnique<FAxisAccessor>());
		PCGExSubAccessor::GAxisAccessor = OwnedAccessors.Last().Get();
		OrderedView.Add(PCGExSubAccessor::GAxisAccessor);

		OwnedAccessors.Add(MakeUnique<FTransformPartAccessor>());
		PCGExSubAccessor::GTransformPartAccessor = OwnedAccessors.Last().Get();
		OrderedView.Add(PCGExSubAccessor::GTransformPartAccessor);

		OwnedAccessors.Add(MakeUnique<FSingleFieldAccessor>());
		PCGExSubAccessor::GSingleFieldAccessor = OwnedAccessors.Last().Get();
		OrderedView.Add(PCGExSubAccessor::GSingleFieldAccessor);

		// Swizzle accessor. Its MatchesToken only accepts 2-4 char combos of
		// {x,y,z,w}, so it never competes with SingleField's single-letter
		// entries or the longer word-like axis and component aliases.
		// Registration order ensures SingleField gets first shot at
		// "X"/"Y"/etc.; Swizzle handles "xy", "xyz", ...
		OwnedAccessors.Add(MakeUnique<FSwizzleAccessor>());
		OrderedView.Add(OwnedAccessors.Last().Get());

		// Container accessors. Count ("Num"/"Count") is registered before
		// Index so that bare "Num"/"Count" tokens route there --
		// FContainerIndexAccessor's MatchesToken only accepts numeric or
		// bracket-wrapped numeric tokens so there's no overlap in practice,
		// but Count-first is cheaper (no numeric parse attempt).
		OwnedAccessors.Add(MakeUnique<FContainerCountAccessor>());
		PCGExSubAccessor::GContainerCountAccessor = OwnedAccessors.Last().Get();
		OrderedView.Add(PCGExSubAccessor::GContainerCountAccessor);

		OwnedAccessors.Add(MakeUnique<FContainerIndexAccessor>());
		PCGExSubAccessor::GContainerIndexAccessor = OwnedAccessors.Last().Get();
		OrderedView.Add(PCGExSubAccessor::GContainerIndexAccessor);

		bInitialized = true;
	}

	TConstArrayView<const ISubAccessor*> FSubAccessorRegistry::GetAll()
	{
		if (!bInitialized)
		{
			Initialize();
		}
		return OrderedView;
	}

	const ISubAccessor* FSubAccessorRegistry::GetAxisAccessor()
	{
		if (!bInitialized)
		{
			Initialize();
		}
		return PCGExSubAccessor::GAxisAccessor;
	}

	const ISubAccessor* FSubAccessorRegistry::GetTransformPartAccessor()
	{
		if (!bInitialized)
		{
			Initialize();
		}
		return PCGExSubAccessor::GTransformPartAccessor;
	}

	const ISubAccessor* FSubAccessorRegistry::GetSingleFieldAccessor()
	{
		if (!bInitialized)
		{
			Initialize();
		}
		return PCGExSubAccessor::GSingleFieldAccessor;
	}

	const ISubAccessor* FSubAccessorRegistry::GetContainerIndexAccessor()
	{
		if (!bInitialized)
		{
			Initialize();
		}
		return PCGExSubAccessor::GContainerIndexAccessor;
	}

	const ISubAccessor* FSubAccessorRegistry::GetContainerCountAccessor()
	{
		if (!bInitialized)
		{
			Initialize();
		}
		return PCGExSubAccessor::GContainerCountAccessor;
	}

	int32 GetNumFieldsForType(EPCGMetadataTypes Type)
	{
		switch (Type)
		{
		case EPCGMetadataTypes::Vector2:
			return 2;
		case EPCGMetadataTypes::Vector:
		case EPCGMetadataTypes::Rotator:
			return 3;
		case EPCGMetadataTypes::Vector4:
		case EPCGMetadataTypes::Quaternion:
			return 4;
		case EPCGMetadataTypes::Transform:
			return 9;
		default:
			return 1;
		}
	}

	bool FSubAccessorRegistry::ParseChain(const TArray<FString>& ExtraNames,
	                                      EPCGMetadataTypes SourceTypeHint,
	                                      FSubSelectionChain& OutChain)
	{
		if (!bInitialized)
		{
			Initialize();
		}

		OutChain.Reset();
		OutChain.SourceTypeHint = SourceTypeHint;

		if (ExtraNames.IsEmpty())
		{
			return false;
		}

		// True left-to-right walk. For each token in input order, try each
		// accessor in registration order; first match wins. Steps are
		// appended to the chain as they're matched, so chain order mirrors
		// token order. Each step's InType chains from the previous step's
		// OutType (greedy resolution).
		EPCGMetadataTypes CurrentType = SourceTypeHint;

		for (const FString& Token : ExtraNames)
		{
			const FString Upper = Token.ToUpper();
			for (const ISubAccessor* Accessor : OrderedView)
			{
				FAccessorParseResult Parsed;
				if (!Accessor->MatchesToken(Upper, Parsed))
				{
					continue;
				}

				FSubSelectionStep Step;
				Step.Accessor = Accessor;
				Step.Parsed = Parsed;
				Step.InType = CurrentType;
				Accessor->ResolveOutputType(CurrentType, Parsed, Step.OutType);
				OutChain.Steps.Add(Step);

				CurrentType = Step.OutType;
				break;
			}
		}

		OutChain.bIsValid = !OutChain.Steps.IsEmpty();
		if (OutChain.bIsValid)
		{
			OutChain.FinalType = OutChain.Steps.Last().OutType;
		}
		return OutChain.bIsValid;
	}

	//
	// Chain compilation
	//

	namespace PCGExSubAccessor
	{
		// Build a synthesized TransformPart step for auto-promotion. The returned
		// step is compiled (InType/OutType/StepGetFn/StepSetFn populated) and
		// ready to splice into the chain ahead of a step that needed promotion.
		FSubSelectionStep MakeTransformPartStep(PCGExTypeOps::ETransformPart Part)
		{
			const ISubAccessor* Accessor = FSubAccessorRegistry::GetTransformPartAccessor();

			FSubSelectionStep Step;
			Step.Accessor = Accessor;
			Step.Parsed.Component = Part;
			// Hint mirrors the legacy STRMAP_TRANSFORM_FIELD values.
			Step.Parsed.SourceTypeHint = (Part == PCGExTypeOps::ETransformPart::Rotation)
				? EPCGMetadataTypes::Quaternion
				: EPCGMetadataTypes::Vector;
			Step.InType = EPCGMetadataTypes::Transform;
			Accessor->ResolveOutputType(Step.InType, Step.Parsed, Step.OutType);
			Step.StepGetFn = Accessor->GetStepGetFn(Step.InType);
			Step.StepSetFn = Accessor->GetStepSetFn(Step.InType);
			return Step;
		}

		// Finalize a step: resolve OutType and fill in hot-path fn pointers.
		void FinalizeCompiledStep(FSubSelectionStep& Step, EPCGMetadataTypes InType)
		{
			Step.InType = InType;
			Step.Accessor->ResolveOutputType(InType, Step.Parsed, Step.OutType);
			Step.StepGetFn = Step.Accessor->GetStepGetFn(InType);
			Step.StepSetFn = Step.Accessor->GetStepSetFn(InType);
		}
	}

	void CompileChainForSource(FSubSelectionChain& InOutChain,
	                           EPCGMetadataTypes SourceType,
	                           const FPCGMetadataAttributeDesc* SourceDesc)
	{
		// Take ownership of the parser-produced step list, rebuild in-place.
		TArray<FSubSelectionStep, TInlineAllocator<2>> Original = MoveTemp(InOutChain.Steps);
		InOutChain.Steps.Reset();

		EPCGMetadataTypes CurrentType = SourceType;

		// CurrentDesc tracks the attribute-descriptor view at this chain
		// position. Uses an index offset into the original SourceDesc's
		// ContainerTypes rather than mutating a copy (avoids O(N) RemoveAt
		// per container step). Non-container steps clear the Desc entirely.
		const FPCGMetadataAttributeDesc* BaseDesc = SourceDesc;
		int32 ContainerDepth = 0;                                 // number of container layers consumed
		TOptional<FPCGMetadataAttributeDesc> StrippedDescStorage; // lazy, only if needed

		auto GetCurrentDesc = [&]() -> const FPCGMetadataAttributeDesc*
		{
			if (!BaseDesc)
			{
				return nullptr;
			}
			if (ContainerDepth == 0)
			{
				return BaseDesc;
			}
			// Synthesize a stripped view on demand (rare -- only for nested containers).
			if (!StrippedDescStorage.IsSet())
			{
				StrippedDescStorage.Emplace(*BaseDesc);
				StrippedDescStorage->ContainerTypes.RemoveAt(0, ContainerDepth);
			}
			return &StrippedDescStorage.GetValue();
		};

		// Detect container accessor types for desc propagation decisions.
		const ISubAccessor* ContainerIndexAccessor = FSubAccessorRegistry::GetContainerIndexAccessor();
		const ISubAccessor* ContainerCountAccessor = FSubAccessorRegistry::GetContainerCountAccessor();

		for (FSubSelectionStep& Step : Original)
		{
			// Re-check classification at each step because auto-promoted
			// TransformPart inserts change the current type.
			for (;;)
			{
				const ISubAccessor::ECompileAction Action =
					Step.Accessor->ClassifyForInType(CurrentType, Step.Parsed, GetCurrentDesc());

				if (Action == ISubAccessor::ECompileAction::Keep)
				{
					PCGExSubAccessor::FinalizeCompiledStep(Step, CurrentType);
					// Compile-time stash (container accessors use this to
					// carry ElementSize into the hot path). No-op for
					// scalar accessors.
					Step.Accessor->PostClassifyFinalize(CurrentType, Step.Parsed, GetCurrentDesc());

					// Desc propagation: container steps strip one ContainerType
					// layer so nested containers remain visible to subsequent
					// steps. Non-container steps clear the Desc entirely.
					const bool bIsContainerStep =
						Step.Accessor == ContainerIndexAccessor ||
						Step.Accessor == ContainerCountAccessor;

					if (bIsContainerStep && BaseDesc && ContainerDepth < BaseDesc->ContainerTypes.Num())
					{
						ContainerDepth++;
						StrippedDescStorage.Reset(); // invalidate lazy cache
					}
					else
					{
						BaseDesc = nullptr; // non-container step clears Desc
					}

					CurrentType = Step.OutType;
					InOutChain.Steps.Add(MoveTemp(Step));
					break;
				}

				if (Action == ISubAccessor::ECompileAction::Drop)
				{
					// Step is nonsensical for the current value type; chain
					// flows past it at the same type.
					break;
				}

				// Promote: insert TransformPart(Position|Rotation) before retrying.
				const PCGExTypeOps::ETransformPart Part =
					(Action == ISubAccessor::ECompileAction::PromoteWithRotation)
					? PCGExTypeOps::ETransformPart::Rotation
					: PCGExTypeOps::ETransformPart::Position;

				FSubSelectionStep Inserted = PCGExSubAccessor::MakeTransformPartStep(Part);
				CurrentType = Inserted.OutType;
				// TransformPart promotion doesn't consume a container layer.
				// Clear Desc -- the promoted value is a scalar sub-component
				// of a Transform, no container semantics.
				BaseDesc = nullptr;
				InOutChain.Steps.Add(MoveTemp(Inserted));
				// Loop back to re-classify the same Step under the new CurrentType.
			}
		}

		InOutChain.SourceTypeHint = SourceType;
		InOutChain.bIsValid = !InOutChain.Steps.IsEmpty();
		InOutChain.FinalType = InOutChain.bIsValid ? InOutChain.Steps.Last().OutType : SourceType;
	}
}
