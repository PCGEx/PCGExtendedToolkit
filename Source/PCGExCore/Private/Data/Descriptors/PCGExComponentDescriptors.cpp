// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Descriptors/PCGExComponentDescriptors.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"

FPCGExPrimitiveComponentDescriptor::FPCGExPrimitiveComponentDescriptor()
	: FPCGExPrimitiveComponentDescriptor(NoInit)
{
	InitFrom(UPrimitiveComponent::StaticClass()->GetDefaultObject<UPrimitiveComponent>());
}

void FPCGExPrimitiveComponentDescriptor::InitFrom(const UPrimitiveComponent* Component, bool bInitBodyInstance)
{
	const UPrimitiveComponent* PrimitiveComponent = Component;

	MinDrawDistance = PrimitiveComponent->MinDrawDistance;
	LDMaxDrawDistance = PrimitiveComponent->LDMaxDrawDistance;
	IndirectLightingCacheQuality = PrimitiveComponent->IndirectLightingCacheQuality;
	LightmapType = PrimitiveComponent->GetLightmapType();
	HLODBatchingPolicy = PrimitiveComponent->HLODBatchingPolicy;
	bEnableAutoLODGeneration = PrimitiveComponent->bEnableAutoLODGeneration;
	bNeverDistanceCull = PrimitiveComponent->bNeverDistanceCull;

	if (bInitBodyInstance)
	{
		BodyInstance.CopyBodyInstancePropertiesFrom(PrimitiveComponent->GetBodyInstance());
	}
	else
	{
		BodyInstance.SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);
	}

	bAlwaysCreatePhysicsState = PrimitiveComponent->bAlwaysCreatePhysicsState;
	bMultiBodyOverlap = PrimitiveComponent->bMultiBodyOverlap;
	bTraceComplexOnMove = PrimitiveComponent->bTraceComplexOnMove;
	bReturnMaterialOnMove = PrimitiveComponent->bReturnMaterialOnMove;
	bAllowCullDistanceVolume = PrimitiveComponent->bAllowCullDistanceVolume;
	bVisibleInReflectionCaptures = PrimitiveComponent->bVisibleInReflectionCaptures;
	bVisibleInRealTimeSkyCaptures = PrimitiveComponent->bVisibleInRealTimeSkyCaptures;
	bVisibleInRayTracing = PrimitiveComponent->bVisibleInRayTracing;
	bRenderInMainPass = PrimitiveComponent->bRenderInMainPass;
	bRenderInDepthPass = PrimitiveComponent->bRenderInDepthPass;
	bReceivesDecals = PrimitiveComponent->bReceivesDecals;
	bHoldout = PrimitiveComponent->bHoldout;
	bOwnerNoSee = PrimitiveComponent->bOwnerNoSee;
	bOnlyOwnerSee = PrimitiveComponent->bOnlyOwnerSee;
	bTreatAsBackgroundForOcclusion = PrimitiveComponent->bTreatAsBackgroundForOcclusion;
	bUseAsOccluder = PrimitiveComponent->bUseAsOccluder;
	bForceMipStreaming = PrimitiveComponent->bForceMipStreaming;
	CastShadow = PrimitiveComponent->CastShadow;
	bEmissiveLightSource = PrimitiveComponent->bEmissiveLightSource;
	bAffectDynamicIndirectLighting = PrimitiveComponent->bAffectDynamicIndirectLighting;
	bAffectIndirectLightingWhileHidden = PrimitiveComponent->bAffectIndirectLightingWhileHidden;
	bAffectDistanceFieldLighting = PrimitiveComponent->bAffectDistanceFieldLighting;
	bCastDynamicShadow = PrimitiveComponent->bCastDynamicShadow;
	bCastStaticShadow = PrimitiveComponent->bCastStaticShadow;
	ShadowCacheInvalidationBehavior = PrimitiveComponent->ShadowCacheInvalidationBehavior;
	bCastVolumetricTranslucentShadow = PrimitiveComponent->bCastVolumetricTranslucentShadow;
	bCastContactShadow = PrimitiveComponent->bCastContactShadow;
	bSelfShadowOnly = PrimitiveComponent->bSelfShadowOnly;
	bCastFarShadow = PrimitiveComponent->bCastFarShadow;
	bCastInsetShadow = PrimitiveComponent->bCastInsetShadow;
	bCastCinematicShadow = PrimitiveComponent->bCastCinematicShadow;
	bCastHiddenShadow = PrimitiveComponent->bCastHiddenShadow;
	bCastShadowAsTwoSided = PrimitiveComponent->bCastShadowAsTwoSided;
	bLightAttachmentsAsGroup = PrimitiveComponent->bLightAttachmentsAsGroup;
	bExcludeFromLightAttachmentGroup = PrimitiveComponent->bExcludeFromLightAttachmentGroup;
	bReceiveMobileCSMShadows = PrimitiveComponent->bReceiveMobileCSMShadows;
	bSingleSampleShadowFromStationaryLights = PrimitiveComponent->bSingleSampleShadowFromStationaryLights;
	bIgnoreRadialImpulse = PrimitiveComponent->bIgnoreRadialImpulse;
	bIgnoreRadialForce = PrimitiveComponent->bIgnoreRadialForce;
	bApplyImpulseOnDamage = PrimitiveComponent->bApplyImpulseOnDamage;
	bReplicatePhysicsToAutonomousProxy = PrimitiveComponent->bReplicatePhysicsToAutonomousProxy;
	bFillCollisionUnderneathForNavmesh = PrimitiveComponent->bFillCollisionUnderneathForNavmesh;
	bRenderCustomDepth = PrimitiveComponent->bRenderCustomDepth;
	bVisibleInSceneCaptureOnly = PrimitiveComponent->bVisibleInSceneCaptureOnly;
	bHiddenInSceneCapture = PrimitiveComponent->bHiddenInSceneCapture;
	CanCharacterStepUpOn = PrimitiveComponent->CanCharacterStepUpOn;
	LightingChannels = PrimitiveComponent->LightingChannels;
	RayTracingGroupId = PrimitiveComponent->RayTracingGroupId;
	CustomDepthStencilValue = PrimitiveComponent->CustomDepthStencilValue;
	TranslucencySortPriority = PrimitiveComponent->TranslucencySortPriority;
	TranslucencySortDistanceOffset = PrimitiveComponent->TranslucencySortDistanceOffset;

	if (!PrimitiveComponent->RuntimeVirtualTextures.IsEmpty())
	{
		RuntimeVirtualTextures.Reserve(PrimitiveComponent->RuntimeVirtualTextures.Num());
		for (const TObjectPtr<URuntimeVirtualTexture>& RVT : PrimitiveComponent->RuntimeVirtualTextures)
		{
			RuntimeVirtualTextures.Add(RVT);
		}
	}

	VirtualTextureLodBias = PrimitiveComponent->VirtualTextureLodBias;
	VirtualTextureCullMips = PrimitiveComponent->VirtualTextureCullMips;
	VirtualTextureMinCoverage = PrimitiveComponent->VirtualTextureMinCoverage;
	VirtualTextureRenderPassType = PrimitiveComponent->VirtualTextureRenderPassType;
	BoundsScale = PrimitiveComponent->BoundsScale;
	RayTracingGroupCullingPriority = PrimitiveComponent->RayTracingGroupCullingPriority;
	CustomDepthStencilWriteMask = PrimitiveComponent->CustomDepthStencilWriteMask;
}

