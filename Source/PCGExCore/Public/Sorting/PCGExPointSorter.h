// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "PCGExSortingCommon.h"
#include "Metadata/PCGAttributePropertySelector.h"

struct FPCGExContext;
struct FPCGExSortRuleConfig;

namespace PCGExData
{
	struct FElement;
	class FFacade;
	class IDataValue;
	class IBufferProxy;
}

namespace PCGExSorting
{
	class FSortCache;

	class PCGEXCORE_API FRuleHandler : public TSharedFromThis<FRuleHandler>
	{
	public:
		FRuleHandler() = default;

		explicit FRuleHandler(const FPCGExSortRuleConfig& Config);

		TSharedPtr<PCGExData::IBufferProxy> Buffer;
		TArray<TSharedPtr<PCGExData::IBufferProxy>> Buffers;
		TArray<TSharedPtr<PCGExData::IDataValue>> DataValues;

		FPCGAttributePropertyInputSelector Selector;

		double Tolerance = DBL_COMPARE_TOLERANCE;
		bool bInvertRule = false;
		bool bAbsolute = false;

		// Tag-based sorting
		bool bUseDataTag = false;
		double CachedTagValue = 0.0;    // For single-facade sorting
		TArray<double> CachedTagValues; // For multi-facade sorting (indexed by Facade->Idx)
	};

	class PCGEXCORE_API FSorter : public TSharedFromThis<FSorter>
	{
		friend class FSortCache;

	protected:
		FPCGExContext* ExecutionContext = nullptr;
		TArray<TSharedPtr<FRuleHandler>> RuleHandlers;

		// Cached for hot path - updated by UpdateCachedState()
		int32 NumRules = 0;
		bool bDescending = false;

		void UpdateCachedState();

		// Internal helper for facade Init (avoids code duplication)
		template <typename FacadeArrayType>
		bool InitFacadesInternal(FPCGExContext* InContext, const FacadeArrayType& InDataFacades);

	public:
		EPCGExSortDirection SortDirection = EPCGExSortDirection::Ascending;
		TSharedPtr<PCGExData::FFacade> DataFacade;

		FSorter(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade, TArray<FPCGExSortRuleConfig> InRuleConfigs);
		explicit FSorter(TArray<FPCGExSortRuleConfig> InRuleConfigs);

		bool Init(FPCGExContext* InContext);
		bool Init(FPCGExContext* InContext, const TArray<TSharedRef<PCGExData::FFacade>>& InDataFacades);
		bool Init(FPCGExContext* InContext, const TArray<TSharedPtr<PCGExData::FFacade>>& InDataFacades);
		bool Init(FPCGExContext* InContext, const TArray<FPCGTaggedData>& InTaggedDatas);

		bool Sort(const int32 A, const int32 B);
		bool Sort(const PCGExData::FElement A, const PCGExData::FElement B);
		bool SortData(const int32 A, const int32 B);

		/** Build a pre-cached sorter for fast bulk sorting. Use when sorting large arrays. */
		TSharedPtr<FSortCache> BuildCache(int32 NumElements) const;

		/** Sorts element indices in place: through a cache over NumElements when one can be built, per pair otherwise. */
		void SortIndices(TArray<int32>& InOutIndices, int32 NumElements);
	};

	/**
	 * Pre-cached sorting values for high-performance bulk sorting.
	 *
	 * Usage:
	 *   auto Cache = Sorter->BuildCache(NumPoints);
	 *   Order.Sort([&](int32 A, int32 B) { return Cache->Compare(A, B); });
	 */
	class PCGEXCORE_API FSortCache
	{
	public:
		struct FRuleCache
		{
			TArray<double> Values;
			double Tolerance = DBL_COMPARE_TOLERANCE;
			bool bInvertRule = false;
		};

	private:
		TArray<FRuleCache> Rules;
		bool bDescending = false;
		int32 NumElements = 0;
		int32 CachedNumRules = 0;

	public:
		FSortCache() = default;

		/** Build cache from a sorter. Populates values in parallel. */
		static TSharedPtr<FSortCache> Build(const FSorter& Sorter, int32 InNumElements);

		/** Get number of cached elements */
		FORCEINLINE int32 Num() const
		{
			return NumElements;
		}

		/** Get number of rules */
		FORCEINLINE int32 NumRules() const
		{
			return CachedNumRules;
		}

		/** Fast comparison using cached values. No virtual calls. */
		FORCEINLINE bool Compare(const int32 A, const int32 B) const
		{
			int32 Result = 0;
			const FRuleCache* RulePtr = Rules.GetData();

			for (int32 i = 0; i < CachedNumRules; i++)
			{
				const FRuleCache& Rule = RulePtr[i];
				const double ValueA = Rule.Values[A];
				const double ValueB = Rule.Values[B];

				if (FMath::IsNearlyEqual(ValueA, ValueB, Rule.Tolerance))
				{
					continue;
				}

				Result = ValueA < ValueB ? -1 : 1;
				if (Rule.bInvertRule)
				{
					Result = -Result;
				}
				break;
			}

			if (bDescending)
			{
				Result = -Result;
			}
			return Result < 0;
		}
	};
}
