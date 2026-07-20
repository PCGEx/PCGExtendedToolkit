// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExSubSelection.h"

#include "CoreMinimal.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Types/PCGExTypeOps.h"

namespace PCGExData
{
	//
	// FSubSelection constructors
	//

	FSubSelection::FSubSelection(const TArray<FString>& ExtraNames)
	{
		Init(ExtraNames);
	}

	FSubSelection::FSubSelection(const FPCGAttributePropertyInputSelector& InSelector)
	{
		Init(InSelector.GetExtraNames());
	}

	FSubSelection::FSubSelection(const FString& Path, const UPCGData* InData)
	{
		FPCGAttributePropertyInputSelector ProxySelector = FPCGAttributePropertyInputSelector();
		ProxySelector.Update(Path);
		if (InData)
		{
			ProxySelector = ProxySelector.CopyAndFixLast(InData);
		}
		Init(ProxySelector.GetExtraNames());
	}

	//
	// Classifier bitmask is populated at Init time (see Init below).
	// The classifier methods are now inline bit-tests in the header.
	//

	EPCGMetadataTypes FSubSelection::GetSubType(const EPCGMetadataTypes Fallback) const
	{
		if (!HasSelection())
		{
			return Fallback;
		}
		if (IsFieldSelection())
		{
			return EPCGMetadataTypes::Double;
		}
		if (IsAxisSelection())
		{
			return EPCGMetadataTypes::Vector;
		}

		// ContainerCount always yields Double (the count). Checked
		// after field/axis so a chain like `.1.X` (ContainerIndex + Field)
		// still reports Double via the field path.
		if (IsContainerCountSelection())
		{
			return EPCGMetadataTypes::Double;
		}

		// Component selection: Vector (Pos/Scale) or Quaternion (Rotation).
		// NOTE: intentionally ungated -- even chains without an explicit
		// component step may have Component populated from Init's default
		// (Position). Gating on IsComponentSelection() changes WorkingType
		// for chains that don't have a component step (Swizzle-only, etc.),
		// which can cause downstream buffer sizing mismatches. The default
		// Component=Position → Vector is a safe fallback for non-component
		// chains (most produce Vector-compatible output).
		switch (Component)
		{
		case PCGExTypeOps::ETransformPart::Position:
		case PCGExTypeOps::ETransformPart::Scale:
			return EPCGMetadataTypes::Vector;
		case PCGExTypeOps::ETransformPart::Rotation:
			return EPCGMetadataTypes::Quaternion;
		}

		// ContainerIndex-only (and any other non-type-changing chain) falls
		// through to Fallback: the element type of a container equals the
		// attribute's reported RealType.
		return Fallback;
	}

	void FSubSelection::SetComponent(const PCGExTypeOps::ETransformPart InComponent)
	{
		// Rebuild the chain with a single TransformPart step so classifier
		// methods and dispatch both see a valid component-selection state.
		// The chain is the source of truth; pure flag mutations are gone.
		const ISubAccessor* Accessor = FSubAccessorRegistry::GetTransformPartAccessor();

		FSubSelectionStep Step;
		Step.Accessor = Accessor;
		Step.Parsed.Component = InComponent;
		Step.Parsed.SourceTypeHint = (InComponent == PCGExTypeOps::ETransformPart::Rotation)
			? EPCGMetadataTypes::Quaternion
			: EPCGMetadataTypes::Vector;
		Step.InType = EPCGMetadataTypes::Transform;
		Accessor->ResolveOutputType(Step.InType, Step.Parsed, Step.OutType);
		Step.StepGetFn = Accessor->GetStepGetFn(Step.InType);
		Step.StepSetFn = Accessor->GetStepSetFn(Step.InType);

		ParsedChain.Reset();
		ParsedChain.Steps.Add(MoveTemp(Step));
		ParsedChain.bIsValid = true;
		ParsedChain.FinalType = ParsedChain.Steps.Last().OutType;

		Component = InComponent;
		PossibleSourceType = ParsedChain.Steps.Last().Parsed.SourceTypeHint;
		ClassifierMask = Bit_HasSelection | Bit_Component;
		CachedCompiledSourceType = EPCGMetadataTypes::Unknown; // invalidate compile cache
	}

