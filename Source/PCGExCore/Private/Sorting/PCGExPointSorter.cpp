// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sorting/PCGExPointSorter.h"

#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExProxyData.h"
#include "Data/PCGExProxyDataHelpers.h"
#include "Sorting/PCGExSortingDetails.h"
#include "Utils/PCGExCompare.h"

namespace PCGExSorting
{
	void FSorter::UpdateCachedState()
	{
		NumRules = RuleHandlers.Num();
		bDescending = (SortDirection == EPCGExSortDirection::Descending);
	}

	FSorter::FSorter(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade, TArray<FPCGExSortRuleConfig> InRuleConfigs)
		: ExecutionContext(InContext)
		  , DataFacade(InDataFacade)
	{
		const UPCGData* InData = InDataFacade->Source->GetIn();
		FName Consumable = NAME_None;

		for (const FPCGExSortRuleConfig& RuleConfig : InRuleConfigs)
		{
			PCGEX_MAKE_SHARED(NewRule, FRuleHandler, RuleConfig)
			RuleHandlers.Add(NewRule);

			if (InContext->bCleanupConsumableAttributes && InData)
			{
				PCGEX_CONSUMABLE_SELECTOR(RuleConfig.Selector, Consumable)
			}
		}
	}

	FRuleHandler::FRuleHandler(const FPCGExSortRuleConfig& Config)
		: Selector(Config.Selector)
		  , Tolerance(Config.Tolerance)
		  , bInvertRule(Config.bInvertRule)
		  , bUseDataTag(Config.bReadDataTag)
	{
	}

	FSorter::FSorter(TArray<FPCGExSortRuleConfig> InRuleConfigs)
	{
		for (const FPCGExSortRuleConfig& RuleConfig : InRuleConfigs)
		{
			PCGEX_MAKE_SHARED(NewRule, FRuleHandler, RuleConfig)
			RuleHandlers.Add(NewRule);
		}
	}

	bool FSorter::Init(FPCGExContext* InContext)
	{
		for (int i = 0; i < RuleHandlers.Num(); i++)
		{
			const TSharedPtr<FRuleHandler> RuleHandler = RuleHandlers[i];

			if (RuleHandler->bUseDataTag)
			{
				// Tag-based sorting: get value from data tags using selector name
				const FString TagName = RuleHandler->Selector.GetName().ToString();
				if (DataFacade && DataFacade->Source->Tags)
				{
					if (const TSharedPtr<PCGExData::IDataValue> TagValue = DataFacade->Source->Tags->GetValue(TagName))
					{
						RuleHandler->CachedTagValue = TagValue->AsDouble();
					}
					else
					{
						PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("Sorting rule tag '{0}' not found on data, rule will be skipped."), FText::FromString(TagName)));
						RuleHandlers.RemoveAt(i);
						i--;
					}
				}
				else
				{
					RuleHandlers.RemoveAt(i);
					i--;
				}
				continue;
			}

			TSharedPtr<PCGExData::IBufferProxy> Buffer = nullptr;

			PCGExData::FProxyDescriptor Descriptor(DataFacade);
			Descriptor.AddFlags(PCGExData::EProxyFlags::Direct);

			if (Descriptor.CaptureStrict(InContext, RuleHandler->Selector, PCGExData::EIOSide::In))
			{
				Buffer = PCGExData::GetProxyBuffer(InContext, Descriptor);
			}

			if (!Buffer)
			{
				PCGEX_LOG_INVALID_SELECTOR_C(InContext, Sorting Rule, RuleHandler->Selector)
				RuleHandlers.RemoveAt(i);
				i--;
				continue;
			}

