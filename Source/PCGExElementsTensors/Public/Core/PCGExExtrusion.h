// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPointFilter.h"
#include "Core/PCGExTensor.h"
#include "Data/PCGExData.h"
#include "Elements/PCGExTensorsTransform.h"
#include "Math/PCGExMath.h"
#include "Paths/PCGExPathIntersectionDetails.h"
#include "Paths/PCGExPathsCommon.h"
#include "Paths/PCGExPathsHelpers.h"

/**
 * PCGExExtrusion - Reusable extrusion classes for tensor-based path generation
 *
 * This module provides the core extrusion functionality that can be used by
 * multiple tensor-based path generation nodes. The classes here are decoupled
 * from specific node implementations via callback interfaces.
 */

namespace PCGExExtrusion
{
	//
	// State Machine
	//

	/** Extrusion state - tracks where in the lifecycle an extrusion is */
	enum class EExtrusionState : uint8
	{
		Probing,
		// Searching for valid start position (when starting in stop filter)
		Extruding,
		// Actively adding points to path
		Completed,
		// Finalized successfully, path is valid
		Stopped // Hit termination condition, no longer advancing
	};

	/** Reason why an extrusion stopped - flags can be combined */
	enum class EStopReason : uint16
	{
		None       = 0,
		Iterations = 1 << 0,
		// Ran out of iterations
		MaxLength = 1 << 1,
		// Hit max path length
		MaxPointCount = 1 << 2,
		// Hit max point count
		StopFilter = 1 << 3,
		// Hit stop filter boundary
		ExternalPath = 1 << 4,
		// Intersected external path
		SelfIntersection = 1 << 5,
		// Intersected another extrusion
		SelfMerge = 1 << 6,
		// Merged with another extrusion
		ClosedLoop = 1 << 7,
		// Detected closed loop back to origin
		SamplingFailed = 1 << 8 // Tensor sampling returned no result
	};

	ENUM_CLASS_FLAGS(EStopReason)

	/** Feature flags - determines which checks are enabled at runtime */
	enum class EExtrusionFlags : uint32
	{
		None    = 0,
		Bounded = 1 << 0,
		// Stop filters are enabled
		ClosedLoop = 1 << 1,
		AllowsChildren = 1 << 2,
		// Allow child extrusions after stopping
		CollisionCheck = 1 << 3,
	};

	ENUM_CLASS_FLAGS(EExtrusionFlags)

	constexpr bool HasFlag(const EExtrusionFlags Flags, EExtrusionFlags Flag)
	{
		return (static_cast<uint32>(Flags) & static_cast<uint32>(Flag)) != 0;
	}

	//
	// Configuration
	//

	/** Immutable configuration for extrusion behavior - set once at creation */
	struct PCGEXELEMENTSTENSORS_API FExtrusionConfig
	{
		// Transform settings
		bool bTransformRotation = true;
		EPCGExTensorTransformMode RotationMode = EPCGExTensorTransformMode::Align;
		EPCGExAxis AlignAxis = EPCGExAxis::Forward;

		// Limits
		double FuseDistance = DBL_COLLOCATION_TOLERANCE;
		double FuseDistanceSquared = DBL_COLLOCATION_TOLERANCE * DBL_COLLOCATION_TOLERANCE;
		EPCGExTensorStopConditionHandling StopHandling = EPCGExTensorStopConditionHandling::Exclude;
		bool bAllowChildExtrusions = false;

		bool bDoExternalIntersections = false;
		bool bIgnoreIntersectionOnOrigin = true;

		bool bDoSelfIntersections = false;
		bool bMergeOnProximity = false;
		double ProximitySegmentBalance = 0.5;

		bool bDetectClosedLoops = false;
		double ClosedLoopSquaredDistance = 0;
		double ClosedLoopSearchDot = 0;

		// Intersection details (cached)
		FPCGExPathIntersectionDetails ExternalPathIntersections;
		FPCGExPathIntersectionDetails SelfPathIntersections;
		FPCGExPathIntersectionDetails MergeDetails;

