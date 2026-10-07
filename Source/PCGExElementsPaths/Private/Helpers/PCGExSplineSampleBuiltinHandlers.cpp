// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExSplineSampleBuiltinHandlers.h"

#include "Algo/BinarySearch.h"
#include "Algo/Reverse.h"
#include "Components/SplineComponent.h"
#include "Containers/BitArray.h"
#include "GameFramework/Actor.h"
#include "LandscapeSplineControlPoint.h"
#include "LandscapeSplineSegment.h"
#include "LandscapeSplinesComponent.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

#include "Paths/PCGExPathsCommon.h" // FPathMetrics

namespace PCGExSplineSampling::BuiltinSources
{
	// Component class paths, so classes from modules PCGEx doesn't link (Water) can be named.
	FTopLevelAssetPath SplineComponentPath() { return FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("SplineComponent")); }
	FTopLevelAssetPath WaterSplineComponentPath() { return FTopLevelAssetPath(TEXT("/Script/Water"), TEXT("WaterSplineComponent")); }
	FTopLevelAssetPath LandscapeSplinesComponentPath() { return FTopLevelAssetPath(TEXT("/Script/Landscape"), TEXT("LandscapeSplinesComponent")); }

	// Captured at registration so unregistering at shutdown never touches UObjects.
	TArray<const UClass*> RegisteredHandlers;

	FSplineExtent MakeExtent(const EExtentSource Source, const double Value, const bool bComponentScale)
	{
		FSplineExtent Extent;
		Extent.Source = Source;
		Extent.Value = Value;
		Extent.bComponentScale = bComponentScale;
		return Extent;
	}

	FSplineExtent MakeCurveExtent(FInterpCurveFloat&& Curve, const double Value, const bool bComponentScale)
	{
		FSplineExtent Extent = MakeExtent(EExtentSource::Curve, Value, bComponentScale);
		Extent.Curve = MoveTemp(Curve);
		return Extent;
	}