void FPCGExPrimitiveComponentDescriptor::InitComponent(UPrimitiveComponent* InComponent) const
{
	UPrimitiveComponent* PrimitiveComponent = InComponent;

	// Only update visibility if it's set to false to avoid massive overhead.
	if (!bVisible)
	{
		PrimitiveComponent->SetVisibility(false, false);
	}

	PrimitiveComponent->MinDrawDistance = MinDrawDistance;
	PrimitiveComponent->LDMaxDrawDistance = LDMaxDrawDistance;
	PrimitiveComponent->IndirectLightingCacheQuality = IndirectLightingCacheQuality;
	PrimitiveComponent->SetLightmapType(LightmapType);
	PrimitiveComponent->HLODBatchingPolicy = HLODBatchingPolicy;
	PrimitiveComponent->bEnableAutoLODGeneration = bEnableAutoLODGeneration;
	PrimitiveComponent->bNeverDistanceCull = bNeverDistanceCull;
	PrimitiveComponent->BodyInstance.CopyBodyInstancePropertiesFrom(&BodyInstance);
	PrimitiveComponent->bAlwaysCreatePhysicsState = bAlwaysCreatePhysicsState;
	PrimitiveComponent->bMultiBodyOverlap = bMultiBodyOverlap;
	PrimitiveComponent->bTraceComplexOnMove = bTraceComplexOnMove;
	PrimitiveComponent->bReturnMaterialOnMove = bReturnMaterialOnMove;
	PrimitiveComponent->bAllowCullDistanceVolume = bAllowCullDistanceVolume;
	PrimitiveComponent->bVisibleInReflectionCaptures = bVisibleInReflectionCaptures;
	PrimitiveComponent->bVisibleInRealTimeSkyCaptures = bVisibleInRealTimeSkyCaptures;
	PrimitiveComponent->bVisibleInRayTracing = bVisibleInRayTracing;
	PrimitiveComponent->bRenderInMainPass = bRenderInMainPass;
	PrimitiveComponent->bRenderInDepthPass = bRenderInDepthPass;
	PrimitiveComponent->bReceivesDecals = bReceivesDecals;
	PrimitiveComponent->bHoldout = bHoldout;
	PrimitiveComponent->bOwnerNoSee = bOwnerNoSee;
	PrimitiveComponent->bOnlyOwnerSee = bOnlyOwnerSee;
	PrimitiveComponent->bTreatAsBackgroundForOcclusion = bTreatAsBackgroundForOcclusion;
	PrimitiveComponent->bUseAsOccluder = bUseAsOccluder;
	PrimitiveComponent->bForceMipStreaming = bForceMipStreaming;
	PrimitiveComponent->CastShadow = CastShadow;
	PrimitiveComponent->bEmissiveLightSource = bEmissiveLightSource;
	PrimitiveComponent->bAffectDynamicIndirectLighting = bAffectDynamicIndirectLighting;
	PrimitiveComponent->bAffectIndirectLightingWhileHidden = bAffectIndirectLightingWhileHidden;
	PrimitiveComponent->bAffectDistanceFieldLighting = bAffectDistanceFieldLighting;
	PrimitiveComponent->bCastDynamicShadow = bCastDynamicShadow;
	PrimitiveComponent->bCastStaticShadow = bCastStaticShadow;
	PrimitiveComponent->ShadowCacheInvalidationBehavior = ShadowCacheInvalidationBehavior;
	PrimitiveComponent->bCastVolumetricTranslucentShadow = bCastVolumetricTranslucentShadow;
	PrimitiveComponent->bCastContactShadow = bCastContactShadow;
	PrimitiveComponent->bSelfShadowOnly = bSelfShadowOnly;
	PrimitiveComponent->bCastFarShadow = bCastFarShadow;
	PrimitiveComponent->bCastInsetShadow = bCastInsetShadow;
	PrimitiveComponent->bCastCinematicShadow = bCastCinematicShadow;
	PrimitiveComponent->bCastHiddenShadow = bCastHiddenShadow;
	PrimitiveComponent->bCastShadowAsTwoSided = bCastShadowAsTwoSided;
	PrimitiveComponent->bLightAttachmentsAsGroup = bLightAttachmentsAsGroup;
	PrimitiveComponent->bExcludeFromLightAttachmentGroup = bExcludeFromLightAttachmentGroup;
	PrimitiveComponent->bReceiveMobileCSMShadows = bReceiveMobileCSMShadows;
	PrimitiveComponent->bSingleSampleShadowFromStationaryLights = bSingleSampleShadowFromStationaryLights;
	PrimitiveComponent->bIgnoreRadialImpulse = bIgnoreRadialImpulse;
	PrimitiveComponent->bIgnoreRadialForce = bIgnoreRadialForce;
	PrimitiveComponent->bApplyImpulseOnDamage = bApplyImpulseOnDamage;
	PrimitiveComponent->bReplicatePhysicsToAutonomousProxy = bReplicatePhysicsToAutonomousProxy;
	PrimitiveComponent->bFillCollisionUnderneathForNavmesh = bFillCollisionUnderneathForNavmesh;
	PrimitiveComponent->bRenderCustomDepth = bRenderCustomDepth;
	PrimitiveComponent->bVisibleInSceneCaptureOnly = bVisibleInSceneCaptureOnly;
	PrimitiveComponent->bHiddenInSceneCapture = bHiddenInSceneCapture;
	PrimitiveComponent->CanCharacterStepUpOn = CanCharacterStepUpOn;
	PrimitiveComponent->LightingChannels = LightingChannels;
	PrimitiveComponent->RayTracingGroupId = RayTracingGroupId;
	PrimitiveComponent->CustomDepthStencilValue = CustomDepthStencilValue;
	PrimitiveComponent->TranslucencySortPriority = TranslucencySortPriority;
	PrimitiveComponent->TranslucencySortDistanceOffset = TranslucencySortDistanceOffset;
	//TargetComponent->RuntimeVirtualTextures = RuntimeVirtualTextures; // TODO : Load & Forward

	if (!RuntimeVirtualTextures.IsEmpty())
	{
		PrimitiveComponent->RuntimeVirtualTextures.Reserve(RuntimeVirtualTextures.Num());
		for (const TSoftObjectPtr<URuntimeVirtualTexture>& SoftRVT : RuntimeVirtualTextures)
		{
			TObjectPtr<URuntimeVirtualTexture> RVT = SoftRVT.Get();
			if (RVT)
			{
				PrimitiveComponent->RuntimeVirtualTextures.Add(RVT);
			}
		}
	}

	PrimitiveComponent->VirtualTextureLodBias = VirtualTextureLodBias;
	PrimitiveComponent->VirtualTextureCullMips = VirtualTextureCullMips;
	PrimitiveComponent->VirtualTextureMinCoverage = VirtualTextureMinCoverage;
	PrimitiveComponent->VirtualTextureRenderPassType = VirtualTextureRenderPassType;
	PrimitiveComponent->BoundsScale = BoundsScale;
	PrimitiveComponent->RayTracingGroupCullingPriority = RayTracingGroupCullingPriority;
	PrimitiveComponent->CustomDepthStencilWriteMask = CustomDepthStencilWriteMask;
}

