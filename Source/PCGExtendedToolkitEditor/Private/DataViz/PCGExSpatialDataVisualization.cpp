// Copyright 2026 Timothé Lapetite and contributors
// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataViz/PCGExSpatialDataVisualization.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGData.h"
#include "PCGDebug.h"
#include "PCGModule.h"
#include "PCGPoint.h"
#include "PCGSettings.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSpatialData.h"
#include "DataVisualizations/PCGDataVisualizationHelpers.h"
#include "Helpers/PCGActorHelpers.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGCustomAccessor.h"

#include "AdvancedPreviewScene.h"
#include "EditorViewportClient.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "PCGExSpatialDataVisualization"

void IPCGExSpatialDataVisualization::ExecuteDebugDisplayHelper(
	const UPCGData* Data,
	const FPCGDebugVisualizationSettings& DebugSettings,
	FPCGContext* Context,
	AActor* TargetActor,
	const FPCGCrc& Crc,
	const TFunction<void(UInstancedStaticMeshComponent*)>& OnISMCCreatedCallback) const
{
	UStaticMesh* Mesh = DebugSettings.PointMesh.LoadSynchronous();

	if (!Mesh)
	{
		PCGE_LOG_C(Error, GraphAndLog, Context, FText::Format(LOCTEXT("UnableToLoadMesh", "Debug display was unable to load mesh '{0}'."),
			           FText::FromString(DebugSettings.PointMesh.ToString())));
		return;
	}

	TArray<TSoftObjectPtr<UMaterialInterface>> Materials;
	Materials.Add(DebugSettings.GetMaterial());

	const UPCGBasePointData* PointData = CollapseToDebugBasePointData(Context, Data);

	if (!PointData)
	{
		return;
	}

	if (PointData->IsEmpty())
	{
		return;
	}

	constexpr int NumCustomData = 8;
	const int32 NumPoints = PointData->GetNumPoints();

	TArray<FTransform> ForwardInstances;
	TArray<FTransform> ReverseInstances;
	TArray<float> InstanceCustomData;

	ForwardInstances.Reserve(NumPoints);
	InstanceCustomData.Reserve(NumCustomData);

	// First, create target instance transforms
	const float PointScale = DebugSettings.PointScale;
	const bool bIsAbsolute = DebugSettings.ScaleMethod == EPCGDebugVisScaleMethod::Absolute;
	const bool bIsRelative = DebugSettings.ScaleMethod == EPCGDebugVisScaleMethod::Relative;
	const bool bScaleWithExtents = DebugSettings.ScaleMethod == EPCGDebugVisScaleMethod::Extents;
	const FVector MeshExtents = Mesh->GetBoundingBox().GetExtent();

	/* Note: A re-used ISMC may have any number of pre-existing instances, so this won't prevent going over the max. But,
	 * the renderer is robust to over-instancing attempts and will not crash. However, MAX_INSTANCE_ID still serves as a
	 * good, scalable heuristic for a max limit.
	 */
	const int32 NumDesiredInstances = FMath::Min(NumPoints, static_cast<int32>(MAX_INSTANCE_ID));
	if (NumDesiredInstances != NumPoints)
	{
		PCGE_LOG_C(Error, GraphAndLog, Context, FText::Format(LOCTEXT( "DebugPointsOverLimit", "Debug point display ({0}) surpassed the max instance limit ({1}) and will be clamped."), FText::AsNumber(NumPoints), FText::AsNumber(MAX_INSTANCE_ID)));
	}

	const FConstPCGPointValueRanges ValueRanges(PointData);
	bool bFoundNonNormalizedInstances = false;

	for (int i = 0; i < NumDesiredInstances; ++i)
	{
		TArray<FTransform>& Instances = ((bIsAbsolute || ValueRanges.TransformRange[i].GetDeterminant() >= 0) ? ForwardInstances : ReverseInstances);
		FTransform& InstanceTransform = Instances.Add_GetRef(ValueRanges.TransformRange[i]);
		if (bIsRelative)
		{
			InstanceTransform.SetScale3D(InstanceTransform.GetScale3D() * PointScale);
		}
		else if (bScaleWithExtents)
		{
			const FVector Extents = PCGPointHelpers::GetExtents(ValueRanges.BoundsMinRange[i], ValueRanges.BoundsMaxRange[i]);
			const FVector LocalCenter = PCGPointHelpers::GetLocalCenter(ValueRanges.BoundsMinRange[i], ValueRanges.BoundsMaxRange[i]);

			const FVector ScaleWithExtents = Extents / MeshExtents;
			const FVector TransformedBoxCenterWithOffset = InstanceTransform.TransformPosition(LocalCenter) - InstanceTransform.GetLocation();
			InstanceTransform.SetTranslation(InstanceTransform.GetTranslation() + TransformedBoxCenterWithOffset);
			InstanceTransform.SetScale3D(InstanceTransform.GetScale3D() * ScaleWithExtents);
		}
		else // absolute scaling only
		{
			InstanceTransform.SetScale3D(FVector(PointScale));
		}

		// If any instances have non-normalized rotations, normalize them to avoid crashing, but emit a warning.
		if (!InstanceTransform.IsRotationNormalized())
		{
			InstanceTransform.NormalizeRotation();
			bFoundNonNormalizedInstances = true;
		}
	}

	if (bFoundNonNormalizedInstances)
	{
		PCGE_LOG_C(Warning, GraphAndLog, Context, LOCTEXT("UnnormalizedRotation", "PCGSpatialDataVisualization: Encountered one or more transforms with unnormalized rotation. Rotations will be normalized for visualization."));
	}

	FPCGISMComponentBuilderParams Params[2];
	Params[0].SettingsCrc = Crc;
	Params[0].bTransient = false;
	Params[0].NumCustomDataFloats = NumCustomData;
	Params[0].Descriptor.StaticMesh = Mesh;
	Params[0].Descriptor.OverrideMaterials = Materials;
	Params[0].Descriptor.Mobility = EComponentMobility::Static;
	Params[0].Descriptor.BodyInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	// No distance culling.
	Params[0].Descriptor.InstanceStartCullDistance = Params[0].Descriptor.InstanceEndCullDistance = 0;
	// Additional performance switches
	Params[0].Descriptor.bAffectDistanceFieldLighting = false;
	Params[0].Descriptor.bAffectDynamicIndirectLighting = false;
	Params[0].Descriptor.bAffectDynamicIndirectLightingWhileHidden = false;
	Params[0].Descriptor.bCastContactShadow = false;
	Params[0].Descriptor.bCastDynamicShadow = false;
	Params[0].Descriptor.bCastShadow = false;
	Params[0].Descriptor.bCastStaticShadow = false;
	Params[0].Descriptor.bGenerateOverlapEvents = false;
	Params[0].Descriptor.bIncludeInHLOD = false;
	Params[0].Descriptor.bReceivesDecals = false;
	Params[0].Descriptor.bVisibleInRayTracing = false;

	// If the root actor we're binding to is movable, then the ISMC should be movable by default
	USceneComponent* SceneComponent = TargetActor ? TargetActor->GetRootComponent() : nullptr;
	if (SceneComponent)
	{
		Params[0].Descriptor.Mobility = SceneComponent->Mobility;
	}

	Params[1] = Params[0];
	Params[1].Descriptor.bReverseCulling = true;

	UPCGComponent* SourceComponent = Cast<UPCGComponent>(Context ? Context->ExecutionSource.Get() : nullptr);

	// Since the instance count is global, track the current instances applied and previously belong to the ISMCs.
	int32 NumCurrentInstances = 0;
	for (int32 Direction = 0; Direction < 2; ++Direction)
	{
		TArray<FTransform>& Instances = (Direction == 0 ? ForwardInstances : ReverseInstances);

		if (Instances.IsEmpty())
		{
			continue;
		}

		UInstancedStaticMeshComponent* ISMC = nullptr;

		if (TargetActor && SourceComponent)
		{
			ISMC = UPCGActorHelpers::GetOrCreateISMC(TargetActor, SourceComponent, Params[Direction], Context);
		}
		else
		{
			// If no target actor/source component were provided, create an ISMC directly instead.
			ISMC = NewObject<UInstancedStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);

			FISMComponentDescriptor Descriptor(Params[Direction].Descriptor);
			Descriptor.InitComponent(ISMC);
			ISMC->SetNumCustomDataFloats(Params[Direction].NumCustomDataFloats);
		}

		check(ISMC && ISMC->NumCustomDataFloats == NumCustomData);

		ISMC->ComponentTags.AddUnique(PCGHelpers::DefaultPCGDebugTag);
		const int32 PreExistingInstanceCount = ISMC->GetInstanceCount();
		NumCurrentInstances += PreExistingInstanceCount;

		// The renderer is robust to going over the instance count, so it's okay not to account for other scene instances here.
		if (NumCurrentInstances + Instances.Num() > MAX_INSTANCE_ID)
		{
			// Drop instances to stay at the max.
			// Account for less than 0 if, for example, the forward was over the limit and the reverse had less than the PreExisting.
			Instances.SetNum(FMath::Max(0, static_cast<int32>(MAX_INSTANCE_ID) - NumCurrentInstances));
			if (Instances.IsEmpty())
			{
				continue;
			}
		}

		NumCurrentInstances += Instances.Num();
		ISMC->AddInstances(Instances, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true);

		// Scan all points looking for points that match current direction and add their custom data.
		int32 PointCounter = 0;
		for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
		{
			const int32 PointDirection = ((bIsAbsolute || ValueRanges.TransformRange[PointIndex].GetDeterminant() >= 0) ? 0 : 1);
			if (PointDirection != Direction)
			{
				continue;
			}

			const FVector4& Color = ValueRanges.ColorRange[PointIndex];
			const FVector Extents = PCGPointHelpers::GetExtents(ValueRanges.BoundsMinRange[PointIndex], ValueRanges.BoundsMaxRange[PointIndex]);
			InstanceCustomData.Add(ValueRanges.DensityRange[PointIndex]);

			InstanceCustomData.Add(Extents[0]);
			InstanceCustomData.Add(Extents[1]);
			InstanceCustomData.Add(Extents[2]);
			InstanceCustomData.Add(Color[0]);
			InstanceCustomData.Add(Color[1]);
			InstanceCustomData.Add(Color[2]);
			InstanceCustomData.Add(Color[3]);

			ISMC->SetCustomData(PreExistingInstanceCount + PointCounter, InstanceCustomData);

			InstanceCustomData.Reset();

			++PointCounter;
		}

		ISMC->UpdateBounds();
		OnISMCCreatedCallback(ISMC);
	}
}

#undef LOCTEXT_NAMESPACE