#pragma region Spline sources

	bool ReadsPointScale(const FSplineExtent& Extent)
	{
		return Extent.Source == EExtentSource::ScaleX || Extent.Source == EExtentSource::ScaleY || Extent.Source == EExtentSource::ScaleZ;
	}

	double ResolveExtent(const FSplineExtent& Extent, const float Key, const FVector& PointScale, const double Stretch)
	{
		double Value = Extent.Value;
		switch (Extent.Source)
		{
		case EExtentSource::ScaleX: Value *= PointScale.X;
			break;
		case EExtentSource::ScaleY: Value *= PointScale.Y;
			break;
		case EExtentSource::ScaleZ: Value *= PointScale.Z;
			break;
		case EExtentSource::Curve: Value *= Extent.Curve.Eval(Key, 0.0f);
			break;
		default: break;
		}

		if (Extent.bComponentScale) { Value *= Stretch; }
		return FMath::IsFinite(Value) ? FMath::Max(0.0, Value) : 0.0;
	}

	// Plain FSplineCurves: read-only to evaluate, where the FSpline backend lazily rebuilds legacy curves, unlocked.
	class FSplineSource final : public FSource
	{
	public:
		FSplineCurves Curves;
		FTransform Transform = FTransform::Identity;
		FVector DefaultUpVector = FVector::UpVector;
		int32 ReparamStepsPerSegment = 10;
		FSplineExtents Extents;

		virtual void Prepare() override
		{
			// Rebuilt at world scale (see FSplineCurves::UpdateSpline): the copied table may be local-space or stale.
			const FVector Scale3D = Transform.GetScale3D();
			const bool bLooped = Curves.Position.bIsLooped;
			const int32 NumPoints = Curves.Position.Points.Num();
			const int32 NumSegments = bLooped ? NumPoints : FMath::Max(0, NumPoints - 1);

			FInterpCurveFloat& Reparam = Curves.ReparamTable;
			Reparam.Points.Reset(NumSegments * ReparamStepsPerSegment + 1);

			float AccumulatedLength = 0.0f;
			for (int32 SegmentIndex = 0; SegmentIndex < NumSegments; SegmentIndex++)
			{
				for (int32 Step = 0; Step < ReparamStepsPerSegment; Step++)
				{
					const float Param = static_cast<float>(Step) / ReparamStepsPerSegment;
					const float SegmentLength = Step == 0 ? 0.0f : Curves.GetSegmentLength(SegmentIndex, Param, bLooped, Scale3D);
					Reparam.Points.Emplace(SegmentLength + AccumulatedLength, SegmentIndex + Param, 0.0f, 0.0f, CIM_Linear);
				}
				AccumulatedLength += Curves.GetSegmentLength(SegmentIndex, 1.0f, bLooped, Scale3D);
			}
			Reparam.Points.Emplace(AccumulatedLength, static_cast<float>(NumSegments), 0.0f, 0.0f, CIM_Linear);

			Length = AccumulatedLength;

			const FVector AbsScale = Scale3D.GetAbs();
			bUniformScale = FMath::IsNearlyEqual(AbsScale.X, AbsScale.Y, UE_KINDA_SMALL_NUMBER) && FMath::IsNearlyEqual(AbsScale.X, AbsScale.Z, UE_KINDA_SMALL_NUMBER);
			bReadPointScale = ReadsPointScale(Extents.Lateral) || ReadsPointScale(Extents.Above) || ReadsPointScale(Extents.Below);
			bStretch = Extents.Lateral.bComponentScale || Extents.Above.bComponentScale || Extents.Below.bComponentScale;
		}

		virtual void Evaluate(TConstArrayView<double> Distances, TArrayView<FStation> OutStations) const override
		{
			const FQuat WorldRotation = Transform.GetRotation();
			const FVector Scale3D = Transform.GetScale3D();
			const double UniformStretch = FMath::Abs(Scale3D.X);

			for (int32 i = 0; i < Distances.Num(); i++)
			{
				const float Key = Curves.ReparamTable.Eval(static_cast<float>(Distances[i]), 0.0f);
				const FVector LocalUp = Curves.Rotation.Eval(Key, FQuat::Identity).GetNormalized().RotateVector(DefaultUpVector);

				FStation& Station = OutStations[i];
				Station.Position = Transform.TransformPosition(Curves.Position.Eval(Key, FVector::ZeroVector));
				Station.Up = WorldRotation.RotateVector(LocalUp).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);

				// Component stretch along the station's own lateral and up axes; a single factor when uniform.
				double LateralStretch = UniformStretch;
				double UpStretch = UniformStretch;
				if (bStretch && !bUniformScale)
				{
					const FVector LocalForward = Curves.Position.EvalDerivative(Key, FVector::ZeroVector).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
					const FVector LocalRight = FVector::CrossProduct(LocalUp, LocalForward).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
					LateralStretch = (Scale3D * LocalRight).Size();
					UpStretch = (Scale3D * LocalUp.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector)).Size();
				}

				const FVector PointScale = bReadPointScale ? Curves.Scale.Eval(Key, FVector::OneVector).GetAbs() : FVector::OneVector;
				Station.Left = Station.Right = ResolveExtent(Extents.Lateral, Key, PointScale, LateralStretch);
				Station.Above = ResolveExtent(Extents.Above, Key, PointScale, UpStretch);
				Station.Below = ResolveExtent(Extents.Below, Key, PointScale, UpStretch);
			}
		}

	private:
		bool bUniformScale = true;
		bool bReadPointScale = false;
		bool bStretch = false;
	};

	// Same read as AWaterBody::GetWaterBodyType (the CDO), through reflection. None when not a water body.
	FName GetWaterBodyType(const AActor* Owner)
	{
		if (!Owner) { return NAME_None; }

		const UClass* OwnerClass = Owner->GetClass();
		const FEnumProperty* TypeProperty = CastField<FEnumProperty>(OwnerClass->FindPropertyByName(TEXT("WaterBodyType")));
		if (!TypeProperty) { return NAME_None; }

		const void* Value = TypeProperty->ContainerPtrToValuePtr<void>(OwnerClass->GetDefaultObject());
		const int64 Type = TypeProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value);
		return FName(*TypeProperty->GetEnum()->GetNameStringByValue(Type));
	}

	bool CopyMetadataCurve(const UObject* Metadata, const FName Name, FInterpCurveFloat& OutCurve)
	{
		const FStructProperty* Property = Metadata ? CastField<FStructProperty>(Metadata->GetClass()->FindPropertyByName(Name)) : nullptr;
		if (!Property || Property->Struct != TBaseStructure<FInterpCurveFloat>::Get()) { return false; }

		OutCurve = *Property->ContainerPtrToValuePtr<FInterpCurveFloat>(Metadata);
		return OutCurve.Points.Num() > 0;
	}

