// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clipper2Lib/clipper.h"
#include "Core/PCGExPathProcessor.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Details/PCGExBlendingDetails.h"
#include "Details/PCGExMatchingDetails.h"
#include "Math/PCGExProjectionDetails.h"

#include "PCGExClipper2Processor.generated.h"

class UPCGExClipper2ProcessorSettings;
struct FPCGExBlendingDetails;
struct FPCGExCarryOverDetails;


UENUM(BlueprintType)
enum class EPCGExClipper2EndpointType : uint8
{
	None  = 0 UMETA(DisplayName = "None"),
	Start = 1 UMETA(DisplayName = "Start"),
	End   = 2 UMETA(DisplayName = "End"),
};

UENUM(BlueprintType)
enum class EPCGExClipper2JoinType : uint8
{
	Square = 0 UMETA(DisplayName = "Square", ToolTip="Square joins"),
	Round  = 1 UMETA(DisplayName = "Round", ToolTip="Round joins"),
	Bevel  = 2 UMETA(DisplayName = "Bevel", ToolTip="Bevel joins"),
	Miter  = 3 UMETA(DisplayName = "Miter", ToolTip="Miter joins"),
};

UENUM(BlueprintType)
enum class EPCGExClipper2EndType : uint8
{
	Polygon = 0 UMETA(DisplayName = "Polygon", ToolTip="Closed polygon (ignores path open/closed state)"),
	Joined  = 1 UMETA(DisplayName = "Joined", ToolTip="Joined ends (creates thin paths with double-sided offsets)"),
	Butt    = 2 UMETA(DisplayName = "Butt", ToolTip="Butt ends"),
	Square  = 3 UMETA(DisplayName = "Square", ToolTip="Square ends"),
	Round   = 4 UMETA(DisplayName = "Round", ToolTip="Round ends"),
};


UENUM(BlueprintType)
enum class EPCGExClipper2FillRule : uint8
{
	EvenOdd  = 0 UMETA(DisplayName = "Even Odd", ToolTip="..."),
	NonZero  = 1 UMETA(DisplayName = "Non Zero", ToolTip="..."),
	Positive = 2 UMETA(DisplayName = "Positive", ToolTip="..."),
	Negative = 3 UMETA(DisplayName = "Negative", ToolTip="..."),
};

UENUM(BlueprintType)
enum class EPCGExGroupingPolicy : uint8
{
	Split       = 0 UMETA(DisplayName = "Separate", ToolTip="Split inputs into separate groups"),
	Consolidate = 1 UMETA(DisplayName = "Merged", ToolTip="Add all inputs into a single group"),
};

UENUM()
enum class EPCGExClipper2OpenPathOutput : uint8
{
	Ignore    = 0 UMETA(DisplayName = "Ignore", ToolTip="Do not output open paths"),
	Output    = 1 UMETA(DisplayName = "Output", ToolTip="Output on the main pin"),
	OutputPin = 2 UMETA(DisplayName = "Output (Pin)", ToolTip="Output on a separate pin"),
};

namespace PCGExClipper2
{
	static PCGExClipper2Lib::JoinType ConvertJoinType(EPCGExClipper2JoinType InType)
	{
		switch (InType)
		{
		case EPCGExClipper2JoinType::Square: return PCGExClipper2Lib::JoinType::Square;
		case EPCGExClipper2JoinType::Round: return PCGExClipper2Lib::JoinType::Round;
		case EPCGExClipper2JoinType::Bevel: return PCGExClipper2Lib::JoinType::Bevel;
		case EPCGExClipper2JoinType::Miter: return PCGExClipper2Lib::JoinType::Miter;
		default: return PCGExClipper2Lib::JoinType::Round;
		}
	}