void FPCGExPrimitiveComponentDescriptor::GetAssetPaths(TSet<FSoftObjectPath>& InAssetPaths) const
{
	for (int i = 0; i < RuntimeVirtualTextures.Num(); i++)
	{
		InAssetPaths.Add(RuntimeVirtualTextures[i].ToSoftObjectPath());
	}
}

FPCGExMeshComponentDescriptor::FPCGExMeshComponentDescriptor()
	: FPCGExMeshComponentDescriptor(NoInit)
{
	InitFrom(UMeshComponent::StaticClass()->GetDefaultObject<UMeshComponent>(), false);
}

void FPCGExMeshComponentDescriptor::InitFrom(const UPrimitiveComponent* Component, bool bInitBodyInstance)
{
	FPCGExPrimitiveComponentDescriptor::InitFrom(Component, bInitBodyInstance);

	const UMeshComponent* MeshComponent = Cast<UMeshComponent>(Component);
	if (!MeshComponent)
	{
		return;
	}

	// OverrideMaterials = SourceComponent->OverrideMaterials; // TODO!!
	OverlayMaterial = MeshComponent->OverlayMaterial;
	OverlayMaterialMaxDrawDistance = MeshComponent->OverlayMaterialMaxDrawDistance;
}

void FPCGExMeshComponentDescriptor::InitComponent(UPrimitiveComponent* InComponent) const
{
	FPCGExPrimitiveComponentDescriptor::InitComponent(InComponent);

	UMeshComponent* TargetComponent = Cast<UMeshComponent>(InComponent);
	if (!TargetComponent)
	{
		return;
	}

	for (int i = 0; i < OverrideMaterials.Num(); i++)
	{
		const TSoftObjectPtr<UMaterialInterface>& Mat = OverrideMaterials[i];
		if (Mat.IsValid() && Mat.Get())
		{
			TargetComponent->SetMaterial(i, Mat.Get());
		}
	}

	if (OverlayMaterial.IsValid() && OverlayMaterial.Get())
	{
		TargetComponent->OverlayMaterial = OverlayMaterial.Get();
	}

	TargetComponent->OverlayMaterialMaxDrawDistance = OverlayMaterialMaxDrawDistance;
}

