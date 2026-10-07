// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/Artifacts/PCGExCellDetails.h"

struct FPCGExContext;
struct FPCGPinProperties;

namespace PCGExMT
{
	class FTaskManager;
}

namespace PCGExData
{
	class FPointIO;
	class FPointIOCollection;
	struct FIOSortKey;
}

namespace PCGExClusters
{
	class FCluster;
	class FCell;
}

namespace PCGExCellTriage
{
	namespace Labels
	{
		const FName SourceBounds = FName("Bounds");

		const FName PathsInside = FName("Paths : Inside");
		const FName PathsTouching = FName("Paths : Touching");
		const FName PathsOutside = FName("Paths : Outside");

		const FName BoundsInside = FName("Bounds : Inside");
		const FName BoundsTouching = FName("Bounds : Touching");
		const FName BoundsOutside = FName("Bounds : Outside");
	}

	constexpr int32 NumCategories = 3;

	// Per-category arrays are indexed by the result's value.
	static_assert(
		static_cast<int32>(EPCGExCellTriageResult::Inside) == 0 &&
		static_cast<int32>(EPCGExCellTriageResult::Touching) == 1 &&
		static_cast<int32>(EPCGExCellTriageResult::Outside) == 2,
		"EPCGExCellTriageResult values index the triage arrays.");

	FORCEINLINE int32 GetCategoryIndex(const EPCGExCellTriageResult InResult)
	{
		return static_cast<int32>(InResult);
	}

	/**
	 * Declares the output pins of a bounded cell finder: the six category pins in Separate mode, where a category
	 * that is not wanted is Advanced, or the two plain cell pins in Combined mode. An artifact that is off has no pin.
	 */
	PCGEXGRAPHS_API void DeclareOutputPins(TArray<FPCGPinProperties>& PinProperties, EPCGExCellTriageOutput InMode, uint8 InFlags, bool bOutputPaths, bool bOutputBounds);

	/** Context side of a bounded cell finder: the bounds, the per-category output collections and their staging. */
	struct PCGEXGRAPHS_API FOutputs
	{
		FBox BoundsFilter = FBox(ForceInit);

		/** Sets the mode and the wanted categories. Enough to classify and tag; Init also creates the collections. */
		void Setup(EPCGExCellTriageOutput InMode, uint8 InFlags);

		/** Reads the bounds from the first spatial data on InBoundsPin, then creates the collections the mode calls for. */
		bool Init(FPCGExContext* InContext, FName InBoundsPin, EPCGExCellTriageOutput InMode, uint8 InFlags, bool bInOutputPaths, bool bInOutputBounds);

		EPCGExCellTriageResult Classify(const PCGExClusters::FCell& InCell) const;

		FORCEINLINE bool IsEnabled(const EPCGExCellTriageResult InResult) const
		{
			return PCGExCellTriage::IsEnabled(InResult, Flags);
		}

		/** The category's tag in Combined mode; empty in Separate mode, where the pin carries the answer. */
		const FString& GetTag(EPCGExCellTriageResult InResult) const;

		const TSharedPtr<PCGExData::FPointIOCollection>& GetPaths(EPCGExCellTriageResult InResult) const;
		const TSharedPtr<PCGExData::FPointIOCollection>& GetBounds(EPCGExCellTriageResult InResult) const;

		/** Stages every collection in pin order and flags the pins that received nothing. Returns the next pin index. */
		int32 StageOutputs(uint64& InOutInactivePinMask, int32 InFirstPinIndex = 0) const;

	private:
		EPCGExCellTriageOutput Mode = EPCGExCellTriageOutput::Separate;
		uint8 Flags = 0;
		bool bOutputPaths = false;
		bool bOutputBounds = false;

		// Combined mode only fills slot 0.
		TSharedPtr<PCGExData::FPointIOCollection> Paths[NumCategories];
		TSharedPtr<PCGExData::FPointIOCollection> Bounds[NumCategories];
	};

	/** Processor side: the cells of one cluster filed by category. A flat path index runs Inside, Touching, Outside. */
	struct PCGEXGRAPHS_API FBuckets
	{
		TArray<TSharedPtr<PCGExClusters::FCell>> Cells[NumCategories];
		TArray<TSharedPtr<PCGExData::FPointIO>> PathIOs[NumCategories];

		/** Files InCell under its category. A cell whose category is not wanted is dropped. */
		void Add(const TSharedPtr<PCGExClusters::FCell>& InCell, const FOutputs& InOutputs);
		void Add(const TArray<TSharedPtr<PCGExClusters::FCell>>& InCells, const FOutputs& InOutputs);

		int32 NumCells() const;
		int32 NumPaths() const;

		/** Category and index within it of a flat path index. Valid once PreparePaths ran. */
		FORCEINLINE void Resolve(const int32 InFlatIndex, EPCGExCellTriageResult& OutCategory, int32& OutIndex) const
		{
			const int32 InsideCount = PathIOs[0].Num();
			const int32 TouchingCount = PathIOs[1].Num();

			if (InFlatIndex < InsideCount)
			{
				OutCategory = EPCGExCellTriageResult::Inside;
				OutIndex = InFlatIndex;
			}
			else if (InFlatIndex < InsideCount + TouchingCount)
			{
				OutCategory = EPCGExCellTriageResult::Touching;
				OutIndex = InFlatIndex - InsideCount;
			}
			else
			{
				OutCategory = EPCGExCellTriageResult::Outside;
				OutIndex = InFlatIndex - InsideCount - TouchingCount;
			}
		}

		/** One OBB point data per non-empty category, staged at InSortKey + category. False when an output could not be created. */
		bool EmitBounds(
			const TSharedPtr<PCGExClusters::FCluster>& InCluster,
			const TSharedRef<PCGExData::FPointIO>& InVtxIO,
			const PCGExData::FIOSortKey& InSortKey,
			const FOutputs& InOutputs,
			const FPCGExCellArtifactsDetails& InArtifacts,
			const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) const;

		/** One path IO per filed cell. False when an output could not be created. */
		bool PreparePaths(const TSharedRef<PCGExData::FPointIO>& InVtxIO, const FOutputs& InOutputs);
	};
}