	bool FSubSelection::SetFieldIndex(const int32 InFieldIndex)
	{
		if (InFieldIndex < 0 || InFieldIndex > 3)
		{
			ParsedChain.Reset();
			ClassifierMask = 0;
			CachedCompiledSourceType = EPCGMetadataTypes::Unknown;
			return false;
		}

		PCGExTypeOps::ESingleField NewField = PCGExTypeOps::ESingleField::X;
		switch (InFieldIndex)
		{
		case 0:
			NewField = PCGExTypeOps::ESingleField::X;
			break;
		case 1:
			NewField = PCGExTypeOps::ESingleField::Y;
			break;
		case 2:
			NewField = PCGExTypeOps::ESingleField::Z;
			break;
		case 3:
			NewField = PCGExTypeOps::ESingleField::W;
			break;
		}

		const ISubAccessor* Accessor = FSubAccessorRegistry::GetSingleFieldAccessor();

		FSubSelectionStep Step;
		Step.Accessor = Accessor;
		Step.Parsed.Field = NewField;
		Step.Parsed.FieldIndex = InFieldIndex;
		Step.Parsed.SourceTypeHint = EPCGMetadataTypes::Vector; // default -- real hint comes from parsing aliases
		Step.InType = EPCGMetadataTypes::Unknown;
		Accessor->ResolveOutputType(Step.InType, Step.Parsed, Step.OutType);
		Step.StepGetFn = nullptr;
		Step.StepSetFn = nullptr;

		ParsedChain.Reset();
		ParsedChain.Steps.Add(MoveTemp(Step));
		ParsedChain.bIsValid = true;
		ParsedChain.FinalType = EPCGMetadataTypes::Double;

		Field = NewField;
		ClassifierMask = Bit_HasSelection | Bit_Field;
		CachedCompiledSourceType = EPCGMetadataTypes::Unknown; // invalidate compile cache
		return true;
	}

	void FSubSelection::Init(const TArray<FString>& ExtraNames)
	{
		// Chain-backed parser. Builds a step sequence via FSubAccessorRegistry,
		// then populates Field/Axis/Component/PossibleSourceType for
		// dispatch-time reads.
		//
		// HasSelection() is true iff the parser actually matched at least one
		// token. Malformed inputs like {Garbage} produce an empty chain.
		ParsedChain.Reset();

		if (ExtraNames.IsEmpty())
		{
			return;
		}

		FSubAccessorRegistry::ParseChain(ExtraNames, EPCGMetadataTypes::Unknown, ParsedChain);

		// Locate steps for populating the legacy dispatch fields.
		const ISubAccessor* AxisAccessor = FSubAccessorRegistry::GetAxisAccessor();
		const ISubAccessor* TransformAccessor = FSubAccessorRegistry::GetTransformPartAccessor();
		const ISubAccessor* FieldAccessor = FSubAccessorRegistry::GetSingleFieldAccessor();

		const FSubSelectionStep* AxisStep = nullptr;
		const FSubSelectionStep* CompStep = nullptr;
		const FSubSelectionStep* FieldStep = nullptr;
		for (const FSubSelectionStep& Step : ParsedChain.Steps)
		{
			if (Step.Accessor == AxisAccessor)
			{
				AxisStep = &Step;
			}
			else if (Step.Accessor == TransformAccessor)
			{
				CompStep = &Step;
			}
			else if (Step.Accessor == FieldAccessor)
			{
				FieldStep = &Step;
			}
		}

		Axis = AxisStep ? AxisStep->Parsed.Axis : EPCGExAxis::Forward;
		Field = FieldStep ? FieldStep->Parsed.Field : PCGExTypeOps::ESingleField::X;
		// Component default stays Rotation for parity with legacy dispatch --
		// some ApplyGet branches read Component when bIsComponentSet was true,
		// but the chain-based classifier methods gate that read now.
		Component = CompStep ? CompStep->Parsed.Component : PCGExTypeOps::ETransformPart::Rotation;

		// PossibleSourceType: component hint wins, else field hint, else axis hint, else Unknown.
		// Axis tokens are parsed with a Quaternion hint -- an axis alone strongly
		// suggests the source is rotational (FQuat/FRotator/FTransform).
		if (CompStep)
		{
			PossibleSourceType = CompStep->Parsed.SourceTypeHint;
		}
		else if (FieldStep)
		{
			PossibleSourceType = FieldStep->Parsed.SourceTypeHint;
		}
		else if (AxisStep)
		{
			PossibleSourceType = AxisStep->Parsed.SourceTypeHint;
		}
		else
		{
			PossibleSourceType = EPCGMetadataTypes::Unknown;
		}

		// Build classifier bitmask from the chain's step accessor identities.
		ClassifierMask = 0;
		const ISubAccessor* ContainerIndexAccessor = FSubAccessorRegistry::GetContainerIndexAccessor();
		const ISubAccessor* ContainerCountAccessor = FSubAccessorRegistry::GetContainerCountAccessor();

		if (!ParsedChain.Steps.IsEmpty())
		{
			ClassifierMask |= Bit_HasSelection;
		}
		for (const FSubSelectionStep& Step : ParsedChain.Steps)
		{
			if (Step.Accessor == FieldAccessor)
			{
				ClassifierMask |= Bit_Field;
			}
			else if (Step.Accessor == AxisAccessor)
			{
				ClassifierMask |= Bit_Axis;
			}
			else if (Step.Accessor == TransformAccessor)
			{
				ClassifierMask |= Bit_Component;
			}
			else if (Step.Accessor == ContainerIndexAccessor)
			{
				ClassifierMask |= Bit_ContainerIndex;
			}
			else if (Step.Accessor == ContainerCountAccessor)
			{
				ClassifierMask |= Bit_ContainerCount;
			}
		}
	}

