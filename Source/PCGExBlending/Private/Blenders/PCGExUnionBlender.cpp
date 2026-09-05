// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


// Cherry picker merges metadata from varied sources into one.
// Initially to handle metadata merging for Fuse Clusters

#include "Blenders/PCGExUnionBlender.h"

#include "Containers/PCGExIndexLookup.h"
#include "Core/PCGExMTCommon.h"
#include "Core/PCGExOpStats.h"
#include "Core/PCGExUnionData.h"
#include "Core/PCGExUnionTable.h"
#include "Data/PCGExData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExPointIO.h"
#include "PCGData.h"
#include "Data/Buffers/PCGExBufferProperty.h"
#include "Data/PCGExProxyData.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Details/PCGExBlendingDetails.h"

namespace PCGExBlending
{
#pragma region FMultiSourceBlender

	FUnionBlender::FMultiSourceBlender::FMultiSourceBlender(const PCGExData::FAttributeIdentity& InIdentity, const TArray<TSharedPtr<PCGExData::FFacade>>& InSources)
		: Identity(InIdentity)
		  , Sources(InSources)
	{
	}

	FUnionBlender::FMultiSourceBlender::FMultiSourceBlender(const TArray<TSharedPtr<PCGExData::FFacade>>& InSources)
		: Sources(InSources)
	{
	}

