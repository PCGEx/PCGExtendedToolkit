// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExClipper2Processor.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Paths/PCGExPath.h"

#include "PCGExClipper2Offset.generated.h"

UENUM(BlueprintType)
enum class EPCGExClipper2OffsetIterationCount : uint8
{
	First   = 0 UMETA(DisplayName = "First"),
	Last    = 1 UMETA(DisplayName = "Last"),
	Average = 2 UMETA(DisplayName = "Average"),
	Min     = 3 UMETA(DisplayName = "Min"),
	Max     = 4 UMETA(DisplayName = "Max")
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/generate/clipper2-offset"))
class UPCGExClipper2OffsetSettings : public UPCGExClipper2ProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Clipper2Offset, "Clipper2 : Offset", "Does a Clipper2 offset operation.");
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Projection settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGeo2DProjectionDetails ProjectionDetails;

	/** Number of iterations to apply */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Iterations", meta = (PCG_Overridable))
	FPCGExInputShorthandNameInteger32Abs Iterations = FPCGExInputShorthandNameInteger32Abs(FName("@Data.Iterations"), 1, false);

	/** How to determine final iteration count when iteration attribute from multiple source differ */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Iterations", meta = (PCG_Overridable, DisplayName=" ├─ Consolidation"))
	EPCGExClipper2OffsetIterationCount IterationConsolidation = EPCGExClipper2OffsetIterationCount::Max;

	/** Minimum guaranteed iterations */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Iterations", meta = (PCG_Overridable, DisplayName=" └─ Min Iterations", ClampMin=0))
	int32 MinIterations = 1;

	/** Offset amount */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorDouble Offset = FPCGExInputShorthandSelectorDouble(FName("Offset"), 10, false);

	/** Offset Scale (mostly useful when using attributes) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" └─ Scale"))
	double OffsetScale = 1.0;

	/** Join type for corners */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExClipper2JoinType JoinType = EPCGExClipper2JoinType::Round;

	/** Miter limit (only used with Miter join type) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" └─ Miter limit", ClampMin=1.0))
	double MiterLimit = 2.0;

	/** End type for closed paths */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExClipper2EndType EndTypeClosed = EPCGExClipper2EndType::Polygon;

	/** End type for open paths */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="!bSkipOpenPaths", EditConditionHides))
	EPCGExClipper2EndType EndTypeOpen = EPCGExClipper2EndType::Round;


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(InlineEditConditionToggle))
	bool bWriteIteration = false;

	/** Write the iteration index to a data attribute */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable, EditCondition="bWriteIteration"))
	FString IterationAttributeName = TEXT("Iteration");


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIteration = false;

	/** Write the iteration index to a tag */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output|Tagging", meta=(PCG_Overridable, EditCondition="bTagIteration"))
	FString IterationTag = TEXT("OffsetNum");

	virtual FPCGExGeo2DProjectionDetails GetProjectionDetails() const override;
	virtual bool SupportOpenMainPaths() const override;
};

struct FPCGExClipper2OffsetContext final : FPCGExClipper2ProcessorContext
{
	friend class FPCGExClipper2OffsetElement;

	TArray<TSharedPtr<PCGExDetails::TSettingValue<double>>> OffsetValues;
	TArray<TSharedPtr<PCGExDetails::TSettingValue<int32>>> IterationValues;

	virtual void Process(const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group) override;
};

class FPCGExClipper2OffsetElement final : public FPCGExClipper2ProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(Clipper2Offset)

	virtual bool PostBoot(FPCGExContext* InContext) const override;
};