#pragma endregion

#pragma region Landscape sources

	struct FLandscapeLocalPoint
	{
		FVector Center;
		FVector Left;  // Left edge as walked
		FVector Right; // Right edge as walked
	};

	struct FLandscapeVertex
	{
		FVector Center = FVector::ZeroVector;
		FVector Up = FVector::UpVector;
		double Left = 0;
		double Right = 0;
		double Distance = 0; // Cumulative world arc length
	};

	// One chain of a landscape spline network: interp points in component space until Prepare flattens them.
	class FLandscapeChainSource final : public FSource
	{
	public:
		FTransform Transform = FTransform::Identity;
		TArray<FLandscapeLocalPoint> LocalPoints;
		double Above = 0;
		double Below = 0;

		virtual void Prepare() override
		{
			Vertices.Reserve(LocalPoints.Num());

			PCGExPaths::FPathMetrics Metrics;
			for (const FLandscapeLocalPoint& Point : LocalPoints)
			{
				const FVector Center = Transform.TransformPosition(Point.Center);

				// Segment joints repeat their control point: skip zero-length steps.
				if (Metrics.Count > 0 && Metrics.IsLastWithinRange(Center, UE_KINDA_SMALL_NUMBER)) { continue; }

				const FVector LeftEdge = Transform.TransformPosition(Point.Left);
				const FVector RightEdge = Transform.TransformPosition(Point.Right);

				FLandscapeVertex& Vertex = Vertices.Emplace_GetRef();
				Vertex.Center = Center;
				Vertex.Up = RightEdge - LeftEdge; // Lateral for now, turned into Up below
				Vertex.Left = FVector::Dist(Center, LeftEdge);
				Vertex.Right = FVector::Dist(Center, RightEdge);
				Vertex.Distance = Metrics.Add(Center);
			}

			LocalPoints.Empty();

			const int32 NumVertices = Vertices.Num();
			if (NumVertices < 2)
			{
				Vertices.Empty();
				return;
			}

			// Central differences; a closed loop's first and last vertices coincide. Forward x Right = Up.
			const FVector FallbackUp = Transform.GetUnitAxis(EAxis::Z);
			for (int32 i = 0; i < NumVertices; i++)
			{
				const int32 PrevIndex = i > 0 ? i - 1 : (bClosedLoop ? NumVertices - 2 : 0);
				const int32 NextIndex = i < NumVertices - 1 ? i + 1 : (bClosedLoop ? 1 : NumVertices - 1);
				const FVector Forward = (Vertices[NextIndex].Center - Vertices[PrevIndex].Center).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);

				FLandscapeVertex& Vertex = Vertices[i];
				Vertex.Up = FVector::CrossProduct(Forward, Vertex.Up).GetSafeNormal(UE_SMALL_NUMBER, FallbackUp);
			}

			Length = Vertices.Last().Distance;
		}

		virtual void Evaluate(TConstArrayView<double> Distances, TArrayView<FStation> OutStations) const override
		{
			const int32 LastSpan = Vertices.Num() - 2;
			if (Distances.IsEmpty() || LastSpan < 0) { return; }

			// Distances ascend within a call: locate the first span once, then walk forward.
			int32 Span = FMath::Clamp(Algo::UpperBoundBy(Vertices, Distances[0], &FLandscapeVertex::Distance) - 1, 0, LastSpan);

			for (int32 i = 0; i < Distances.Num(); i++)
			{
				const double Distance = Distances[i];
				while (Span < LastSpan && Vertices[Span + 1].Distance <= Distance) { Span++; }

				// Spans are never empty: Prepare drops zero-length steps.
				const FLandscapeVertex& A = Vertices[Span];
				const FLandscapeVertex& B = Vertices[Span + 1];
				const double Alpha = FMath::Clamp((Distance - A.Distance) / (B.Distance - A.Distance), 0.0, 1.0);

				FStation& Station = OutStations[i];
				Station.Position = FMath::Lerp(A.Center, B.Center, Alpha);
				Station.Up = FMath::Lerp(A.Up, B.Up, Alpha).GetSafeNormal(UE_SMALL_NUMBER, A.Up);
				Station.Left = FMath::Lerp(A.Left, B.Left, Alpha);
				Station.Right = FMath::Lerp(A.Right, B.Right, Alpha);
				Station.Above = Above;
				Station.Below = Below;
			}
		}

	private:
		TArray<FLandscapeVertex> Vertices; // Ascending Distance
	};

