// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExDensitySurfaceSampler.h"

#include "PCGGraphExecutionStateInterface.h"
#include "PCGPin.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGSurfaceData.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Helpers/PCGHelpers.h"
#include "Helpers/PCGSettingsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExDensitySurfaceSamplerElement"
#define PCGEX_NAMESPACE DensitySurfaceSampler

PCGEX_INITIALIZE_ELEMENT(DensitySurfaceSampler)

namespace PCGExDensitySurfaceSampler
{
	PCGEX_CTX_STATE(State_PrimingSurfaces)

	/** Sampling area of a surface in grid space, as the engine Surface Sampler finds it, overlapped with the bounding shape when there is one. */
	FBox GetDomain(const UPCGSurfaceData* InSurface, const FBox& InBoundingShapeBounds)
	{
		FBox Domain = InSurface->GetLocalBounds();
		if (Domain.IsValid)
		{
			Domain = Domain.TransformBy(InSurface->GetTransform());
		}
		else if (InSurface->IsBounded())
		{
			Domain = InSurface->GetBounds();
		}
		else
		{
			// Unbounded data (a world ray hit, say) only samples inside a bounding shape.
			return InBoundingShapeBounds;
		}

		return InBoundingShapeBounds.IsValid ? PCGHelpers::OverlapBounds(Domain, InBoundingShapeBounds) : Domain;
	}

	/** The driver's lookup. With Max Spacing on, it also turns the spacing blend into a density. */
	PCGExFloatLUT MakeDriverCurve(FPCGExContext* InContext, const UPCGExDensitySurfaceSamplerSettings* InSettings, const PCGExDensitySampling::FConfig& InConfig)
	{
		if (!InSettings->bUseMaxSpacing)
		{
			return InSettings->bUseDriverCurve ? InSettings->DriverCurve.MakeLookup(EPCGExCurveLUTMode::Lookup, 256) : nullptr;
		}

		// Jittered grid cells have this spacing too, unless Point Extents force them larger.
		const double SpacingAtMaxDensity = InConfig.GetBlueNoiseSpacing();
		if (InSettings->MaxSpacing <= SpacingAtMaxDensity)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(
				LOCTEXT("MaxSpacingTooSmall", "Max Spacing is not above the spacing at Max Density ({0} cm), so the driver has no effect."),
				FText::AsNumber(FMath::RoundToInt32(SpacingAtMaxDensity))));
		}

		const FPCGExWeightCurve Identity(0, 1);
		return PCGExDensitySampling::MakeSpacingLookup(InSettings->bUseDriverCurve ? InSettings->DriverCurve : Identity, InSettings->MaxSpacing / SpacingAtMaxDensity);
	}

	bool BuildModel(FPCGExContext* InContext, const UPCGExDensitySurfaceSamplerSettings* InSettings, PCGExDensitySampling::FConfig& InOutConfig)
	{
		using namespace PCGExDensitySampling;

		FDensityModel& OutModel = InOutConfig.Model;

		switch (InSettings->DriverSource)
		{
		case EPCGExDensityDriverSource::Constant:
			break;
		case EPCGExDensityDriverSource::Attribute:
			if (InSettings->DriverAttribute.IsNone())
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, LOCTEXT("MissingDriverAttribute", "Driver Attribute is empty."));
				return false;
			}
			OutModel.AddFactor(EFactorOp::Multiply, OutModel.AddChannel(EChannelSource::Attribute, InSettings->DriverAttribute), 0, MakeDriverCurve(InContext, InSettings, InOutConfig));
			break;
		case EPCGExDensityDriverSource::PointDensity:
			OutModel.AddFactor(EFactorOp::Multiply, OutModel.AddChannel(EChannelSource::PointDensity, NAME_None), 0, MakeDriverCurve(InContext, InSettings, InOutConfig));
			break;
		default:
			PCGE_LOG_C(Error, GraphAndLog, InContext, LOCTEXT("UnknownDriverSource", "Driver Source holds a value this node does not know; pick it again."));
			return false;
		}

		if (!InSettings->bUseModifiers)
		{
			return true;
		}

		for (const FPCGExDensityModifier& Modifier : InSettings->Modifiers)
		{
			if (!Modifier.bEnabled)
			{
				continue;
			}

			if (Modifier.Attribute.IsNone())
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, LOCTEXT("MissingModifierAttribute", "An enabled modifier has no attribute."));
				return false;
			}

			const int32 Channel = OutModel.AddChannel(EChannelSource::Attribute, Modifier.Attribute);

			switch (Modifier.Operation)
			{
			case EPCGExDensityModifierOp::Multiply:
				OutModel.AddFactor(EFactorOp::Multiply, Channel, 0, Modifier.Curve.MakeLookup(EPCGExCurveLUTMode::Lookup, 256));
				break;
			case EPCGExDensityModifierOp::KeepAbove:
				OutModel.AddFactor(EFactorOp::KeepAbove, Channel, Modifier.Threshold, nullptr);
				break;
			case EPCGExDensityModifierOp::ExcludeAbove:
				OutModel.AddFactor(EFactorOp::ExcludeAbove, Channel, Modifier.Threshold, nullptr);
				break;
			default:
				PCGE_LOG_C(Error, GraphAndLog, InContext, LOCTEXT("UnknownModifierOp", "A modifier operation holds a value this node does not know; pick it again."));
				return false;
			}
		}

		return true;
	}
}

