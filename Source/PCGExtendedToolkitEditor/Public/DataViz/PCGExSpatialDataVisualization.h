// Copyright 2026 Timothé Lapetite and contributors
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGDataVisualization.h"
#include "PCGEditorModule.h"

class AActor;
class UPCGData;
class UPCGBasePointData;
class UPCGPointData;
struct FPCGContext;
struct FPCGDebugVisualizationSettings;

/** Default implementation for spatial data. Collapses to a PointData representation. */
class IPCGExSpatialDataVisualization : public IPCGDataVisualization
{
public:
	// ~Begin IPCGDataVisualization interface
	virtual void ExecuteDebugDisplay(FPCGContext* Context, const UPCGSettingsInterface* SettingsInterface, const UPCGData* Data, AActor* TargetActor) const override;
	virtual FPCGTableVisualizerInfo GetTableVisualizerInfoWithDomain(const UPCGData* Data, const FPCGMetadataDomainID& DomainID) const override;

	// Show the sampled points by default
	virtual FPCGMetadataDomainID GetDefaultDomainForInspection(const UPCGData* Data) const override
	{
		return PCGMetadataDomainID::Elements;
	}

	virtual FString GetDomainDisplayNameForInspection(const UPCGData* Data, const FPCGMetadataDomainID& DomainID) const override;
	virtual TArray<FPCGMetadataDomainID> GetAllSupportedDomainsForInspection(const UPCGData* Data) const override;
	virtual FPCGSetupSceneFunc GetViewportSetupFunc(const UPCGSettingsInterface* SettingsInterface, const UPCGData* Data) const override;
	// ~End IPCGDataVisualization interface

	UE_DEPRECATED(5.6, "Implement CollapseToDebugBasePointData instead")
	virtual const UPCGPointData* CollapseToDebugPointData(FPCGContext* Context, const UPCGData* Data) const;

	virtual const UPCGBasePointData* CollapseToDebugBasePointData(FPCGContext* Context, const UPCGData* Data) const;

	virtual void ExecuteDebugDisplayHelper(
		const UPCGData* Data,
		const FPCGDebugVisualizationSettings& DebugSettings,
		FPCGContext* Context,
		AActor* TargetActor,
		const FPCGCrc& Crc,
		const TFunction<void(class UInstancedStaticMeshComponent*)>& OnISMCCreatedCallback) const;
};
