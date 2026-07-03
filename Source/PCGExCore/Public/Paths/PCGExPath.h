// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <atomic>
#include <functional>
#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "PCGExCoreMacros.h"
#include "PCGExOctree.h"
#include "Math/PCGExMath.h"
#include "Math/PCGExProjectionDetails.h"
#include "Utils/PCGValueRange.h"

class UPCGBasePointData;
struct FPCGExPathIntersectionDetails;

namespace PCGExMT
{
	struct FScope;
}

struct FPCGExContext;
class UPCGPolygon2DData;
class UPCGSplineData;

namespace PCGExData
{
	class FPointIO;
	struct FElement;
	class FFacadePreloader;

	template <typename T>
	class TBuffer;
}

namespace PCGExPaths
{
	PCGEXCORE_API void BuildProjectedPoints2D(
		const TConstPCGValueRange<FTransform>& Positions,
		const FPCGExGeo2DProjectionDetails& Projection,
		TArray<FVector2D>& OutProjectedPoints,
		FBox2D& OutBounds);

	struct PCGEXCORE_API FPathEdge
	{
		int32 Start = -1;
		int32 End = -1;
		FVector Dir = FVector::ZeroVector;
		double Length = 0;
		FBoxSphereBounds Bounds = FBoxSphereBounds{};

		int32 AltStart = -1;

		FPathEdge(const int32 InStart, const int32 InEnd, const TConstPCGValueRange<FTransform>& Positions, const double Expansion = 0);

		void Update(const TConstPCGValueRange<FTransform>& Positions, const double Expansion = 0);

		bool ShareIndices(const FPathEdge& Other) const;
		bool Connects(const FPathEdge& Other) const;
		bool ShareIndices(const FPathEdge* Other) const;
		FORCEINLINE double GetLength() const
		{
			return Length;
		}
	};

	class FPath;

	class PCGEXCORE_API IPathEdgeExtra : public TSharedFromThis<IPathEdgeExtra>
	{
	protected:
		bool bClosedLoop = false;

	public:
		explicit IPathEdgeExtra(const int32 InNumSegments, bool InClosedLoop)
			: bClosedLoop(InClosedLoop)
		{
		}

		virtual ~IPathEdgeExtra() = default;

		virtual void ProcessSingleEdge(const FPath* Path, const FPathEdge& Edge)
		{
			ProcessFirstEdge(Path, Edge);
		}

		virtual void ProcessFirstEdge(const FPath* Path, const FPathEdge& Edge)
		{
			ProcessEdge(Path, Edge);
		};
		virtual void ProcessEdge(const FPath* Path, const FPathEdge& Edge) = 0;

		virtual void ProcessLastEdge(const FPath* Path, const FPathEdge& Edge)
		{
			ProcessEdge(Path, Edge);
		}

		virtual void ProcessingDone(const FPath* Path);
	};

	template <typename T>
	class PCGEXCORE_API TPathEdgeExtra : public IPathEdgeExtra
	{
	protected:
		TArray<T> Data;

	public:
		TArray<T> Values;

		explicit TPathEdgeExtra(const int32 InNumSegments, bool InClosedLoop)
			: IPathEdgeExtra(InNumSegments, InClosedLoop)
		{
			if constexpr (std::is_trivially_copyable_v<T>)
			{
				Data.SetNumUninitialized(InNumSegments);
			}
			else
			{
				Data.SetNum(InNumSegments);
			}
		}

		FORCEINLINE T& operator[](const int32 At)
		{
			return Data[At];
		}

		FORCEINLINE T operator[](const int32 At) const
		{
			return Data[At];
		}

		FORCEINLINE void Set(const int32 At, const T Value)
		{
			Data[At] = Value;
		}

		FORCEINLINE T Get(const int32 At)
		{
			return Data[At];
		}

		FORCEINLINE T& GetMutable(const int32 At)
		{
			return Data[At];
		}

		FORCEINLINE T Get(const FPathEdge& At)
		{
			return Data[At.Start];
		}
	};

	PCGEX_OCTREE_SEMANTICS(FPathEdge, { return Element->Bounds;}, { return A == B; })

	class PCGEXCORE_API FPath : public TSharedFromThis<FPath>
	{
	protected:
		bool bClosedLoop = false;
		TConstPCGValueRange<FTransform> Positions;
		TUniquePtr<FPathEdgeOctree> EdgeOctree;
		TArray<TSharedPtr<IPathEdgeExtra>> Extras;

		TArray<FVector2D> ProjectedPoints;
		FPCGExGeo2DProjectionDetails Projection;
		FBox2D ProjectedBounds = FBox2D();