#pragma region UPCGExDensitySurfaceSamplerSettings

bool UPCGExDensitySurfaceSamplerSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (bUnbounded && InPin && InPin->Properties.Label == PCGExDensitySurfaceSampler::BoundingShapeLabel)
	{
		return false;
	}

	return Super::IsPinUsedByNodeExecution(InPin);
}

TArray<FPCGPinProperties> UPCGExDensitySurfaceSamplerSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	{
		FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(PCGExDensitySurfaceSampler::SurfaceLabel, FPCGDataTypeInfoSurface::AsId());
		PCGEX_PIN_TOOLTIP("Surfaces to sample, such as a landscape. One output per surface.")
		PCGEX_PIN_STATUS(Required)
	}

	PCGEX_PIN_SPATIAL(PCGExDensitySurfaceSampler::BoundingShapeLabel, "Points must lie inside this shape. Without one, the component bounds are used unless Unbounded is on.", Normal)

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExDensitySurfaceSamplerSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGPinConstants::DefaultOutputLabel, "Sampled points, one data per surface.", Normal)
	return PinProperties;
}

#pragma endregion

#pragma region FPCGExDensitySurfaceSamplerElement

void FPCGExDensitySurfaceSamplerElement::GetDependenciesCrc(const FPCGGetDependenciesCrcParams& InParams, FPCGCrc& OutCrc) const
{
	FPCGCrc Crc;
	IPCGElement::GetDependenciesCrc(InParams, Crc);

	if (const UPCGExDensitySurfaceSamplerSettings* Settings = Cast<UPCGExDensitySurfaceSamplerSettings>(InParams.Settings))
	{
		bool bUnbounded = Settings->bUnbounded;
		PCGSettingsHelpers::GetOverrideValue(*InParams.InputData, Settings, GET_MEMBER_NAME_CHECKED(UPCGExDensitySurfaceSamplerSettings, bUnbounded), Settings->bUnbounded, bUnbounded);
		const bool bBoundsConnected = InParams.InputData->GetInputsByPin(PCGExDensitySurfaceSampler::BoundingShapeLabel).Num() > 0;

		if (!bUnbounded && !bBoundsConnected && InParams.ExecutionSource)
		{
			if (const UPCGData* SelfData = InParams.ExecutionSource->GetExecutionState().GetSelfData())
			{
				Crc.Combine(SelfData->GetOrComputeCrc(/*bFullDataCrc=*/false));
			}
		}
	}

	OutCrc = Crc;
}

