// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExClipper2RectClip.h"

#include "Clipper2Lib/clipper.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"

#define LOCTEXT_NAMESPACE "PCGExClipper2RectClipElement"
#define PCGEX_NAMESPACE Clipper2RectClip

PCGEX_INITIALIZE_ELEMENT(Clipper2RectClip)

bool UPCGExClipper2RectClipSettings::WantsOperands() const
{
	return BoundsSource == EPCGExRectClipBoundsSource::Operand;
}

FPCGExGeo2DProjectionDetails UPCGExClipper2RectClipSettings::GetProjectionDetails() const
{
	return ProjectionDetails;
}

bool UPCGExClipper2RectClipSettings::SupportOpenMainPaths() const
{
	return !bSkipOpenPaths;
}

bool UPCGExClipper2RectClipSettings::SupportOpenOperandPaths() const
{
	return true; // Operands are only used for bounds, so open paths are fine
}

bool UPCGExClipper2RectClipSettings::OperandsAsBounds() const
{
	return true;
}

namespace PCGExClipper2RectClip
{
	// Helper to check if point P lies on segment AB (within tolerance)
	// Returns alpha (0-1) if on segment, -1 if not
	double PointOnSegment(
		int64_t Px, int64_t Py,
		int64_t Ax, int64_t Ay,
		int64_t Bx, int64_t By,
		int64_t Tolerance)
	{
		// Vector AB
		const double ABx = static_cast<double>(Bx - Ax);
		const double ABy = static_cast<double>(By - Ay);

		// Vector AP
		const double APx = static_cast<double>(Px - Ax);
		const double APy = static_cast<double>(Py - Ay);

		// Length squared of AB
		const double ABLenSq = ABx * ABx + ABy * ABy;
		if (ABLenSq < 1.0)
		{
			return -1.0; // Degenerate segment
		}

		// Project P onto line AB, get parameter t
		const double t = (APx * ABx + APy * ABy) / ABLenSq;

		// Check if t is in valid range [0, 1]
		if (t < -0.001 || t > 1.001)
		{
			return -1.0;
		}

		// Calculate closest point on segment
		const double ClosestX = Ax + t * ABx;
		const double ClosestY = Ay + t * ABy;

		// Check distance from P to closest point
		const double DistX = Px - ClosestX;
		const double DistY = Py - ClosestY;
		const double DistSq = DistX * DistX + DistY * DistY;

		// Use tolerance squared for comparison
		const double TolSq = static_cast<double>(Tolerance) * static_cast<double>(Tolerance);
		if (DistSq > TolSq)
		{
			return -1.0;
		}

		return FMath::Clamp(t, 0.0, 1.0);
	}

	/**
	 * Pre-built lookup over a set of source paths, so multiple RectClip results can restore their Z values
	 * without re-scanning the sources for every call (the as-lines loop calls once per subject path).
	 */
	struct FSourceZLookup
	{
		// (X,Y) -> Z for every source point (exact matches). Keyed on the low 32 bits of each coord -- same
		// tradeoff as the intersection-blend map (exact while |coord| < 2^31 scaled units).
		TMap<uint64, int64_t> PointZMap;
		const PCGExClipper2Lib::Paths64* SourcePaths = nullptr;

		explicit FSourceZLookup(const PCGExClipper2Lib::Paths64& InSourcePaths)
			: SourcePaths(&InSourcePaths)
		{
			int32 NumPts = 0;
			for (const PCGExClipper2Lib::Path64& SrcPath : InSourcePaths)
			{
				NumPts += static_cast<int32>(SrcPath.size());
			}
			PointZMap.Reserve(NumPts);

			for (const PCGExClipper2Lib::Path64& SrcPath : InSourcePaths)
			{
				for (const PCGExClipper2Lib::Point64& SrcPt : SrcPath)
				{
					const uint64 Key = PCGEx::H64(
						static_cast<uint32>(SrcPt.x & 0xFFFFFFFF),
						static_cast<uint32>(SrcPt.y & 0xFFFFFFFF));
					PointZMap.Add(Key, SrcPt.z);
				}
			}
		}
	};

