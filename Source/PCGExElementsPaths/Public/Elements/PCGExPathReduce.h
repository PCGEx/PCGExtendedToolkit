// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Core/PCGExPathProcessor.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Factories/PCGExFactories.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Helpers/PCGExPathSimplifier.h"

#include "PCGExPathReduce.generated.h"

namespace PCGExPaths
{
	class FPath;
}

UENUM(BlueprintType)
enum class EPCGExPathReduceFilterMode : uint8
{
	Preserve = 0 UMETA(DisplayName = "Preserve", ToolTip="Filters drive points that are guaranteed to be preserved. Any other may be removed."),
	Anchor   = 1 UMETA(DisplayName = "Anchors", ToolTip="Filters define which points the path will be reduced to")
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/modify/path-reduce"))
class UPCGExPathReduceSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PathReduce, "Path : Reduce", "Reduce point but attempts to preserve aspect using tangents");
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourceFiltersLabel, "Filter which points are going to be preserved.", PCGExFactories::PointFilters(), Mode == EPCGExPathReduceFilterMode::Anchor)
	//~End UPCGSettings

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExPathReduceFilterMode Mode = EPCGExPathReduceFilterMode::Preserve;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Error Tolerance", EditCondition="Mode == EPCGExPathReduceFilterMode::Preserve", EditConditionHides))
	double ErrorTolerance = 10;


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName ArriveName = "ArriveTangent";

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName LeaveName = "LeaveTangent";

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExTangentSmoothing SmoothingMode = EPCGExTangentSmoothing::Full;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="SmoothingMode != EPCGExTangentSmoothing::None"))
	FPCGExInputShorthandNameDouble01 Smoothing = FPCGExInputShorthandNameDouble01(FName("Smoothing"), 1.0, false);

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;
};

struct FPCGExPathReduceContext final : FPCGExPathProcessorContext
{
	friend class FPCGExPathReduceElement;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExPathReduceElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PathReduce)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExPathReduce
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExPathReduceContext, UPCGExPathReduceSettings>
	{
		bool bClosedLoop = false;

		TSharedPtr<PCGExData::TBuffer<FVector>> ArriveWriter;
		TSharedPtr<PCGExData::TBuffer<FVector>> LeaveWriter;
		TSharedPtr<PCGExDetails::TSettingValue<double>> SmoothingGetter;

		TArray<int8> Mask;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
			DefaultPointFilterValue = false;
		}

		virtual ~FProcessor() override;
		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void CompleteWork() override;
	};
}