	//
	// Type-Erased Interface Implementation
	//
	// ApplyGet/ApplySet walk ParsedChain.Steps directly via accessor
	// virtual calls. This is a non-hot fallback path;
	// FCachedSubSelection is the performance-critical surface.
	//
	// NOTE: Container steps require compile-time ContainerElementSize
	// (populated by PostClassifyFinalize at FCachedSubSelection::Initialize
	// time). FSubSelection doesn't have a SourceDesc, so container steps
	// in the parsed chain won't have ElementSize populated -- they produce
	// zero-filled output (graceful degradation). For container-aware
	// dispatch, callers should use FCachedSubSelection.
	//

	void FSubSelection::ApplyGet(EPCGMetadataTypes SourceType, const void* Source,
	                             void* OutValue, EPCGMetadataTypes& OutType) const
	{
		if (!HasSelection())
		{
			const PCGExTypeOps::ITypeOpsBase* Ops = PCGExTypeOps::FTypeOpsRegistry::Get(SourceType);
			if (Ops)
			{
				Ops->Copy(Source, OutValue);
				OutType = SourceType;
			}
			else
			{
				OutType = EPCGMetadataTypes::Unknown;
			}
			return;
		}

		// 1-entry compile cache: avoid re-running CompileChainForSource
		// when the same FSubSelection is applied to many elements of the
		// same SourceType (the common case).
		if (CachedCompiledSourceType != SourceType)
		{
			CachedCompiled = ParsedChain;
			CompileChainForSource(CachedCompiled, SourceType);
			CachedCompiledSourceType = SourceType;
		}

		const FSubSelectionChain& Compiled = CachedCompiled;

		if (Compiled.Steps.IsEmpty())
		{
			OutType = EPCGMetadataTypes::Unknown;
			return;
		}

		// Walk the compiled chain with double-buffered intermediates.
		alignas(16) uint8 BufA[96];
		alignas(16) uint8 BufB[96];
		void* Bufs[2] = {BufA, BufB};

		const void* CurrentIn = Source;
		int32 BufIdx = 0;
		const int32 LastIdx = Compiled.Steps.Num() - 1;

		for (int32 i = 0; i < LastIdx; ++i)
		{
			const FSubSelectionStep& Step = Compiled.Steps[i];
			check(Step.StepGetFn);
			void* StepOut = Bufs[BufIdx];
			Step.StepGetFn(CurrentIn, StepOut, Step.Parsed);
			CurrentIn = StepOut;
			BufIdx = 1 - BufIdx;
		}

		// Last step writes directly to OutValue.
		const FSubSelectionStep& Last = Compiled.Steps[LastIdx];
		check(Last.StepGetFn);
		Last.StepGetFn(CurrentIn, OutValue, Last.Parsed);

		OutType = Compiled.FinalType;
	}

