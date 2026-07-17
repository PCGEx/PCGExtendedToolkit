// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSampleSurfaceGuided.h"

#include "Async/ParallelFor.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExTexCommon.h"
#include "Core/PCGExTexParamFactoryProvider.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPrimitiveData.h"
#include "Data/External/PCGExMesh.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Details/PCGExSettingsDetails.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Sampling/PCGExSamplingHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExSampleSurfaceGuidedElement"
#define PCGEX_NAMESPACE SampleSurfaceGuided

UPCGExSampleSurfaceGuidedSettings::UPCGExSampleSurfaceGuidedSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Origin.Update(TEXT("$Position"));
}

#if WITH_EDITOR
void UPCGExSampleSurfaceGuidedSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	InOutNode->RenameInputPin(PCGPinConstants::DefaultInputLabel, PCGExSampling::Labels::SourceSourceLabel);

	PCGEX_IF_VERSION_LOWER(1, 74, 3)
	{
		// Rewire Distance
		PCGEX_SHORTHAND_RENAME_PIN(LocalMaxDistance, MaxDistance, Distance)
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExSampleSurfaceGuidedSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 74, 3)
	{
		Distance.Update(DistanceInput_DEPRECATED == EPCGExTraceSampleDistanceInput::Constant ? EPCGExInputValueType::Constant : EPCGExInputValueType::Attribute,
		                LocalMaxDistance_DEPRECATED, MaxDistance_DEPRECATED);
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

FName UPCGExSampleSurfaceGuidedSettings::GetMainInputPin() const
{
	return PCGExSampling::Labels::SourceSourceLabel;
}

TArray<FPCGPinProperties> UPCGExSampleSurfaceGuidedSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	if (SurfaceSource == EPCGExSurfaceSource::ActorReferences)
	{
		PCGEX_PIN_POINT(PCGExSampling::Labels::SourceActorReferencesLabel, "Points with actor reference paths.", Required)
	}
	if (SurfaceSource == EPCGExSurfaceSource::Primitives)
	{
		PCGEX_PIN_PRIMITIVES(FName("Primitives"), TEXT("Primitive data to test against."), Required)
	}
	if (bWriteRenderMat && bExtractTextureParameters)
	{
		PCGEX_PIN_FACTORIES(PCGExTexture::SourceTexLabel, "External texture params definitions.", Required, FPCGExDataTypeInfoTexParam::AsId())
	}
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(SampleSurfaceGuided)

PCGExData::EIOInit UPCGExSampleSurfaceGuidedSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(SampleSurfaceGuided)

bool FPCGExSampleSurfaceGuidedElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(SampleSurfaceGuided)

	PCGEX_FWD(ApplySampling)
	Context->ApplySampling.Init();

	PCGEX_FOREACH_FIELD_SURFACEGUIDED(PCGEX_OUTPUT_VALIDATE_NAME)

	if (Settings->bWriteRenderMat && Settings->bExtractTextureParameters)
	{
		Context->bExtractTextureParams = true;

		if (!PCGExFactories::GetInputFactories(InContext, PCGExTexture::SourceTexLabel, Context->TexParamsFactories, {FPCGExDataTypeInfoTexParam::AsId()}))
		{
			return false;
		}

		for (const TObjectPtr<const UPCGExTexParamFactoryData>& Factory : Context->TexParamsFactories)
		{
			PCGEX_VALIDATE_NAME_C(InContext, Factory->Config.TextureIDAttributeName)
		}
	}

	Context->bUseInclude = Settings->SurfaceSource != EPCGExSurfaceSource::All;
	if (Settings->SurfaceSource == EPCGExSurfaceSource::ActorReferences)
	{
		PCGEX_VALIDATE_NAME_CONSUMABLE(Settings->ActorReference)
		Context->ActorReferenceDataFacade = PCGExData::TryGetSingleFacade(Context, PCGExSampling::Labels::SourceActorReferencesLabel, false, true);
		if (!Context->ActorReferenceDataFacade)
		{
			return false;
		}

		if (!PCGExSampling::Helpers::GetIncludedActors(Context, Context->ActorReferenceDataFacade.ToSharedRef(), Settings->ActorReference, Context->IncludedActors))
		{
			return false;
		}
	}
	else if (Settings->SurfaceSource == EPCGExSurfaceSource::Primitives)
	{
		const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(FName("Primitives"));
		for (const FPCGTaggedData& TaggedData : Inputs)
		{
			const UPCGPrimitiveData* PrimitiveData = Cast<UPCGPrimitiveData>(TaggedData.Data);
			if (!PrimitiveData)
			{
				continue;
			}

			UPrimitiveComponent* Component = PrimitiveData->GetComponent().Get();
			if (!IsValid(Component))
			{
				continue;
			}

			Context->IncludedPrimitives.Add(Component);
		}

		if (Context->IncludedPrimitives.IsEmpty())
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("Missing primitive data."));
			return false;
		}
	}

	Context->bSupportsUVQuery = UPhysicsSettings::Get()->bSupportUVFromHitResults;
	if (Settings->bWriteUVCoords && !Context->bSupportsUVQuery)
	{
		if (!Settings->bQuietUVSettingsWarning)
		{
			PCGE_LOG(Warning, GraphAndLog, FTEXT("`Project Settings->Physics->Support UV From Hit Results' must be set to true for UV Query to work."));
		}
		Context->bWriteUVCoords = false;
	}

	Context->CollisionSettings = Settings->CollisionSettings;
	Context->CollisionSettings.Init(Context);

	return true;
}

bool FPCGExSampleSurfaceGuidedElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSampleSurfaceGuidedElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(SampleSurfaceGuided)
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
				if (Settings->bPruneFailedSamples)
				{
					NewBatch->bRequiresWriteStep = true;
				}
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to sample."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExSampleSurfaceGuided
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExSampleSurfaceGuided::Process);

		SurfacesForward = Context->bUseInclude ? Settings->AttributesForwarding.TryGetHandler(Context->ActorReferenceDataFacade, PointDataFacade, false) : nullptr;

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		// Allocate edge native properties

		EPCGPointNativeProperties AllocateFor = EPCGPointNativeProperties::None;

		if (Settings->bWriteVertexColor)
		{
			AllocateFor |= EPCGPointNativeProperties::Color;
		}
		if (Context->ApplySampling.WantsApply())
		{
			AllocateFor |= EPCGPointNativeProperties::Transform;
		}

		PointDataFacade->GetOut()->AllocateProperties(AllocateFor);

		SamplingMask.SetNumUninitialized(PointDataFacade->GetNum());

		CrossAxis = Settings->CrossAxis.GetValueSetting();
		if (!CrossAxis->Init(PointDataFacade))
		{
			return false;
		}

		OriginGetter = PointDataFacade->GetBroadcaster<FVector>(Settings->Origin, true);
		if (!OriginGetter)
		{
			PCGE_LOG_C(Error, GraphAndLog, ExecutionContext, FTEXT("Some inputs don't have the required Origin data."));
			return false;
		}

		DirectionGetter = PointDataFacade->GetBroadcaster<FVector>(Settings->Direction, true);
		if (!DirectionGetter)
		{
			PCGE_LOG_C(Error, GraphAndLog, ExecutionContext, FTEXT("Some inputs don't have the required Direction data."));
			return false;
		}

		{
			const TSharedRef<PCGExData::FFacade>& OutputFacade = PointDataFacade;
			PCGEX_FOREACH_FIELD_SURFACEGUIDED(PCGEX_OUTPUT_INIT)
		}

		// So texture params are registered last, otherwise they're first in the list and it's confusing
		TexParamLookup = MakeShared<PCGExTexture::FLookup>();
		if (!TexParamLookup->BuildFrom(Context->TexParamsFactories))
		{
			TexParamLookup.Reset();
		}
		else
		{
			TexParamLookup->PrepareForWrite(Context, PointDataFacade);
		}

		DistanceGetter = Settings->Distance.GetValueSetting();
		if (!DistanceGetter->Init(PointDataFacade))
		{
			return false;
		}

		if (Context->CollisionSettings.TraceMode == EPCGExTraceMode::Sphere)
		{
			SphereRadiusGetter = Context->CollisionSettings.SphereRadius.GetValueSetting();
			if (!SphereRadiusGetter->Init(PointDataFacade))
			{
				return false;
			}
		}
		else if (Context->CollisionSettings.TraceMode == EPCGExTraceMode::Box)
		{
			BoxHalfExtentsGetter = Context->CollisionSettings.BoxHalfExtents.GetValueSetting();
			if (!BoxHalfExtentsGetter->Init(PointDataFacade))
			{
				return false;
			}
		}

		World = Context->GetWorld();
		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
		TProcessor<FPCGExSampleSurfaceGuidedContext, UPCGExSampleSurfaceGuidedSettings>::PrepareLoopScopesForPoints(Loops);
		MaxDistanceValue = MakeShared<PCGExMT::TScopedNumericValue<double>>(Loops, 0);
		if (Settings->bWriteVertexColor)
		{
			FaceIndex.Init(-1, PointDataFacade->GetNum());
			MeshIndex.Init(-1, PointDataFacade->GetNum());
			HitLocation.Init(FVector::ZeroVector, PointDataFacade->GetNum());
			ScopedMeshes = MakeShared<PCGExMT::TScopedArray<const UStaticMesh*>>(Loops, nullptr);
		}
	}

	void FProcessor::ProcessTraceResult(const PCGExMT::FScope& Scope, const FHitResult& HitResult, const int32 Index, const FVector& Origin, const FVector& Direction, PCGExData::FMutablePoint& MutablePoint)
	{
		const FVector Impact = HitResult.ImpactPoint;

		const double HitDistance = (Impact - Origin).Size();
		PCGEX_OUTPUT_VALUE(Location, Index, Impact)
		PCGEX_OUTPUT_VALUE(LookAt, Index, Direction)
		PCGEX_OUTPUT_VALUE(Normal, Index, HitResult.ImpactNormal)
		PCGEX_OUTPUT_VALUE(Distance, Index, HitDistance)
		PCGEX_OUTPUT_VALUE(IsInside, Index, FVector::DotProduct(Direction, HitResult.Normal) > 0)
		PCGEX_OUTPUT_VALUE(Success, Index, true)

		SamplingMask[Index] = true;

		{
			const double Prev = MaxDistanceValue->Get(Scope);
			if (HitDistance > Prev)
			{
				MaxDistanceValue->Set(Scope, HitDistance);
			}
		}

		if (Context->ApplySampling.WantsApply())
		{
			const FVector Cross = CrossAxis->Read(Index) * (Settings->CrossAxis.bFlip ? 1 : -1);
			const FQuat Rot = PCGExMath::MakeRot(Settings->RotationConstruction, HitResult.ImpactNormal, Cross);
			const FTransform OutTransform(Rot, Impact, FVector::OneVector);
			Context->ApplySampling.Apply(MutablePoint, OutTransform, OutTransform);
		}

		if (Settings->bWriteUVCoords)
		{
			FVector2D UVCoords;
			if (!UGameplayStatics::FindCollisionUV(HitResult, Settings->UVChannel, UVCoords))
			{
				UVCoords = FVector2D::ZeroVector;
			}

			PCGEX_OUTPUT_VALUE(UVCoords, Index, UVCoords)
		}

		PCGEX_OUTPUT_VALUE(FaceIndex, Index, HitResult.FaceIndex)

		const AActor* HitActor = HitResult.GetActor();
		const int32* HitIndex = nullptr;

		if (HitActor)
		{
			HitIndex = Context->IncludedActors.Find(HitActor);
			PCGEX_OUTPUT_VALUE(ActorReference, Index, FSoftObjectPath(HitActor->GetPathName()))
		}

		if (const UPhysicalMaterial* PhysMat = HitResult.PhysMaterial.Get())
		{
			PCGEX_OUTPUT_VALUE(PhysMat, Index, FSoftObjectPath(PhysMat->GetPathName()))
		}

		if (const UPrimitiveComponent* HitComponent = HitResult.GetComponent())
		{
			PCGEX_OUTPUT_VALUE(HitComponentReference, Index, FSoftObjectPath(HitComponent->GetPathName()))

			UMaterialInterface* RenderMat = HitComponent->GetMaterial(Settings->RenderMaterialIndex);
			PCGEX_OUTPUT_VALUE(RenderMat, Index, FSoftObjectPath(RenderMat ? RenderMat->GetPathName() : TEXT("")))

			if (TexParamLookup)
			{
				TexParamLookup->ExtractParams(Index, RenderMat);
			}

			if (ScopedMeshes)
			{
				const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(HitComponent);

				if (StaticMeshComponent)
				{
					HitLocation[Index] = Impact;
					FaceIndex[Index] = HitResult.FaceIndex;
					ScopedMeshes->Get_Ref(Scope)[Index - Scope.Start] = StaticMeshComponent->GetStaticMesh();
				}
			}
		}

		if (SurfacesForward && HitIndex)
		{
			SurfacesForward->Forward(*HitIndex, Index);
		}

		FPlatformAtomics::InterlockedExchange(&bAnySuccess, 1);
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::SampleSurfaceGuided::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		double DirMult = Settings->bInvertDirection ? -1 : 1;

		PCGEX_SCOPE_LOOP(Index)
		{
			const FVector Direction = DirectionGetter->Read(Index).GetSafeNormal() * DirMult;
			const FVector Origin = OriginGetter->Read(Index);
			const double MaxDistance = DistanceGetter->Read(Index);

			PCGExData::FMutablePoint MutablePoint = PointDataFacade->GetOutPoint(Index);

			auto SamplingFailed = [&]()
			{
				SamplingMask[Index] = false;

				PCGEX_OUTPUT_VALUE(Location, Index, InTransforms[Index].GetLocation())
				PCGEX_OUTPUT_VALUE(Normal, Index, Direction*-1)
				PCGEX_OUTPUT_VALUE(LookAt, Index, Direction)
				PCGEX_OUTPUT_VALUE(Distance, Index, MaxDistance)
				//PCGEX_OUTPUT_VALUE(UVCoords, Index, FVector2D::ZeroVector)
				//PCGEX_OUTPUT_VALUE(FaceIndex, Index, -1)
				//PCGEX_OUTPUT_VALUE(RenderMat, Index, -1)
				//PCGEX_OUTPUT_VALUE(IsInside, Index, false)
				//PCGEX_OUTPUT_VALUE(Success, Index, false)
				//PCGEX_OUTPUT_VALUE(ActorReference, Index, TEXT(""))
				//PCGEX_OUTPUT_VALUE(PhysMat, Index, TEXT(""))
				if (TexParamLookup)
				{
					TexParamLookup->ExtractParams(Index, nullptr);
				}
			};


			if (!PointFilterCache[Index])
			{
				if (Settings->bProcessFilteredOutAsFails)
				{
					SamplingFailed();
				}
				continue;
			}

			FCollisionQueryParams CollisionParams;
			Context->CollisionSettings.Update(CollisionParams);
			CollisionParams.bReturnPhysicalMaterial = Settings->bWritePhysMat;
			CollisionParams.bReturnFaceIndex = Settings->bWriteFaceIndex || Settings->bWriteUVCoords || Settings->bWriteVertexColor;

			const FVector Trace = Direction * MaxDistance;
			const FVector End = Origin + Trace;

			bool bSuccess = false;
			FHitResult HitResult;
			TArray<FHitResult> HitResults;

			if (Settings->SurfaceSource == EPCGExSurfaceSource::Primitives)
			{
				double ClosestDistSq = TNumericLimits<double>::Max();

				for (UPrimitiveComponent* Primitive : Context->IncludedPrimitives)
				{
					if (!IsValid(Primitive))
					{
						continue;
					}

					FHitResult TempHit;
					bool bHit = false;

					switch (Context->CollisionSettings.TraceMode)
					{
					case EPCGExTraceMode::Line:
						bHit = Primitive->LineTraceComponent(TempHit, Origin, End, CollisionParams);
						break;
					case EPCGExTraceMode::Sphere:
					{
						const double Radius = SphereRadiusGetter->Read(Index);
						const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
						bHit = Primitive->SweepComponent(TempHit, Origin, End, FQuat::Identity, Shape, CollisionParams.bTraceComplex);
					}
					break;
					case EPCGExTraceMode::Box:
					{
						const FVector HalfExtents = BoxHalfExtentsGetter->Read(Index);
						const FCollisionShape Shape = FCollisionShape::MakeBox(HalfExtents);
						bHit = Primitive->SweepComponent(TempHit, Origin, End, FQuat::Identity, Shape, CollisionParams.bTraceComplex);
					}
					break;
					}

					if (bHit)
					{
						const double DistSq = FVector::DistSquared(Origin, TempHit.ImpactPoint);
						if (DistSq < ClosestDistSq)
						{
							ClosestDistSq = DistSq;
							HitResult = TempHit;
							bSuccess = true;
						}
					}
				}

				if (bSuccess)
				{
					ProcessTraceResult(Scope, HitResult, Index, Origin, Direction, MutablePoint);
				}
				if (!bSuccess)
				{
					SamplingFailed();
				}
				continue;
			}

			auto ProcessMultipleTraceResult = [&]()
			{
				for (const FHitResult& Hit : HitResults)
				{
					if (Context->IncludedActors.Contains(Hit.GetActor()))
					{
						HitResult = Hit;
						ProcessTraceResult(Scope, HitResult, Index, Origin, Direction, MutablePoint);
						bSuccess = true;
						return;
					}
				}
			};

			switch (Context->CollisionSettings.CollisionType)
			{
			case EPCGExCollisionFilterType::Channel:
				if (Context->bUseInclude)
				{
					bool bHit = false;
					switch (Context->CollisionSettings.TraceMode)
					{
					case EPCGExTraceMode::Line:
						bHit = World->LineTraceMultiByChannel(HitResults, Origin, End, Context->CollisionSettings.CollisionChannel, CollisionParams);
						break;
					case EPCGExTraceMode::Sphere:
					{
						const double Radius = SphereRadiusGetter->Read(Index);
						const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
						bHit = World->SweepMultiByChannel(HitResults, Origin, End, FQuat::Identity, Context->CollisionSettings.CollisionChannel, Shape, CollisionParams);
					}
					break;
					case EPCGExTraceMode::Box:
					{
						const FVector HalfExtents = BoxHalfExtentsGetter->Read(Index);
						const FCollisionShape Shape = FCollisionShape::MakeBox(HalfExtents);
						bHit = World->SweepMultiByChannel(HitResults, Origin, End, FQuat::Identity, Context->CollisionSettings.CollisionChannel, Shape, CollisionParams);
					}
					break;
					}
					if (bHit)
					{
						ProcessMultipleTraceResult();
					}
				}
				else
				{
					bool bHit = false;
					switch (Context->CollisionSettings.TraceMode)
					{
					case EPCGExTraceMode::Line:
						bHit = World->LineTraceSingleByChannel(HitResult, Origin, End, Context->CollisionSettings.CollisionChannel, CollisionParams);
						break;
					case EPCGExTraceMode::Sphere:
					{
						const double Radius = SphereRadiusGetter->Read(Index);
						const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
						bHit = World->SweepSingleByChannel(HitResult, Origin, End, FQuat::Identity, Context->CollisionSettings.CollisionChannel, Shape, CollisionParams);
					}
					break;
					case EPCGExTraceMode::Box:
					{
						const FVector HalfExtents = BoxHalfExtentsGetter->Read(Index);
						const FCollisionShape Shape = FCollisionShape::MakeBox(HalfExtents);
						bHit = World->SweepSingleByChannel(HitResult, Origin, End, FQuat::Identity, Context->CollisionSettings.CollisionChannel, Shape, CollisionParams);
					}
					break;
					}
					if (bHit)
					{
						ProcessTraceResult(Scope, HitResult, Index, Origin, Direction, MutablePoint);
						bSuccess = true;
					}
				}
				break;
			case EPCGExCollisionFilterType::ObjectType:
				if (Context->bUseInclude)
				{
					bool bHit = false;
					switch (Context->CollisionSettings.TraceMode)
					{
					case EPCGExTraceMode::Line:
						bHit = World->LineTraceMultiByObjectType(HitResults, Origin, End, FCollisionObjectQueryParams(Context->CollisionSettings.CollisionObjectType), CollisionParams);
						break;
					case EPCGExTraceMode::Sphere:
					{
						const double Radius = SphereRadiusGetter->Read(Index);
						const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
						bHit = World->SweepMultiByObjectType(HitResults, Origin, End, FQuat::Identity, FCollisionObjectQueryParams(Context->CollisionSettings.CollisionObjectType), Shape, CollisionParams);
					}
					break;
					case EPCGExTraceMode::Box:
					{
						const FVector HalfExtents = BoxHalfExtentsGetter->Read(Index);
						const FCollisionShape Shape = FCollisionShape::MakeBox(HalfExtents);
						bHit = World->SweepMultiByObjectType(HitResults, Origin, End, FQuat::Identity, FCollisionObjectQueryParams(Context->CollisionSettings.CollisionObjectType), Shape, CollisionParams);
					}
					break;
					}
					if (bHit)
					{
						ProcessMultipleTraceResult();
					}
				}
				else
				{
					bool bHit = false;
					switch (Context->CollisionSettings.TraceMode)
					{
					case EPCGExTraceMode::Line:
						bHit = World->LineTraceSingleByObjectType(HitResult, Origin, End, FCollisionObjectQueryParams(Context->CollisionSettings.CollisionObjectType), CollisionParams);
						break;
					case EPCGExTraceMode::Sphere:
					{
						const double Radius = SphereRadiusGetter->Read(Index);
						const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
						bHit = World->SweepSingleByObjectType(HitResult, Origin, End, FQuat::Identity, FCollisionObjectQueryParams(Context->CollisionSettings.CollisionObjectType), Shape, CollisionParams);
					}
					break;
					case EPCGExTraceMode::Box:
					{
						const FVector HalfExtents = BoxHalfExtentsGetter->Read(Index);
						const FCollisionShape Shape = FCollisionShape::MakeBox(HalfExtents);
						bHit = World->SweepSingleByObjectType(HitResult, Origin, End, FQuat::Identity, FCollisionObjectQueryParams(Context->CollisionSettings.CollisionObjectType), Shape, CollisionParams);
					}
					break;
					}
					if (bHit)
					{
						ProcessTraceResult(Scope, HitResult, Index, Origin, Direction, MutablePoint);
						bSuccess = true;
					}
				}
				break;
			case EPCGExCollisionFilterType::Profile:
				if (Context->bUseInclude)
				{
					bool bHit = false;
					switch (Context->CollisionSettings.TraceMode)
					{
					case EPCGExTraceMode::Line:
						bHit = World->LineTraceMultiByProfile(HitResults, Origin, End, Context->CollisionSettings.CollisionProfileName, CollisionParams);
						break;
					case EPCGExTraceMode::Sphere:
					{
						const double Radius = SphereRadiusGetter->Read(Index);
						const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
						bHit = World->SweepMultiByProfile(HitResults, Origin, End, FQuat::Identity, Context->CollisionSettings.CollisionProfileName, Shape, CollisionParams);
					}
					break;
					case EPCGExTraceMode::Box:
					{
						const FVector HalfExtents = BoxHalfExtentsGetter->Read(Index);
						const FCollisionShape Shape = FCollisionShape::MakeBox(HalfExtents);
						bHit = World->SweepMultiByProfile(HitResults, Origin, End, FQuat::Identity, Context->CollisionSettings.CollisionProfileName, Shape, CollisionParams);
					}
					break;
					}
					if (bHit)
					{
						ProcessMultipleTraceResult();
					}
				}
				else
				{
					bool bHit = false;
					switch (Context->CollisionSettings.TraceMode)
					{
					case EPCGExTraceMode::Line:
						bHit = World->LineTraceSingleByProfile(HitResult, Origin, End, Context->CollisionSettings.CollisionProfileName, CollisionParams);
						break;
					case EPCGExTraceMode::Sphere:
					{
						const double Radius = SphereRadiusGetter->Read(Index);
						const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
						bHit = World->SweepSingleByProfile(HitResult, Origin, End, FQuat::Identity, Context->CollisionSettings.CollisionProfileName, Shape, CollisionParams);
					}
					break;
					case EPCGExTraceMode::Box:
					{
						const FVector HalfExtents = BoxHalfExtentsGetter->Read(Index);
						const FCollisionShape Shape = FCollisionShape::MakeBox(HalfExtents);
						bHit = World->SweepSingleByProfile(HitResult, Origin, End, FQuat::Identity, Context->CollisionSettings.CollisionProfileName, Shape, CollisionParams);
					}
					break;
					}
					if (bHit)
					{
						ProcessTraceResult(Scope, HitResult, Index, Origin, Direction, MutablePoint);
						bSuccess = true;
					}
				}
				break;
			default:
				break;
			}

			if (!bSuccess)
			{
				SamplingFailed();
			}
		}
	}

	void FProcessor::GetVertexColorAtHit(const int32 Index, FVector4& OutColor) const
	{
		const int32 MIndex = MeshIndex[Index];
		const int32 FIndex = FaceIndex[Index];

		if (MIndex < 0 || FIndex < 0)
		{
			return;
		}

		const PCGExMesh::FMeshData& Data = MeshData[MIndex];

		if (!Data.HasColor())
		{
			return;
		}

		const int32 Index0 = Data.Indices[FIndex * 3 + 0];
		const int32 Index1 = Data.Indices[FIndex * 3 + 1];
		const int32 Index2 = Data.Indices[FIndex * 3 + 2];

		const FVector BaryCoords = FMath::ComputeBaryCentric2D(
			HitLocation[Index],
			FVector(Data.Positions->VertexPosition(Index0)),
			FVector(Data.Positions->VertexPosition(Index1)),
			FVector(Data.Positions->VertexPosition(Index2)));

		FLinearColor LinearColor =
			FLinearColor(Data.Colors->VertexColor(Index0)) * BaryCoords.X
			+ FLinearColor(Data.Colors->VertexColor(Index1)) * BaryCoords.Y
			+ FLinearColor(Data.Colors->VertexColor(Index2)) * BaryCoords.Z;

		OutColor = LinearColor;
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		if (ScopedMeshes)
		{
			TMap<const UStaticMesh*, int32> StaticMeshIndexMap;

			StaticMeshIndexMap.Reserve(100);
			MeshData.Reserve(100);

			int32 j = 0;
			ScopedMeshes->ForEach([&](const TArray<const UStaticMesh*>& Meshes)
			{
				const int32 Count = Meshes.Num();
				for (int32 i = 0; i < Count; i++)
				{
					const UStaticMesh* Mesh = Meshes[i];
					if (!Mesh)
					{
						j++;
						continue;
					}

					int32& IndexRef = StaticMeshIndexMap.FindOrAdd(Mesh, -1);
					if (IndexRef == -1)
					{
						PCGExMesh::FMeshData Data(Mesh);
						if (Data.bIsValid)
						{
							IndexRef = MeshData.Add(Data);
						}
					}

					MeshIndex[j] = IndexRef;
					j++;
				}
			});

			ScopedMeshes.Reset();
			StaticMeshIndexMap.Empty();
			TPCGValueRange<FVector4> OutColors = PointDataFacade->GetOut()->GetColorValueRange(false);
			ParallelFor(FaceIndex.Num(), [&](int32 i)
			{
				GetVertexColorAtHit(i, OutColors[i]);
			});
		}

		if (!Settings->bOutputNormalizedDistance || !DistanceWriter)
		{
			return;
		}

		const int32 NumPoints = PointDataFacade->GetNum();
		MaxSampledDistance = MaxDistanceValue->Max();

		if (Settings->bOutputOneMinusDistance)
		{
			const double InvMaxDist = 1.0 / MaxSampledDistance;
			const double Scale = Settings->DistanceScale;

			for (int i = 0; i < NumPoints; i++)
			{
				const double D = DistanceWriter->GetValue(i);
				DistanceWriter->SetValue(i, (1.0 - D * InvMaxDist) * Scale);
			}
		}
		else
		{
			const double Scale = (1.0 / MaxSampledDistance) * Settings->DistanceScale;

			for (int i = 0; i < NumPoints; i++)
			{
				const double D = DistanceWriter->GetValue(i);
				DistanceWriter->SetValue(i, D * Scale);
			}
		}
	}

	void FProcessor::CompleteWork()
	{
		PointDataFacade->WriteFastest(TaskManager);

		if (Settings->bTagIfHasSuccesses && bAnySuccess)
		{
			PointDataFacade->Source->Tags->AddRaw(Settings->HasSuccessesTag);
		}
		if (Settings->bTagIfHasNoSuccesses && !bAnySuccess)
		{
			PointDataFacade->Source->Tags->AddRaw(Settings->HasNoSuccessesTag);
		}
	}

	void FProcessor::Write()
	{
		if (Settings->bPruneFailedSamples)
		{
			(void)PointDataFacade->Source->Gather(SamplingMask);
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