	public:
		explicit FPath(const bool IsClosed = false);
		FPath(const TConstPCGValueRange<FTransform>& InTransforms, const bool IsClosed, const double Expansion = 0);
		explicit FPath(const UPCGBasePointData* InPointData, const double Expansion = 0);
		virtual ~FPath() = default;

		FBox Bounds = FBox(ForceInit);
		TArray<FPathEdge> Edges;
		int32 NumPoints = 0;
		int32 NumEdges = 0;
		int32 LastIndex = 0;
		int32 LastEdge = 0;
		int32 Idx = -1;

		int32 ConvexitySign = 0;
		bool bIsConvex = true;

		int32 IOIndex = -1;
		double TotalLength = 0;

		PCGExMT::FScope GetEdgeScope(const int32 InLoopIndex = -1) const;

		const TConstPCGValueRange<FTransform>& GetPositions() const
		{
			return Positions;
		}

		int32 LoopPointIndex(const int32 Index) const;
		int32 SafePointIndex(const int32 Index) const;

		FORCEINLINE virtual FVector GetPos(const int32 Index) const
		{
			return Positions[SafePointIndex(Index)].GetLocation();
		}

		FORCEINLINE virtual FVector GetPos_Unsafe(const int32 Index) const
		{
			return Positions[Index].GetLocation();
		}

		FORCEINLINE bool IsValidEdgeIndex(const int32 Index) const
		{
			return Index >= 0 && Index < NumEdges;
		}

		virtual FVector DirToNextPoint(const int32 Index) const;

		FVector DirToPrevPoint(const int32 Index) const
		{
			return DirToNextPoint(SafePointIndex(Index - 1)) * -1;
		}

		FVector DirToNeighbor(const int32 Index, const int32 Offset) const;

		virtual int32 NextPointIndex(const int32 Index) const
		{
			return SafePointIndex(Index + 1);
		}

		virtual int32 PrevPointIndex(const int32 Index) const
		{
			return SafePointIndex(Index - 1);
		}

		FVector GetEdgePositionAtAlpha(const FPathEdge& Edge, const double Alpha) const
		{
			return FMath::Lerp(Positions[Edge.End].GetLocation(), Positions[Edge.Start].GetLocation(), Alpha);
		}

		FVector GetEdgePositionAtAlpha(const int32 Index, const double Alpha) const
		{
			const FPathEdge& Edge = Edges[Index];
			return FMath::Lerp(Positions[Edge.Start].GetLocation(), Positions[Edge.End].GetLocation(), Alpha);
		}

		virtual bool IsEdgeValid(const FPathEdge& Edge) const
		{
			return FVector::DistSquared(GetPos_Unsafe(Edge.Start), GetPos_Unsafe(Edge.End)) > 0;
		}

		virtual bool IsEdgeValid(const int32 Index) const
		{
			return IsEdgeValid(Edges[Index]);
		}

		PCGExMath::FClosestPosition FindClosestIntersection(const FPCGExPathIntersectionDetails& InDetails, const PCGExMath::FSegment& Segment) const;

		PCGExMath::FClosestPosition FindClosestIntersection(const FPCGExPathIntersectionDetails& InDetails, const PCGExMath::FSegment& Segment, PCGExMath::FClosestPosition& OutClosestPosition) const;

		void BuildEdgeOctree();
		void BuildPartialEdgeOctree(const TArray<int8>& Filter);
		void BuildPartialEdgeOctree(const TBitArray<>& Filter);

		const FPathEdgeOctree* GetEdgeOctree() const
		{
			return EdgeOctree.Get();
		}

		FORCEINLINE bool IsClosedLoop() const
		{
			return bClosedLoop;
		}

		void UpdateConvexity(const int32 Index);

		template <typename T, typename... Args>
		TSharedPtr<T> AddExtra(const bool bImmediateCompute = false, Args&&... InArgs)
		{
			PCGEX_MAKE_SHARED(Extra, T, NumEdges, bClosedLoop, std::forward<Args>(InArgs)...)

			if (bImmediateCompute)
			{
				if (NumEdges == 1)
				{
					Extra->ProcessSingleEdge(this, Edges[0]);
				}
				else
				{
					if (bClosedLoop)
					{
						for (int i = 0; i < NumEdges; ++i)
						{
							Extra->ProcessEdge(this, Edges[i]);
						}
					}
					else
					{
						Extra->ProcessFirstEdge(this, Edges[0]);
						for (int i = 1; i < LastEdge; ++i)
						{
							Extra->ProcessEdge(this, Edges[i]);
						}
						Extra->ProcessLastEdge(this, Edges[LastEdge]);
					}
				}

				Extra->ProcessingDone(this);
			}
			else
			{
				Extras.Add(Extra);
			}

			return Extra;
		}