	bool FUnionBlender::FMultiSourceBlender::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InTargetData, const PCGExData::EProxyFlags InProxyFlags)
	{
		check(InTargetData);

		TRACE_CPUPROFILER_EVENT_SCOPE(FMultiSourceBlender::Init)

		if (Param.Selector.GetSelection() == EPCGAttributePropertySelection::Attribute)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Attribute)

			const EPCGMetadataTypes WorkingType = Identity.GetType();
			if (WorkingType == EPCGMetadataTypes::Unknown)
			{
				// Unknown attribute type
				return false;
			}

			if (bPromoteToElements)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(PromoteToElements)

				// Promote @Data -> Elements: the output is a fresh Elements attribute. We MUST force the Elements
				// domain explicitly (MakeElementIdentifier) -- a by-name GetWritable sanitizes the domain against the
				// output's existing attributes and would resolve the same-named single-slot @Data attribute instead.
				// Each output element is then blended from its contributors' @Data value: source proxies capture @Data
				// (the singleton reads for any index), the output proxy captures Elements (written per entry). The
				// resulting blenders run through the normal per-entry FUnionBlender::Blend path.
				if (!InTargetData->GetWritable(WorkingType, PCGExMetaHelpers::MakeElementIdentifier(Identity.Name), PCGExData::EBufferInit::New))
				{
					PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(FTEXT("FMultiSourceBlender : Cannot create elements output for : \"{0}\""), FText::FromName(Identity.Name)));
					return false;
				}

				FPCGAttributePropertyInputSelector OutSelector = Param.Selector;
				OutSelector.SetDomainName(PCGDataConstants::DefaultDomainName);

				auto MakeCrossDomainBlender = [&](const TSharedPtr<PCGExData::FFacade>& InSourceFacade, const FPCGAttributePropertyInputSelector& InSourceSelector, const PCGExData::EIOSide InSourceSide) -> TSharedPtr<FProxyDataBlender>
				{
					PCGExData::FProxyDescriptor DescA(InSourceFacade, PCGExData::EProxyRole::Read);
					if (!DescA.Capture(InContext, InSourceSelector, InSourceSide)) { return nullptr; }

					PCGExData::FProxyDescriptor DescC(InTargetData, PCGExData::EProxyRole::Write);
					if (!DescC.Capture(InContext, OutSelector, PCGExData::EIOSide::Out)) { return nullptr; }

					DescA.AddFlags(InProxyFlags | PCGExData::EProxyFlags::Shared);
					PCGExData::EProxyFlags WriteFlags = InProxyFlags;
					EnumRemoveFlags(WriteFlags, PCGExData::EProxyFlags::Direct);
					DescC.AddFlags(WriteFlags | PCGExData::EProxyFlags::Shared);

					return CreateProxyBlender(InContext, Param.Blending, DescA, DescC);
				};

				// Main blender: finisher over the Elements output (Begin/EndMultiBlend operate on C).
				MainBlender = MakeCrossDomainBlender(InTargetData, OutSelector, PCGExData::EIOSide::Out);
				if (!MainBlender) { return false; }

				// One sub-blender per contributing source: reads that source's @Data singleton, writes the Elements output.
				const TArray<int32> SupportedList = SupportedSources.Array();
				for (const int32 SourceIdx : SupportedList)
				{
					SubBlenders[SourceIdx] = MakeCrossDomainBlender(Sources[SourceIdx], Param.Selector, PCGExData::EIOSide::In);
					if (!SubBlenders[SourceIdx]) { return false; }
				}

				return true;
			}

			const FPCGAttributeIdentifier Identifier = Identity.GetIdentifier();
			TSharedPtr<PCGExData::IBuffer> InitializationBuffer = nullptr;

			// FAttributeIdentity inherits from FPCGMetadataAttributeDesc -- use IsSameType for the
			// shape comparison instead of GetTypeId. Property-backed attributes both report Unknown
			// from FPCGMetadataAttributeBase::GetTypeId, so the legacy id check would false-positive
			// match a Struct against a TArray<float> against an Object on the target side.
			if (const FPCGMetadataAttributeBase* ExistingAttribute = InTargetData->FindConstAttribute(Identifier);
				ExistingAttribute && ExistingAttribute->GetAttributeDesc().IsSameType(Identity))
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(Inherit)
				// This attribute exists on target already
				InitializationBuffer = InTargetData->GetWritable(WorkingType, ExistingAttribute, PCGExData::EBufferInit::Inherit);
			}
			else
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(New)
				// This attribute needs to be initialized
				InitializationBuffer = InTargetData->GetWritable(WorkingType, DefaultValue, PCGExData::EBufferInit::New);
			}

			if (!InitializationBuffer)
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format(FTEXT("FMultiSourceBlender : Cannot create writable output for : \"{0}\""), FText::FromName(Identity.Name)));
				return false;
			}

			// Property-backed buffers cache an FProperty for container layout / deep-copy semantics.
			// IBuffer::GetSourceProperty returns null for typed TBuffer<T> -- no cast or gate needed.
			const FProperty* InitProperty = InitializationBuffer->GetSourceProperty();

			MainBlender = InitProperty
				? CreateProxyBlender(WorkingType, Param.Blending, true, InitProperty)
				: CreateProxyBlender(WorkingType, Param.Blending, true, Identity.ValueTypeObject);

			{
				TArray<int32> SupportedList(SupportedSources.Array());
				for (int32 i = 0; i < SupportedList.Num(); i++)
				{
					const int32 SourceIdx = SupportedList[i];

					TSharedPtr<FProxyDataBlender> SubBlender = InitProperty
						? CreateProxyBlender(WorkingType, Param.Blending, true, InitProperty)
						: CreateProxyBlender(WorkingType, Param.Blending, true, Identity.ValueTypeObject);

					SubBlenders[SourceIdx] = SubBlender;

					// Type is known from Identity -- use the validation-skipping capture path.
					if (!SubBlender->InitFromParam(InContext, Param, InTargetData, Sources[SourceIdx], PCGExData::EIOSide::In, InProxyFlags, WorkingType, &Identity))
					{
						return false;
					}
				};

			}

			if (!MainBlender->InitFromParam(InContext, Param, InTargetData, InTargetData, PCGExData::EIOSide::Out, InProxyFlags, WorkingType, &Identity))
			{
				return false;
			}
		}
		else if (Param.Selector.GetSelection() == EPCGAttributePropertySelection::Property)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Property)

			const EPCGMetadataTypes WorkingType = PCGExMetaHelpers::GetPropertyType(Param.Selector.GetPointProperty());

			MainBlender = CreateProxyBlender(WorkingType, Param.Blending);

			{
				TArray<int32> SupportedList;
				if (SupportedSources.IsEmpty())
				{
					SupportedList.Reserve(Sources.Num());
					for (int32 j = 0; j < Sources.Num(); j++)
					{
						SupportedList.Add(j);
					}
				}
				else
				{
					SupportedList = SupportedSources.Array();
				}

				for (int32 i = 0; i < SupportedList.Num(); i++)
				{
					const int32 SourceIdx = SupportedList[i];
					TSharedPtr<FProxyDataBlender> SubBlender = CreateProxyBlender(WorkingType, Param.Blending);

					SubBlenders[SourceIdx] = SubBlender;

					// Property type is known -- use the validation-skipping capture path (no attribute desc).
					if (!SubBlender->InitFromParam(InContext, Param, InTargetData, Sources[SourceIdx], PCGExData::EIOSide::In, InProxyFlags, WorkingType))
					{
						return false;
					}
				};
			}

			if (!MainBlender->InitFromParam(InContext, Param, InTargetData, InTargetData, PCGExData::EIOSide::Out, InProxyFlags, WorkingType))
			{
				return false;
			}
		}
		else
		{
			return false;
		}


		return true;
	}