	/**
	 * Restore Z values for paths output by RectClip64.
	 *
	 * RectClip64 sets Z=0 for all intersection points (see GetLineIntersectPt in clipper_core.h).
	 * This function restores proper Z values by:
	 * 1. Matching output points to original source points by X/Y coordinates (exact match)
	 * 2. For intersection points, finding which SOURCE EDGE they lie on, creating proper
	 *    FIntersectionBlendInfo with both endpoints + alpha, and marking Z as INTERSECTION_MARKER
	 *
	 * @param OutPaths - The paths output by RectClip64 (modified in place)
	 * @param Sources - Pre-built lookup over the original source paths with valid Z encodings
	 * @param Group - The processing group to store intersection blend info into
	 * @param Tolerance - Distance tolerance for matching points (in Clipper2 integer units)
	 */
	void RestoreZValuesForRectClipResults(
		PCGExClipper2Lib::Paths64& OutPaths,
		const FSourceZLookup& Sources,
		const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group,
		int64_t Tolerance = 2)
	{
		// Process each output path
		for (PCGExClipper2Lib::Path64& OutPath : OutPaths)
		{
			const size_t NumPoints = OutPath.size();
			if (NumPoints == 0)
			{
				continue;
			}

			for (size_t i = 0; i < NumPoints; i++)
			{
				PCGExClipper2Lib::Point64& Pt = OutPath[i];

				// First, try exact match with source points
				const uint64 Key = PCGEx::H64(
					static_cast<uint32>(Pt.x & 0xFFFFFFFF),
					static_cast<uint32>(Pt.y & 0xFFFFFFFF));

				if (const int64_t* FoundZ = Sources.PointZMap.Find(Key))
				{
					// Exact match found - restore original Z
					Pt.z = *FoundZ;
					continue;
				}

				// No exact match - this is an intersection point
				// Find which source edge this point lies on
				bool bFoundEdge = false;

				for (const PCGExClipper2Lib::Path64& SrcPath : *Sources.SourcePaths)
				{
					if (bFoundEdge)
					{
						break;
					}

					const size_t SrcNumPts = SrcPath.size();
					if (SrcNumPts < 2)
					{
						continue;
					}

					// Check each edge in the source path
					for (size_t j = 0; j < SrcNumPts; j++)
					{
						const size_t NextJ = (j + 1) % SrcNumPts;
						const PCGExClipper2Lib::Point64& A = SrcPath[j];
						const PCGExClipper2Lib::Point64& B = SrcPath[NextJ];

						// Check if Pt lies on edge A->B
						const double Alpha = PointOnSegment(
							Pt.x, Pt.y,
							A.x, A.y,
							B.x, B.y,
							Tolerance);

						if (Alpha >= 0.0)
						{
							// Point lies on this edge - create proper blend info
							uint32 APtIdx, ASrcIdx, BPtIdx, BSrcIdx;
							PCGEx::H64(static_cast<uint64>(A.z), APtIdx, ASrcIdx);
							PCGEx::H64(static_cast<uint64>(B.z), BPtIdx, BSrcIdx);

							PCGExClipper2::FIntersectionBlendInfo Info;
							Info.E1BotPointIdx = APtIdx;
							Info.E1BotSourceIdx = ASrcIdx;
							Info.E1TopPointIdx = BPtIdx;
							Info.E1TopSourceIdx = BSrcIdx;
							// For RectClip, we only have one edge (no second intersecting edge)
							// Duplicate the same edge so the averaging in OutputPaths64 still works
							Info.E2BotPointIdx = APtIdx;
							Info.E2BotSourceIdx = ASrcIdx;
							Info.E2TopPointIdx = BPtIdx;
							Info.E2TopSourceIdx = BSrcIdx;
							Info.E1Alpha = Alpha;
							Info.E2Alpha = Alpha;

							Group->AddIntersectionBlendInfo(Pt.x, Pt.y, Info);

							// Mark as intersection point
							Pt.z = static_cast<int64_t>(PCGEx::H64(
								PCGExClipper2::INTERSECTION_MARKER,
								PCGExClipper2::INTERSECTION_MARKER));

							bFoundEdge = true;
							break;
						}
					}
				}

				if (!bFoundEdge)
				{
					// No edge match found - still mark as intersection so the
					// Layer 2 fallback (neighbor interpolation) handles it
					Pt.z = static_cast<int64_t>(PCGEx::H64(
						PCGExClipper2::INTERSECTION_MARKER,
						PCGExClipper2::INTERSECTION_MARKER));
				}
			}
		}
	}
}

void FPCGExClipper2RectClipContext::ApplyPadding(PCGExClipper2Lib::Rect64& Rect, double Padding, const FVector2D& Scale, int32 Precision)
{
	const int64 PaddingX = static_cast<int64>(Padding * Scale.X * Precision);
	const int64 PaddingY = static_cast<int64>(Padding * Scale.Y * Precision);

	Rect.left -= PaddingX;
	Rect.right += PaddingX;
	Rect.top -= PaddingY;
	Rect.bottom += PaddingY;
}