bool FPCGExDensitySurfaceSamplerElement::Boot(FPCGExContext* InContext) const
{
	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(DensitySurfaceSampler)

	if (Settings->MaxDensity <= 0)
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("InvalidMaxDensity", "Max Density must be greater than 0."));
		return false;
	}

	// Also refuses NaN and infinity, which an override can bring in.
	if (Settings->bUseMaxSpacing && Settings->DriverSource != EPCGExDensityDriverSource::Constant && !(Settings->MaxSpacing > 0 && FMath::IsFinite(Settings->MaxSpacing)))
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("InvalidMaxSpacing", "Max Spacing must be greater than 0."));
		return false;
	}

	if (Settings->PointExtents.X <= 0 || Settings->PointExtents.Y <= 0)
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("InvalidExtents", "Point Extents must be greater than 0 on X and Y."));
		return false;
	}

	switch (Settings->Distribution)
	{
	case EPCGExDensityDistribution::BlueNoise:
	case EPCGExDensityDistribution::JitteredGrid:
		break;
	case EPCGExDensityDistribution::PoissonDisk:
		if (Settings->PoissonRadiusFactor <= 0)
		{
			PCGE_LOG(Error, GraphAndLog, LOCTEXT("InvalidRadiusFactor", "Radius Factor must be greater than 0."));
			return false;
		}
		break;
	default:
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("UnknownDistribution", "Distribution holds a value this node does not know; pick it again."));
		return false;
	}

	switch (Settings->GridThinning)
	{
	case EPCGExDensityGridThinning::Random:
	case EPCGExDensityGridThinning::BlueNoise:
		break;
	default:
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("UnknownThinning", "Thinning holds a value this node does not know; pick it again."));
		return false;
	}

	switch (Settings->Evaluation)
	{
	case EPCGExDensityEvaluation::Exact:
		break;
	case EPCGExDensityEvaluation::DensityMap:
		if (Settings->DensityMapSpacing <= 0)
		{
			PCGE_LOG(Error, GraphAndLog, LOCTEXT("InvalidMapSpacing", "Map Spacing must be greater than 0."));
			return false;
		}
		break;
	default:
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("UnknownEvaluation", "Evaluation holds a value this node does not know; pick it again."));
		return false;
	}

	const bool bStrictSpacing = Settings->bStrictSpacing && Settings->Distribution != EPCGExDensityDistribution::PoissonDisk;
	if (bStrictSpacing && Settings->SpacingFactor <= 0)
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("InvalidSpacingFactor", "Strict Spacing must be greater than 0."));
		return false;
	}

	if (Settings->bWriteRank && !PCGExMetaHelpers::IsWritableAttributeName(Settings->RankAttributeName))
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("InvalidRankName", "Rank is not a valid attribute name."));
		return false;
	}

	PCGEX_MAKE_SHARED(Config, PCGExDensitySampling::FConfig)
	Config->Distribution = Settings->Distribution;
	Config->GridThinning = Settings->GridThinning;
	Config->Evaluation = Settings->Evaluation;
	Config->MaxDensity = Settings->MaxDensity;
	Config->PointExtents = Settings->PointExtents;
	Config->Looseness = Settings->Looseness;
	Config->PointSteepness = Settings->PointSteepness;
	Config->DensityMapSpacing = Settings->DensityMapSpacing;
	Config->bAlignToLandscapeVertices = Settings->bAlignToLandscapeVertices;
	Config->bStrictSpacing = bStrictSpacing;
	Config->SpacingFactor = Settings->SpacingFactor;
	Config->PoissonRadiusFactor = Settings->PoissonRadiusFactor;
	Config->bOutputSurfaceAttributes = Settings->bOutputSurfaceAttributes;
	Config->bWriteLocalDensity = Settings->bWriteLocalDensity;
	Config->bWriteRank = Settings->bWriteRank;
	Config->RankAttributeName = Settings->RankAttributeName;
	Config->Seed = Context->GetSeed();

	if (!PCGExDensitySurfaceSampler::BuildModel(Context, Settings, *Config))
	{
		return false;
	}

	Config->Finalize();
	Context->Config = Config;

	// Without a bounding shape, a bounded run falls back to the component bounds; no valid bounds means nothing to sample.
	const UPCGSpatialData* BoundingShape = nullptr;
	FBox BoundingShapeBounds(EForceInit::ForceInit);
	bool bNoOperation = false;

	if (!Settings->bUnbounded)
	{
		bool bUnionWasCreated = false;
		BoundingShape = PCGSettingsHelpers::ComputeBoundingShape(Context, PCGExDensitySurfaceSampler::BoundingShapeLabel, bUnionWasCreated);
		if (BoundingShape)
		{
			if (bUnionWasCreated)
			{
				Context->ManagedObjects->Add(const_cast<UPCGSpatialData*>(BoundingShape));
			}

			BoundingShapeBounds = BoundingShape->GetBounds();
		}

		bNoOperation = !BoundingShapeBounds.IsValid;
	}
	else if (!Context->InputData.GetInputsByPin(PCGExDensitySurfaceSampler::BoundingShapeLabel).IsEmpty())
	{
		PCGE_LOG(Verbose, LogOnly, LOCTEXT("BoundsIgnored", "The Bounding Shape input is ignored because Unbounded is on."));
	}

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGExDensitySurfaceSampler::SurfaceLabel);
	int32 OutputIndex = 0;

	for (const FPCGTaggedData& TaggedData : Inputs)
	{
		const UPCGSurfaceData* Surface = Cast<UPCGSurfaceData>(TaggedData.Data);
		if (!Surface)
		{
			continue;
		}

		PCGEX_MAKE_SHARED(Job, PCGExDensitySampling::FSurfaceJob)
		Job->Config = Config;
		Job->Surface = Surface;
		Job->BoundingShape = BoundingShape;

		// Every surface gets an output, empty when nothing is sampled, as the engine Surface Sampler does.
		Job->Output = PCGExData::NewPointIO(Context, PCGPinConstants::DefaultOutputLabel, OutputIndex++);
		if (!Job->Output->InitializeOutput<UPCGPointArrayData>(PCGExData::EIOInit::New))
		{
			return false;
		}

		Job->InitializeOutputData();
		Job->Output->bAllowEmptyOutput = true;
		Job->Output->Tags->Append(TaggedData.Tags);

		if (!bNoOperation)
		{
			const FBox Domain = PCGExDensitySurfaceSampler::GetDomain(Surface, BoundingShapeBounds);
			if (!Domain.IsValid && !Surface->IsBounded())
			{
				PCGE_LOG(Warning, GraphAndLog, LOCTEXT("UnboundedSurface", "A surface has no bounds of its own: connect a Bounding Shape to sample it."));
			}
			else
			{
				Job->Prepare(Context, Domain);
			}
		}

		Context->Jobs.Add(Job);
	}

	if (Context->Jobs.IsEmpty())
	{
		PCGEX_LOG_MISSING_INPUT(Context, LOCTEXT("NoSurface", "No surface to sample."))
		return false;
	}

	return true;
}

bool FPCGExDensitySurfaceSamplerElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExDensitySurfaceSamplerElement::AdvanceWork);

	PCGEX_CONTEXT_AND_SETTINGS(DensitySurfaceSampler)
	PCGEX_EXECUTION_CHECK

	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->SetState(PCGExDensitySurfaceSampler::State_PrimingSurfaces);

		// In editor, landscape cache entries are only created on the game thread: the surface schedules that, we wait.
		TArray<FPCGTaskId> PrimingTasks;
		for (const TSharedPtr<PCGExDensitySampling::FSurfaceJob>& Job : Context->Jobs)
		{
			if (Job->IsPrepared())
			{
				PrimingTasks.Append(Job->Surface->PrepareForSpatialQuery(Context, Job->GetQueryBounds()));
			}
		}

		if (!PrimingTasks.IsEmpty())
		{
			Context->DynamicDependencies.Append(PrimingTasks);
			Context->bIsPaused = true;
			return false;
		}
	}

	PCGEX_ON_STATE(PCGExDensitySurfaceSampler::State_PrimingSurfaces)
	{
		Context->SetState(PCGExCommon::States::State_WaitingOnAsyncWork);

		bool bAnyPrepared = false;
		for (const TSharedPtr<PCGExDensitySampling::FSurfaceJob>& Job : Context->Jobs)
		{
			bAnyPrepared |= Job->IsPrepared();
		}

		// A group with no work never ends: only start one when a surface has something to sample.
		if (bAnyPrepared)
		{
			PCGEX_ASYNC_GROUP_CHKD_RET(Context->GetTaskManager(), SampleSurfaces, true)

			const TWeakPtr<FPCGContextHandle> WeakHandle = Context->GetWeakSelfHandle();
			for (int32 i = 0; i < Context->Jobs.Num(); i++)
			{
				if (!Context->Jobs[i]->IsPrepared())
				{
					continue;
				}

				SampleSurfaces->AddSimpleCallback([WeakHandle, Index = i]()
				{
					FPCGContext::FSharedContext<FPCGExDensitySurfaceSamplerContext> SharedContext(WeakHandle);
					FPCGExDensitySurfaceSamplerContext* SharedCtx = SharedContext.Get();
					if (!SharedCtx)
					{
						return;
					}

					SharedCtx->Jobs[Index]->Run(SharedCtx);
				});
			}

			SampleSurfaces->StartSimpleCallbacks();
		}
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExCommon::States::State_WaitingOnAsyncWork)
	{
		TSet<FName> MissingAttributes;
		for (const TSharedPtr<PCGExDensitySampling::FSurfaceJob>& Job : Context->Jobs)
		{
			MissingAttributes.Append(Job->MissingAttributes);
			(void)Job->Output->StageOutput(Context);
		}

		if (!MissingAttributes.IsEmpty() && !Context->bQuietMissingAttributeError)
		{
			TArray<FString> Names;
			for (const FName& Name : MissingAttributes)
			{
				Names.Add(Name.ToString());
			}

			PCGE_LOG(Warning, GraphAndLog, FText::Format(LOCTEXT("MissingAttributes", "Not found on the surface, or not numeric (read as 0): {0}"), FText::FromString(FString::Join(Names, TEXT(", ")))));
		}

		Context->Done();
	}

	return Context->TryComplete();
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
