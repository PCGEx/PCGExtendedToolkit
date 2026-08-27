// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "SubPoints/DataBlending/PCGExSubPointsBlendOperation.h"

#include "Blenders/PCGExMetadataBlender.h"
#include "Data/PCGExPointElements.h"


bool FPCGExSubPointsBlendOperation::PrepareForData(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InTargetFacade, const TSet<FName>* IgnoreAttributeSet)
{
	return PrepareForData(InContext, InTargetFacade, InTargetFacade, PCGExData::EIOSide::Out, IgnoreAttributeSet);
}

bool FPCGExSubPointsBlendOperation::PrepareForData(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InTargetFacade, const TSharedPtr<PCGExData::FFacade>& InSourceFacade, const PCGExData::EIOSide InSourceSide, const TSet<FName>* IgnoreAttributeSet)
{
	if (!FPCGExSubPointsOperation::PrepareForData(InContext, InTargetFacade, IgnoreAttributeSet))
	{
		return false;
	}

	if (bPreserveTransform)
	{
		bPreservePosition = bPreserveRotation = bPreserveScale = true;
	}

	BlendingDetails = BlendFactory->BlendingDetails;

	if (bPreservePosition)
	{
		BlendingDetails.PropertiesOverrides.bOverridePosition = true;
		BlendingDetails.PropertiesOverrides.PositionBlending = EPCGExBlendingType::None;
	}

	if (bPreserveRotation)
	{
		BlendingDetails.PropertiesOverrides.bOverrideRotation = true;
		BlendingDetails.PropertiesOverrides.RotationBlending = EPCGExBlendingType::None;
	}

	if (bPreserveScale)
	{
		BlendingDetails.PropertiesOverrides.bOverrideScale = true;
		BlendingDetails.PropertiesOverrides.ScaleBlending = EPCGExBlendingType::None;
	}

	MetadataBlender = MakeShared<PCGExBlending::FMetadataBlender>();

	MetadataBlender->SetTargetData(InTargetFacade);
	MetadataBlender->SetSourceData(InSourceFacade, InSourceSide, true);

	return MetadataBlender->Init(InContext, BlendingDetails, IgnoreAttributeSet);
}

void FPCGExSubPointsBlendOperation::ProcessSubPoints(const PCGExData::FConstPoint& From, const PCGExData::FConstPoint& To, PCGExData::FScope& Scope, const PCGExPaths::FPathMetrics& Metrics) const
{
	BlendSubPoints(From, To, Scope, Metrics);
}

void FPCGExSubPointsBlendOperation::BlendSubPoints(const PCGExData::FConstPoint& From, const PCGExData::FConstPoint& To, PCGExData::FScope& Scope, const PCGExPaths::FPathMetrics& Metrics) const
{
}

void FPCGExSubPointsBlendOperation::BlendSubPoints(PCGExData::FScope& Scope, const PCGExPaths::FPathMetrics& Metrics) const
{
	BlendSubPoints(Scope.CFirst(), Scope.CLast(), Scope, Metrics);
}

UPCGExSubPointsBlendInstancedFactory::UPCGExSubPointsBlendInstancedFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UPCGExSubPointsBlendInstancedFactory::PostInitProperties()
{
	Super::PostInitProperties();

	// Not in the constructor: virtual dispatch cannot reach a subclass from a base constructor, so
	// every rule would take the base's Lerp regardless of its own override.
	if (BlendingDetails.DefaultBlending == EPCGExBlendingType::Unset)
	{
		BlendingDetails.DefaultBlending = GetDefaultBlending();
	}
}

void UPCGExSubPointsBlendInstancedFactory::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
	if (const UPCGExSubPointsBlendInstancedFactory* TypedOther = Cast<UPCGExSubPointsBlendInstancedFactory>(Other))
	{
		BlendingDetails = TypedOther->BlendingDetails;
	}
}
