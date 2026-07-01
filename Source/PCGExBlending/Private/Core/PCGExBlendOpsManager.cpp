// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExBlendOpsManager.h"

#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExBlendOpFactory.h"
#include "Core/PCGExBlendOpsSchema.h"
#include "Core/PCGExOpStats.h"
#include "Data/PCGExData.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"


#define LOCTEXT_NAMESPACE "PCGExCreateAttributeBlend"
#define PCGEX_NAMESPACE CreateAttributeBlend

namespace PCGExBlending
{
	void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader, const TArray<TObjectPtr<const UPCGExBlendOpFactory>>& Factories)
	{
		for (const TObjectPtr<const UPCGExBlendOpFactory>& Factory : Factories)
		{
			Factory->RegisterBuffersDependencies(InContext, FacadePreloader);
		}
	}

	void RegisterBuffersDependencies_SourceA(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader, const TArray<TObjectPtr<const UPCGExBlendOpFactory>>& Factories)
	{
		for (const TObjectPtr<const UPCGExBlendOpFactory>& Factory : Factories)
		{
			Factory->RegisterBuffersDependenciesForSourceA(InContext, FacadePreloader);
		}
	}

	void RegisterBuffersDependencies_SourceB(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader, const TArray<TObjectPtr<const UPCGExBlendOpFactory>>& Factories)
	{
		for (const TObjectPtr<const UPCGExBlendOpFactory>& Factory : Factories)
		{
			Factory->RegisterBuffersDependenciesForSourceB(InContext, FacadePreloader);
		}
	}

	void RegisterBuffersDependencies_Sources(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader, const TArray<TObjectPtr<const UPCGExBlendOpFactory>>& Factories)
	{
		for (const TObjectPtr<const UPCGExBlendOpFactory>& Factory : Factories)
		{
			Factory->RegisterBuffersDependenciesForSourceA(InContext, FacadePreloader);
			Factory->RegisterBuffersDependenciesForSourceB(InContext, FacadePreloader);
		}
	}

	FBlendOpsManager::FBlendOpsManager(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool MultiBlendOnly)
		: bUsedForMultiBlendOnly(MultiBlendOnly)
	{
		SetWeightFacade(InDataFacade);
		SetSources(InDataFacade);
		SetTargetFacade(InDataFacade);
		Operations = MakeShared<TArray<TSharedPtr<FPCGExBlendOperation>>>();
	}

	FBlendOpsManager::FBlendOpsManager(const bool MultiBlendOnly)
		: bUsedForMultiBlendOnly(MultiBlendOnly)
	{
		Operations = MakeShared<TArray<TSharedPtr<FPCGExBlendOperation>>>();
	}

	void FBlendOpsManager::SetWeightFacade(const TSharedPtr<PCGExData::FFacade>& InDataFacade)
	{
		WeightFacade = InDataFacade;
	}

	void FBlendOpsManager::SetSources(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const PCGExData::EIOSide Side)
	{
		SetSourceA(InDataFacade, Side);
		SetSourceB(InDataFacade, Side);
	}

	void FBlendOpsManager::SetSourceA(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const PCGExData::EIOSide Side)
	{
		SourceAFacade = InDataFacade;
		SideA = Side;
	}

	void FBlendOpsManager::SetSourceB(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const PCGExData::EIOSide Side)
	{
		SourceBFacade = InDataFacade;
		SideB = Side;
	}

	void FBlendOpsManager::SetTargetFacade(const TSharedPtr<PCGExData::FFacade>& InDataFacade)
	{
		TargetFacade = InDataFacade;
	}

	bool FBlendOpsManager::Init(FPCGExContext* InContext, const TArray<TObjectPtr<const UPCGExBlendOpFactory>>& InFactories, const bool bTolerateMissingAttributes)
	{
		check(SourceAFacade)
		check(SourceBFacade)
		check(TargetFacade)

		if (!WeightFacade)
		{
			WeightFacade = SourceAFacade;
		}
		check(WeightFacade)

		// Phase 1: Build supersede set from non-monolithic (individual) factory output names
		TSet<FName> SupersedeNames;
		for (const TObjectPtr<const UPCGExBlendOpFactory>& Factory : InFactories)
		{
			if (Factory->IsMonolithic())
			{
				continue;
			}
			const FName OutputName = UPCGExBlendOpFactory::GetOutputTargetName(Factory->Config);
			if (!OutputName.IsNone())
			{
				SupersedeNames.Add(OutputName);
			}
		}

		Operations->Reserve(InFactories.Num());
		CachedOperations.Reserve(InFactories.Num());

		// Phase 2: Create operations from all factories, passing supersede set
		for (const TObjectPtr<const UPCGExBlendOpFactory>& Factory : InFactories)
		{
			TArray<TSharedPtr<FPCGExBlendOperation>> NewOps;

			if (!Factory->CreateOperations(InContext, SourceAFacade, TargetFacade, NewOps, &SupersedeNames))
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("An operation could not be created."));
				return false; // FAIL
			}

			for (const TSharedPtr<FPCGExBlendOperation>& Op : NewOps)
			{
				// Monolithic ops may fail preparation when a source lacks the attribute; background sources
				// (bTolerateMissingAttributes) likewise skip ops they can't resolve rather than failing.
				if (!SetupOperation(InContext, Op, Factory->IsMonolithic() || bTolerateMissingAttributes))
				{
					return false;
				}
			}
		}

		return ValidateOutputs(InContext);
	}

	bool FBlendOpsManager::Init(FPCGExContext* InContext, const TSharedPtr<const FBlendOpsSchema>& InSchema)
	{
		check(SourceAFacade)
		check(SourceBFacade)
		check(TargetFacade)

		if (!WeightFacade)
		{
			WeightFacade = SourceAFacade;
		}
		check(WeightFacade)

		const TArray<FBlendOpsSchemaEntry>* Entries = InSchema ? InSchema->GetEntries(SourceAFacade->GetIn()) : nullptr;
		if (!Entries)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Blend ops schema has no entries for the given source."));
			return false;
		}

		Operations->Reserve(Entries->Num());
		CachedOperations.Reserve(Entries->Num());

		for (const FBlendOpsSchemaEntry& Entry : *Entries)
		{
			TSharedPtr<FPCGExBlendOperation> Op = Entry.Factory->CreateOperation(InContext);
			if (!Op)
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("An operation could not be created."));
				return false;
			}

			Op->Config = Entry.Config;

			if (!SetupOperation(InContext, Op, Entry.bSoftFail))
			{
				return false;
			}
		}

		return ValidateOutputs(InContext);
	}

	bool FBlendOpsManager::SetupOperation(FPCGExContext* InContext, const TSharedPtr<FPCGExBlendOperation>& InOperation, const bool bTolerateFailure)
	{
		InOperation->bUsedForMultiBlendOnly = bUsedForMultiBlendOnly;

		// Assign blender facades
		InOperation->WeightFacade = WeightFacade;

		InOperation->Source_A_Facade = SourceAFacade;
		InOperation->SideA = SideA;

		InOperation->Source_B_Facade = SourceBFacade;
		InOperation->SideB = SideB;

		InOperation->TargetFacade = TargetFacade;

		InOperation->OpIdx = Operations->Add(InOperation);
		InOperation->SiblingOperations = Operations;

		if (!InOperation->PrepareForData(InContext))
		{
			// Failed ops stay in Operations (stable sibling indices) but are never cached/executed.
			return bTolerateFailure;
		}

		CachedOperations.Add(InOperation.Get());
		return true;
	}

	bool FBlendOpsManager::ValidateOutputs(FPCGExContext* InContext) const
	{
		// A composite property and its sub-components can't both be written in one stack: the result
		// would be order-dependent and couldn't carry per-component blend modes.
		TSet<FName> OutputNames;
		OutputNames.Reserve(CachedOperations.Num());
		for (const FPCGExBlendOperation* Op : CachedOperations)
		{
			OutputNames.Add(UPCGExBlendOpFactory::GetOutputTargetName(Op->Config));
		}

		auto RejectConflict = [&](const FName Composite, const TArray<FName>& Components) -> bool
		{
			if (!OutputNames.Contains(Composite)) { return false; }
			for (const FName& Component : Components)
			{
				if (!OutputNames.Contains(Component)) { continue; }
				PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(FTEXT("Blend output '{0}' conflicts with its sub-component '{1}'. Blend either the composite or its components, not both."), FText::FromName(Composite), FText::FromName(Component)));
				return true;
			}
			return false;
		};

		if (RejectConflict(FName(TEXT("$Transform")), {FName(TEXT("$Position")), FName(TEXT("$Rotation")), FName(TEXT("$Scale"))})) { return false; }
		if (RejectConflict(FName(TEXT("$Extents")), {FName(TEXT("$BoundsMin")), FName(TEXT("$BoundsMax"))})) { return false; }

		return true;
	}

	void FBlendOpsManager::BlendAutoWeight(const int32 SourceIndex, const int32 TargetIndex) const
	{
		for (const auto Op : CachedOperations)
		{
			Op->BlendAutoWeight(SourceIndex, TargetIndex);
		}
	}

	void FBlendOpsManager::Blend(const int32 SourceIndex, const int32 TargetIndex, const double InWeight) const
	{
		for (const auto Op : CachedOperations)
		{
			Op->Blend(SourceIndex, TargetIndex, InWeight);
		}
	}

	void FBlendOpsManager::Blend(const int32 SourceAIndex, const int32 SourceBIndex, const int32 TargetIndex, const double InWeight) const
	{
		for (const auto Op : CachedOperations)
		{
			Op->Blend(SourceAIndex, SourceBIndex, TargetIndex, InWeight);
		}
	}

	void FBlendOpsManager::BlendAutoWeight(const PCGExMT::FScope& Scope) const
	{
		for (const auto Op : CachedOperations)
		{
			Op->BlendScope(Scope);
		}
	}

	void FBlendOpsManager::BlendAutoWeight(const PCGExMT::FScope& Scope, TArrayView<const int8> Mask) const
	{
		for (const auto Op : CachedOperations)
		{
			Op->BlendScope(Scope, Mask);
		}
	}

	void FBlendOpsManager::InitScopedTrackers(const TArray<PCGExMT::FScope>& Loops)
	{
		ScopedTrackers = MakeShared<PCGExMT::TScopedArray<PCGEx::FOpStats>>(Loops);
		ScopedTrackers->ForEach([&](TArray<PCGEx::FOpStats>& Array)
		{
			InitTrackers(Array);
		});
	}

	TArray<PCGEx::FOpStats>& FBlendOpsManager::GetScopedTrackers(const PCGExMT::FScope& Scope) const
	{
		return ScopedTrackers->Get_Ref(Scope);
	}

	void FBlendOpsManager::RemapOperationIndices(const TMap<FName, int32>& SharedIndexMap, const int32 TotalCount)
	{
		for (const TSharedPtr<FPCGExBlendOperation>& Op : *Operations)
		{
			if (!Op)
			{
				continue;
			}
			const FName Name = UPCGExBlendOpFactory::GetOutputTargetName(Op->Config);
			if (const int32* Idx = SharedIndexMap.Find(Name))
			{
				Op->OpIdx = *Idx;
			}
		}
		SharedOperationCount = TotalCount;
	}

	void FBlendOpsManager::InitTrackers(TArray<PCGEx::FOpStats>& Trackers) const
	{
		Trackers.SetNumZeroed(GetNumOperations());
	}

	void FBlendOpsManager::BeginMultiBlend(const int32 TargetIndex, TArray<PCGEx::FOpStats>& Trackers) const
	{
		for (const auto Op : CachedOperations)
		{
			Trackers[Op->OpIdx] = Op->BeginMultiBlend(TargetIndex);
		}
	}

	void FBlendOpsManager::MultiBlend(const int32 SourceIndex, const int32 TargetIndex, const double InWeight, TArray<PCGEx::FOpStats>& Trackers) const
	{
		for (const auto Op : CachedOperations)
		{
			Op->MultiBlend(SourceIndex, TargetIndex, InWeight, Trackers[Op->OpIdx]);
		}
	}

	void FBlendOpsManager::EndMultiBlend(const int32 TargetIndex, TArray<PCGEx::FOpStats>& Trackers) const
	{
		for (const auto Op : CachedOperations)
		{
			Op->EndMultiBlend(TargetIndex, Trackers[Op->OpIdx]);
		}
	}

	void FBlendOpsManager::Cleanup(FPCGExContext* InContext)
	{
		TSet<TSharedPtr<PCGExData::IBuffer>> DisabledBuffers;
		for (const auto Op : CachedOperations)
		{
			Op->CompleteWork(DisabledBuffers);
		}

		for (const TSharedPtr<PCGExData::IBuffer>& Buffer : DisabledBuffers)
		{
			// If disabled buffer does not exist on input, delete it entirely
			if (!Buffer->OutAttribute)
			{
				continue;
			}
			if (!TargetFacade->GetIn()->Metadata->HasAttribute(Buffer->OutAttribute->Name))
			{
				TargetFacade->GetOut()->Metadata->DeleteAttribute(Buffer->OutAttribute->Name);
				// TODO : Check types and make sure we're not deleting something
			}

			if (Buffer->InAttribute)
			{
				// Log a warning that can be silenced that we may have removed a valid attribute
			}
		}

		Operations->Empty();
		Operations.Reset();
	}
}
#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
