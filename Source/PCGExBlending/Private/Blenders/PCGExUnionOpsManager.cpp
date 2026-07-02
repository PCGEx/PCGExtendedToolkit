// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Blenders/PCGExUnionOpsManager.h"

#include "Containers/PCGExIndexLookup.h"
#include "Core/PCGExBlendOpFactory.h"
#include "Core/PCGExBlendOpsManager.h"
#include "Core/PCGExBlendOpsSchema.h"
#include "Core/PCGExOpStats.h"
#include "Core/PCGExUnionData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"

namespace PCGExBlending
{
	FUnionOpsManager::FUnionOpsManager(const TArray<TObjectPtr<const UPCGExBlendOpFactory>>* InBlendingFactories, const PCGExMath::IDistances* InDistances)
		: BlendingFactories(InBlendingFactories)
		  , Distances(InDistances)
	{
	}

	FUnionOpsManager::~FUnionOpsManager()
	{
	}

	bool FUnionOpsManager::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& TargetData, const TArray<TSharedRef<PCGExData::FFacade>>& InSources, const TSharedPtr<const FBlendOpsSchema>& InSchema, const TConstArrayView<bool> InBackgroundSources)
	{
		CurrentTargetData = TargetData;

		int32 MaxIndex = 0;

		Blenders.Reserve(BlendingFactories->Num());

		for (const TSharedRef<PCGExData::FFacade>& Src : InSources)
		{
			MaxIndex = FMath::Max(Src->Source->IOIndex, MaxIndex);
		}
		IOLookup = MakeShared<PCGEx::FIndexLookup>(MaxIndex + 1);

		// A source flagged in InBackgroundSources is best-effort: factories on the spot, missing attributes
		// tolerated. Every other source is primary (strict; schema if one was provided).
		int32 SrcPos = 0;

		for (const TSharedRef<PCGExData::FFacade>& Src : InSources)
		{
			IOLookup->Set(Src->Source->IOIndex, SourcesData.Add(Src->GetIn()));

			TSharedPtr<FBlendOpsManager> BlendOpsManager = MakeShared<FBlendOpsManager>(TargetData, true);
			BlendOpsManager->SetSourceA(Src, PCGExData::EIOSide::In);

			const bool bIsBackground = InBackgroundSources.IsValidIndex(SrcPos) && InBackgroundSources[SrcPos];
			const bool bInitOk = bIsBackground
				                     ? BlendOpsManager->Init(InContext, *BlendingFactories, /*bTolerateMissingAttributes=*/true)
				                     : (InSchema ? BlendOpsManager->Init(InContext, InSchema) : BlendOpsManager->Init(InContext, *BlendingFactories));
			if (!bInitOk)
			{
				return false;
			}

			Blenders.Add(BlendOpsManager);
			SrcPos++;
		}

		// Build a shared OpIdx space across all blenders so the same attribute
		// always maps to the same tracker slot, regardless of which source it came from.
		TMap<FName, int32> SharedIndexMap;
		int32 NextIdx = 0;

		for (const TSharedPtr<FBlendOpsManager>& Blender : Blenders)
		{
			for (const FPCGExBlendOperation* Op : Blender->GetCachedOperations())
			{
				const FName Name = UPCGExBlendOpFactory::GetOutputTargetName(Op->Config);

				// Every op must resolve to an output name to map into the shared tracker slots; an
				// unnamed op would keep its local OpIdx and index the tracker out of bounds in Blend().
				if (Name.IsNone())
				{
					PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("A blend operation has no resolvable output target and cannot participate in union blending."));
					return false;
				}

				if (!SharedIndexMap.Contains(Name))
				{
					SharedIndexMap.Add(Name, NextIdx++);
				}
			}
		}

		// Remap all blenders and build a flat unique ops list (one per attribute) for Begin/End
		TSet<int32> SeenIndices;
		UniqueOps.Reserve(NextIdx);

		for (const TSharedPtr<FBlendOpsManager>& Blender : Blenders)
		{
			Blender->RemapOperationIndices(SharedIndexMap, NextIdx);

			for (FPCGExBlendOperation* Op : Blender->GetCachedOperations())
			{
				if (!SeenIndices.Contains(Op->OpIdx))
				{
					SeenIndices.Add(Op->OpIdx);
					UniqueOps.Add(Op);
				}
			}
		}

		return true;
	}

	bool FUnionOpsManager::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& TargetData, const TArray<TSharedRef<PCGExData::FFacade>>& InSources, const TSharedPtr<PCGExData::FUnionMetadata>& InUnionMetadata, const TSharedPtr<const FBlendOpsSchema>& InSchema, const TConstArrayView<bool> InBackgroundSources)
	{
		CurrentUnionMetadata = InUnionMetadata;
		return Init(InContext, TargetData, InSources, InSchema, InBackgroundSources);
	}

	void FUnionOpsManager::InitTrackers(TArray<PCGEx::FOpStats>& Trackers) const
	{
		check(!Blenders.IsEmpty())
		Blenders[0]->InitTrackers(Trackers);
	}

	int32 FUnionOpsManager::ComputeWeights(const int32 WriteIndex, const TSharedPtr<PCGExData::IUnionData>& InUnionData, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints) const
	{
		const PCGExData::FConstPoint Target = CurrentTargetData->Source->GetOutPoint(WriteIndex);
		return InUnionData->ComputeWeights(SourcesData, IOLookup, Target, Distances, OutWeightedPoints);
	}

	void FUnionOpsManager::Blend(const int32 WriteIndex, const TArray<PCGExData::FWeightedPoint>& InWeightedPoints, TArray<PCGEx::FOpStats>& Trackers) const
	{
		check(!Blenders.IsEmpty())

		const PCGExData::FConstPoint Target = CurrentTargetData->Source->GetOutPoint(WriteIndex);

		if (InWeightedPoints.IsEmpty())
		{
			return;
		}

		// Begin/End use UniqueOps: one prepared op per attribute, ensures
		// every tracker slot is initialized/finalized exactly once.
		for (const auto Op : UniqueOps)
		{
			Trackers[Op->OpIdx] = Op->BeginMultiBlend(WriteIndex);
		}
		for (const PCGExData::FWeightedPoint& P : InWeightedPoints)
		{
			Blenders[P.IO]->MultiBlend(P.Index, WriteIndex, P.Weight, Trackers);
		}
		for (const auto Op : UniqueOps)
		{
			Op->EndMultiBlend(WriteIndex, Trackers[Op->OpIdx]);
		}
	}

	void FUnionOpsManager::MergeSingle(const int32 WriteIndex, const TSharedPtr<PCGExData::IUnionData>& InUnionData, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints, TArray<PCGEx::FOpStats>& Trackers) const
	{
		check(InUnionData)
		if (!ComputeWeights(WriteIndex, InUnionData, OutWeightedPoints))
		{
			return;
		}
		Blend(WriteIndex, OutWeightedPoints, Trackers);
	}

	void FUnionOpsManager::MergeSingle(const int32 UnionIndex, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints, TArray<PCGEx::FOpStats>& Trackers) const
	{
		if (!ComputeWeights(UnionIndex, CurrentUnionMetadata->Get(UnionIndex), OutWeightedPoints))
		{
			return;
		}
		Blend(UnionIndex, OutWeightedPoints, Trackers);
	}

	void FUnionOpsManager::Cleanup(FPCGExContext* InContext)
	{
		for (const TSharedPtr<FBlendOpsManager>& Blender : Blenders)
		{
			Blender->Cleanup(InContext);
		}
	}
}
