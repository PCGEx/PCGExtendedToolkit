// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Blenders/PCGExMetadataBlender.h"
#include "Core/PCGExPathProcessor.h"
#include "Details/PCGExBlendingDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Math/PCGExMath.h"

#include "PCGExPathResample.generated.h"

namespace PCGExPaths
{
	class FPathEdgeLength;
	class FPath;
}

UENUM()
enum class EPCGExResampleMode : uint8
{
	Sweep        = 0 UMETA(DisplayName = "Sweep", ToolTip="Walk the path at a fixed interval and emit a new point at each step. Output point count comes from the resolution, and attributes are blended between the two source points each sample falls between."),
	Redistribute = 1 UMETA(DisplayName = "Redistribute", ToolTip="Keep the input points and only move them, spreading them evenly along the original polyline. Point count and attributes are left alone, so the blending settings do not apply."),
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/modify/path-resample"))
class UPCGExResamplePathSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(ResamplePath, "Path : Resample", "Resample path to enforce equally spaced points.");
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

public:
	/** How the path is resampled. Sweep walks along the path at fixed intervals, Redistribute keeps point count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExResampleMode Mode = EPCGExResampleMode::Sweep;

	/** Resolution mode */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="Mode == EPCGExResampleMode::Sweep"))
	EPCGExResolutionMode ResolutionMode = EPCGExResolutionMode::Distance;

	/** Adjust sample spacing so all samples are evenly distributed across the path length. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode == EPCGExResampleMode::Sweep && ResolutionMode == EPCGExResolutionMode::Distance", EditConditionHides))
	bool bRedistributeEvenly = true;

	/** (ignored for closed loops) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="!bRedistributeEvenly && Mode == EPCGExResampleMode::Sweep && ResolutionMode == EPCGExResolutionMode::Distance", EditConditionHides))
	bool bPreserveLastPoint = false;

	UPROPERTY()
	double Resolution_DEPRECATED = 10;

	/** Resolution -- only support @Data attribute and will try to read from the data domain only. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Resolution", EditCondition="Mode == EPCGExResampleMode::Sweep", EditConditionHides))
	FPCGExInputShorthandNameDoubleAbs SampleLength = FPCGExInputShorthandNameDoubleAbs(NAME_None, 100, false);

	/** How to handle fractional sample counts (round, floor, or ceiling). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="Mode == EPCGExResampleMode::Sweep", EditConditionHides))
	EPCGExTruncateMode Truncate = EPCGExTruncateMode::Round;

	/** Blending settings used to smooth attributes.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExBlendingDetails BlendingSettings = FPCGExBlendingDetails(EPCGExBlendingType::Lerp, EPCGExBlendingType::None);

	/** Ensure each resampled point gets a unique random seed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bEnsureUniqueSeeds = true;
};

struct FPCGExResamplePathContext final : FPCGExPathProcessorContext
{
	friend class FPCGExResamplePathElement;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExResamplePathElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(ResamplePath)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExResamplePath
{
	struct FPointSample
	{
		int32 Start = 0;
		int32 End = 1;
		FVector Location = FVector::ZeroVector;
		double Distance = 0;
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExResamplePathContext, UPCGExResamplePathSettings>
	{
		bool bPreserveLastPoint = false;
		bool bAutoSampleSize = false;
		int32 NumSamples = 0;
		double SampleLength = 0;
		TArray<FPointSample> Samples;

		TSharedPtr<PCGExBlending::FMetadataBlender> MetadataBlender;

		TSharedPtr<PCGExPaths::FPath> Path;
		TSharedPtr<PCGExPaths::FPathEdgeLength> PathLength;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		virtual void CompleteWork() override;
	};
}