	void FSubSelection::ApplySet(EPCGMetadataTypes TargetType, void* Target,
	                             EPCGMetadataTypes SourceType, const void* Source) const
	{
		if (!HasSelection())
		{
			PCGExTypeOps::FConversionTable::Convert(SourceType, Source, TargetType, Target);
			return;
		}

		// Reuse the 1-entry compile cache (ApplySet compiles against TargetType).
		if (CachedCompiledSourceType != TargetType)
		{
			CachedCompiled = ParsedChain;
			CompileChainForSource(CachedCompiled, TargetType);
			CachedCompiledSourceType = TargetType;
		}

		const FSubSelectionChain& Compiled = CachedCompiled;

		// Single Num() read: the analyzer can't correlate separate IsEmpty()/Num() calls
		// and otherwise assumes a negative LastIdx below (C6385)
		const int32 NumSteps = Compiled.Steps.Num();

		if (NumSteps == 0)
		{
			PCGExTypeOps::FConversionTable::Convert(SourceType, Source, TargetType, Target);
			return;
		}

		const int32 LastIdx = NumSteps - 1;

		// All steps must have a SetFn for inject to work.
		for (const FSubSelectionStep& Step : Compiled.Steps)
		{
			if (!Step.StepSetFn)
			{
				PCGExTypeOps::FConversionTable::Convert(SourceType, Source, TargetType, Target);
				return;
			}
		}

		// Extract phase: walk forward to populate intermediates.
		constexpr int32 MaxSteps = 4;
		checkf(NumSteps <= MaxSteps,
		       TEXT("FSubSelection chain exceeded MaxSteps (%d > %d)"),
		       NumSteps, MaxSteps);

		alignas(16) uint8 Buffers[MaxSteps][96];

		{
			const void* CurrentIn = Target;
			for (int32 i = 0; i < LastIdx; ++i)
			{
				const FSubSelectionStep& Step = Compiled.Steps[i];
				check(Step.StepGetFn);
				Step.StepGetFn(CurrentIn, Buffers[i], Step.Parsed);
				CurrentIn = Buffers[i];
			}
		}

		// Convert source to the chain's final type if needed, then inject.
		alignas(16) uint8 NewChildBuf[96];
		const void* NewChild = Source;
		if (SourceType != Compiled.FinalType)
		{
			PCGExTypeOps::FConversionTable::Convert(SourceType, Source, Compiled.FinalType, NewChildBuf);
			NewChild = NewChildBuf;
		}

		// Inject at last step.
		void* LastParent = (LastIdx == 0) ? Target : Buffers[LastIdx - 1];
		Compiled.Steps[LastIdx].StepSetFn(LastParent, NewChild, Compiled.Steps[LastIdx].Parsed);

		// Walk backward, propagating mutations up.
		for (int32 i = LastIdx - 1; i >= 0; --i)
		{
			void* Parent = (i == 0) ? Target : Buffers[i - 1];
			Compiled.Steps[i].StepSetFn(Parent, Buffers[i], Compiled.Steps[i].Parsed);
		}
	}

	double FSubSelection::ExtractFieldToDouble(EPCGMetadataTypes SourceType, const void* Source) const
	{
		const ISubAccessor* FieldAccessor = FSubAccessorRegistry::GetSingleFieldAccessor();
		FAccessorParseResult Parsed;
		Parsed.Field = Field;

		double Out = 0.0;
		FieldAccessor->ApplyGet(SourceType, Source, EPCGMetadataTypes::Double, &Out, Parsed);
		return Out;
	}

	void FSubSelection::InjectFieldFromDouble(EPCGMetadataTypes TargetType, void* Target, double Value) const
	{
		const ISubAccessor* FieldAccessor = FSubAccessorRegistry::GetSingleFieldAccessor();
		FAccessorParseResult Parsed;
		Parsed.Field = Field;

		FieldAccessor->ApplySet(TargetType, Target, EPCGMetadataTypes::Double, &Value, Parsed);
	}

	//
	// Legacy Type-Erased Interface (GetVoid/SetVoid)
	//
	// Delegates to ApplyGet/ApplySet (the chain walker) with intermediate
	// type conversion. For performance, prefer FCachedSubSelection.
	//

