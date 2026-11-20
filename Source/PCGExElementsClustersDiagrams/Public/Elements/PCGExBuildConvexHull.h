// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPointsProcessor.h"
#include "Graphs/PCGExGraphDetails.h"

#include "PCGExBuildConvexHull.generated.h"

namespace PCGExGraphs
{
	class FGraphBuilder;
}

namespace PCGExMath
{
	namespace Geo
	{
		class TDelaunay3;
	}

	class TDelaunay3;
}

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-convex-hull-3d"))
class UPCGExBuildConvexHullSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(BuildConvexHull, "Cluster : Convex Hull 3D", "Create a 3D Convex Hull triangulation for each input dataset.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(ClusterGenerator);
	}
#endif
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual FName GetMainOutputPin() const override;
	//~End UPCGExPointsProcessorSettings

	/** Graph & Edges output properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Cluster Output Settings"))
	FPCGExGraphBuilderDetails GraphBuilderDetails = FPCGExGraphBuilderDetails();

private:
	friend class FPCGExBuildConvexHullElement;
};

struct FPCGExBuildConvexHullContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExBuildConvexHullElement;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExBuildConvexHullElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BuildConvexHull)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExBuildConvexHull
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExBuildConvexHullContext, UPCGExBuildConvexHullSettings>
	{
	protected:
		TSharedPtr<PCGExMath::Geo::TDelaunay3> Delaunay;
		TSharedPtr<PCGExGraphs::FGraphBuilder> GraphBuilder;

		TArray<uint64> Edges;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void CompleteWork() override;
		virtual void Write() override;
		virtual void Output() override;
	};
}