FBox FPCGExClipper2RectClipContext::ComputeCombinedBounds(const TArray<int32>& Indices)
{
	FBox CombinedBounds(ForceInit);

	for (const int32 Idx : Indices)
	{
		if (Idx < 0 || Idx >= AllOpData->Facades.Num())
		{
			continue;
		}

		const TSharedPtr<PCGExData::FFacade>& Facade = AllOpData->Facades[Idx];
		const UPCGBasePointData* PointData = Facade->Source->GetIn();

		const FBox DataBounds = PointData->GetBounds();
		if (DataBounds.IsValid)
		{
			CombinedBounds += DataBounds;
		}
	}

	return CombinedBounds;
}

PCGExClipper2Lib::Rect64 FPCGExClipper2RectClipContext::ComputeClipRect(
	const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group,
	const UPCGExClipper2RectClipSettings* Settings)
{
	const int32 Scale = Settings->Precision;

	FBox WorldBounds(ForceInit);

	switch (Settings->BoundsSource)
	{
	case EPCGExRectClipBoundsSource::Operand:
		WorldBounds = ComputeCombinedBounds(Group->OperandIndices);
		break;

	case EPCGExRectClipBoundsSource::Manual:
		WorldBounds = Settings->ManualBounds;
		break;
	}

	// Check if we have valid bounds
	if (!WorldBounds.IsValid)
	{
		return PCGExClipper2Lib::Rect64(); // Return empty rect
	}

	// Project all 8 corners of the 3D bounding box to find the 2D bounds after projection
	TArray<FVector, TInlineAllocator<8>> Corners;
	Corners.Add(FVector(WorldBounds.Min.X, WorldBounds.Min.Y, WorldBounds.Min.Z));
	Corners.Add(FVector(WorldBounds.Max.X, WorldBounds.Min.Y, WorldBounds.Min.Z));
	Corners.Add(FVector(WorldBounds.Min.X, WorldBounds.Max.Y, WorldBounds.Min.Z));
	Corners.Add(FVector(WorldBounds.Max.X, WorldBounds.Max.Y, WorldBounds.Min.Z));
	Corners.Add(FVector(WorldBounds.Min.X, WorldBounds.Min.Y, WorldBounds.Max.Z));
	Corners.Add(FVector(WorldBounds.Max.X, WorldBounds.Min.Y, WorldBounds.Max.Z));
	Corners.Add(FVector(WorldBounds.Min.X, WorldBounds.Max.Y, WorldBounds.Max.Z));
	Corners.Add(FVector(WorldBounds.Max.X, WorldBounds.Max.Y, WorldBounds.Max.Z));

	// Project corners and find 2D min/max
	double MinX = TNumericLimits<double>::Max();
	double MaxX = TNumericLimits<double>::Lowest();
	double MinY = TNumericLimits<double>::Max();
	double MaxY = TNumericLimits<double>::Lowest();

	for (const FVector& Corner : Corners)
	{
		const FVector Projected = ProjectionDetails.Project(Corner);
		MinX = FMath::Min(MinX, Projected.X);
		MaxX = FMath::Max(MaxX, Projected.X);
		MinY = FMath::Min(MinY, Projected.Y);
		MaxY = FMath::Max(MaxY, Projected.Y);
	}

	// Floor the min edges / ceil the max edges so the integer rect always CONTAINS the projected bounds
	// (plain truncation shrinks positive maxima and negative minima by up to a full clipper unit).
	PCGExClipper2Lib::Rect64 ClipRect;
	ClipRect.left = FMath::FloorToInt64(MinX * Scale);
	ClipRect.right = FMath::CeilToInt64(MaxX * Scale);
	ClipRect.top = FMath::FloorToInt64(MinY * Scale);
	ClipRect.bottom = FMath::CeilToInt64(MaxY * Scale);

	// Apply padding
	ApplyPadding(ClipRect, Settings->BoundsPadding, Settings->BoundsPaddingScale, Scale);

	return ClipRect;
}