#pragma endregion
}

namespace PCGExSplineSampling
{
	TSharedRef<FSource> MakeSplineComponentSource(const USplineComponent* SplineComponent, const FSplineExtents& Extents)
	{
		check(SplineComponent);

		const TSharedRef<BuiltinSources::FSplineSource> Source = MakeShared<BuiltinSources::FSplineSource>();
		Source->Curves = SplineComponent->GetSplineCurves(); // Converted from whatever backend the component runs on
		Source->Transform = SplineComponent->GetComponentTransform();
		Source->DefaultUpVector = SplineComponent->DefaultUpVector;
		Source->ReparamStepsPerSegment = FMath::Max(1, SplineComponent->ReparamStepsPerSegment);
		Source->Extents = Extents;
		Source->bClosedLoop = Source->Curves.Position.bIsLooped;
		return Source;
	}

	void BuildChains(const int32 NumNodes, const TConstArrayView<TPair<int32, int32>> Edges, const TConstArrayView<double> EdgeWeights, TArray<FChain>& OutChains)
	{
		// Which end of an edge touches a node: 0 = its start node, 1 = its end node.
		struct FEdgeEnd
		{
			int32 Edge;
			int32 End;
		};

		TArray<TArray<FEdgeEnd, TInlineAllocator<2>>> NodeEdges;
		NodeEdges.SetNum(NumNodes);
		for (int32 Edge = 0; Edge < Edges.Num(); Edge++)
		{
			NodeEdges[Edges[Edge].Key].Add({Edge, 0});
			NodeEdges[Edges[Edge].Value].Add({Edge, 1});
		}

		TBitArray<> Visited(false, Edges.Num());

		// Same rules as PCGExClusters::FNodeChain::BuildChain; coming back to the seed closes the loop.
		auto Walk = [&](const int32 Seed, FEdgeEnd From)
		{
			FChain& Chain = OutChains.Emplace_GetRef();

			while (true)
			{
				Visited[From.Edge] = true;

				// Leaving a node through an edge's end node walks that edge backward.
				const bool bReversed = From.End == 1;
				Chain.Links.Add({From.Edge, bReversed});

				const int32 ArrivalEnd = bReversed ? 0 : 1;
				const int32 Next = ArrivalEnd == 0 ? Edges[From.Edge].Key : Edges[From.Edge].Value;
				if (Next == Seed)
				{
					Chain.bClosedLoop = true;
					break;
				}

				const TArray<FEdgeEnd, TInlineAllocator<2>>& Ends = NodeEdges[Next];
				if (Ends.Num() != 2) { break; } // Leaf or junction

				const FEdgeEnd& Other = (Ends[0].Edge == From.Edge && Ends[0].End == ArrivalEnd) ? Ends[1] : Ends[0];
				if (Visited[Other.Edge]) { break; }

				From = Other;
			}

			// Follow the direction most of the chain was authored in.
			double ForwardWeight = 0;
			double ReversedWeight = 0;
			for (const FChainLink& Link : Chain.Links)
			{
				(Link.bReversed ? ReversedWeight : ForwardWeight) += EdgeWeights.IsValidIndex(Link.Edge) ? EdgeWeights[Link.Edge] : 1.0;
			}

			if (ReversedWeight > ForwardWeight)
			{
				Algo::Reverse(Chain.Links);
				for (FChainLink& Link : Chain.Links) { Link.bReversed = !Link.bReversed; }
			}
		};

		// Seeds: leaves and junctions, one walk per edge not chained yet.
		for (int32 Node = 0; Node < NumNodes; Node++)
		{
			if (NodeEdges[Node].Num() == 2) { continue; }
			for (const FEdgeEnd& EdgeEnd : NodeEdges[Node])
			{
				if (!Visited[EdgeEnd.Edge]) { Walk(Node, EdgeEnd); }
			}
		}

		// Whatever is left only goes through binary nodes: isolated loops.
		for (int32 Edge = 0; Edge < Edges.Num(); Edge++)
		{
			if (!Visited[Edge]) { Walk(Edges[Edge].Key, {Edge, 0}); }
		}
	}
}