		virtual void ComputeEdgeExtra(const int32 Index);
		virtual void ExtraComputingDone();
		virtual void ComputeAllEdgeExtra();

		virtual FTransform GetClosestTransform(const FVector& WorldPosition, int32& OutEdgeIndex, float& OutLerp, const bool bUseScale = false) const PCGEX_NOT_IMPLEMENTED_RET(GetClosestTransform(const FVector& WorldPosition, int32& OutEdgeIndex, float& OutLerp), FTransform::Identity);

		virtual FTransform GetClosestTransform(const FVector& WorldPosition, float& OutAlpha, const bool bUseScale = false) const PCGEX_NOT_IMPLEMENTED_RET(GetClosestTransformAndKey(const FVector& WorldPosition, float& OutAlpha), FTransform::Identity);

		virtual FTransform GetClosestTransform(const FVector& WorldPosition, bool& bIsInside, const bool bUseScale) const PCGEX_NOT_IMPLEMENTED_RET(GetClosestTransform(const FVector& WorldPosition, bool& bIsInside), FTransform::Identity);

		virtual FTransform GetClosestTransform(const FVector& WorldPosition, const bool bUseScale) const PCGEX_NOT_IMPLEMENTED_RET(GetClosestTransform(const FVector& WorldPosition), FTransform::Identity);

		virtual bool GetClosestPosition(const FVector& WorldPosition, FVector& OutPosition) const PCGEX_NOT_IMPLEMENTED_RET(GetClosestTransform(const FVector& WorldPosition), false);

		virtual bool GetClosestPosition(const FVector& WorldPosition, FVector& OutPosition, bool& bIsInside) const PCGEX_NOT_IMPLEMENTED_RET(GetClosestTransform(const FVector& WorldPosition), false);

		virtual int32 GetClosestEdge(const FVector& WorldPosition, float& OutLerp) const PCGEX_NOT_IMPLEMENTED_RET(GetClosestEdge(const FVector& WorldPosition, float& OutLerp), - 1);

		virtual int32 GetClosestEdge(const double InTime, float& OutLerp) const PCGEX_NOT_IMPLEMENTED_RET(GetClosestEdge(const double InTime, float& OutLerp), - 1);

		void BuildProjection();
		void BuildProjection(const FPCGExGeo2DProjectionDetails& InProjectionDetails);
		void OffsetProjection(const double Offset);

		const TArray<FVector2D>& GetProjectedPoints() const
		{
			return ProjectedPoints;
		}

		const FPCGExGeo2DProjectionDetails& GetProjection() const
		{
			return Projection;
		}

		virtual bool IsInsideProjection(const FVector& WorldPosition) const;
		virtual bool Contains(const TConstPCGValueRange<FTransform>& InPositions, const double Tolerance = 0) const;

	protected:
		void BuildPath(const double Expansion);
	};

#pragma region Edge Extras

	template <typename T>
	class FPathEdgeCustomData : public TPathEdgeExtra<T>
	{
	public:
		using ProcessEdgeFunc = std::function<T(const FPath*, const FPathEdge&)>;
		ProcessEdgeFunc ProcessEdgeCallback;

		explicit FPathEdgeCustomData(const int32 InNumSegments, const bool InClosedLoop, ProcessEdgeFunc&& Func)
			: TPathEdgeExtra<T>(InNumSegments, InClosedLoop)
			  , ProcessEdgeCallback(Func)
		{
		}

		virtual void ProcessEdge(const FPath* Path, const FPathEdge& Edge) override
		{
			this->SetValue(Edge.Start, ProcessEdgeCallback(Path, Edge));
		}
	};

	class PCGEXCORE_API FPathEdgeLength : public TPathEdgeExtra<double>
	{
	public:
		double TotalLength = 0;
		TArray<double> CumulativeLength;

		explicit FPathEdgeLength(const int32 InNumSegments, const bool InClosedLoop)
			: TPathEdgeExtra(InNumSegments, InClosedLoop)
		{
		}

		virtual void ProcessEdge(const FPath* Path, const FPathEdge& Edge) override;
		virtual void ProcessingDone(const FPath* Path) override;
	};

	class PCGEXCORE_API FPathEdgeLengthSquared : public TPathEdgeExtra<double>
	{
	public:
		explicit FPathEdgeLengthSquared(const int32 InNumSegments, const bool InClosedLoop)
			: TPathEdgeExtra(InNumSegments, InClosedLoop)
		{
		}