		// Computed flags
		EExtrusionFlags Flags = EExtrusionFlags::None;

		/** Compute flags from current settings */
		void ComputeFlags(bool bHasStopFilters, bool bHasExternalPaths);

		/** Initialize intersection details */
		void InitIntersectionDetails();
	};

	//
	// Collision Detection
	//

	/** Result of collision detection */
	struct PCGEXELEMENTSTENSORS_API FCollisionResult
	{
		bool bHasCollision = false;
		FVector Position = FVector::ZeroVector;
		EStopReason Reason = EStopReason::None;
		int32 OtherIndex = -1; // Index of other path/extrusion if applicable

		explicit operator bool() const
		{
			return bHasCollision;
		}

		void Set(const FVector& InPosition, EStopReason InReason, int32 InOtherIndex = -1)
		{
			bHasCollision = true;
			Position = InPosition;
			Reason = InReason;
			OtherIndex = InOtherIndex;
		}
	};

	//
	// Branching Support
	//

	/** Branch point for future branching extrusions */
	struct PCGEXELEMENTSTENSORS_API FBranchPoint
	{
		int32 SourceExtrusionIndex = -1;
		int32 PointIndex = -1;
		FTransform BranchTransform = FTransform::Identity;
		FVector BranchDirection = FVector::ZeroVector;
		double BranchAngle = 0.0; // Angle from main direction
	};

	//
	// Callbacks Interface
	//

	class FExtrusion;

	/** Callback for when a child extrusion needs to be created */
	using FChildExtrusionCallback = std::function<TSharedPtr<FExtrusion>(const TSharedRef<FExtrusion>& Parent)>;

	/** Callback for applying tags to completed extrusions */
	using FApplyTagsCallback = std::function<void(FExtrusion& Extrusion, PCGExData::FPointIO& Source)>;

	/** Callback for validating path before completion */
	using FValidatePathCallback = std::function<bool(int32 PointCount)>;

	/** Collection of callbacks for decoupled extrusion operations */
	struct PCGEXELEMENTSTENSORS_API FExtrusionCallbacks
	{
		/** Called when a child extrusion should be created (for AllowsChildren) */
		FChildExtrusionCallback OnCreateChild;

		/** Called to apply tags based on stop reason */
		FApplyTagsCallback OnApplyTags;

		/** Called to validate path point count before finalizing */
		FValidatePathCallback OnValidatePath;
	};

	//
	// Extrusion Class
	//

	/**
	 * FExtrusion - Single extrusion instance that advances along tensor field
	 *
	 * Lifecycle:
	 *   1. Constructed with seed point and configuration
	 *   2. Advance() called each iteration to sample tensor and move head
	 *   3. Collision checks performed (if enabled)
	 *   4. Points inserted into path
	 *   5. Complete() called when stopped or finished
	 *
	 * Decoupling:
	 *   - Uses FExtrusionCallbacks for owner communication
	 *   - No direct dependency on specific processor types
	 *   - Can be used by multiple tensor-based path generation nodes
	 */
	class PCGEXELEMENTSTENSORS_API FExtrusion : public TSharedFromThis<FExtrusion>
	{
	public:
		FExtrusion(const int32 InSeedIndex, const TSharedRef<PCGExData::FFacade>& InFacade, const int32 InMaxIterations, const FExtrusionConfig& InConfig);
		virtual ~FExtrusion() = default;

		//~ State
		EExtrusionState State = EExtrusionState::Probing;
		EStopReason StopReason = EStopReason::None;

		// bIsExtruding stays true once set, even after completion
		// This is needed for self-intersection checks to work correctly
		bool bIsExtruding = false;

		//~ Configuration (immutable after construction)
		const FExtrusionConfig& Config;
		EExtrusionFlags Flags = EExtrusionFlags::None;

		//~ Callbacks for decoupled operations
		FExtrusionCallbacks Callbacks;

		//~ Hierarchy (for child/branching extrusions)
		TWeakPtr<FExtrusion> ParentExtrusion;
		bool bIsChildExtrusion = false;
		bool bIsFollowUp = false;

