// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Descriptors/PCGExDynamicMeshDescriptor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"

FPCGExDynamicMeshDescriptor::FPCGExDynamicMeshDescriptor()
	: FPCGExDynamicMeshDescriptor(NoInit)
{
	InitFrom(UDynamicMeshComponent::StaticClass()->GetDefaultObject<UDynamicMeshComponent>(), false);
}

void FPCGExDynamicMeshDescriptor::InitFrom(const UPrimitiveComponent* Component, bool bInitBodyInstance)
{
	FPCGExMeshComponentDescriptor::InitFrom(Component, bInitBodyInstance);

	const UDynamicMeshComponent* DynamicMeshComponent = Cast<UDynamicMeshComponent>(Component);
	if (!DynamicMeshComponent)
	{
		return;
	}

	bExplicitShowWireframe = DynamicMeshComponent->bExplicitShowWireframe;
	WireframeColor = DynamicMeshComponent->WireframeColor;
	ColorMode = DynamicMeshComponent->ColorMode;
	ConstantColor = DynamicMeshComponent->ConstantColor;
	ColorSpaceMode = DynamicMeshComponent->ColorSpaceMode;
	bEnableFlatShading = DynamicMeshComponent->bEnableFlatShading;
	bEnableViewModeOverrides = DynamicMeshComponent->bEnableViewModeOverrides;
	bEnableRaytracing = DynamicMeshComponent->bEnableRaytracing;
}

void FPCGExDynamicMeshDescriptor::InitComponent(UPrimitiveComponent* InComponent) const
{
	FPCGExMeshComponentDescriptor::InitComponent(InComponent);

	UDynamicMeshComponent* DynamicMeshComponent = Cast<UDynamicMeshComponent>(InComponent);
	if (!DynamicMeshComponent)
	{
		return;
	}

	DynamicMeshComponent->bUseAsyncCooking = bUseAsyncCooking;
	DynamicMeshComponent->bDeferCollisionUpdates = bDeferCollisionUpdates;
	DynamicMeshComponent->SetComplexAsSimpleCollisionEnabled(bEnableComplexCollision, false);

	DynamicMeshComponent->bExplicitShowWireframe = bExplicitShowWireframe;
	DynamicMeshComponent->WireframeColor = WireframeColor;
	DynamicMeshComponent->ColorMode = ColorMode;
	DynamicMeshComponent->ConstantColor = ConstantColor;
	DynamicMeshComponent->ColorSpaceMode = ColorSpaceMode;
	DynamicMeshComponent->bEnableFlatShading = bEnableFlatShading;
	DynamicMeshComponent->bEnableViewModeOverrides = bEnableViewModeOverrides;
	DynamicMeshComponent->bEnableRaytracing = bEnableRaytracing;
}