#pragma region UPCGExBasicSplineSampleHandler

FTopLevelAssetPath UPCGExBasicSplineSampleHandler::GetComponentClassPath() const
{
	return PCGExSplineSampling::BuiltinSources::SplineComponentPath();
}

void UPCGExBasicSplineSampleHandler::CreateSources(const UActorComponent* Component, const PCGExSplineSampling::FParams& Params, TArray<TSharedRef<PCGExSplineSampling::FSource>>& OutSources) const
{
	using namespace PCGExSplineSampling;
	using namespace PCGExSplineSampling::BuiltinSources;

	const USplineComponent* SplineComponent = Cast<USplineComponent>(Component);
	if (!SplineComponent) { return; }

	FSplineExtents Extents;
	Extents.Lateral = MakeExtent(EExtentSource::ScaleY, Params.HalfWidth, true);
	Extents.Above = MakeExtent(EExtentSource::ScaleZ, Params.Above, true);
	Extents.Below = MakeExtent(EExtentSource::ScaleZ, Params.Below, true);

	OutSources.Add(MakeSplineComponentSource(SplineComponent, Extents));
}

#pragma endregion

#pragma region UPCGExWaterSplineSampleHandler

FTopLevelAssetPath UPCGExWaterSplineSampleHandler::GetComponentClassPath() const
{
	return PCGExSplineSampling::BuiltinSources::WaterSplineComponentPath();
}

void UPCGExWaterSplineSampleHandler::CreateSources(const UActorComponent* Component, const PCGExSplineSampling::FParams& Params, TArray<TSharedRef<PCGExSplineSampling::FSource>>& OutSources) const
{
	using namespace PCGExSplineSampling;
	using namespace PCGExSplineSampling::BuiltinSources;

	const USplineComponent* SplineComponent = Cast<USplineComponent>(Component);
	if (!SplineComponent) { return; }

	static const FName River(TEXT("River"));
	static const FName Lake(TEXT("Lake"));
	static const FName Ocean(TEXT("Ocean"));

	FSplineExtents Extents;
	Extents.Lateral = MakeExtent(EExtentSource::Constant, Params.HalfWidth, false);
	Extents.Above = MakeExtent(EExtentSource::Constant, Params.Above, false);
	Extents.Below = MakeExtent(EExtentSource::Constant, Params.Below, false);

	// Only what the body type authors (UWaterSplineMetadata::CanEditRiverWidth / CanEditDepth); the rest are defaults.
	const FName BodyType = GetWaterBodyType(SplineComponent->GetOwner());
	const USplineMetadata* Metadata = SplineComponent->GetSplinePointsMetadata();

	FInterpCurveFloat Curve;
	if (!BodyType.IsNone() && BodyType != Lake && BodyType != Ocean && CopyMetadataCurve(Metadata, TEXT("Depth"), Curve))
	{
		// The water system queries depth unscaled.
		Extents.Below = MakeCurveExtent(MoveTemp(Curve), 1.0, false);
	}

	if (BodyType == River && CopyMetadataCurve(Metadata, TEXT("RiverWidth"), Curve))
	{
		// RiverWidth is the full width, laid out in component space: it follows the component scale.
		Extents.Lateral = MakeCurveExtent(MoveTemp(Curve), 0.5, true);
	}

	OutSources.Add(MakeSplineComponentSource(SplineComponent, Extents));
}

#pragma endregion

#pragma region UPCGExLandscapeSplineSampleHandler

FTopLevelAssetPath UPCGExLandscapeSplineSampleHandler::GetComponentClassPath() const
{
	return PCGExSplineSampling::BuiltinSources::LandscapeSplinesComponentPath();
}