	static PCGExClipper2Lib::EndType ConvertEndType(EPCGExClipper2EndType InType)
	{
		switch (InType)
		{
		case EPCGExClipper2EndType::Polygon: return PCGExClipper2Lib::EndType::Polygon;
		case EPCGExClipper2EndType::Joined: return PCGExClipper2Lib::EndType::Joined;
		case EPCGExClipper2EndType::Butt: return PCGExClipper2Lib::EndType::Butt;
		case EPCGExClipper2EndType::Square: return PCGExClipper2Lib::EndType::Square;
		case EPCGExClipper2EndType::Round: return PCGExClipper2Lib::EndType::Round;
		default: return PCGExClipper2Lib::EndType::Round;
		}
	}

	static PCGExClipper2Lib::FillRule ConvertFillRule(EPCGExClipper2FillRule InRule)
	{
		switch (InRule)
		{
		case EPCGExClipper2FillRule::EvenOdd: return PCGExClipper2Lib::FillRule::EvenOdd;
		default:
		case EPCGExClipper2FillRule::NonZero: return PCGExClipper2Lib::FillRule::NonZero;
		case EPCGExClipper2FillRule::Positive: return PCGExClipper2Lib::FillRule::Positive;
		case EPCGExClipper2FillRule::Negative: return PCGExClipper2Lib::FillRule::Negative;
		}
	}

	// Special marker for intersection points - uses high bit pattern that's unlikely in normal usage
	constexpr uint32 INTERSECTION_MARKER = 0xFFFFFFFF;

	/** Controls how output transforms are computed */
	enum class ETransformRestoration : uint8
	{
		/** Restore original transforms from source points (for booleans where positions don't change) */
		FromSource,
		/** Unproject Clipper2 coordinates back to 3D (for offset/inflate where positions change) */
		Unproject
	};

	// Intersection blend info - stores the 4 contributing source points
	struct PCGEXELEMENTSCLIPPER2_API FIntersectionBlendInfo
	{
		// Edge 1: bot -> top
		uint32 E1BotPointIdx = 0;
		uint32 E1BotSourceIdx = 0;
		uint32 E1TopPointIdx = 0;
		uint32 E1TopSourceIdx = 0;

		// Edge 2: bot -> top
		uint32 E2BotPointIdx = 0;
		uint32 E2BotSourceIdx = 0;
		uint32 E2TopPointIdx = 0;
		uint32 E2TopSourceIdx = 0;

		// Alpha along each edge (0-1)
		double E1Alpha = 0.5;
		double E2Alpha = 0.5;
	};

	class PCGEXELEMENTSCLIPPER2_API FOpData : public TSharedFromThis<FOpData>
	{
	public:
		TArray<TSharedPtr<PCGExData::FFacade>> Facades;
		TArray<PCGExClipper2Lib::Path64> Paths;
		TArray<bool> IsClosedLoop;
		TArray<FPCGExGeo2DProjectionDetails> Projections;

		// Per-path, per-point projected Z values (for unprojection during offset/inflate)
		TArray<TArray<double>> ProjectedZValues;

		explicit FOpData(const int32 InReserve);
		void AddReserve(const int32 InReserve);

		int32 Num() const { return Facades.Num(); }
	};

	/**
	 * Unified processing group that encapsulates subjects, operands, and cached data.
	 * This provides a clean interface for Clipper2 operations.
	 */
	struct PCGEXELEMENTSCLIPPER2_API FProcessingGroup : TSharedFromThis<FProcessingGroup>
	{
		// Group index for deterministic output ordering
		int32 GroupIndex = 0;

		// Indices into AllOpData
		TArray<int32> SubjectIndices;
		TArray<int32> OperandIndices;

		// Cached paths for quick access (references into AllOpData->Paths)
		PCGExClipper2Lib::Paths64 SubjectPaths;
		PCGExClipper2Lib::Paths64 OpenSubjectPaths;

		PCGExClipper2Lib::Paths64 OperandPaths;
		PCGExClipper2Lib::Paths64 OpenOperandPaths;

		// Combined source indices for blending
		TArray<int32> AllSourceIndices;
		TSharedPtr<PCGExData::FTags> GroupTags;

		// Intersection blend info map: keyed by encoded (x,y) position
		TMap<uint64, FIntersectionBlendInfo> IntersectionBlendInfos;
		mutable FCriticalSection IntersectionLock;

		FProcessingGroup() = default;

