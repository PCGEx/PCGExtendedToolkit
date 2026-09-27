// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExProjection.h"

#include "Data/PCGExData.h"
#include "Data/PCGExDataMacros.h"
#include "Data/PCGExPointElements.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGSpatialData.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Elements/PCGProjectionElement.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Helpers/PCGPointHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGPoint.h"
#include "Sampling/PCGExSamplingCommon.h"

#define LOCTEXT_NAMESPACE "PCGExProjectionElement"
#define PCGEX_NAMESPACE Projection

UPCGExProjectionSettings::UPCGExProjectionSettings()
{
	// Stock node parity: position & rotation projected by default
	ApplySampling.bApplyTransform = true;
	ApplySampling.TransformPosition = static_cast<uint8>(EPCGExApplySampledComponentFlags::All);
	ApplySampling.TransformRotation = static_cast<uint8>(EPCGExApplySampledComponentFlags::All);
}

void UPCGExProjectionSettings::InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const
{
	// Concrete, not Spatial: composite data (union, intersection, projection...) gets a Make Concrete conversion
	// upfront instead of collapsing single-threaded under its cache lock from inside the parallel loop.
	{
		FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(PCGProjectionConstants::ProjectionTargetLabel, FPCGDataTypeInfoConcrete::AsId(), false, false);
		PCGEX_PIN_TOOLTIP("The spatial data to project onto (landscape, surface, points, volume...). Only the first spatial data is used.")
		PCGEX_PIN_STATUS(Required)
	}
}

PCGEX_INITIALIZE_ELEMENT(Projection)

PCGExData::EIOInit UPCGExProjectionSettings::GetMainDataInitializationPolicy() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(Projection)

#pragma region FPCGExProjectionElement

bool FPCGExProjectionElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(Projection)

	// First spatial data on the pin, same as the stock node.
	const TArray<FPCGTaggedData> Targets = Context->InputData.GetInputsByPin(PCGProjectionConstants::ProjectionTargetLabel);
	for (const FPCGTaggedData& TaggedData : Targets)
	{
		if (const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(TaggedData.Data))
		{
			Context->ProjectionTarget = SpatialData;
			break;
		}
	}

	if (!Context->ProjectionTarget)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Missing or invalid projection target (expected spatial data)."));
		return false;
	}

	PCGEX_FWD(ApplySampling)
	Context->ApplySampling.Init();

	// Position is always projected (pruning and look-at need it). Rotation only when a component is applied or
	// normal-to-density reads it, scale only when applied, so targets can skip that work (e.g. landscape normals).
	// Attribute/color params are unused by ProjectPoint.
	Context->ProjectionParams.bProjectPositions = true;
	Context->ProjectionParams.bProjectRotations = Settings->bNormalToDensity || !Context->ApplySampling.TrRotComponents.IsEmpty();
	Context->ProjectionParams.bProjectScales = !Context->ApplySampling.TrScaComponents.IsEmpty();

	// Invalid name : warn & disable the output, same as the other sampling nodes
	PCGEX_OUTPUT_VALIDATE_NAME(Success, bool, false)

	if (Settings->AttributesForwarding.bEnabled)
	{
		// Every UPCGSpatialData owns a metadata subobject; this only trips on a custom target.
		if (const UPCGMetadata* TargetMetadata = Context->ProjectionTarget->ConstMetadata())
		{
			Context->bForwardAttributes = true;

			// Attributes a target synthesizes while initializing metadata (landscape layers, actor reference...) are
			// unknown here; they bypass this include-set and are filtered by the handler per processor.
			FPCGExForwardDetails Filter = Settings->AttributesForwarding;
			Filter.Init();

			TArray<FPCGAttributeIdentifier> Identifiers;
			PCGExDataFilter::Helpers::GetAttributes(TargetMetadata, EPCGExAttributeDomainScope::Elements, Identifiers);
			for (const FPCGAttributeIdentifier& Identifier : Identifiers)
			{
				if (Filter.Test(Identifier.Name.ToString()))
				{
					Context->ForwardedTargetAttributes.Add(Identifier.Name);
				}
			}
		}
		else
		{
			PCGE_LOG(Warning, GraphAndLog, FTEXT("The projection target has no metadata; attribute forwarding is disabled."));
		}
	}

	if (const UPCGBasePointData* PointTarget = Cast<UPCGBasePointData>(Context->ProjectionTarget))
	{
		// Build the octree here rather than under the cached-data lock from the first worker threads.
		(void)PointTarget->GetPointOctree();
	}

	if (!Context->ApplySampling.WantsApply() && !Settings->bPruneFailedProjections && !Context->bWriteSuccess && !Context->bForwardAttributes && !Settings->bNormalToDensity)
	{
		PCGE_LOG(Warning, GraphAndLog, FTEXT("Nothing to apply, prune, forward or write : the node has no effect."));
	}

	return true;
}