			RuleHandler->Buffer = Buffer;
		}

		UpdateCachedState();
		return NumRules > 0;
	}

	template <typename FacadeArrayType>
	bool FSorter::InitFacadesInternal(FPCGExContext* InContext, const FacadeArrayType& InDataFacades)
	{
		int32 MaxIndex = 0;
		for (const auto& Facade : InDataFacades)
		{
			MaxIndex = FMath::Max(Facade->Idx, MaxIndex);
		}
		MaxIndex++;

		for (int32 i = 0; i < RuleHandlers.Num(); i++)
		{
			const TSharedPtr<FRuleHandler>& RuleHandler = RuleHandlers[i];

			if (RuleHandler->bUseDataTag)
			{
				// Tag-based sorting: get value from each facade's data tags
				const FString TagName = RuleHandler->Selector.GetName().ToString();
				RuleHandler->CachedTagValues.SetNum(MaxIndex);
				RuleHandler->DataValues.SetNum(MaxIndex);
				bool bFoundAny = false;

				for (const auto& InFacade : InDataFacades)
				{
					double TagValue = 0.0;
					if (InFacade->Source->Tags)
					{
						if (const TSharedPtr<PCGExData::IDataValue> DataValue = InFacade->Source->Tags->GetValue(TagName))
						{
							TagValue = DataValue->AsDouble();
							RuleHandler->DataValues[InFacade->Idx] = DataValue;
							bFoundAny = true;
						}
					}
					RuleHandler->CachedTagValues[InFacade->Idx] = TagValue;
				}

				if (!bFoundAny)
				{
					PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("Sorting rule tag '{0}' not found on any data, rule will be skipped."), FText::FromString(TagName)));
					RuleHandlers.RemoveAt(i);
					i--;
				}
				continue;
			}

			// For facade sorting, we need data-level values (not per-point buffers)
			RuleHandler->DataValues.SetNum(MaxIndex);
			RuleHandler->Buffers.SetNum(MaxIndex);

			for (int32 f = 0; f < InDataFacades.Num(); f++)
			{
				const auto& InFacade = InDataFacades[f];
				const UPCGData* Data = InFacade->Source->GetIn();

				// First try to get a data-level value (for SortData)
				if (TSharedPtr<PCGExData::IDataValue> DataValue = PCGExData::TryGetValueFromData(Data, RuleHandler->Selector))
				{
					RuleHandler->DataValues[InFacade->Idx] = DataValue;
				}

				// Also set up buffers for per-point sorting (Sort with FElement)
				TSharedPtr<PCGExData::FFacade> FacadePtr = InFacade;
				PCGExData::FProxyDescriptor Descriptor(FacadePtr);
				Descriptor.AddFlags(PCGExData::EProxyFlags::Direct);

				TSharedPtr<PCGExData::IBufferProxy> Buffer = nullptr;
				if (Descriptor.CaptureStrict(InContext, RuleHandler->Selector, PCGExData::EIOSide::In))
				{
					Buffer = PCGExData::GetProxyBuffer(InContext, Descriptor);
				}

				if (!Buffer && !RuleHandler->DataValues[InFacade->Idx])
				{
					PCGEX_LOG_INVALID_SELECTOR_C(InContext, Sorting Rule, RuleHandler->Selector)
					RuleHandlers.RemoveAt(i);
					i--;
					break;
				}
				RuleHandler->Buffers[InFacade->Idx] = Buffer;
			}
		}

		UpdateCachedState();
		return NumRules > 0;
	}

	bool FSorter::Init(FPCGExContext* InContext, const TArray<TSharedRef<PCGExData::FFacade>>& InDataFacades)
	{
		return InitFacadesInternal(InContext, InDataFacades);
	}

	bool FSorter::Init(FPCGExContext* InContext, const TArray<TSharedPtr<PCGExData::FFacade>>& InDataFacades)
	{
		return InitFacadesInternal(InContext, InDataFacades);
	}

	bool FSorter::Init(FPCGExContext* InContext, const TArray<FPCGTaggedData>& InTaggedDatas)
	{
		const int32 NumDatas = InTaggedDatas.Num();

		for (int32 i = 0; i < RuleHandlers.Num(); i++)
		{
			const TSharedPtr<FRuleHandler>& RuleHandler = RuleHandlers[i];
			RuleHandler->DataValues.SetNum(NumDatas);

			if (RuleHandler->bUseDataTag)
			{
				// Lenient: datas without a value are left null (skipped in comparison); rule survives if any data resolves.
				bool bFoundAny = false;
				for (int32 f = 0; f < NumDatas; f++)
				{
					if (TSharedPtr<PCGExData::IDataValue> DataValue = PCGExData::TryGetValueFromData(InTaggedDatas[f], RuleHandler->Selector))
					{
						RuleHandler->DataValues[f] = DataValue;
						bFoundAny = true;
					}
				}

				if (!bFoundAny)
				{
					PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("Sorting rule tag '{0}' not found on any data, rule will be skipped."), FText::FromString(RuleHandler->Selector.GetName().ToString())));
					RuleHandlers.RemoveAt(i);
					i--;
				}
				continue;
			}

			for (int32 f = 0; f < NumDatas; f++)
			{
				const UPCGData* Data = InTaggedDatas[f].Data;

				TSharedPtr<PCGExData::IDataValue> DataValue = PCGExData::TryGetValueFromData(Data, RuleHandler->Selector);
				if (!DataValue)
				{
					PCGEX_LOG_INVALID_SELECTOR_C(InContext, Sorting Rule, RuleHandler->Selector)
					RuleHandlers.RemoveAt(i);
					i--;
					break;
				}

				RuleHandler->DataValues[f] = DataValue;
			}
		}

		UpdateCachedState();
		return NumRules > 0;
	}

	bool FSorter::Sort(const int32 A, const int32 B)
	{
		int32 Result = 0;
		const TSharedPtr<FRuleHandler>* RulePtr = RuleHandlers.GetData();

		for (int32 i = 0; i < NumRules; i++)
		{
			const FRuleHandler* Rule = RulePtr[i].Get();
			double ValueA, ValueB;

			if (Rule->bUseDataTag)
			{
				// Tag-based: all points share the same value - skip this rule for point sorting
				continue;
			}

			ValueA = Rule->Buffer->ReadAsDouble(A);
			ValueB = Rule->Buffer->ReadAsDouble(B);

			if (FMath::IsNearlyEqual(ValueA, ValueB, Rule->Tolerance))
			{
				continue;
			}

			Result = ValueA < ValueB ? -1 : 1;
			if (Rule->bInvertRule)
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

	bool FSorter::Sort(const PCGExData::FElement A, const PCGExData::FElement B)
	{
		int32 Result = 0;
		const TSharedPtr<FRuleHandler>* RulePtr = RuleHandlers.GetData();

		for (int32 i = 0; i < NumRules; i++)
		{
			const FRuleHandler* Rule = RulePtr[i].Get();
			double ValueA, ValueB;

			if (Rule->bUseDataTag)
			{
				ValueA = Rule->CachedTagValues[A.IO];
				ValueB = Rule->CachedTagValues[B.IO];
			}
			else
			{
				ValueA = Rule->Buffers[A.IO]->ReadAsDouble(A.Index);
				ValueB = Rule->Buffers[B.IO]->ReadAsDouble(B.Index);
			}

			if (FMath::IsNearlyEqual(ValueA, ValueB, Rule->Tolerance))
			{
				continue;
			}

			Result = ValueA < ValueB ? -1 : 1;
			if (Rule->bInvertRule)
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

	bool FSorter::SortData(const int32 A, const int32 B)
	{
		int32 Result = 0;
		const TSharedPtr<FRuleHandler>* RulePtr = RuleHandlers.GetData();

		for (int32 i = 0; i < NumRules; i++)
		{
			const FRuleHandler* Rule = RulePtr[i].Get();
			PCGExData::IDataValue* DataValueA = Rule->DataValues[A].Get();
			PCGExData::IDataValue* DataValueB = Rule->DataValues[B].Get();

			if (!DataValueA || !DataValueB)
			{
				continue;
			}

			if (DataValueA->IsNumeric() || DataValueB->IsNumeric())
			{
				const double ValueA = DataValueA->AsDouble();
				const double ValueB = DataValueB->AsDouble();

				if (FMath::IsNearlyEqual(ValueA, ValueB, Rule->Tolerance))
				{
					continue;
				}
				Result = ValueA < ValueB ? -1 : 1;
			}
			else
			{
				const FString ValueA = DataValueA->AsString();
				const FString ValueB = DataValueB->AsString();

				if (PCGExCompare::StrictlyEqual(ValueA, ValueB))
				{
					continue;
				}
				Result = ValueA < ValueB ? -1 : 1;
			}

			if (Rule->bInvertRule)
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

	TSharedPtr<FSortCache> FSorter::BuildCache(int32 NumElements) const
	{
		return FSortCache::Build(*this, NumElements);
	}

	void FSorter::SortIndices(TArray<int32>& InOutIndices, const int32 NumElements)
	{
		if (const TSharedPtr<FSortCache> Cache = BuildCache(NumElements))
		{
			InOutIndices.Sort([&](const int32 A, const int32 B)
			{
				return Cache->Compare(A, B);
			});
		}
		else
		{
			InOutIndices.Sort([&](const int32 A, const int32 B)
			{
				return Sort(A, B);
			});
		}
	}

#pragma region FSortCache

	TSharedPtr<FSortCache> FSortCache::Build(const FSorter& Sorter, int32 InNumElements)
	{
		// Pre-computes all sort values into flat arrays for cache-friendly comparison.
		// Collects raw pointers to buffers and value arrays before the parallel loop
		// to avoid shared_ptr overhead and virtual dispatch per-element in the hot path.
		// The resulting cache enables O(1) value lookup during subsequent sort comparisons.
		TRACE_CPUPROFILER_EVENT_SCOPE(FSortCache::Build);

		if (InNumElements <= 0 || Sorter.RuleHandlers.IsEmpty())
		{
			return nullptr;
		}

		TSharedPtr<FSortCache> Cache = MakeShared<FSortCache>();
		Cache->NumElements = InNumElements;
		Cache->bDescending = (Sorter.SortDirection == EPCGExSortDirection::Descending);

		const int32 NumRules = Sorter.RuleHandlers.Num();
		Cache->Rules.SetNum(NumRules);
		Cache->CachedNumRules = NumRules;

		// Collect buffer pointers and tag values for fast access in parallel loop
		TArray<PCGExData::IBufferProxy*> Buffers;
		TArray<double> TagValues;
		TArray<bool> UseTagFlags;
		Buffers.SetNum(NumRules);
		TagValues.SetNum(NumRules);
		UseTagFlags.SetNum(NumRules);

		// Initialize rule caches and collect buffers/tag values
		for (int32 RuleIdx = 0; RuleIdx < NumRules; RuleIdx++)
		{
			const TSharedPtr<FRuleHandler>& Handler = Sorter.RuleHandlers[RuleIdx];
			FRuleCache& RuleCache = Cache->Rules[RuleIdx];

			RuleCache.Tolerance = Handler->Tolerance;
			RuleCache.bInvertRule = Handler->bInvertRule;
			RuleCache.Values.SetNumUninitialized(InNumElements);

			UseTagFlags[RuleIdx] = Handler->bUseDataTag;
			if (Handler->bUseDataTag)
			{
				TagValues[RuleIdx] = Handler->CachedTagValue;
				Buffers[RuleIdx] = nullptr;
			}
			else
			{
				TagValues[RuleIdx] = 0.0;
				Buffers[RuleIdx] = Handler->Buffer.Get();
			}
		}

		// Get raw pointers to value arrays for capture
		TArray<double*> ValueArrays;
		ValueArrays.SetNum(NumRules);
		for (int32 RuleIdx = 0; RuleIdx < NumRules; RuleIdx++)
		{
			ValueArrays[RuleIdx] = Cache->Rules[RuleIdx].Values.GetData();
		}

		// Get raw pointers for capture
		const bool* UseTagFlagsData = UseTagFlags.GetData();
		const double* TagValuesData = TagValues.GetData();
		PCGExData::IBufferProxy* const* BuffersData = Buffers.GetData();

		// Single parallel pass - process all rules for each point
		PCGExMT::ParallelOrSequential(
			InNumElements,
			[&](const int32 i)
			{
				for (int32 RuleIdx = 0; RuleIdx < NumRules; RuleIdx++)
				{
					if (UseTagFlagsData[RuleIdx])
					{
						// Tag-based: constant value for all points
						ValueArrays[RuleIdx][i] = TagValuesData[RuleIdx];
					}
					else if (PCGExData::IBufferProxy* Buffer = BuffersData[RuleIdx])
					{
						ValueArrays[RuleIdx][i] = Buffer->ReadAsDouble(i);
					}
					else
					{
						ValueArrays[RuleIdx][i] = 0.0;
					}
				}
			});

		return Cache;
	}

#pragma endregion
}