		// Prepare cached paths from AllOpData
		void Prepare(const TSharedPtr<FOpData>& AllOpData);
		void PreProcess(const UPCGExClipper2ProcessorSettings* InSettings);

		// Check if this group is valid for processing
		bool IsValid() const { return !SubjectPaths.empty() || !OpenSubjectPaths.empty(); }

		// Add intersection blend info (thread-safe)
		void AddIntersectionBlendInfo(int64_t X, int64_t Y, const FIntersectionBlendInfo& Info);

		// Get intersection blend info by position
		const FIntersectionBlendInfo* GetIntersectionBlendInfo(int64_t X, int64_t Y) const;

		// Create the ZCallback for this group
		PCGExClipper2Lib::ZCallback64 CreateZCallback();
	};
}

/**
 * 
 */
UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Cavalier")
class PCGEXELEMENTSCLIPPER2_API UPCGExClipper2ProcessorSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExClipper2ProcessorSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_NAME(Path); }
#endif
	//~End UPCGSettings

protected:
	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

public:
	/** If enabled, lets you to create sub-groups to operate on. If disabled, data is processed individually. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Processing")
	FPCGExMatchingDetails MainDataMatching = FPCGExMatchingDetails(EPCGExMatchingDetailsUsage::Default);

	/** How should data be grouped when data matching is disabled */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Processing", meta = (PCG_Overridable, EditCondition="!WantsDataMatching()"))
	EPCGExGroupingPolicy MainInputGroupingPolicy = EPCGExGroupingPolicy::Consolidate;

	/** If enabled, lets you to pick which are matched with which main data.
	 * Note that the match is done against every single data within a group and then consolidated;
	 * this means if you have a [A,B,C] group, ABC will be executed against operands for A, B and C individually. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Processing", meta=(EditCondition="WantsOperands()", EditConditionHides))
	FPCGExMatchingDetails OperandsDataMatching = FPCGExMatchingDetails(EPCGExMatchingDetailsUsage::Default);

	/** Skip paths that aren't closed */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Processing", meta = (PCG_Overridable))
	bool bSkipOpenPaths = false;

	/** Decimal precision 
	 * Clipper2 Uses int64 under the hood to preserve extreme precision, so we scale floating point values then back. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tweaks", meta = (PCG_Overridable, ClampMin=1))
	int32 Precision = 100;

	/** Cleanup */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tweaks", meta = (PCG_Overridable))
	bool bSimplifyPaths = false;

	/** Keep collinear points in the output instead of simplifying them. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tweaks", meta = (PCG_Overridable))
	bool bPreserveCollinear = true;

	/** Tolerance for arc approximation when generating curved offsets. Higher values = fewer points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tweaks", meta = (PCG_Overridable))
	double ArcTolerance = 5.0;

	FORCEINLINE double GetArcTolerance() const { return ArcTolerance * static_cast<double>(Precision); }

	/** Filter in/out which attributes get carried over from inputs to outputs. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable))
	FPCGExCarryOverDetails CarryOverDetails;

	/** Filter in/out which attributes get carried over from inputs to outputs. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable))
	FPCGExBlendingDetails BlendingDetails = FPCGExBlendingDetails(EPCGExBlendingType::Average, EPCGExBlendingType::None);

	/** How to handle open paths in the output (ignore, output to main pin, or separate pin). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_NotOverridable))
	EPCGExClipper2OpenPathOutput OpenPathsOutput = EPCGExClipper2OpenPathOutput::Output;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output|Tagging", meta=(InlineEditConditionToggle))
	bool bTagHoles = false;

	/** Write this tag on paths that are holes */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output|Tagging", meta=(EditCondition="bTagHoles"))
	FString HoleTag = TEXT("Hole");

	/** Write a flag marking intersection points (points created by Clipper2 at path crossings). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output|Flags", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bFlagIntersections = false;

	/** Name of the boolean attribute for intersection points */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output|Flags", meta = (PCG_Overridable, EditCondition="bFlagIntersections"))
	FName IntersectionFlagName = "IsIntersection";

	/** Write a flag identifying the start/end of joint arcs at the original path's endpoints (e.g. round caps on offset/inflate). Only applies to open source paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output|Flags", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bFlagJoints = false;

	/** Name of the int32 attribute for joint type */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output|Flags", meta = (PCG_Overridable, EditCondition="bFlagJoints"))
	FName JointFlagName = "JointType";

	/** Pick which value will be written for each joint type. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output|Flags", EditFixedSize, meta = (ReadOnlyKeys, DisplayName=" └─ Mapping", EditCondition="bFlagJoints", HideEditConditionToggle))
	TMap<EPCGExClipper2EndpointType, int32> JointTypeValueMapping;

	/** (DEBUG) If enabled, performs a union of all paths in the group before proceeding to the operation */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable), AdvancedDisplay)
	bool bUnionGroupBeforeOperation = false;

	/** (DEBUG) If enabled, performs a union of all paths in the operand group before proceeding to the operation */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable), AdvancedDisplay)
	bool bUnionOperandsBeforeOperation = false;

	UFUNCTION()
	virtual bool WantsDataMatching() const;

	UFUNCTION()
	virtual bool WantsOperands() const;

	virtual FPCGExGeo2DProjectionDetails GetProjectionDetails() const;
	virtual bool SupportOpenMainPaths() const;
	virtual bool SupportOpenOperandPaths() const;
	virtual bool OperandsAsBounds() const;
};

