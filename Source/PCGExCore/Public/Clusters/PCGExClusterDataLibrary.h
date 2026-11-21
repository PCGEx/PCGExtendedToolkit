// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <functional>

#include "CoreMinimal.h"
#include "PCGExCoreMacros.h"

namespace PCGExClusters
{
	class FCluster;
}

struct FPCGExContext;

namespace PCGExData
{
	class FDataForwardHandler;
	class FPointIOCollection;
	class FPointIO;
	class FPointIOTaggedEntries;
	class FPointIOTaggedDictionary;
}

namespace PCGExClusters
{
	using FProblem = TTuple<bool, FText>;

	enum class EProblem : uint8
	{
		None             = 0,
		DoubleMarking    = 1,
		VtxTagButNoMeta  = 2,
		EdgeTagButNoMeta = 3,
		NoTags           = 4,
		VtxDupes         = 5,
		RoamingEdges     = 6,
		RoamingVtx       = 7,
		VtxWrongPin      = 8,
		EdgeWrongPin     = 9,
	};

	class PCGEXCORE_API FDataLibrary : public TSharedFromThis<FDataLibrary>
	{
		bool bDisableInvalidData = true;
		TArray<int32> ProblemsTracker;
		TSet<TSharedPtr<PCGExData::FPointIO>> Invalidated;

	public:
		TArray<TSharedPtr<PCGExData::FPointIO>> TaggedVtx;
		TArray<TSharedPtr<PCGExData::FPointIO>> TaggedEdges;

		TSharedPtr<PCGExData::FPointIOTaggedDictionary> InputDictionary;

		explicit FDataLibrary(const bool InDisableInvalidData);

		bool Build(const TSharedPtr<PCGExData::FPointIOCollection>& InMixedCollection);
		bool Build(const TSharedPtr<PCGExData::FPointIOCollection>& InVtxCollection, const TSharedPtr<PCGExData::FPointIOCollection>& InEdgeCollection);

		bool IsDataValid(const TSharedPtr<PCGExData::FPointIO>& InPointIO) const;

		TSharedPtr<PCGExData::FPointIOTaggedEntries> GetAssociatedEdges(const TSharedPtr<PCGExData::FPointIO>& InVtxIO) const;

		/** InMuted skips specific problems, letting callers honor their own quiet toggles (e.g. missing-pair warnings). */
		void PrintLogs(FPCGExContext* InContext, bool bSkipTrivial = false, bool bSkipImportant = false, const TSet<EProblem>* InMuted = nullptr);

	protected:
		bool BuildDictionary();

		void Invalidate(const TSharedPtr<PCGExData::FPointIO>& InPointData, const EProblem Problem);
		void Log(const EProblem Problem);
	};

	// Utility to forward data from vtx/edges clusters to path-like outputs (cells, paths, etc)
	class PCGEXCORE_API FClusterDataForwardHandler : public TSharedFromThis<FClusterDataForwardHandler>
	{
	protected:
		TSharedPtr<FCluster> Cluster;
		TSharedPtr<PCGExData::FDataForwardHandler> VtxDataForwardHandler;
		TSharedPtr<PCGExData::FDataForwardHandler> EdgeDataForwardHandler;

	public:
		FClusterDataForwardHandler(const TSharedPtr<FCluster>& InCluster, const TSharedPtr<PCGExData::FDataForwardHandler>& InVtxDataForwardHandler, const TSharedPtr<PCGExData::FDataForwardHandler>& InEdgeDataForwardHandler);
	};
}
