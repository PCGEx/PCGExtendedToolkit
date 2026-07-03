// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExBlendingCommon.h"
#include "Core/PCGExProxyDataBlending.h"
#include "Types/PCGExAttributeIdentity.h"
#include "UObject/Object.h"

struct FPCGExBlendingDetails;
struct FPCGExCarryOverDetails;

template <typename T>
class FPCGMetadataAttribute;

namespace PCGExData
{
	class IUnionMetadata;
	class FUnionMetadata;
}

namespace PCGExMath
{
	class IDistances;
}

namespace PCGExBlending
{
	struct FPropertiesBlender;
}

namespace PCGExBlending
{
	using FGetSourceIdx = std::function<int32(const TSharedRef<PCGExData::FFacade>& InFacade)>;

	class PCGEXBLENDING_API FUnionBlender final : public IUnionBlender
	{
	public:
		const FPCGExCarryOverDetails* CarryOverDetails = nullptr;

		FUnionBlender(const FPCGExBlendingDetails* InBlendingDetails, const FPCGExCarryOverDetails* InCarryOverDetails, const PCGExMath::IDistances* InDistanceDetails);
		virtual ~FUnionBlender() override;

		class FMultiSourceBlender : public TSharedFromThis<FMultiSourceBlender>
		{
			friend FUnionBlender;

		public:
			FBlendingParam Param;
			PCGExData::FAttributeIdentity Identity;
			const FPCGMetadataAttributeBase* DefaultValue = nullptr;

			TSharedPtr<FProxyDataBlender> MainBlender; // Finisher, only used to initialize tracker & complete the multiblend

			// Data-domain (@Data) attribute in KEEP mode: reduced once across sources into slot 0 (FUnionBlender::Init),
			// never run through the per-entry element blend -- that would collapse the single slot to last-wins.
			bool bDataDomain = false;

			// Data-domain (@Data) attribute in PROMOTE mode (bDataDomainToElements): the output is an Elements
			// attribute and each element is blended from its contributors' @Data value via cross-domain proxies
			// (source captured @Data, output captured Elements). Runs through the normal per-entry Blend.
			bool bPromoteToElements = false;

			FMultiSourceBlender(const PCGExData::FAttributeIdentity& InIdentity, const TArray<TSharedPtr<PCGExData::FFacade>>& InSources);
			explicit FMultiSourceBlender(const TArray<TSharedPtr<PCGExData::FFacade>>& InSources);

			~FMultiSourceBlender() = default;

			// Used to initialize an attribute of a given type
			bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InTargetData, PCGExData::EProxyFlags InProxyFlags = PCGExData::EProxyFlags::None);

		protected:
			TSet<int32> SupportedSources;
			const TArray<TSharedPtr<PCGExData::FFacade>>& Sources; // Sources (null if attribute is not valid on that source)
			TArray<TSharedPtr<FProxyDataBlender>> SubBlenders;     // One blender per source

			void SetNum(const int32 InNum)
			{
				SubBlenders.SetNum(InNum);
			}
		};

		void AddSources(const TArray<TSharedRef<PCGExData::FFacade>>& InSources, const TSet<FName>* IgnoreAttributeSet = nullptr, FGetSourceIdx GetSourceIdxFn = nullptr, const TSet<int32>* RelevantIOIndices = nullptr);

		/** True once AddSources gathered at least one attribute to blend or carry over. Property blenders are only
		 *  added during Init, so queried between AddSources and Init this tells whether the target will have
		 *  attribute values written (i.e. needs valid metadata entry keys). */
		bool HasAnyAttributeBlenders() const
		{
			return !Blenders.IsEmpty();
		}

		// bWantsDirectAccess replaces the previous "soft blending" concept
		// Blenders will be initialized with an attribute instead of a buffer if it is enabled
		bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& TargetData, PCGExData::EProxyFlags InProxyFlags = PCGExData::EProxyFlags::None);
		bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& TargetData, const TSharedPtr<PCGExData::IUnionMetadata>& InUnionMetadata, PCGExData::EProxyFlags InProxyFlags = PCGExData::EProxyFlags::None);

		virtual void InitTrackers(TArray<PCGEx::FOpStats>& Trackers) const override
		{
		};
		virtual int32 ComputeWeights(const int32 WriteIndex, const TSharedPtr<PCGExData::IUnionData>& InUnionData, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints) const override;
		virtual int32 ComputeWeights(const int32 WriteIndex, TConstArrayView<PCGExData::FElement> InElements, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints) const override;
		virtual int32 ComputeWeights(const int32 WriteIndex, const TSharedPtr<PCGExData::IUnionMetadata>& InMetadata, const int32 EntryIndex, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints) const override;
		virtual void Blend(const int32 WriteIndex, const TArray<PCGExData::FWeightedPoint>& InWeightedPoints, TArray<PCGEx::FOpStats>& Trackers) const override;
		virtual void MergeSingle(const int32 WriteIndex, const TSharedPtr<PCGExData::IUnionData>& InUnionData, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints, TArray<PCGEx::FOpStats>& Trackers) const override;
		virtual void MergeSingle(const int32 UnionIndex, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints, TArray<PCGEx::FOpStats>& Trackers) const override;

		// Unhide the base-class overloads we inherit verbatim (span-based MergeSingle and the
		// IUnionMetadata-based MergeSingle default impl). C++ name lookup hides all base overloads
		// when any same-named member is declared in the derived class, so we re-expose explicitly.
		using IUnionBlender::MergeSingle;
		using IUnionBlender::ComputeWeights;

	protected:
		TSet<FString> TypeMismatches;
		bool Validate(FPCGExContext* InContext, const bool bQuiet) const;

		bool bPreserveAttributesDefaultValue = false;
		const FPCGExBlendingDetails* BlendingDetails = nullptr;
		const PCGExMath::IDistances* DistanceDetails = nullptr;

		TArray<FBlendingParam> PropertyParams;
		TArray<TSharedPtr<FMultiSourceBlender>> Blenders;

		TSet<FString> UniqueTags;
		TArray<FString> UniqueTagsList;
		TArray<FPCGMetadataAttribute<bool>*> TagAttributes;
		TSharedPtr<PCGEx::FIndexLookup> IOLookup;

		TArray<TSharedPtr<PCGExData::FFacade>> Sources;
		TArray<const UPCGBasePointData*> SourcesData;
		TSet<int32> RelevantSourcePositions; // Optimization: if set, only these source positions get property blenders

		TSharedPtr<PCGExData::IUnionMetadata> CurrentUnionMetadata;
		TSharedPtr<PCGExData::FFacade> CurrentTargetData;
	};
}