		//~ Shared resources (set by owner)
		TSharedPtr<TArray<TSharedPtr<PCGExPaths::FPath>>> SolidPaths;   // For self-intersection (grows as extrusions complete)
		TArray<TSharedPtr<PCGExPaths::FPath>>* ExternalPaths = nullptr; // For external path intersection
		TSharedPtr<PCGExTensor::FTensorsHandler> TensorsHandler;
		TSharedPtr<PCGExPointFilter::FManager> StopFilters;

		//~ Head position and direction
		FTransform Head = FTransform::Identity;
		FVector ExtrusionDirection = FVector::ZeroVector;

		//~ Seed and limits
		int32 SeedIndex = -1;
		int32 RemainingIterations = 0;
		double MaxLength = TNumericLimits<double>::Max();
		int32 MaxPointCount = MAX_int32;

		//~ Path data
		PCGExPaths::FPathMetrics Metrics;
		TSharedRef<PCGExData::FFacade> PointDataFacade;
		FBox Bounds = FBox(ForceInit);

		//~ Future branching support
		TArray<FBranchPoint> PendingBranches;

		//~ Main interface
		bool Advance();
		void Complete();

		//~ Collision interface
		PCGExMath::FSegment GetHeadSegment() const;        // Segment between last two inserted points
		PCGExMath::FSegment GetCurrentHeadSegment() const; // Segment from last inserted point to current head position
		PCGExMath::FClosestPosition FindCrossing(const PCGExMath::FSegment& InSegment, bool& OutIsLastSegment, PCGExMath::FClosestPosition& OutClosestPosition, int32 TruncateSearch = 0) const;
		bool TryMerge(const PCGExMath::FSegment& InSegment, const PCGExMath::FClosestPosition& InMerge);

		//~ Modification
		void SetHead(const FTransform& InHead);
		void CutOff(const FVector& InCutOff);
		void Shorten(const FVector& InCutOff);
		void Cleanup();

		//~ State queries
		FORCEINLINE bool IsActive() const
		{
			return State == EExtrusionState::Probing || State == EExtrusionState::Extruding;
		}

		FORCEINLINE bool IsComplete() const
		{
			return State == EExtrusionState::Completed || State == EExtrusionState::Stopped;
		}

		FORCEINLINE bool IsValidPath() const
		{
			return (State == EExtrusionState::Completed || State == EExtrusionState::Stopped) && ExtrudedPoints.Num() >= 2;
		}

		FORCEINLINE int32 GetPointCount() const
		{
			return ExtrudedPoints.Num();
		}

		FORCEINLINE bool HasStopReason(EStopReason InReason) const
		{
			return EnumHasAnyFlags(StopReason, InReason);
		}

		FORCEINLINE bool AdvancedOnly() const
		{
			return bAdvancedOnly;
		}

	protected:
		//~ Internal data
		TArray<FTransform> ExtrudedPoints;
		TArray<FBox> SegmentBounds;
		double DistToLastSum = 0;
		PCGExData::FConstPoint Origin;
		PCGExData::FProxyPoint ProxyHead;
		FVector LastInsertion = FVector::ZeroVector;
		FTransform ActiveTransform = FTransform::Identity;
		bool bAdvancedOnly = false;

		//~ Internal methods
		void ApplyRotation(const PCGExTensor::FTensorSample& Sample);
		bool CheckClosedLoop(const FVector& PreviousHeadLocation);
		bool CheckStopFilters();
		bool CheckCollisions(const PCGExMath::FSegment& Segment);
		FCollisionResult CheckExternalIntersection(const PCGExMath::FSegment& Segment);
		FCollisionResult CheckSelfIntersection(const PCGExMath::FSegment& Segment, PCGExMath::FClosestPosition& OutMerge);
		FCollisionResult ResolveCollisionPriority(const FCollisionResult& Crossing, const FCollisionResult& Merge);

		void Insert(const FTransform& InPoint);
		void StartNewExtrusion();
		void Stop(EStopReason InReason);
	};
}