bool FPCGExProjectionElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExProjectionElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(Projection)
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
				// CompleteWork only flushes attribute writers; the Write step (Gather) runs regardless of bSkipCompletion.
				NewBatch->bSkipCompletion = !Context->bWriteSuccess && !Context->bForwardAttributes;
				NewBatch->bRequiresWriteStep = Settings->bPruneFailedProjections;
			}))
		{
			return Context->CancelExecution(TEXT("No data."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

#pragma endregion

#pragma region FProcessor

namespace PCGExProjection
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExProjection::Process);

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		// Never steal the projection target itself : worker threads would write the transforms
		// ProjectPoint is reading through the target's octree.
		const bool bInputIsTarget = PointDataFacade->GetIn() == Context->ProjectionTarget;
		PCGEX_INIT_IO(PointDataFacade->Source, bInputIsTarget ? PCGExData::EIOInit::Duplicate : Settings->GetMainDataInitializationPolicy())

		// Only what gets written is allocated
		EPCGPointNativeProperties AllocateFor = EPCGPointNativeProperties::None;

		if (Context->ApplySampling.WantsApply())
		{
			AllocateFor |= EPCGPointNativeProperties::Transform;
		}

		if (Settings->bNormalToDensity)
		{
			NormalToDensity = MakeShared<PCGExSampling::FNormalToDensity>();
			if (!NormalToDensity->Init(Settings->NormalToDensity, PointDataFacade))
			{
				return false;
			}

			AllocateFor |= EPCGPointNativeProperties::Density;
		}

		if (AllocateFor != EPCGPointNativeProperties::None)
		{
			PointDataFacade->GetOut()->AllocateProperties(AllocateFor);
		}

		bPrune = Settings->bPruneFailedProjections;
		if (bPrune)
		{
			ProjectionMask.SetNumUninitialized(PointDataFacade->GetNum());
			PruneFiltered = Settings->bProcessFilteredOutAsFails ? 0 : 1;
		}

		if (Context->bWriteSuccess)
		{
			SuccessWriter = PointDataFacade->GetWritable<bool>(Settings->SuccessAttributeName, false, true, PCGExData::EBufferInit::Inherit);
			if (!SuccessWriter)
			{
				return false;
			}
		}

		if (Context->bForwardAttributes)
		{
			if (!InitForwarding())
			{
				return false;
			}
		}

		StartParallelLoopForPoints();

		return true;
	}

	bool FProcessor::InitForwarding()
	{
		// Same setup as UPCGProjectionData::SetupTargetMetadata: a scratch metadata parented to the target so a
		// point target's entries inherit values, with the target's known attributes narrowed to the include-set.
		SampledMetadata = Context->ManagedObjects->New<UPCGMetadata>();
		if (!SampledMetadata)
		{
			return false;
		}

		SampledMetadata->SetupDomainsFromPCGDataType<UPCGBasePointData>();

		FPCGInitializeFromDataParams InitParams(Context->ProjectionTarget);
		FPCGMetadataDomainInitializeParams ElementsParams(nullptr, &Context->ForwardedTargetAttributes);
		ElementsParams.FilterMode = EPCGMetadataFilterMode::IncludeAttributes;
		InitParams.MetadataInitializeParams.DomainInitializeParams.Emplace(PCGMetadataDomainID::Elements, MoveTemp(ElementsParams));

		Context->ProjectionTarget->InitializeTargetMetadata(InitParams, SampledMetadata);

		AttributesForward = Settings->AttributesForwarding.TryGetHandler(SampledMetadata, PointDataFacade);
		if (!AttributesForward || AttributesForward->IsEmpty())
		{
			// Nothing survives the filter: skip the per-point metadata work entirely.
			AttributesForward.Reset();
			Context->ManagedObjects->Destroy(SampledMetadata);
			SampledMetadata = nullptr;
			return true;
		}

		SampledEntries.Init(PCGInvalidEntryKey, PointDataFacade->GetNum());
		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::Projection::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		const UPCGBasePointData* InPointData = PointDataFacade->GetIn();
		UPCGBasePointData* OutPointData = PointDataFacade->GetOut();

		const FPCGExApplySamplingDetails& ApplySampling = Context->ApplySampling;
		const bool bApply = ApplySampling.WantsApply();
		const bool bLookAt = ApplySampling.bApplyLookAt;

		const PCGExSampling::FNormalToDensity* Density = NormalToDensity.Get();

		if (!bApply && !bPrune && !SuccessWriter && !AttributesForward && !Density)
		{
			// Nothing would consume the projection result
			return;
		}

		// With data stealing (Forward), In and Out are the same object: each point copies its input transform
		// to a local before anything is written at the same index.
		TConstPCGValueRange<FTransform> InTransforms = InPointData->GetConstTransformValueRange();
		TConstPCGValueRange<FVector> InBoundsMin = InPointData->GetConstBoundsMinValueRange();
		TConstPCGValueRange<FVector> InBoundsMax = InPointData->GetConstBoundsMaxValueRange();
		TPCGValueRange<FTransform> OutTransforms = bApply ? OutPointData->GetTransformValueRange(false) : TPCGValueRange<FTransform>();
		TPCGValueRange<float> OutDensities = Density ? OutPointData->GetDensityValueRange(false) : TPCGValueRange<float>();

		PCGExSampling::FNormalToDensity::FScopeView DensityView;
		if (Density)
		{
			Density->PrepareScope(Scope, DensityView);
		}

		// Rotation projected for the normal alone must not reach the look-at fallback.
		const bool bRestoreRotation = Density && ApplySampling.TrRotComponents.IsEmpty();

		const UPCGSpatialData* Target = Context->ProjectionTarget;
		const FPCGProjectionParams& ProjectionParams = Context->ProjectionParams;

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index])
			{
				if (bPrune)
				{
					ProjectionMask[Index] = PruneFiltered;
				}
				continue;
			}

			const FTransform InTransform = InTransforms[Index];
			const FBox LocalBounds = PCGPointHelpers::GetLocalBounds(InBoundsMin[Index], InBoundsMax[Index]);

			FPCGPoint Projected;
			const bool bSuccess = Target->ProjectPoint(InTransform, LocalBounds, ProjectionParams, Projected, SampledMetadata);

			if (bPrune)
			{
				ProjectionMask[Index] = bSuccess ? 1 : 0;
			}

			if (SuccessWriter)
			{
				SuccessWriter->SetValue(Index, bSuccess);
			}

			if (!bSuccess)
			{
				continue;
			}

			if (AttributesForward)
			{
				SampledEntries[Index] = Projected.MetadataEntry;
			}

			if (Density)
			{
				Density->Apply(DensityView, Index - Scope.Start, Projected.Transform.GetUnitAxis(EAxis::Z), OutDensities[Index]);
			}

			if (!bApply)
			{
				continue;
			}

			if (bRestoreRotation)
			{
				Projected.Transform.SetRotation(InTransform.GetRotation());
			}

			// Look-at from the original point toward its projected location (X forward), same convention as
			// Sample Nearest Surface. A point that did not move keeps whatever rotation ProjectPoint returned.
			FTransform LookAt = Projected.Transform;
			if (bLookAt)
			{
				const FVector Direction = (Projected.Transform.GetLocation() - InTransform.GetLocation()).GetSafeNormal();
				if (!Direction.IsNearlyZero())
				{
					LookAt = FTransform(FRotationMatrix::MakeFromX(Direction).ToQuat(), Projected.Transform.GetLocation(), FVector::OneVector);
				}
			}

			ApplySampling.Apply(OutTransforms[Index], Projected.Transform, LookAt);
		}

		if (AttributesForward)
		{
			AttributesForward->ForwardEntriesScoped(Scope, SampledEntries);
		}
	}

	void FProcessor::CompleteWork()
	{
		if (AttributesForward)
		{
			SampledEntries.Empty();

			// Values are in the writers now; release the scratch entries instead of holding them until the context ends.
			Context->ManagedObjects->Destroy(SampledMetadata);
			SampledMetadata = nullptr;
		}

		PointDataFacade->WriteFastest(TaskManager);
	}

	void FProcessor::Write()
	{
		if (bPrune)
		{
			(void)PointDataFacade->Source->Gather(ProjectionMask);
		}
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
