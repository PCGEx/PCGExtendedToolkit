// Copyright 2026 Timothé Lapetite and contributors
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataVisualizations/PCGSpatialDataVisualization.h"

/** The engine's spatial data visualization, with debug components that are not transient so they persist with the level. */
class IPCGExSpatialDataVisualization : public IPCGSpatialDataVisualization
{
public:
	virtual void ExecuteDebugDisplayHelper(
		const UPCGData* Data,
		const FPCGDebugVisualizationSettings& DebugSettings,
		FPCGContext* Context,
		AActor* TargetActor,
		const FPCGCrc& Crc,
		const TFunction<void(class UInstancedStaticMeshComponent*)>& OnISMCCreatedCallback) const override;
};
