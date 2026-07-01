// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExBlendOpFactoryProvider.h"
#include "PCGExProxyDataBlending.h"

class UPCGExBlendOpFactory;
class FPCGExBlendOperation;

namespace PCGExMT
{
	template <typename T>
	class TScopedArray;
}

namespace PCGExBlending
{
	class FBlendOpsSchema;
	struct FBlendOpsSchemaEntry;

	PCGEXBLENDING_API void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader, const TArray<TObjectPtr<const UPCGExBlendOpFactory>>& Factories);

	PCGEXBLENDING_API void RegisterBuffersDependencies_SourceA(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader, const TArray<TObjectPtr<const UPCGExBlendOpFactory>>& Factories);

	PCGEXBLENDING_API void RegisterBuffersDependencies_SourceB(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader, const TArray<TObjectPtr<const UPCGExBlendOpFactory>>& Factories);

	PCGEXBLENDING_API void RegisterBuffersDependencies_Sources(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader, const TArray<TObjectPtr<const UPCGExBlendOpFactory>>& Factories);

	class PCGEXBLENDING_API FBlendOpsManager : public IBlender
	{
	protected:
		TSharedPtr<PCGExData::FFacade> WeightFacade;

		TSharedPtr<PCGExData::FFacade> SourceAFacade;
		PCGExData::EIOSide SideA = PCGExData::EIOSide::In;

		TSharedPtr<PCGExData::FFacade> SourceBFacade;
		PCGExData::EIOSide SideB = PCGExData::EIOSide::In;

		TSharedPtr<PCGExData::FFacade> TargetFacade;
		TSharedPtr<TArray<TSharedPtr<FPCGExBlendOperation>>> Operations;
		TArray<FPCGExBlendOperation*> CachedOperations;

		bool bUsedForMultiBlendOnly = false;

	public:
		explicit FBlendOpsManager(const TSharedPtr<PCGExData::FFacade>& InDataFacade, const bool MultiBlendOnly = false);
		explicit FBlendOpsManager(const bool MultiBlendOnly = false);
		virtual ~FBlendOpsManager() override = default;

		void SetWeightFacade(const TSharedPtr<PCGExData::FFacade>& InDataFacade);
		void SetSourceA(const TSharedPtr<PCGExData::FFacade>& InDataFacade, PCGExData::EIOSide Side = PCGExData::EIOSide::In);
		void SetSourceB(const TSharedPtr<PCGExData::FFacade>& InDataFacade, PCGExData::EIOSide Side = PCGExData::EIOSide::In);
		void SetSources(const TSharedPtr<PCGExData::FFacade>& InDataFacade, PCGExData::EIOSide Side = PCGExData::EIOSide::In);
		void SetTargetFacade(const TSharedPtr<PCGExData::FFacade>& InDataFacade);

		/** Init from factories, resolving configs against SourceA on the spot. This is the right path
		 * when the source is unique to this blender (e.g. a processor's own facade) -- resolution then
		 * happens exactly once anyway. When many blenders share the same sources (multi-target sampling),
		 * resolve once into a FBlendOpsSchema instead and use the schema overload.
		 *
		 * bTolerateMissingAttributes: when true, an op whose operand attribute is absent on SourceA is
		 * silently dropped (left out of the cached/executed set) instead of failing the whole init. Use it
		 * for best-effort "background" sources that only contribute the attributes they happen to carry. */
		bool Init(FPCGExContext* InContext, const TArray<TObjectPtr<const UPCGExBlendOpFactory>>& InFactories, const bool bTolerateMissingAttributes = false);

		/** Init from a pre-resolved schema: instantiates ops without re-resolving factory configs.
		 * The schema must have been built from the same source facades (keyed by their In data). */
		bool Init(FPCGExContext* InContext, const TSharedPtr<const FBlendOpsSchema>& InSchema);

		int32 GetNumOperations() const
		{
			return SharedOperationCount >= 0 ? SharedOperationCount : (Operations ? Operations->Num() : 0);
		}

		TArrayView<FPCGExBlendOperation* const> GetCachedOperations() const
		{
			return CachedOperations;
		}

		void RemapOperationIndices(const TMap<FName, int32>& SharedIndexMap, int32 TotalCount);

		void BlendAutoWeight(const int32 SourceIndex, const int32 TargetIndex) const;
		virtual void Blend(const int32 SourceIndex, const int32 TargetIndex, const double InWeight) const override;
		virtual void Blend(const int32 SourceAIndex, const int32 SourceBIndex, const int32 TargetIndex, const double InWeight) const override;

		void BlendAutoWeight(const PCGExMT::FScope& Scope) const;
		void BlendAutoWeight(const PCGExMT::FScope& Scope, TArrayView<const int8> Mask) const;

		void InitScopedTrackers(const TArray<PCGExMT::FScope>& Loops);
		TArray<PCGEx::FOpStats>& GetScopedTrackers(const PCGExMT::FScope& Scope) const;

		virtual void InitTrackers(TArray<PCGEx::FOpStats>& Trackers) const override;

		void virtual BeginMultiBlend(const int32 TargetIndex, TArray<PCGEx::FOpStats>& Trackers) const override;
		void virtual MultiBlend(const int32 SourceIndex, const int32 TargetIndex, const double InWeight, TArray<PCGEx::FOpStats>& Trackers) const override;
		void virtual EndMultiBlend(const int32 TargetIndex, TArray<PCGEx::FOpStats>& Trackers) const override;

		void Cleanup(FPCGExContext* InContext);

	protected:
		int32 SharedOperationCount = -1;
		TSharedPtr<PCGExMT::TScopedArray<PCGEx::FOpStats>> ScopedTrackers;

		/** Wires facades/indices into a freshly created op and runs PrepareForData.
		 * On preparation failure, returns bTolerateFailure (failed ops stay in Operations but aren't cached). */
		bool SetupOperation(FPCGExContext* InContext, const TSharedPtr<FPCGExBlendOperation>& InOperation, bool bTolerateFailure);

		/** Rejects composite/sub-component output conflicts (e.g. $Transform vs $Position). */
		bool ValidateOutputs(FPCGExContext* InContext) const;
	};
}
