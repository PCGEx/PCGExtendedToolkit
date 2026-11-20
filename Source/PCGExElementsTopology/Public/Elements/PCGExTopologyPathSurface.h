// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExTopology.h"
#include "Core/PCGExClusterMT.h"
#include "Core/PCGExPathProcessor.h"
#include "Data/PCGDynamicMeshData.h"

#include "PCGExTopologyPathSurface.generated.h"


UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="topology/topology-path-surface"))
class PCGEXELEMENTSTOPOLOGY_API UPCGExTopologyPathSurfaceSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(TopologyPathSurface, "Topology : Path Surface", "Create a path surface topology");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::DynamicMesh;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return FLinearColor::White;
	}
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	//~End UPCGSettings

public:
	/** Topology settings. Some settings will be ignored based on selected output mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExTopologyDetails Topology;

protected:
	virtual bool IsCacheable() const override
	{
		return false;
	}

private:
	friend class FPCGExTopologyPathSurfaceElement;
};

struct PCGEXELEMENTSTOPOLOGY_API FPCGExTopologyPathSurfaceContext : FPCGExPathProcessorContext
{
	friend class FPCGExTopologyPathSurfaceElement;
	virtual void RegisterAssetDependencies() override;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class PCGEXELEMENTSTOPOLOGY_API FPCGExTopologyPathSurfaceElement : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(TopologyPathSurface)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExTopologyPathSurface
{
	class PCGEXELEMENTSTOPOLOGY_API FProcessor : public PCGExPointsMT::TProcessor<FPCGExTopologyPathSurfaceContext, UPCGExTopologyPathSurfaceSettings>
	{
	protected:
		TObjectPtr<UDynamicMesh> InternalMesh;
		UPCGDynamicMeshData* InternalMeshData = nullptr;
		FPCGExTopologyUVDetails UVDetails;

	public:
		TObjectPtr<UDynamicMesh> GetInternalMesh()
		{
			return InternalMesh;
		}

		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor<FPCGExTopologyPathSurfaceContext, UPCGExTopologyPathSurfaceSettings>(InPointDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void Output() override;
	};
}