void UPCGExLandscapeSplineSampleHandler::CreateSources(const UActorComponent* Component, const PCGExSplineSampling::FParams& Params, TArray<TSharedRef<PCGExSplineSampling::FSource>>& OutSources) const
{
	using namespace PCGExSplineSampling;
	using namespace PCGExSplineSampling::BuiltinSources;

	const ULandscapeSplinesComponent* SplinesComponent = Cast<ULandscapeSplinesComponent>(Component);
	if (!SplinesComponent) { return; }

	// Control points are nodes, segments that can be interpolated are edges.
	TArray<const ULandscapeSplineSegment*> Segments;
	TArray<TPair<int32, int32>> Edges;
	TArray<double> EdgeLengths;
	TMap<const ULandscapeSplineControlPoint*, int32> NodeIndices;

	auto GetNode = [&NodeIndices](const ULandscapeSplineControlPoint* ControlPoint)
	{
		if (const int32* Index = NodeIndices.Find(ControlPoint)) { return *Index; }
		return NodeIndices.Add(ControlPoint, NodeIndices.Num());
	};

	for (const TObjectPtr<ULandscapeSplineSegment>& SegmentPtr : SplinesComponent->GetSegments())
	{
		const ULandscapeSplineSegment* Segment = SegmentPtr.Get();
		if (!Segment || Segment->GetPoints().Num() < 2) { continue; }

		const ULandscapeSplineControlPoint* Start = Segment->Connections[0].ControlPoint.Get();
		const ULandscapeSplineControlPoint* End = Segment->Connections[1].ControlPoint.Get();
		if (!Start || !End) { continue; }

		// Component-space length, only used to orient chains.
		PCGExPaths::FPathMetrics Metrics;
		for (const FLandscapeSplineInterpPoint& Point : Segment->GetPoints()) { Metrics.Add(Point.Center); }

		const int32 StartNode = GetNode(Start);
		const int32 EndNode = GetNode(End);

		Segments.Add(Segment);
		Edges.Emplace(StartNode, EndNode);
		EdgeLengths.Add(Metrics.Length);
	}

	if (Segments.IsEmpty()) { return; }

	TArray<FChain> Chains;
	BuildChains(NodeIndices.Num(), Edges, EdgeLengths, Chains);

	// Copy the walked interp points only; Prepare transforms and flattens them on a worker.
	const FTransform Transform = SplinesComponent->GetComponentTransform();
	for (const FChain& Chain : Chains)
	{
		const TSharedRef<FLandscapeChainSource> Source = MakeShared<FLandscapeChainSource>();
		Source->Transform = Transform;
		Source->bClosedLoop = Chain.bClosedLoop;
		Source->Above = Params.Above;
		Source->Below = Params.Below;

		int32 NumPoints = 0;
		for (const FChainLink& Link : Chain.Links) { NumPoints += Segments[Link.Edge]->GetPoints().Num(); }
		Source->LocalPoints.Reserve(NumPoints);

		for (const FChainLink& Link : Chain.Links)
		{
			const TArray<FLandscapeSplineInterpPoint>& Points = Segments[Link.Edge]->GetPoints();
			const int32 Num = Points.Num();

			for (int32 i = 0; i < Num; i++)
			{
				const FLandscapeSplineInterpPoint& Point = Points[Link.bReversed ? Num - 1 - i : i];

				// Walked backward, the authored left edge is on our right.
				const FVector& AuthoredLeft = Params.bIncludeFalloff ? Point.FalloffLeft : Point.Left;
				const FVector& AuthoredRight = Params.bIncludeFalloff ? Point.FalloffRight : Point.Right;
				Source->LocalPoints.Add({Point.Center, Link.bReversed ? AuthoredRight : AuthoredLeft, Link.bReversed ? AuthoredLeft : AuthoredRight});
			}
		}

		OutSources.Add(Source);
	}
}

#pragma endregion

#pragma region Registration

namespace PCGExSplineSampling
{
	void RegisterBuiltinHandlers()
	{
		FHandlerRegistry& Registry = FHandlerRegistry::Get();
		for (UClass* HandlerClass : {UPCGExBasicSplineSampleHandler::StaticClass(), UPCGExWaterSplineSampleHandler::StaticClass(), UPCGExLandscapeSplineSampleHandler::StaticClass()})
		{
			if (Registry.Register(HandlerClass)) { BuiltinSources::RegisteredHandlers.Add(HandlerClass); }
		}
	}

	void UnregisterBuiltinHandlers()
	{
		FHandlerRegistry& Registry = FHandlerRegistry::Get();
		for (const UClass* HandlerClass : BuiltinSources::RegisteredHandlers) { Registry.Unregister(HandlerClass); }
		BuiltinSources::RegisteredHandlers.Empty();
	}
}

#pragma endregion
