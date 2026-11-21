// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Filtering/PCGExDiscardSame.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExAttributeHasher.h"


#define LOCTEXT_NAMESPACE "PCGExDiscardSameElement"
#define PCGEX_NAMESPACE DiscardSame

#if WITH_EDITOR
void UPCGExDiscardSameSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 72, 0)
	{
		if (bTestAttributeHash_DEPRECATED)
		{
			TestAttributesHash = EPCGExDiscardAttributeHashMode::Single;
		}
	}
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

TArray<FPCGPinProperties> UPCGExDiscardSameSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExCommon::Labels::OutputDiscardedLabel, "Discarded outputs.", Normal)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(DiscardSame)
PCGEX_ELEMENT_BATCH_POINT_IMPL(DiscardSame)

bool FPCGExDiscardSameElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExDiscardSameElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(DiscardSame)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any input to check."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	for (const TSharedPtr<PCGExData::FPointIO>& PointIO : Context->MainPoints->Pairs)
	{
		if (!PointIO->IsEnabled())
		{
			PointIO->OutputPin = PCGExCommon::Labels::OutputDiscardedLabel;
			PointIO->Enable();
		}

		(void)PointIO->StageOutput(Context);
	}

	return Context->TryComplete();
}

namespace PCGExDiscardSame
{
	bool FProcessor::CompareHashers(const TArray<TSharedPtr<PCGEx::FAttributeHasher>>& InHashers)
	{
		if (Hashers.Num() != InHashers.Num())
		{
			return false;
		}
		for (int i = 0; i < Hashers.Num(); i++)
		{
			if (Hashers[i]->GetHash() != InHashers[i]->GetHash())
			{
				return false;
			}
		}
		return true;
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Forward)

		// 1 - Build comparison points

		if (Settings->TestAttributesHash != EPCGExDiscardAttributeHashMode::None)
		{
			TArray<FPCGExAttributeHashConfig> AttribtueHashConfigs;
			AttribtueHashConfigs.Reserve(Settings->AttributeHashConfigs.Num() + 1);
			if (Settings->TestAttributesHash == EPCGExDiscardAttributeHashMode::Single || Settings->bIncludeSingleAttribute)
			{
				AttribtueHashConfigs.Add(Settings->AttributeHashConfig);
			}
			if (Settings->TestAttributesHash == EPCGExDiscardAttributeHashMode::List)
			{
				AttribtueHashConfigs.Append(Settings->AttributeHashConfigs);
			}

			Hashers.Reserve(AttribtueHashConfigs.Num());
			for (const FPCGExAttributeHashConfig& HashConfig : AttribtueHashConfigs)
			{
				TSharedPtr<PCGEx::FAttributeHasher> Hasher = MakeShared<PCGEx::FAttributeHasher>(HashConfig);
				if (!Hasher->Init(Context, PointDataFacade))
				{
					continue;
				}
				if (Hasher->RequiresCompilation())
				{
					Hasher->Compile(TaskManager, nullptr);
				}
				Hashers.Add(Hasher);
			}
		}

		TSet<uint64> PositionHashes;
		const FVector PosCWTolerance = FVector(PCGEx::SafeScalarTolerance(Settings->TestPositionTolerance));

		const UPCGBasePointData* InPoints = PointDataFacade->GetIn();
		HashPointsCount = InPoints->GetNumPoints();

		if (Settings->bTestPositions)
		{
			PositionHashes.Reserve(HashPointsCount);
		}

		FBox Bounds = FBox(ForceInit);

		TConstPCGValueRange<FTransform> InTransforms = InPoints->GetConstTransformValueRange();

		for (const FTransform& InTransform : InTransforms)
		{
			const FVector& Pos = InTransform.GetLocation();
			Bounds += Pos;
			if (Settings->bTestPositions)
			{
				PositionHashes.Add(PCGEx::GH3(Pos, PosCWTolerance));
			}
		}

		if (Settings->bTestPositions)
		{
			TArray<uint64> PosHashes = PositionHashes.Array();
			PositionHashes.Empty();
			PosHashes.Sort();
			HashPositions = CityHash64(reinterpret_cast<const char*>(PosHashes.GetData()), PosHashes.Num() * sizeof(int64));
		}
		else
		{
			HashPositions = PointDataFacade->Source->IOIndex;
		}

		if (Settings->bTestBounds)
		{
			const FVector BoundsCWTolerance = FVector(PCGEx::SafeScalarTolerance(Settings->TestBoundsTolerance));
			HashBounds = HashCombineFast(PCGEx::GH3(Bounds.Min, BoundsCWTolerance), PCGEx::GH3(Bounds.Max, BoundsCWTolerance));
		}
		else
		{
			HashBounds = PointDataFacade->Source->IOIndex;
		}

		return true;
	}

	void FProcessor::CompleteWork()
	{
		TArray<TSharedRef<FProcessor>> SameAs;
		SameAs.Reserve(Context->MainPoints->Num());

		const TSharedPtr<PCGExPointsMT::TBatch<FProcessor>> Batch = StaticCastSharedPtr<PCGExPointsMT::TBatch<FProcessor>>(ParentBatch.Pin());

		PCGEX_SHARED_THIS_DECL

		const double Tol = Settings->TestPointCountTolerance;

		if (Settings->TestMode == EPCGExFilterGroupMode::AND)
		{
			for (int Pi = 0; Pi < Batch->GetNumProcessors(); Pi++)
			{
				const TSharedRef<FProcessor> P = Batch->GetProcessorRef<FProcessor>(Pi);
				if (P == ThisPtr)
				{
					continue;
				}

				if (Settings->bTestBounds && P->HashBounds != HashBounds)
				{
					continue;
				}
				if (Settings->bTestPositions && P->HashPositions != HashPositions)
				{
					continue;
				}
				if (Settings->bTestPointCount && !FMath::IsNearlyEqual(P->HashPointsCount, HashPointsCount, Tol))
				{
					continue;
				}
				if (!P->CompareHashers(Hashers))
				{
					continue;
				}

				SameAs.Add(P);
			}
		}
		else
		{
			for (int Pi = 0; Pi < Batch->GetNumProcessors(); Pi++)
			{
				const TSharedRef<FProcessor> P = Batch->GetProcessorRef<FProcessor>(Pi);
				if (P == ThisPtr)
				{
					continue;
				}

				if ((Settings->bTestBounds && P->HashBounds == HashBounds)
					|| (Settings->bTestPositions && P->HashPositions == HashPositions)
					|| (Settings->bTestPointCount && FMath::IsNearlyEqual(P->HashPointsCount, HashPointsCount, Tol))
					|| (Settings->TestAttributesHash != EPCGExDiscardAttributeHashMode::None && P->CompareHashers(Hashers)))
				{
					SameAs.Add(P);
				}
			}
		}

		if (SameAs.IsEmpty())
		{
			return;
		}

		if (Settings->Mode == EPCGExDiscardSameMode::FIFO)
		{
			for (const TSharedRef<FProcessor>& P : SameAs)
			{
				if (P->PointDataFacade->Source->IOIndex > PointDataFacade->Source->IOIndex)
				{
					PointDataFacade->Source->Disable();
					return;
				}
			}
		}
		else if (Settings->Mode == EPCGExDiscardSameMode::LIFO)
		{
			for (const TSharedRef<FProcessor>& P : SameAs)
			{
				if (P->PointDataFacade->Source->IOIndex < PointDataFacade->Source->IOIndex)
				{
					PointDataFacade->Source->Disable();
					return;
				}
			}
		}
		else
		{
			PointDataFacade->Source->Disable();
		}
	}
}
#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