void FPCGExMeshComponentDescriptor::GetAssetPaths(TSet<FSoftObjectPath>& InAssetPaths) const
{
	FPCGExPrimitiveComponentDescriptor::GetAssetPaths(InAssetPaths);

	for (int i = 0; i < RuntimeVirtualTextures.Num(); i++)
	{
		InAssetPaths.Add(RuntimeVirtualTextures[i].ToSoftObjectPath());
	}
	
	for (int i = 0; i < OverrideMaterials.Num(); i++)
	{
		InAssetPaths.Add(OverrideMaterials[i].ToSoftObjectPath());
	}

	InAssetPaths.Add(OverlayMaterial.ToSoftObjectPath());

}

FPCGExStaticMeshComponentDescriptor::FPCGExStaticMeshComponentDescriptor()
	: FPCGExStaticMeshComponentDescriptor(NoInit)
{
	InitFrom(UStaticMeshComponent::StaticClass()->GetDefaultObject<UStaticMeshComponent>(), false);
}

void FPCGExStaticMeshComponentDescriptor::InitFrom(const UPrimitiveComponent* Component, bool bInitBodyInstance)
{
	FPCGExMeshComponentDescriptor::InitFrom(Component, bInitBodyInstance);

	const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
	if (!StaticMeshComponent)
	{
		return;
	}

	ForcedLodModel = StaticMeshComponent->ForcedLodModel;
	MinLOD = StaticMeshComponent->MinLOD;
	WireframeColorOverride = StaticMeshComponent->WireframeColorOverride;
	bForceNaniteForMasked = StaticMeshComponent->bForceNaniteForMasked;
	bDisallowNanite = StaticMeshComponent->bDisallowNanite;
	bEvaluateWorldPositionOffset = StaticMeshComponent->bEvaluateWorldPositionOffset;
	bWorldPositionOffsetWritesVelocity = StaticMeshComponent->bWorldPositionOffsetWritesVelocity;
	bEvaluateWorldPositionOffsetInRayTracing = StaticMeshComponent->bEvaluateWorldPositionOffsetInRayTracing;
	WorldPositionOffsetDisableDistance = StaticMeshComponent->WorldPositionOffsetDisableDistance;
	bOverrideWireframeColor = StaticMeshComponent->bOverrideWireframeColor;
	bOverrideMinLOD = StaticMeshComponent->bOverrideMinLOD;
	bDisallowMeshPaintPerInstance = 0;
	bIgnoreInstanceForTextureStreaming = StaticMeshComponent->bIgnoreInstanceForTextureStreaming;
	bOverrideLightMapRes = StaticMeshComponent->bOverrideLightMapRes;
	bCastDistanceFieldIndirectShadow = StaticMeshComponent->bCastDistanceFieldIndirectShadow;
	bOverrideDistanceFieldSelfShadowBias = StaticMeshComponent->bOverrideDistanceFieldSelfShadowBias;
	bUseDefaultCollision = StaticMeshComponent->bUseDefaultCollision;
	bGenerateOverlapEvents = StaticMeshComponent->GetGenerateOverlapEvents();
	bSortTriangles = StaticMeshComponent->bSortTriangles;
	bReverseCulling = StaticMeshComponent->bReverseCulling;
	OverriddenLightMapRes = StaticMeshComponent->OverriddenLightMapRes;
	DistanceFieldIndirectShadowMinVisibility = StaticMeshComponent->DistanceFieldIndirectShadowMinVisibility;
	DistanceFieldSelfShadowBias = StaticMeshComponent->DistanceFieldSelfShadowBias;
	StreamingDistanceMultiplier = StaticMeshComponent->StreamingDistanceMultiplier;
	LightmassSettings = StaticMeshComponent->LightmassSettings;
}