#pragma endregion

	FUnionBlender::FUnionBlender(const FPCGExBlendingDetails* InBlendingDetails, const FPCGExCarryOverDetails* InCarryOverDetails, const PCGExMath::IDistances* InDistanceDetails)
		: CarryOverDetails(InCarryOverDetails)
		  , BlendingDetails(InBlendingDetails)
		  , DistanceDetails(InDistanceDetails)
	{
		BlendingDetails->GetPointPropertyBlendingParams(PropertyParams);
	}

	FUnionBlender::~FUnionBlender()
	{
	}

	void FUnionBlender::AddSources(const TArray<TSharedRef<PCGExData::FFacade>>& InSources, const TSet<FName>* IgnoreAttributeSet, FGetSourceIdx GetSourceIdxFn, const TSet<int32>* RelevantIOIndices)
	{
		if (!GetSourceIdxFn)
		{
			GetSourceIdxFn = [](const TSharedRef<PCGExData::FFacade>& InFacade)
			{
				return InFacade->Source->IOIndex;
			};
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(FUnionBlender::AddSources)

		int32 MaxIndex = 0;
		for (const TSharedRef<PCGExData::FFacade>& Src : InSources)
		{
			MaxIndex = FMath::Max(GetSourceIdxFn(Src), MaxIndex);
		}
		IOLookup = MakeShared<PCGEx::FIndexLookup>(MaxIndex + 1);

		// When the carry-over opts into Data->Elements, @Data attributes are PROMOTED to per-element outputs (blended
		// per entry); otherwise they're KEPT and reduced once into slot 0. Default is keep.
		const bool bWantsDataToElements = CarryOverDetails && CarryOverDetails->bDataDomainToElements;

		const int32 NumSources = InSources.Num();
		Sources.Reserve(NumSources);
		SourcesData.SetNumUninitialized(NumSources);

		TMap<FPCGAttributeIdentifier, TSharedPtr<FMultiSourceBlender>> BlenderLookup;

		for (int i = 0; i < InSources.Num(); i++)
		{
			const TSharedRef<PCGExData::FFacade>& Facade = InSources[i];
			const int32 IOIndex = GetSourceIdxFn(Facade);

			Sources.Add(Facade);
			SourcesData[i] = Facade->GetIn();
			IOLookup->Set(IOIndex, i);

			// Track which source positions are relevant for property blending
			// If no filter provided, all sources are relevant
			if (!RelevantIOIndices || RelevantIOIndices->Contains(IOIndex))
			{
				RelevantSourcePositions.Add(i);
			}

			EnumAddFlags(AllocatedProperties, Facade->GetAllocations());

			UniqueTags.Append(Facade->Source->Tags->RawTags);

			TArray<PCGExData::FAttributeIdentity> SourceAttributes;
			GetFilteredIdentities(Facade->GetIn()->Metadata, SourceAttributes, BlendingDetails, CarryOverDetails, IgnoreAttributeSet);

			// Check of this new source' attributes
			// See if it adds any new, non-conflicting one
			for (const PCGExData::FAttributeIdentity& Identity : SourceAttributes)
			{
				// First, grab the Param for this attribute
				// Getting a fail means it's filtered out.
				FBlendingParam Param{};
				const FPCGAttributeIdentifier Identifier = Identity.GetIdentifier();
				if (!BlendingDetails->GetBlendingParam(Identifier, Param))
				{
					continue;
				}

				const FPCGMetadataAttributeBase* SourceAttribute = Facade->FindConstAttribute(Identifier);
				if (!SourceAttribute)
				{
					continue;
				}

				TSharedPtr<FMultiSourceBlender> MultiAttribute = BlenderLookup.FindRef(Identifier);

				if (MultiAttribute)
				{
					// A multi-source blender was found for this attribute!

					if (!MultiAttribute->Identity.IsSameType(Identity))
					{
						// Type mismatch, ignore for this source
						TypeMismatches.Add(Identity.Name.ToString());
						continue;
					}
				}
				else
				{
					// Initialize new multi attribute
					// We give it the first source attribute we found, this will be used
					// to set the underlying default value of the attribute (as a best guess kind of move)
					MultiAttribute = Blenders.Add_GetRef(MakeShared<FMultiSourceBlender>(Identity, Sources));
					MultiAttribute->Param = Param;
					MultiAttribute->DefaultValue = SourceAttribute;
					const bool bIsDataDomain = Identity.InDataDomain();
					MultiAttribute->bDataDomain = bIsDataDomain && !bWantsDataToElements;
					MultiAttribute->bPromoteToElements = bIsDataDomain && bWantsDataToElements;
					MultiAttribute->SetNum(NumSources);
					BlenderLookup.Add(Identifier, MultiAttribute);
				}

				check(MultiAttribute)

				MultiAttribute->SupportedSources.Add(i);
			}
		}
	}

	bool FUnionBlender::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& TargetData, const PCGExData::EProxyFlags InProxyFlags)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FUnionBlender::Init)

		CurrentTargetData = TargetData;

		if (!Validate(InContext, false))
		{
			return false;
		}

		// Create property blender at the last moment
		Blenders.Reserve(Blenders.Num() + PropertyParams.Num());
		for (const FBlendingParam& Param : PropertyParams)
		{
			if (!EnumHasAnyFlags(AllocatedProperties, PCGExMetaHelpers::GetPropertyNativeTypes(Param.Selector.GetPointProperty())))
			{
				// Don't create a blender for properties that no source has allocated
				continue;
			}

			TSharedPtr<FMultiSourceBlender> MultiAttribute = Blenders.Add_GetRef(MakeShared<FMultiSourceBlender>(Sources));
			MultiAttribute->Param = Param;
			MultiAttribute->SetNum(Sources.Num());
			MultiAttribute->SupportedSources = RelevantSourcePositions; // Optimization: only create blenders for relevant sources
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(InitBlenders)

			for (const TSharedPtr<FMultiSourceBlender>& MultiAttribute : Blenders)
			{
				if (!MultiAttribute->Init(InContext, CurrentTargetData, InProxyFlags))
				{
					return false;
				}
			}
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(BlendDataDomain)

			// @Data attributes are per-data singletons: blending them per entry would collapse every write onto
			// slot 0 (last-wins). Reduce each across its contributing sources exactly once here, into slot 0.
			// Sources are visited in ascending index order for determinism; N==1 degenerates to a carry-over.
			for (const TSharedPtr<FMultiSourceBlender>& MultiAttribute : Blenders)
			{
				if (!MultiAttribute->bDataDomain || !MultiAttribute->MainBlender) { continue; }

				TArray<int32> SupportedList = MultiAttribute->SupportedSources.Array();
				if (SupportedList.IsEmpty()) { continue; }
				SupportedList.Sort();

				// Uniform weighting (1 per source): EndMultiBlend normalizes by count/total weight, so this yields
				// the 1/N mean for Average and the mode-appropriate result for Min/Max/etc.
				PCGEx::FOpStats Tracker = MultiAttribute->MainBlender->BeginMultiBlend(0);
				for (const int32 SourceIdx : SupportedList)
				{
					if (const TSharedPtr<FProxyDataBlender>& SubBlender = MultiAttribute->SubBlenders[SourceIdx])
					{
						SubBlender->MultiBlend(0, 0, 1.0, Tracker);
					}
				}
				MultiAttribute->MainBlender->EndMultiBlend(0, Tracker);
			}
		}

		return true;
	}

	bool FUnionBlender::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& TargetData, const TSharedPtr<PCGExData::IUnionMetadata>& InUnionMetadata, const PCGExData::EProxyFlags InProxyFlags)
	{
		CurrentUnionMetadata = InUnionMetadata;
		return Init(InContext, TargetData, InProxyFlags);
	}

	int32 FUnionBlender::ComputeWeights(const int32 WriteIndex, const TSharedPtr<PCGExData::IUnionData>& InUnionData, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints) const
	{
		if (!InUnionData.IsValid())
		{
			return 0;
		}
		const PCGExData::FConstPoint Target = CurrentTargetData->Source->GetOutPoint(WriteIndex);
		return InUnionData->ComputeWeights(SourcesData, IOLookup, Target, DistanceDetails, OutWeightedPoints);
	}

	int32 FUnionBlender::ComputeWeights(const int32 WriteIndex, TConstArrayView<PCGExData::FElement> InElements, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints) const
	{
		const PCGExData::FConstPoint Target = CurrentTargetData->Source->GetOutPoint(WriteIndex);
		return PCGExData::FUnionTable::ComputeWeightsForSpan(InElements, SourcesData, IOLookup, Target, DistanceDetails, OutWeightedPoints);
	}

	int32 FUnionBlender::ComputeWeights(const int32 WriteIndex, const TSharedPtr<PCGExData::IUnionMetadata>& InMetadata, const int32 EntryIndex, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints) const
	{
		if (!InMetadata.IsValid())
		{
			return 0;
		}
		const PCGExData::FConstPoint Target = CurrentTargetData->Source->GetOutPoint(WriteIndex);
		return InMetadata->ComputeWeights(EntryIndex, SourcesData, IOLookup, Target, DistanceDetails, OutWeightedPoints);
	}

	void FUnionBlender::Blend(const int32 WriteIndex, const TArray<PCGExData::FWeightedPoint>& InWeightedPoints, TArray<PCGEx::FOpStats>& Trackers) const
	{
		if (InWeightedPoints.IsEmpty())
		{
			return;
		}

		// For each attribute/property we want to blend
		for (const TSharedPtr<FMultiSourceBlender>& MultiAttribute : Blenders)
		{
			if (MultiAttribute->bDataDomain) { continue; } // Reduced once in Init, not per entry.
			PCGEx::FOpStats Tracking = MultiAttribute->MainBlender->BeginMultiBlend(WriteIndex);

			// For each point in the union, check if there is an attribute blender for that source; and if so, add it to the blend
			for (const PCGExData::FWeightedPoint& P : InWeightedPoints)
			{
				if (const TSharedPtr<FProxyDataBlender>& Blender = MultiAttribute->SubBlenders[P.IO])
				{
					Blender->MultiBlend(P.Index, WriteIndex, P.Weight, Tracking);
				}
			}

			MultiAttribute->MainBlender->EndMultiBlend(WriteIndex, Tracking);
		}
	}

	void FUnionBlender::MergeSingle(const int32 WriteIndex, const TSharedPtr<PCGExData::IUnionData>& InUnionData, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints, TArray<PCGEx::FOpStats>& Trackers) const
	{
		check(InUnionData)
		if (!ComputeWeights(WriteIndex, InUnionData, OutWeightedPoints))
		{
			return;
		}
		Blend(WriteIndex, OutWeightedPoints, Trackers);
	}

	void FUnionBlender::MergeSingle(const int32 UnionIndex, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints, TArray<PCGEx::FOpStats>& Trackers) const
	{
		// Resolves through the IUnionMetadata interface so the same code path serves both the legacy
		// FUnionMetadata (sparse, IUnionData-backed) and the new FUnionTable (dense, span-backed).
		if (!ComputeWeights(UnionIndex, CurrentUnionMetadata, UnionIndex, OutWeightedPoints))
		{
			return;
		}
		Blend(UnionIndex, OutWeightedPoints, Trackers);
	}

	bool FUnionBlender::Validate(FPCGExContext* InContext, const bool bQuiet) const
	{
		if (TypeMismatches.IsEmpty())
		{
			return true;
		}

		if (!bQuiet)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(FTEXT("The following attributes have the same name but different types, and will not blend as expected: {0}"), FText::FromString(FString::Join(TypeMismatches.Array(), TEXT(", ")))));
		}

		return true;
	}
}