void FPCGExClipper2RectClipContext::Process(const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group)
{
	const UPCGExClipper2RectClipSettings* Settings = GetInputSettings<UPCGExClipper2RectClipSettings>();

	if (!Group->IsValid())
	{
		return;
	}

	// Compute the clipping rectangle
	PCGExClipper2Lib::Rect64 ClipRect = ComputeClipRect(Group, Settings);

	// Validate rectangle
	if (ClipRect.IsEmpty())
	{
		PCGE_LOG_C(Warning, GraphAndLog, this, FTEXT("Computed clip rectangle is empty or invalid. Skipping group."));
		return;
	}

	if (Settings->bInvertClip)
	{
		// For inverted clip, use boolean difference with the rectangle. Edge crossings get blend info via the
		// ZCallback; the rect's own corners can pass through into the output, so they're encoded as intersection
		// markers upfront -- OutputPaths64's neighbor fallback then positions them from their clipper coords.
		// (A raw z of 0 would decode as source 0 / point 0 and snap them onto that point's transform.)
		const int64_t CornerZ = static_cast<int64_t>(PCGEx::H64(PCGExClipper2::INTERSECTION_MARKER, PCGExClipper2::INTERSECTION_MARKER));

		PCGExClipper2Lib::Path64 RectPath;
		RectPath.reserve(4);
		RectPath.emplace_back(ClipRect.left, ClipRect.top, CornerZ);
		RectPath.emplace_back(ClipRect.right, ClipRect.top, CornerZ);
		RectPath.emplace_back(ClipRect.right, ClipRect.bottom, CornerZ);
		RectPath.emplace_back(ClipRect.left, ClipRect.bottom, CornerZ);

		PCGExClipper2Lib::Paths64 RectPaths = {RectPath};

		PCGExClipper2Lib::Clipper64 Clipper;
		Clipper.SetZCallback(Group->CreateZCallback());

		if (!Group->SubjectPaths.empty())
		{
			Clipper.AddSubject(Group->SubjectPaths);
		}
		if (!Group->OpenSubjectPaths.empty())
		{
			Clipper.AddOpenSubject(Group->OpenSubjectPaths);
		}
		Clipper.AddClip(RectPaths);

		PCGExClipper2Lib::Paths64 ClosedResults;
		PCGExClipper2Lib::Paths64 OpenResults;

		if (!Clipper.Execute(PCGExClipper2Lib::ClipType::Difference, PCGExClipper2Lib::FillRule::NonZero, ClosedResults, OpenResults))
		{
			PCGE_LOG_C(Warning, GraphAndLog, this, FTEXT("Clipper2 inverted rect clip failed; the group was skipped."));
			return;
		}

		if (!ClosedResults.empty())
		{
			TArray<TSharedPtr<PCGExData::FPointIO>> OutputPaths;
			OutputPaths64(ClosedResults, Group, OutputPaths, true, 0);
		}

		if (Settings->OpenPathsOutput != EPCGExClipper2OpenPathOutput::Ignore && !OpenResults.empty())
		{
			TArray<TSharedPtr<PCGExData::FPointIO>> OutputPaths;
			OutputPaths64(OpenResults, Group, OutputPaths, false, 1);
		}
	}
	else
	{
		// Normal clip using optimized RectClip64
		PCGExClipper2Lib::Paths64 ClosedResults;
		PCGExClipper2Lib::Paths64 OpenResults;

		// Clip closed paths
		if (!Group->SubjectPaths.empty())
		{
			// Built once for the whole subject set -- the as-lines loop restores Z once per subject path.
			const PCGExClipper2RectClip::FSourceZLookup SubjectSources(Group->SubjectPaths);

			if (Settings->bClipAsLines)
			{
				for (const PCGExClipper2Lib::Path64& SrcPath : Group->SubjectPaths)
				{
					PCGExClipper2Lib::Rect64 PathBounds = PCGExClipper2Lib::GetBounds(SrcPath);

					const bool bEntirelyInside =
						PathBounds.left >= ClipRect.left &&
						PathBounds.right <= ClipRect.right &&
						PathBounds.top >= ClipRect.top &&
						PathBounds.bottom <= ClipRect.bottom;

					if (bEntirelyInside)
					{
						ClosedResults.push_back(SrcPath);
					}
					else
					{
						// For closed paths, explicitly close the loop by appending V0
						// RectClipLines64 doesn't process the edge from last vertex back to V0 otherwise
						PCGExClipper2Lib::Path64 ExplicitlyClosed = SrcPath;
						if (!SrcPath.empty())
						{
							ExplicitlyClosed.push_back(SrcPath[0]);
						}

						PCGExClipper2Lib::RectClipLines64 LineClipper(ClipRect);
						PCGExClipper2Lib::Paths64 SinglePath = {ExplicitlyClosed};
						PCGExClipper2Lib::Paths64 ClippedResults = LineClipper.Execute(SinglePath);

						// Restore Z values using original source paths
						PCGExClipper2RectClip::RestoreZValuesForRectClipResults(ClippedResults, SubjectSources, Group);

						// Now that the path is explicitly closed, segments that should connect at V0 will exist
						// Find and merge them
						if (ClippedResults.size() >= 2 && !SrcPath.empty())
						{
							const PCGExClipper2Lib::Point64& V0 = SrcPath[0];

							int SegmentStartingAtV0 = -1;
							int SegmentEndingAtV0 = -1;

							for (int s = 0; s < static_cast<int>(ClippedResults.size()); s++)
							{
								const PCGExClipper2Lib::Path64& Seg = ClippedResults[s];
								if (Seg.empty())
								{
									continue;
								}

								if (Seg.front().x == V0.x && Seg.front().y == V0.y)
								{
									SegmentStartingAtV0 = s;
								}
								if (Seg.back().x == V0.x && Seg.back().y == V0.y)
								{
									SegmentEndingAtV0 = s;
								}
							}

							if (SegmentStartingAtV0 >= 0 && SegmentEndingAtV0 >= 0 &&
								SegmentStartingAtV0 != SegmentEndingAtV0)
							{
								PCGExClipper2Lib::Path64& EndingSeg = ClippedResults[SegmentEndingAtV0];
								PCGExClipper2Lib::Path64& StartingSeg = ClippedResults[SegmentStartingAtV0];

								// Append StartingSeg onto EndingSeg (skip duplicate V0)
								EndingSeg.reserve(EndingSeg.size() + StartingSeg.size() - 1);
								for (size_t k = 1; k < StartingSeg.size(); k++)
								{
									EndingSeg.push_back(StartingSeg[k]);
								}

								StartingSeg.clear();
							}
						}

						for (auto& Path : ClippedResults)
						{
							if (!Path.empty())
							{
								OpenResults.push_back(std::move(Path));
							}
						}
					}
				}
			}
			else
			{
				// Normal polygon clipping
				PCGExClipper2Lib::RectClip64 Clipper(ClipRect);
				ClosedResults = Clipper.Execute(Group->SubjectPaths);

				PCGExClipper2RectClip::RestoreZValuesForRectClipResults(ClosedResults, SubjectSources, Group);
			}
		}

		// Clip open paths
		if (!Group->OpenSubjectPaths.empty())
		{
			const PCGExClipper2RectClip::FSourceZLookup OpenSubjectSources(Group->OpenSubjectPaths);

			if (Settings->bClipOpenPathsAsLines || Settings->bClipAsLines)
			{
				// Use RectClipLines for open paths (or when bClipAsLines is enabled)
				PCGExClipper2Lib::RectClipLines64 LineClipper(ClipRect);
				PCGExClipper2Lib::Paths64 OpenLinesResults = LineClipper.Execute(Group->OpenSubjectPaths);

				PCGExClipper2RectClip::RestoreZValuesForRectClipResults(OpenLinesResults, OpenSubjectSources, Group);

				for (auto& Path : OpenLinesResults)
				{
					OpenResults.push_back(std::move(Path));
				}
			}
			else
			{
				// Treat open paths as closed polygons
				PCGExClipper2Lib::RectClip64 Clipper(ClipRect);
				PCGExClipper2Lib::Paths64 OpenAsClosedResults = Clipper.Execute(Group->OpenSubjectPaths);

				PCGExClipper2RectClip::RestoreZValuesForRectClipResults(OpenAsClosedResults, OpenSubjectSources, Group);

				for (auto& Path : OpenAsClosedResults)
				{
					ClosedResults.push_back(std::move(Path));
				}
			}
		}

		// Output results
		if (!ClosedResults.empty())
		{
			TArray<TSharedPtr<PCGExData::FPointIO>> OutputPaths;
			OutputPaths64(ClosedResults, Group, OutputPaths, true, 0, PCGExClipper2::ETransformRestoration::Unproject);
		}

		if (Settings->OpenPathsOutput != EPCGExClipper2OpenPathOutput::Ignore && !OpenResults.empty())
		{
			TArray<TSharedPtr<PCGExData::FPointIO>> OutputPaths;
			OutputPaths64(OpenResults, Group, OutputPaths, false, 1, PCGExClipper2::ETransformRestoration::Unproject);
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