struct PCGEXELEMENTSCLIPPER2_API FPCGExClipper2ProcessorContext : FPCGExPathProcessorContext
{
	friend class FPCGExClipper2ProcessorElement;

	TSharedPtr<PCGExData::FPointIOCollection> OperandsCollection;

	TSharedPtr<PCGExClipper2::FOpData> AllOpData;

	// Unified processing groups
	TArray<TSharedPtr<PCGExClipper2::FProcessingGroup>> ProcessingGroups;

	FPCGExCarryOverDetails CarryOverDetails;
	FPCGExBlendingDetails BlendingDetails;

	FPCGExGeo2DProjectionDetails ProjectionDetails;

	TSharedPtr<PCGExData::FTags> GatherTags(const TArray<int8>& VisitedSources);

	/**
	 * Convert Clipper2 Paths64 results back to PCGEx point data with metadata blending.
	 *
	 * @param InPaths - The Clipper2 output paths to convert
	 * @param Group - The processing group containing source info and intersection blend data
	 * @param OutPaths - Output array of point IO objects
	 * @param bClosedPaths
	 * @param CallSiteIndex - Deterministic identifier for this call site within the group (e.g., 0=closed, 1=open)
	 * @param TransformMode - How to compute output transforms (FromSource for booleans, Unproject for offset/inflate)
	 */
	void OutputPaths64(
		PCGExClipper2Lib::Paths64& InPaths,
		const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group,
		TArray<TSharedPtr<PCGExData::FPointIO>>& OutPaths,
		bool bClosedPaths,
		int32 CallSiteIndex,
		PCGExClipper2::ETransformRestoration TransformMode = PCGExClipper2::ETransformRestoration::FromSource);

	/**
	 * Process a single group. Derived classes must implement this.
	 * @param Group - The processing group to operate on
	 */
	virtual void Process(const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group);
};

class PCGEXELEMENTSCLIPPER2_API FPCGExClipper2ProcessorElement : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(Clipper2Processor)


	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
	virtual void OutputWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const;

	int32 BuildDataFromCollection(
		FPCGExClipper2ProcessorContext* Context,
		const UPCGExClipper2ProcessorSettings* Settings,
		const TSharedPtr<PCGExData::FPointIOCollection>& Collection,
		bool bSupportOpenPaths, TArray<int32>& OutIndices) const;

	void BuildProcessingGroups(
		FPCGExClipper2ProcessorContext* Context,
		const UPCGExClipper2ProcessorSettings* Settings,
		const TArray<int32>& MainIndices,
		const TArray<int32>& OperandIndices) const;
};

namespace PCGExClipper2
{
}
