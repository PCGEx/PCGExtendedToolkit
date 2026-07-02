// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Clusters/Artifacts/PCGExChainGating.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/Artifacts/PCGExChain.h"
#include "Math/PCGExMath.h"

namespace PCGExClusters
{
	void ComputeChainMetrics(const FNodeChain& Chain, const FCluster& Cluster, FChainMetrics& OutMetrics, const EChainMetricFlags Flags)
	{
		// Chain nodes are Seed followed by every Links[i].Node (this holds for single-edge, open and
		// closed chains alike). A closed loop additionally closes back onto the seed.
		OutMetrics.VtxCount = Chain.Links.Num() + 1;
		OutMetrics.EdgeCount = Chain.bIsClosedLoop ? OutMetrics.VtxCount : OutMetrics.VtxCount - 1;

		// Angle metrics are opt-in: each family's per-edge work runs only when its own flag is set.
		const bool bNeedCorner = EnumHasAnyFlags(Flags, EChainMetricFlags::CornerAngle);
		const bool bNeedCurvature = EnumHasAnyFlags(Flags, EChainMetricFlags::TotalCurvature);
		const bool bNeedEndpoint = EnumHasAnyFlags(Flags, EChainMetricFlags::EndpointDeviation);
		const bool bNeedDirections = Flags != EChainMetricFlags::None;

		const FVector SeedPos = Cluster.GetPos(Chain.Seed.Node);

		FVector PrevPos = SeedPos;
		FVector PrevDir = FVector::ZeroVector;  // last valid edge direction
		FVector FirstDir = FVector::ZeroVector; // first valid edge direction
		bool bHasDir = false;                   // at least one non-degenerate edge seen

		double LengthAccum = 0; // summed edge length
		double MinDot = 1.0;    // dot of the sharpest corner (1 = straight)
		double SumDegrees = 0;  // accumulated unsigned deflection, in degrees

		// Folds one edge (PrevPos -> NextPos) into the running metrics. Length is always accumulated;
		// direction work is skipped when no angle metric is requested. A segment below GetSafeNormal's
		// degenerate threshold (SizeSquared < SMALL_NUMBER, i.e. length < ~1e-4) carries no reliable
		// direction - matching FCluster::GetDir - so it adds length but never a phantom corner.
		auto ProcessEdge = [&](const FVector& NextPos)
		{
			const FVector Delta = NextPos - PrevPos;
			const double SegLengthSq = Delta.SizeSquared();
			const double SegLength = FMath::Sqrt(SegLengthSq);
			PrevPos = NextPos;
			LengthAccum += SegLength;

			if (!bNeedDirections || SegLengthSq <= SMALL_NUMBER)
			{
				return;
			}

			const FVector Dir = Delta / SegLength;
			if (bHasDir)
			{
				if (bNeedCorner || bNeedCurvature)
				{
					const double Dot = FVector::DotProduct(PrevDir, Dir);
					if (bNeedCorner)
					{
						MinDot = FMath::Min(MinDot, Dot);
					}
					if (bNeedCurvature)
					{
						SumDegrees += PCGExMath::DotToDegrees(Dot);
					}
				}
			}
			else
			{
				FirstDir = Dir;
				bHasDir = true;
			}

			PrevDir = Dir;
		};

		for (const PCGExGraphs::FLink& Lk : Chain.Links)
		{
			ProcessEdge(Cluster.GetPos(Lk.Node));
		}
		if (Chain.bIsClosedLoop)
		{
			ProcessEdge(SeedPos);
		}

		// A closed loop also turns at the seed itself: fold the wrap corner between the last and the
		// first edge direction so a sharp seed corner is not silently ignored.
		if (Chain.bIsClosedLoop && bHasDir && (bNeedCorner || bNeedCurvature))
		{
			const double Dot = FVector::DotProduct(PrevDir, FirstDir);
			if (bNeedCorner)
			{
				MinDot = FMath::Min(MinDot, Dot);
			}
			if (bNeedCurvature)
			{
				SumDegrees += PCGExMath::DotToDegrees(Dot);
			}
		}

		OutMetrics.Length = LengthAccum;
		OutMetrics.SharpestCorner = bNeedCorner ? PCGExMath::DotToDegrees(MinDot) : 0.0;
		OutMetrics.TotalCurvature = SumDegrees; // 0 unless TotalCurvature was requested

		// A closed loop has no endpoints, so its endpoint deviation is undefined - report 0 (inert)
		// rather than the seed-corner angle, which the corner/curvature metrics already capture.
		OutMetrics.EndpointDeviation = (bNeedEndpoint && bHasDir && !Chain.bIsClosedLoop)
			? PCGExMath::DotToDegrees(FVector::DotProduct(FirstDir, PrevDir))
			: 0.0;
	}
}

