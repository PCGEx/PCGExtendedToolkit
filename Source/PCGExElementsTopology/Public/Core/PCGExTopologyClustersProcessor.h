// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/Artifacts/PCGExCell.h"
#include "PCGExTopology.h"
#include "Clusters/Artifacts/PCGExCellDetails.h"
#include "Core/PCGExClustersProcessor.h"

#include "PCGExTopologyClustersProcessor.generated.h"


class UPCGDynamicMeshData;

UENUM()
enum class EPCGExTopologyOutputMode : uint8
{
	Legacy         = 0 UMETA(DisplayName = "Legacy (Deprecated)", ToolTip="DEPRECATED: No longer supported. Use PCG Dynamic Mesh instead."),
	PCGDynamicMesh = 1 UMETA(DisplayName = "PCG Dynamic Mesh", ToolTip="Creates a PCG dynamic mesh."),
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters")
class PCGEXELEMENTSTOPOLOGY_API UPCGExTopologyClustersProcessorSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(TopologyProcessor, "Topology", "Base processor to output meshes from clusters");
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::DynamicMesh; }
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor::White; }
#endif

	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;

	virtual bool SupportsEdgeConstraints() const { return true; }

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

#if WITH_EDITOR
	virtual void ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
#endif

	//~End UPCGSettings

public:
	/** Output mode. Legacy mode is deprecated and will log an error if used. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExTopologyOutputMode OutputMode = EPCGExTopologyOutputMode::PCGDynamicMesh;

	/** Projection settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGeo2DProjectionDetails ProjectionDetails = FPCGExGeo2DProjectionDetails(true);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExCellConstraintsDetails Constraints;

	/** Topology settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExTopologyDetails Topology;

protected:
	virtual bool IsCacheable() const override { return false; }

private:
	friend class FPCGExTopologyClustersProcessorElement;
};

struct PCGEXELEMENTSTOPOLOGY_API FPCGExTopologyClustersProcessorContext : FPCGExClustersProcessorContext
{
	friend class FPCGExTopologyClustersProcessorElement;
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> EdgeConstraintsFilterFactories;

	TSharedPtr<PCGExClusters::FProjectedPointSet> Holes;
	TSharedPtr<PCGExData::FFacade> HolesFacade;
	TArray<TSharedPtr<TMap<uint64, int32>>> HashMaps;

	virtual void RegisterAssetDependencies() override;
};

class PCGEXELEMENTSTOPOLOGY_API FPCGExTopologyClustersProcessorElement : public FPCGExClustersProcessorElement
{
protected:
	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
};

namespace PCGExTopologyEdges
{
	const FName SourceEdgeConstrainsFiltersLabel = FName("ConstrainedEdgeFilters");

	class PCGEXELEMENTSTOPOLOGY_API IProcessor : public PCGExClusterMT::IProcessor
	{
		friend class IBatch;

	protected:
		TSharedPtr<PCGExClusters::FProjectedPointSet> Holes;
		FPCGExTopologyUVDetails UVDetails;

		const FVector2D CWTolerance = FVector2D(0.001);
		bool bIsPreviewMode = false;

		TSharedPtr<PCGExClusters::FCell> WrapperCell;
		TObjectPtr<UDynamicMesh> InternalMesh;
		UPCGDynamicMeshData* InternalMeshData = nullptr;

		TSharedPtr<PCGEx::FIndexLookup> VerticesLookup;

		TSharedPtr<PCGExClusters::FCellConstraints> CellsConstraints;

		int32 ConstrainedEdgesNum = 0;

	public:
		TSharedPtr<TMap<uint64, int32>> ProjectedHashMap;

		TObjectPtr<UDynamicMesh> GetInternalMesh() { return InternalMesh; }

		IProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade);

		virtual ~IProcessor() override = default;

		virtual void InitConstraints();
		virtual TSharedPtr<PCGExClusters::FCluster> HandleCachedCluster(const TSharedRef<PCGExClusters::FCluster>& InClusterRef) override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void Output() override;
		virtual void Cleanup() override;

	protected:
		void FilterConstrainedEdgeScope(const PCGExMT::FScope& Scope);
		void ApplyPointData();
	};

	template <typename TContext, typename TSettings>
	class PCGEXELEMENTSTOPOLOGY_API TProcessor : public IProcessor
	{
	protected:
		TContext* Context = nullptr;
		const TSettings* Settings = nullptr;

	public:
		TProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: IProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
			static_assert(std::is_base_of_v<FPCGExTopologyClustersProcessorContext, TContext>, "TContext must inherit from FPCGExTopologyProcessorContext");
			static_assert(std::is_base_of_v<UPCGExTopologyClustersProcessorSettings, TSettings>, "TSettings must inherit from UPCGExTopologyProcessorSettings");
		}

		virtual ~TProcessor() override = default;

		virtual void SetExecutionContext(FPCGExContext* InContext) override
		{
			IProcessor::SetExecutionContext(InContext);
			Context = static_cast<TContext*>(ExecutionContext);
			Settings = InContext->GetInputSettings<TSettings>();
		}

		TContext* GetContext() { return Context; }
		const TSettings* GetSettings() { return Settings; }
	};

	class PCGEXELEMENTSTOPOLOGY_API IBatch : public PCGExClusterMT::IBatch
	{
	protected:
		const FVector2D CWTolerance = FVector2D(0.001);
		TSharedPtr<TMap<uint64, int32>> ProjectedHashMap;

	public:
		IBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, const TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges);

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;
		virtual void Output() override;

	protected:
		virtual void OnInitialPostProcess() override;
	};

	template <typename T>
	class PCGEXELEMENTSTOPOLOGY_API TBatch : public IBatch
	{
	protected:
		virtual TSharedPtr<PCGExClusterMT::IProcessor> NewProcessorInstance(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade) const override
		{
			TSharedPtr<PCGExClusterMT::IProcessor> NewInstance = MakeShared<T>(InVtxDataFacade, InEdgeDataFacade);
			return NewInstance;
		}

	public:
		TBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, const TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
			: IBatch(InContext, InVtx, InEdges)
		{
		}
	};
}
