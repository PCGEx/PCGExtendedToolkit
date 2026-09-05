// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Elements/PCGExAttributeRemap.h"

#include "PCGExVersion.h"
#include "PCGExVersion.h"
#include "Containers/PCGExScopedContainers.h"
#include "Containers/PCGExScopedContainers.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExProxyData.h"
#include "Data/PCGExProxyDataHelpers.h"
#include "Data/PCGExSubAccessor.h"
#include "Details/PCGExSettingsDetails.h"


#define LOCTEXT_NAMESPACE "PCGExAttributeRemap"
#define PCGEX_NAMESPACE AttributeRemap

#if WITH_EDITOR
FString UPCGExAttributeRemapSettings::GetDisplayName() const
{
	if (Attributes.WantsRemappedOutput())
	{
		return TEXT("Remap : ") + Attributes.Source.ToString() + TEXT(" → ") + Attributes.Target.ToString();
	}

	return TEXT("Remap : ") + Attributes.Source.ToString();
}

void UPCGExAttributeRemapSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 70, 11)
	{
		if (SourceAttributeName_DEPRECATED != NAME_None)
		{
			Attributes.Source = SourceAttributeName_DEPRECATED;
		}
		if (TargetAttributeName_DEPRECATED != NAME_None)
		{
			Attributes.Target = TargetAttributeName_DEPRECATED;
			Attributes.bOutputToDifferentName = (SourceAttributeName_DEPRECATED != TargetAttributeName_DEPRECATED);
		}
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

void FPCGExAttributeRemapContext::RegisterAssetDependencies()
{
	FPCGExPointsProcessorContext::RegisterAssetDependencies();
	for (const FPCGExComponentRemapRule& Rule : RemapSettings)
	{
		AddAssetDependency(Rule.RemapDetails.RemapCurve.ToSoftObjectPath());
	}
}

#if WITH_EDITOR
TArray<FText> UPCGExAttributeRemapSettings::GetNodeTitleAliases() const
{
	return {FTEXT("PCGEx | Curve Remap")};
}
#endif

PCGEX_INITIALIZE_ELEMENT(AttributeRemap)

PCGExData::EIOInit UPCGExAttributeRemapSettings::GetMainDataInitializationPolicy() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(AttributeRemap)

bool FPCGExAttributeRemapElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(AttributeRemap)

	if (!Settings->Attributes.ValidateNamesOrProperties(Context))
	{
		return false;
	}

	Context->RemapSettings[0] = Settings->BaseRemap;
	Context->RemapSettings[1] = Settings->Component2RemapOverride;
	Context->RemapSettings[2] = Settings->Component3RemapOverride;
	Context->RemapSettings[3] = Settings->Component4RemapOverride;

	return true;
}

void FPCGExAttributeRemapElement::PostLoadAssetsDependencies(FPCGExContext* InContext) const
{
	FPCGExPointsProcessorElement::PostLoadAssetsDependencies(InContext);

	PCGEX_CONTEXT_AND_SETTINGS(AttributeRemap)

	for (int i = 0; i < 4; i++)
	{
		Context->RemapSettings[i].RemapDetails.Init();
	}

	Context->RemapIndices[0] = 0;
	Context->RemapIndices[1] = Settings->bOverrideComponent2 ? 1 : 0;
	Context->RemapIndices[2] = Settings->bOverrideComponent3 ? 2 : 0;
	Context->RemapIndices[3] = Settings->bOverrideComponent4 ? 3 : 0;
}

bool FPCGExAttributeRemapElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExAttributeRemapElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(AttributeRemap)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bSkipCompletion = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any data to remap."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExAttributeRemap
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		TArray<TSharedPtr<PCGExData::IBufferProxy>> UntypedInputProxies;
		TArray<TSharedPtr<PCGExData::IBufferProxy>> UntypedOutputProxies;

		PCGExData::FProxyDescriptor InputDescriptor;
		PCGExData::FProxyDescriptor OutputDescriptor;

		InputDescriptor.DataFacade = PointDataFacade;
		OutputDescriptor.DataFacade = PointDataFacade;
		OutputDescriptor.Role = PCGExData::EProxyRole::Write;

		if (!InputDescriptor.Capture(Context, Settings->Attributes.GetSourceSelector(), PCGExData::EIOSide::In))
		{
			return false;
		}

		// Attribute Remap is a per-field numeric operation (clamp + remap arithmetic).
		// It has no meaning on extended-type scalars (Struct, Object, Class, Soft*,
		// Byte, Text, Enum) or container attributes (TArray/TSet/TMap). Reject those here.
		{
			const bool bIsContainer = InputDescriptor.SourceDesc.IsValid() && !InputDescriptor.SourceDesc.IsSingleValue();
			if (bIsContainer || !PCGExMetaHelpers::IsLegacyScalarType(InputDescriptor.RealType))
			{
				PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("Attribute Remap requires a numeric scalar attribute. Struct, Object, container, and other extended types are not supported."));
				return false;
			}
		}

		// Number of dimensions to be remapped
		UnderlyingType = InputDescriptor.WorkingType;
		Dimensions = FMath::Min(4, PCGExData::GetNumFieldsForType(UnderlyingType));

		if (!GetPerFieldProxyBuffers(Context, InputDescriptor, Dimensions, UntypedInputProxies))
		{
			return false;
		}

		if (!OutputDescriptor.CaptureStrict(Context, Settings->Attributes.GetTargetSelector(), PCGExData::EIOSide::Out, false))
		{
			// This might be expected if the destination does not exist

			if (Dimensions == 1 && Settings->Attributes.WantsRemappedOutput() && !OutputDescriptor.SubSelection.HasSelection())
			{
				// We're remapping a component to a single value with no subselection
				OutputDescriptor.RealType = InputDescriptor.WorkingType;
			}
			else
			{
				// We're remapping to a component within the same larger type
				OutputDescriptor.RealType = InputDescriptor.RealType;
			}

			if (Settings->bAutoCastIntegerToDouble && (OutputDescriptor.RealType == EPCGMetadataTypes::Integer32 || OutputDescriptor.RealType == EPCGMetadataTypes::Integer64))
			{
				OutputDescriptor.RealType = EPCGMetadataTypes::Double;
			}

			OutputDescriptor.WorkingType = InputDescriptor.WorkingType;
		}
		else
		{
			// TODO : Grab default type for attribute if it cannot be inferred
			// GetPerFieldProxyBuffers expect a valid RealType to work from
		}

		if (!GetPerFieldProxyBuffers(Context, OutputDescriptor, Dimensions, UntypedOutputProxies))
		{
			return false;
		}

		for (int i = 0; i < Dimensions; i++)
		{
			TSharedPtr<PCGExData::IBufferProxy> InProxy = UntypedInputProxies[i];
			TSharedPtr<PCGExData::IBufferProxy> OutProxy = UntypedOutputProxies[i];

			if (InProxy->WorkingType != EPCGMetadataTypes::Double)
			{
				// TODO : Some additional validation, just making sure we can safely cast those
			}

			if (OutProxy->WorkingType != EPCGMetadataTypes::Double)
			{
				// TODO : Some additional validation, just making sure we can safely cast those
			}

			InputProxies.Add(InProxy);
			OutputProxies.Add(OutProxy);
		}

		Rules.Reserve(Dimensions);
		for (int i = 0; i < Dimensions; i++)
		{
			FPCGExComponentRemapRule& Rule = Rules.Add_GetRef(FPCGExComponentRemapRule(Context->RemapSettings[Context->RemapIndices[i]]));
			if (!Rule.RemapDetails.bUseInMin)
			{
				Rule.RemapDetails.InMin = TNumericLimits<double>::Max();
			}
			if (!Rule.RemapDetails.bUseInMax)
			{
				Rule.RemapDetails.InMax = TNumericLimits<double>::Lowest();
			}
		}

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
		for (FPCGExComponentRemapRule& Rule : Rules)
		{
			Rule.MinCache = MakeShared<PCGExMT::TScopedNumericValue<double>>(Loops, TNumericLimits<double>::Max());
			Rule.MaxCache = MakeShared<PCGExMT::TScopedNumericValue<double>>(Loops, TNumericLimits<double>::Lowest());
			Rule.SnapCache = Rule.RemapDetails.Snap.GetValueSetting();
		}
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExAttributeRemap::Fetch);

		PointDataFacade->Fetch(Scope);

		// Per-dim locals hoisted out of the inner loop so we walk the input buffer
		// once across all dimensions (cache-friendly for multi-component types).
		PCGExData::IBufferProxy* InProxy[4] = {nullptr, nullptr, nullptr, nullptr};
		PCGExData::IBufferProxy* OutProxy[4] = {nullptr, nullptr, nullptr, nullptr};
		FPCGExClampDetails* Clamp[4] = {nullptr, nullptr, nullptr, nullptr};
		bool bAbs[4] = {false, false, false, false};
		double Min[4] = {TNumericLimits<double>::Max(), TNumericLimits<double>::Max(), TNumericLimits<double>::Max(), TNumericLimits<double>::Max()};
		double Max[4] = {TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest()};

		for (int d = 0; d < Dimensions; d++)
		{
			InProxy[d] = InputProxies[d].Get();
			OutProxy[d] = OutputProxies[d].Get();
			Clamp[d] = &Rules[d].InputClampDetails;
			bAbs[d] = Rules[d].RemapDetails.bUseAbsoluteRange;
		}

		PCGEX_SCOPE_LOOP(i)
		{
			for (int d = 0; d < Dimensions; d++)
			{
				const double V = Clamp[d]->GetClampedValue(InProxy[d]->Get<double>(i));
				OutProxy[d]->Set(i, V);
				const double Cmp = bAbs[d] ? FMath::Abs(V) : V;
				Min[d] = FMath::Min(Min[d], Cmp);
				Max[d] = FMath::Max(Max[d], Cmp);
			}
		}

		for (int d = 0; d < Dimensions; d++)
		{
			Rules[d].MinCache->Set(Scope, Min[d]);
			Rules[d].MaxCache->Set(Scope, Max[d]);
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		// Fix min/max range
		for (FPCGExComponentRemapRule& Rule : Rules)
		{
			if (!Rule.RemapDetails.bUseInMin)
			{
				Rule.RemapDetails.InMin = Rule.MinCache->Min();
			}
			if (!Rule.RemapDetails.bUseInMax)
			{
				Rule.RemapDetails.InMax = Rule.MaxCache->Max();
			}
			if (Rule.RemapDetails.RangeMethod == EPCGExRangeType::FullRange && Rule.RemapDetails.InMin > 0)
			{
				Rule.RemapDetails.InMin = 0;
			}
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExAttributeRemap::RemapRange);

		for (int d = 0; d < Dimensions; d++)
		{
			FPCGExComponentRemapRule& Rule = Rules[d];
			TSharedPtr<PCGExData::IBufferProxy> InProxy = InputProxies[d];
			TSharedPtr<PCGExData::IBufferProxy> OutProxy = OutputProxies[d];

			const int Strategy = (Rule.RemapDetails.bUseAbsoluteRange ? 2 : 0)
				+ (Rule.RemapDetails.bPreserveSign ? 1 : 0);

			switch (Strategy)
			{
			case 3: // Absolute + PreserveSign
				PCGEX_PARALLEL_FOR(
					PointDataFacade->GetNum(),
					double V = InProxy->Get<double>(i);
					OutProxy->Set(i, Rule.OutputClampDetails.GetClampedValue(Rule.RemapDetails.GetRemappedValue(FMath::Abs(V), Rule.SnapCache->Read(i)) * PCGExMath::SignPlus(V)));
					)
				break;
			case 2: // Absolute only
				PCGEX_PARALLEL_FOR(
					PointDataFacade->GetNum(),
					OutProxy->Set(i, Rule.OutputClampDetails.GetClampedValue(Rule.RemapDetails.GetRemappedValue(FMath::Abs(InProxy->Get<double>(i)), Rule.SnapCache->Read(i))));
					)
				break;
			case 1: // Preserve sign only
				PCGEX_PARALLEL_FOR(
					PointDataFacade->GetNum(),
					OutProxy->Set(i, Rule.OutputClampDetails.GetClampedValue(Rule.RemapDetails.GetRemappedValue(InProxy->Get<double>(i), Rule.SnapCache->Read(i))));
					)
				break;
			default: // Neither toggle: remap the raw value -- Abs belongs to the absolute-range strategies only.
				PCGEX_PARALLEL_FOR(
					PointDataFacade->GetNum(),
					OutProxy->Set(i, Rule.OutputClampDetails.GetClampedValue(Rule.RemapDetails.GetRemappedValue(InProxy->Get<double>(i), Rule.SnapCache->Read(i))));
					)
				break;
			}
		}

		PointDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