#pragma region FPCGExChainGatingDetails

bool FPCGExChainGatingDetails::IsEnabled() const
{
	return bCheckMinVtxCount || bCheckMaxVtxCount ||
		bCheckMinEdgeCount || bCheckMaxEdgeCount ||
		bCheckMinLength || bCheckMaxLength ||
		bCheckMinCornerAngle || bCheckMaxCornerAngle ||
		bCheckMinTotalCurvature || bCheckMaxTotalCurvature ||
		bCheckMinEndpointDeviation || bCheckMaxEndpointDeviation;
}

bool FPCGExChainGatingDetails::Test(const PCGExClusters::FNodeChain& Chain, const PCGExClusters::FCluster& Cluster) const
{
	if (!IsEnabled())
	{
		return false;
	}

	// Only request the angle metrics that are actually gated, so their per-edge math is skipped otherwise.
	PCGExClusters::EChainMetricFlags MetricFlags = PCGExClusters::EChainMetricFlags::None;
	if (bCheckMinCornerAngle || bCheckMaxCornerAngle)
	{
		MetricFlags |= PCGExClusters::EChainMetricFlags::CornerAngle;
	}
	if (bCheckMinTotalCurvature || bCheckMaxTotalCurvature)
	{
		MetricFlags |= PCGExClusters::EChainMetricFlags::TotalCurvature;
	}
	if (bCheckMinEndpointDeviation || bCheckMaxEndpointDeviation)
	{
		MetricFlags |= PCGExClusters::EChainMetricFlags::EndpointDeviation;
	}

	PCGExClusters::FChainMetrics Metrics;
	PCGExClusters::ComputeChainMetrics(Chain, Cluster, Metrics, MetricFlags);

	const bool bAndLogic = (Logic == EPCGExChainGatingLogic::All);

	// AND starts true and clears on any failing criterion; OR starts false and sets on any passing one.
	// IsEnabled() guarantees at least one criterion contributes, so the seed value is always overwritten.
	bool bResult = bAndLogic;

	auto Apply = [&](const bool bPass)
	{
		if (bAndLogic)
		{
			bResult &= bPass;
		}
		else
		{
			bResult |= bPass;
		}
	};

	if (bCheckMinVtxCount)
	{
		Apply(Metrics.VtxCount >= MinVtxCount);
	}
	if (bCheckMaxVtxCount)
	{
		Apply(Metrics.VtxCount <= MaxVtxCount);
	}
	if (bCheckMinEdgeCount)
	{
		Apply(Metrics.EdgeCount >= MinEdgeCount);
	}
	if (bCheckMaxEdgeCount)
	{
		Apply(Metrics.EdgeCount <= MaxEdgeCount);
	}
	if (bCheckMinLength)
	{
		Apply(Metrics.Length >= MinLength);
	}
	if (bCheckMaxLength)
	{
		Apply(Metrics.Length <= MaxLength);
	}
	if (bCheckMinCornerAngle)
	{
		Apply(Metrics.SharpestCorner >= MinCornerAngle);
	}
	if (bCheckMaxCornerAngle)
	{
		Apply(Metrics.SharpestCorner <= MaxCornerAngle);
	}
	if (bCheckMinTotalCurvature)
	{
		Apply(Metrics.TotalCurvature >= MinTotalCurvature);
	}
	if (bCheckMaxTotalCurvature)
	{
		Apply(Metrics.TotalCurvature <= MaxTotalCurvature);
	}
	if (bCheckMinEndpointDeviation)
	{
		Apply(Metrics.EndpointDeviation >= MinEndpointDeviation);
	}
	if (bCheckMaxEndpointDeviation)
	{
		Apply(Metrics.EndpointDeviation <= MaxEndpointDeviation);
	}

	return bResult;
}

#pragma endregion
