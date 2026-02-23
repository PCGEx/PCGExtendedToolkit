// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExMatchingCommon.h"

struct FPCGExContext;
class FPCGExMatchRuleOperation;
class UPCGExMatchRuleFactoryData;
class UPCGData;

namespace PCGExData
{
	class FTags;
	class FPointIO;
	class FFacade;
	struct FConstPoint;
}

struct FPCGExTaggedData;
struct FPCGExMatchingDetails;

namespace PCGExMatching
{
	struct PCGEXMATCHING_API FScope
	{
	private:
		int32 NumCandidates = 0;
		int32 Counter = 0;
		int8 Valid = true;

	public:
		FScope() = default;
		explicit FScope(const int32 InNumCandidates, const bool bUnlimited = false);

		void RegisterMatch();
		FORCEINLINE int32 GetNumCandidates() const { return NumCandidates; }
		FORCEINLINE int32 GetCounter() const { return FPlatformAtomics::AtomicRead(&Counter); }
		FORCEINLINE bool IsValid() const { return static_cast<bool>(FPlatformAtomics::AtomicRead(&Valid)); }

		void Invalidate();
	};

	class PCGEXMATCHING_API FDataMatcher : public TSharedFromThis<FDataMatcher>
	{
	protected:
		const FPCGExMatchingDetails* Details = nullptr;

		int32 NumSources = 0;
		bool bWantsRecursion = false;
		int32 MaxRecursionDepth = -1; // -1 = unlimited

		TSharedPtr<TArray<FPCGExTaggedData>> MatchableSources;
		TSharedPtr<TArray<PCGExData::FConstPoint>> MatchableSourceFirstElements;
		TMap<const UPCGData*, int32> MatchableSourcesMap;
		TArray<TSharedPtr<FPCGExMatchRuleOperation>> Operations;

		TArray<TSharedPtr<FPCGExMatchRuleOperation>> RequiredOperations;
		TArray<TSharedPtr<FPCGExMatchRuleOperation>> OptionalOperations;

	public:
		EPCGExMapMatchMode MatchMode = EPCGExMapMatchMode::Disabled;

		FDataMatcher();

		FORCEINLINE int32 GetNumSources() const { return NumSources; }
		bool FindIndex(const UPCGData* InData, int32& OutIndex) const;

		void SetDetails(const FPCGExMatchingDetails* InDetails);

		bool Init(FPCGExContext* InContext, const TArray<const UPCGData*>& InMatchableSources, const TArray<TSharedPtr<PCGExData::FTags>>& InTags, const bool bThrowError, const FName InFactoriesLabel = NAME_None);
		bool Init(FPCGExContext* InContext, const TArray<TSharedRef<PCGExData::FFacade>>& InMatchableSources, const bool bThrowError, const FName InFactoriesLabel = NAME_None);
		bool Init(FPCGExContext* InContext, const TArray<TSharedPtr<PCGExData::FFacade>>& InMatchableSources, const bool bThrowError, const FName InFactoriesLabel = NAME_None);
		bool Init(FPCGExContext* InContext, const TArray<FPCGExTaggedData>& InMatchableSources, const bool bThrowError, const FName InFactoriesLabel = NAME_None);
		bool Init(FPCGExContext* InContext, const TSharedPtr<FDataMatcher>& InOtherDataMatcher, const FName InFactoriesLabel, const bool bThrowError);

		/** Init with pre-loaded match rule factories (no context needed for factory loading). */
		bool Init(const TArray<TObjectPtr<const UPCGExMatchRuleFactoryData>>& InFactories, const TArray<TSharedPtr<PCGExData::FFacade>>& InMatchableSources, const bool bThrowError);

		/** Data-level test: looks up MatchableSourceFirstElements[DataIndex] (always the first point).
		 *  Use for static/collection-level matching (bCheckAgainstDataBounds). */
		bool Test(const UPCGData* InMatchableSource, const FPCGExTaggedData& InDataCandidate, FScope& InMatchingScope) const;

		/** Point-level test: uses the provided point directly (reads attributes at that point's index).
		 *  Use for per-point matching -- caller must set FConstPoint.IO=0 when there is a single MatchableSource. */
		bool Test(const PCGExData::FConstPoint& InInMatchableElement, const FPCGExTaggedData& InDataCandidate, FScope& InMatchingScope) const;

		bool PopulateIgnoreList(const FPCGExTaggedData& InDataCandidate, FScope& InMatchingScope, TSet<const UPCGData*>& OutIgnoreList) const;

		/** Builds a static ignore list by testing each candidate via Test(UPCGData*, ...) -- first point only.
		 *  For per-point filtering, call Test(FConstPoint, ...) per-point instead. */
		bool PopulateIgnoreListFromCandidates(const TArray<FPCGExTaggedData>& InCandidates, FScope& InMatchingScope, TSet<const UPCGData*>& OutIgnoreList) const;

		/** Per-point exclude builder for inverse matching. Tests the given point against each candidate,
		 *  adding non-matching candidates' Data to OutExclude. Sets InPoint.IO=0 internally (required
		 *  for inverse matching with a single MatchableSource). Returns true if at least one candidate matched. */
		bool BuildPerPointExclude(PCGExData::FConstPoint InPoint, const TArray<FPCGExTaggedData>& InCandidates, TSet<const UPCGData*>& OutExclude) const;

		int32 GetMatchingSourcesIndices(const FPCGExTaggedData& InDataCandidate, FScope& InMatchingScope, TArray<int32>& OutMatches, const TSet<int32>* InExcludedSources = nullptr) const;

		bool HandleUnmatchedOutput(const TSharedPtr<PCGExData::FFacade>& InFacade, const bool bForward = true) const;

		/** Returns true if any operation wants recursive/transitive matching */
		FORCEINLINE bool WantsRecursion() const { return bWantsRecursion; }

	protected:
		int32 GetMatchLimitFor(const FPCGExTaggedData& InDataCandidate) const;
		void RegisterTaggedData(FPCGExContext* InContext, const FPCGExTaggedData& InTaggedData);
		bool InitInternal(FPCGExContext* InContext, const FName InFactoriesLabel);
		bool InitInternal(const TArray<TObjectPtr<const UPCGExMatchRuleFactoryData>>& InFactories);
	};
}