	void FSubSelection::GetVoid(EPCGMetadataTypes SourceType, const void* Source, EPCGMetadataTypes WorkingType, void* Target) const
	{
		if (!HasSelection())
		{
			PCGExTypeOps::FConversionTable::Convert(SourceType, Source, WorkingType, Target);
			return;
		}

		alignas(16) uint8 IntermediateBuffer[96];
		EPCGMetadataTypes IntermediateType = EPCGMetadataTypes::Unknown;

		ApplyGet(SourceType, Source, IntermediateBuffer, IntermediateType);

		if (IntermediateType == WorkingType)
		{
			const PCGExTypeOps::ITypeOpsBase* TypeOps = PCGExTypeOps::FTypeOpsRegistry::Get(IntermediateType);
			if (TypeOps)
			{
				TypeOps->Copy(IntermediateBuffer, Target);
			}
		}
		else if (IntermediateType != EPCGMetadataTypes::Unknown)
		{
			PCGExTypeOps::FConversionTable::Convert(IntermediateType, IntermediateBuffer, WorkingType, Target);
		}
		else
		{
			PCGExTypeOps::FConversionTable::Convert(SourceType, Source, WorkingType, Target);
		}
	}

	void FSubSelection::SetVoid(EPCGMetadataTypes TargetType, void* Target,
	                            EPCGMetadataTypes SourceType, const void* Source) const
	{
		if (!HasSelection())
		{
			PCGExTypeOps::FConversionTable::Convert(SourceType, Source, TargetType, Target);
			return;
		}

		ApplySet(TargetType, Target, SourceType, Source);
	}

	//
	// Helper functions for type resolution
	//

	bool TryGetType(const FPCGAttributePropertyInputSelector& InputSelector, const UPCGData* InData, EPCGMetadataTypes& OutType)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TryGetType)
		
		OutType = EPCGMetadataTypes::Unknown;

		if (!IsValid(InData))
		{
			return false;
		}

		const FPCGAttributePropertyInputSelector FixedSelector = InputSelector.CopyAndFixLast(InData);
		if (!FixedSelector.IsValid())
		{
			return false;
		}

		if (FixedSelector.GetSelection() == EPCGAttributePropertySelection::Attribute)
		{
			if (!InData->Metadata)
			{
				return false;
			}
			if (const FPCGMetadataAttributeBase* AttributeBase = InData->Metadata->GetConstAttribute(PCGExMetaHelpers::GetAttributeIdentifier(FixedSelector, InData)))
			{
				OutType = PCGExMetaHelpers::GetAttributeType(AttributeBase);
			}
		}
		else if (FixedSelector.GetSelection() == EPCGAttributePropertySelection::ExtraProperty)
		{
			OutType = PCGExMetaHelpers::GetPropertyType(FixedSelector.GetExtraProperty());
		}
		else if (FixedSelector.GetSelection() == EPCGAttributePropertySelection::Property)
		{
			OutType = PCGExMetaHelpers::GetPropertyType(FixedSelector.GetPointProperty());
		}

		return OutType != EPCGMetadataTypes::Unknown;
	}

	bool TryGetTypeAndSource(const FPCGAttributePropertyInputSelector& InputSelector, const TSharedPtr<FFacade>& InDataFacade, EPCGMetadataTypes& OutType, EIOSide& InOutSide)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TryGetTypeAndSource)
		
		OutType = EPCGMetadataTypes::Unknown;
		if (InOutSide == EIOSide::In)
		{
			if (!TryGetType(InputSelector, InDataFacade->GetIn(), OutType))
			{
				if (TryGetType(InputSelector, InDataFacade->GetOut(), OutType))
				{
					InOutSide = EIOSide::Out;
				}
			}
		}
		else
		{
			if (!TryGetType(InputSelector, InDataFacade->GetOut(), OutType))
			{
				if (TryGetType(InputSelector, InDataFacade->GetIn(), OutType))
				{
					InOutSide = EIOSide::In;
				}
			}
		}

		return OutType != EPCGMetadataTypes::Unknown;
	}

	bool TryGetTypeAndSource(const FName AttributeName, const TSharedPtr<FFacade>& InDataFacade, EPCGMetadataTypes& OutType, EIOSide& InOutSource)
	{
		FPCGAttributePropertyInputSelector Selector;
		Selector.SetAttributeName(AttributeName);
		return TryGetTypeAndSource(Selector, InDataFacade, OutType, InOutSource);
	}
}