		virtual void ProcessEdge(const FPath* Path, const FPathEdge& Edge) override;
	};

	class PCGEXCORE_API FPathEdgeNormal : public TPathEdgeExtra<FVector>
	{
		FVector Up = FVector::UpVector;

	public:
		explicit FPathEdgeNormal(const int32 InNumSegments, const bool InClosedLoop, const FVector& InUp)
			: TPathEdgeExtra(InNumSegments, InClosedLoop)
			  , Up(InUp)
		{
		}

		virtual void ProcessEdge(const FPath* Path, const FPathEdge& Edge) override;
	};

	class PCGEXCORE_API FPathEdgeBinormal : public TPathEdgeExtra<FVector>
	{
		FVector Up = FVector::UpVector;

	public:
		TArray<FVector> Normals;

		explicit FPathEdgeBinormal(const int32 InNumSegments, const bool InClosedLoop, const FVector& InUp = FVector::UpVector)
			: TPathEdgeExtra(InNumSegments, InClosedLoop)
			  , Up(InUp)
		{
			Normals.SetNumUninitialized(InNumSegments);
		}

		virtual void ProcessFirstEdge(const FPath* Path, const FPathEdge& Edge) override;
		virtual void ProcessEdge(const FPath* Path, const FPathEdge& Edge) override;
	};

	class PCGEXCORE_API FPathEdgeAvgNormal : public TPathEdgeExtra<FVector>
	{
		FVector Up = FVector::UpVector;

	public:
		explicit FPathEdgeAvgNormal(const int32 InNumSegments, const bool InClosedLoop, const FVector& InUp = FVector::UpVector)
			: TPathEdgeExtra(InNumSegments, InClosedLoop)
			  , Up(InUp)
		{
		}

		virtual void ProcessFirstEdge(const FPath* Path, const FPathEdge& Edge) override;
		virtual void ProcessEdge(const FPath* Path, const FPathEdge& Edge) override;
	};

	/** Torsion-minimizing edge normal (parallel transport): the first edge's normal is carried along
	 * the path, rotated at each vertex by the minimal rotation between consecutive edge directions,
	 * instead of being re-derived from the Up vector on every edge. Immune to the vertical-tangent
	 * singularity of the Up-cross based providers; on closed loops the residual seam twist is
	 * distributed along the path. The full chain is computed once, on whichever edge callback fires
	 * first — safe with both sequential and scoped/parallel extra computation. */
	class PCGEXCORE_API FPathEdgeParallelTransportNormal : public TPathEdgeExtra<FVector>
	{
		FVector Up = FVector::UpVector;
		std::atomic<bool> bComputed{false};
		FCriticalSection ComputeLock;

	public:
		explicit FPathEdgeParallelTransportNormal(const int32 InNumSegments, const bool InClosedLoop, const FVector& InUp = FVector::UpVector)
			: TPathEdgeExtra(InNumSegments, InClosedLoop)
			  , Up(InUp)
		{
		}

		virtual void ProcessSingleEdge(const FPath* Path, const FPathEdge& Edge) override;
		virtual void ProcessFirstEdge(const FPath* Path, const FPathEdge& Edge) override;
		virtual void ProcessEdge(const FPath* Path, const FPathEdge& Edge) override;
		virtual void ProcessLastEdge(const FPath* Path, const FPathEdge& Edge) override;

	protected:
		void ComputeAll(const FPath* Path);
	};

	class PCGEXCORE_API FPathEdgeHalfAngle : public TPathEdgeExtra<double>
	{
		FVector Up = FVector::UpVector;

	public:
		explicit FPathEdgeHalfAngle(const int32 InNumSegments, const bool InClosedLoop, const FVector& InUp = FVector::UpVector)
			: TPathEdgeExtra(InNumSegments, InClosedLoop)
			  , Up(InUp)
		{
		}

		virtual void ProcessFirstEdge(const FPath* Path, const FPathEdge& Edge) override;
		virtual void ProcessEdge(const FPath* Path, const FPathEdge& Edge) override;
	};

	class PCGEXCORE_API FPathEdgeFullAngle : public TPathEdgeExtra<double>
	{
		FVector Up = FVector::UpVector;

	public:
		explicit FPathEdgeFullAngle(const int32 InNumSegments, const bool InClosedLoop, const FVector& InUp = FVector::UpVector)
			: TPathEdgeExtra(InNumSegments, InClosedLoop)
			  , Up(InUp)
		{
		}

		virtual void ProcessFirstEdge(const FPath* Path, const FPathEdge& Edge) override;
		virtual void ProcessEdge(const FPath* Path, const FPathEdge& Edge) override;
	};

#pragma endregion
}