void FPCGExStaticMeshComponentDescriptor::InitComponent(UPrimitiveComponent* InComponent) const
{
	FPCGExMeshComponentDescriptor::InitComponent(InComponent);

	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(InComponent);
	if (!StaticMeshComponent)
	{
		return;
	}

	StaticMeshComponent->ForcedLodModel = ForcedLodModel;
	StaticMeshComponent->MinLOD = MinLOD;
	StaticMeshComponent->WireframeColorOverride = WireframeColorOverride;
	StaticMeshComponent->bForceNaniteForMasked = bForceNaniteForMasked;
	StaticMeshComponent->bDisallowNanite = bDisallowNanite;
	StaticMeshComponent->bEvaluateWorldPositionOffset = bEvaluateWorldPositionOffset;
	StaticMeshComponent->bWorldPositionOffsetWritesVelocity = bWorldPositionOffsetWritesVelocity;
	StaticMeshComponent->bEvaluateWorldPositionOffsetInRayTracing = bEvaluateWorldPositionOffsetInRayTracing;
	StaticMeshComponent->WorldPositionOffsetDisableDistance = WorldPositionOffsetDisableDistance;
	StaticMeshComponent->bOverrideWireframeColor = bOverrideWireframeColor;
	StaticMeshComponent->bOverrideMinLOD = bOverrideMinLOD;
	StaticMeshComponent->bIgnoreInstanceForTextureStreaming = bIgnoreInstanceForTextureStreaming;
	StaticMeshComponent->bOverrideLightMapRes = bOverrideLightMapRes;
	StaticMeshComponent->bCastDistanceFieldIndirectShadow = bCastDistanceFieldIndirectShadow;
	StaticMeshComponent->bOverrideDistanceFieldSelfShadowBias = bOverrideDistanceFieldSelfShadowBias;
	StaticMeshComponent->bUseDefaultCollision = bUseDefaultCollision;
	StaticMeshComponent->SetGenerateOverlapEvents(bGenerateOverlapEvents);
	StaticMeshComponent->bSortTriangles = bSortTriangles;
	StaticMeshComponent->bReverseCulling = bReverseCulling;
	StaticMeshComponent->OverriddenLightMapRes = OverriddenLightMapRes;
	StaticMeshComponent->DistanceFieldIndirectShadowMinVisibility = DistanceFieldIndirectShadowMinVisibility;
	StaticMeshComponent->DistanceFieldSelfShadowBias = DistanceFieldSelfShadowBias;
	StaticMeshComponent->StreamingDistanceMultiplier = StreamingDistanceMultiplier;
	StaticMeshComponent->LightmassSettings = LightmassSettings;
}
