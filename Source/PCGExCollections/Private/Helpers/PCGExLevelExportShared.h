// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "PCGComponent.h"
#include "PCGExLog.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Data/PCGExDataValue.h"
#include "Data/PCGPointArrayData.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGExDefaultLevelDataExporter.h"
#include "Helpers/PCGExMetaHelpersMacros.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "UObject/UObjectIterator.h"

/**
 * Point-data and tag helpers shared by the default exporter and the built-in export handlers.
 * Module-private; inline in a named namespace so Unity builds never see two definitions.
 */
namespace PCGExLevelExportShared
{
	/** Point data with transform + bounds allocated, ranges returned for filling. */
	inline UPCGBasePointData* CreatePointData(
		UObject* Outer, const int32 NumPoints,
		TPCGValueRange<FTransform>& OutTransforms,
		TPCGValueRange<FVector>& OutBoundsMin,
		TPCGValueRange<FVector>& OutBoundsMax)
	{
		UPCGBasePointData* PointData = NewObject<UPCGPointArrayData>(Outer);
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(
			PointData, NumPoints,
			EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax);

		OutTransforms = PointData->GetTransformValueRange();
		OutBoundsMin = PointData->GetBoundsMinValueRange();
		OutBoundsMax = PointData->GetBoundsMaxValueRange();

		return PointData;
	}

	inline void InitMetadata(UPCGBasePointData* PointData, const int32 NumPoints)
	{
		UPCGMetadata* Meta = PointData->MutableMetadata();
		TPCGValueRange<int64> MetaEntries = PointData->GetMetadataEntryValueRange();

		TArray<TTuple<int64, int64>> DelayedEntries;
		DelayedEntries.SetNum(NumPoints);

		for (int32 i = 0; i < NumPoints; i++)
		{
			MetaEntries[i] = Meta->AddEntryPlaceholder();
			DelayedEntries[i] = MakeTuple(MetaEntries[i], static_cast<int64>(-1));
		}

		Meta->AddDelayedEntries(DelayedEntries);
	}

	inline void WorldBoundsToLocal(const FBox& WorldBounds, const FTransform& ActorTransform, FVector& OutBoundsMin, FVector& OutBoundsMax)
	{
		if (WorldBounds.IsValid)
		{
			const FTransform InvTransform = ActorTransform.Inverse();
			const FVector LocalMin = InvTransform.TransformPosition(WorldBounds.Min);
			const FVector LocalMax = InvTransform.TransformPosition(WorldBounds.Max);

			// Re-min/max after transform (rotation can swap axes)
			OutBoundsMin = LocalMin.ComponentMin(LocalMax);
			OutBoundsMax = LocalMin.ComponentMax(LocalMax);
		}
		else
		{
			OutBoundsMin = FVector::ZeroVector;
			OutBoundsMax = FVector::ZeroVector;
		}
	}

	/** Bounds stay relative to the actor's own transform (frame-invariant); only the written transform moves into the source frame. */
	inline void EvaluateActorItem(AActor* Actor, const FPCGExLevelExportSource& Source, const UPCGExBoundsEvaluator* Evaluator, FTransform& OutTransform, FVector& OutBoundsMin, FVector& OutBoundsMax)
	{
		const FTransform ActorTransform = Actor->GetActorTransform();
		OutTransform = Source.ToFrame(ActorTransform);

		const FBox WorldBounds = Evaluator ? Evaluator->EvaluateActorBounds(Actor, nullptr, -1) : FBox(ForceInit);
		WorldBoundsToLocal(WorldBounds, ActorTransform, OutBoundsMin, OutBoundsMax);
	}

	/** Attribute name -> PCG metadata type. First registration wins; conflicts are warned and discarded. */
	struct FValueTagRegistry
	{
		TMap<FName, EPCGMetadataTypes> TypeMap;

		/** Attributes the export writes itself on this pin. A tag of the same name is skipped, warned once per name. */
		TSet<FName> Reserved;

		bool Register(const FName& Name, const EPCGMetadataTypes NewType, const FString& SourceName)
		{
			if (Reserved.Contains(Name))
			{
				bool bAlreadyReported = false;
				ReportedReserved.Add(Name, &bAlreadyReported);
				if (!bAlreadyReported)
				{
					UE_LOG(LogPCGEx, Warning,
					       TEXT("Value tag '%s' on '%s' is skipped: the export writes its own attribute of that name. Rename the tag to export it as an attribute."),
					       *Name.ToString(), *SourceName);
				}
				return false;
			}

			if (const EPCGMetadataTypes* Existing = TypeMap.Find(Name))
			{
				if (*Existing != NewType)
				{
					UE_LOG(LogPCGEx, Warning,
					       TEXT("Value tag type conflict: '%s' on '%s'. Attribute was already registered with a different type; this value will be discarded."),
					       *Name.ToString(), *SourceName);
					return false;
				}
				return true;
			}
			TypeMap.Add(Name, NewType);
			return true;
		}

	private:
		TSet<FName> ReportedReserved;
	};

	/** PlainTags become bool=true attributes; ValueTags (Name:Value) become typed attributes. */
	struct FParsedTags
	{
		TArray<FName> PlainTags;
		TArray<TPair<FName, TSharedPtr<PCGExData::IDataValue>>> ValueTags;
	};

	/**
	 * The one plain-vs-value classification. None is an empty array slot, never a tag nor a value-tag name.
	 * Null registry parses silently (no type bookkeeping) -- for tag SETS, never for attribute writes.
	 */
	inline FParsedTags ParseTags(const TConstArrayView<FName> RawTags, FValueTagRegistry* Registry, const FString& SourceName)
	{
		FParsedTags Result;

		for (const FName& Tag : RawTags)
		{
			if (Tag.IsNone())
			{
				continue;
			}

			FString Key;
			const TSharedPtr<PCGExData::IDataValue> DataValue = PCGExData::TryGetValueFromTag(Tag.ToString(), Key);

			if (DataValue.IsValid())
			{
				const FName AttrName(Key);
				if (!AttrName.IsNone() && (!Registry || Registry->Register(AttrName, DataValue->GetTypeId(), SourceName)))
				{
					Result.ValueTags.Add(TPair<FName, TSharedPtr<PCGExData::IDataValue>>(AttrName, DataValue));
				}
			}
			else if (!Registry || Registry->Register(Tag, EPCGMetadataTypes::Boolean, SourceName))
			{
				Result.PlainTags.Add(Tag);
			}
		}
		return Result;
	}

	/**
	 * Tags a user put on an export source. PCG stamps bookkeeping on what it generates -- its markers and
	 * the generating component's name -- which is neither entry identity nor an attribute.
	 */
	struct FAuthoredTags
	{
		static bool IsPCGMarker(const FName& Tag)
		{
			return Tag == PCGHelpers::DefaultPCGTag
				|| Tag == PCGHelpers::DefaultPCGDebugTag
				|| Tag == PCGHelpers::DefaultPCGActorTag
				|| Tag == PCGHelpers::MarkedForCleanupPCGTag;
		}

		static TArray<FName> OfActor(const AActor* Actor)
		{
			TArray<FName> Tags;
			Tags.Reserve(Actor->Tags.Num());
			for (const FName& Tag : Actor->Tags)
			{
				if (!Tag.IsNone() && !IsPCGMarker(Tag))
				{
					Tags.Add(Tag);
				}
			}
			return Tags;
		}

		TArray<FName> OfComponent(const UActorComponent* Component)
		{
			const TArray<FName>& RawTags = Component->ComponentTags;
			const int32 MarkerIndex = RawTags.IndexOfByKey(PCGHelpers::DefaultPCGTag);
			const bool bGenerated = MarkerIndex != INDEX_NONE;
			if (bGenerated && !PCGSourceNames.IsSet())
			{
				GatherPCGSourceNames(Component->GetWorld());
			}

			TArray<FName> Tags;
			Tags.Reserve(RawTags.Num());
			bool bSourceNamed = false;
			for (const FName& Tag : RawTags)
			{
				if (Tag.IsNone() || IsPCGMarker(Tag))
				{
					continue;
				}

				if (bGenerated && PCGSourceNames->Contains(Tag))
				{
					bSourceNamed = true;
					continue;
				}

				Tags.Add(Tag);
			}

			// UPCGActorHelpers::GetOrCreateManagedISMC writes its source's name right after the marker (5.5+): the
			// only handle left once that source is deleted, unloaded, or not a component.
			if (bGenerated && !bSourceNamed && RawTags.IsValidIndex(MarkerIndex + 1) && Component->IsA<UInstancedStaticMeshComponent>())
			{
				Tags.Remove(RawTags[MarkerIndex + 1]);
			}

			return Tags;
		}

	private:
		// PCG tags a generated component with its source PCG component's FName and keeps no back-pointer,
		// so every PCG component of the world is a candidate. Gathered on the first generated component.
		TOptional<TSet<FName>> PCGSourceNames;

		void GatherPCGSourceNames(const UWorld* World)
		{
			TSet<FName>& Names = PCGSourceNames.Emplace();
			if (!World)
			{
				// Every world-less template component would compare equal.
				return;
			}

			for (TObjectIterator<UPCGComponent> It; It; ++It)
			{
				if (It->GetWorld() == World)
				{
					Names.Add(It->GetFName());
				}
			}
		}
	};

	inline FParsedTags ParseActorTags(const AActor* Actor, FValueTagRegistry* Registry)
	{
		const TArray<FName> Tags = FAuthoredTags::OfActor(Actor);
		return ParseTags(Tags, Registry, Actor->GetActorNameOrLabel());
	}

	inline TMap<FName, FPCGMetadataAttributeBase*> CreateValueTagAttributes(UPCGMetadata* Meta, const FValueTagRegistry& Registry)
	{
		TMap<FName, FPCGMetadataAttributeBase*> AttrMap;
		AttrMap.Reserve(Registry.TypeMap.Num());

		for (const TPair<FName, EPCGMetadataTypes>& Elem : Registry.TypeMap)
		{
			const FName& Name = Elem.Key;
			FPCGMetadataAttributeBase* Attr = nullptr;
#define PCGEX_CREATE_VALUE_TAG_ATTR(_TYPE, _NAME) Attr = Meta->CreateAttribute<_TYPE>(Name, _TYPE{}, false, true);
			PCGEX_EXECUTEWITHRIGHTTYPE(Elem.Value, PCGEX_CREATE_VALUE_TAG_ATTR)
#undef PCGEX_CREATE_VALUE_TAG_ATTR
			if (Attr)
			{
				AttrMap.Add(Name, Attr);
			}
		}
		return AttrMap;
	}

	inline void SetValueTagAttributes(const TMap<FName, FPCGMetadataAttributeBase*>& AttrMap, const int64 Entry, const FParsedTags& Parsed)
	{
		for (const FName& Tag : Parsed.PlainTags)
		{
			if (FPCGMetadataAttributeBase* const* BasePtr = AttrMap.Find(Tag))
			{
				static_cast<FPCGMetadataAttribute<bool>*>(*BasePtr)->SetValue(Entry, true);
			}
		}

		for (const TPair<FName, TSharedPtr<PCGExData::IDataValue>>& VT : Parsed.ValueTags)
		{
			FPCGMetadataAttributeBase* const* BasePtr = AttrMap.Find(VT.Key);
			if (!BasePtr)
			{
				continue;
			}

			FPCGMetadataAttributeBase* Base = *BasePtr;
			const TSharedPtr<PCGExData::IDataValue>& Val = VT.Value;

#define PCGEX_SET_VALUE_TAG_ATTR(_TYPE, _NAME) static_cast<FPCGMetadataAttribute<_TYPE>*>(Base)->SetValue(Entry, Val->GetValue<_TYPE>());
			PCGEX_EXECUTEWITHRIGHTTYPE(Val->GetTypeId(), PCGEX_SET_VALUE_TAG_ATTR)
#undef PCGEX_SET_VALUE_TAG_ATTR
		}
	}

	/** Adds the effective form of tags (actor or component) under the parse mode: every tag, plain tags, or plain +
	 *  value-tag names. */
	inline void AppendEffectiveTags(const TConstArrayView<FName> RawTags, const EPCGExValueTagMode Mode, TSet<FName>& OutTags)
	{
		if (Mode == EPCGExValueTagMode::NoParsing)
		{
			for (const FName& Tag : RawTags)
			{
				if (!Tag.IsNone())
				{
					OutTags.Add(Tag);
				}
			}
			return;
		}

		const FParsedTags Parsed = ParseTags(RawTags, nullptr, FString());
		for (const FName& Tag : Parsed.PlainTags)
		{
			OutTags.Add(Tag);
		}
		if (Mode == EPCGExValueTagMode::ParseAndKeep)
		{
			for (const TPair<FName, TSharedPtr<PCGExData::IDataValue>>& VT : Parsed.ValueTags)
			{
				OutTags.Add(VT.Key);
			}
		}
	}

	/** The tag set an actor's authored tags contribute under the parse mode. */
	inline TSet<FName> BuildEffectiveTags(const AActor* Actor, const EPCGExValueTagMode Mode)
	{
		const TArray<FName> ActorTags = FAuthoredTags::OfActor(Actor);
		TSet<FName> Tags;
		AppendEffectiveTags(ActorTags, Mode, Tags);
		return Tags;
	}

	/** Comma-joined tag names for the InstanceTags attribute. Empty under Parse (typed attributes carry everything). */
	inline FString BuildInstanceTagString(const AActor* Actor, const EPCGExValueTagMode Mode)
	{
		FString TagsStr;
		auto Append = [&TagsStr](const FName& Tag)
		{
			if (!TagsStr.IsEmpty())
			{
				TagsStr += TEXT(",");
			}
			TagsStr += Tag.ToString();
		};

		if (Mode == EPCGExValueTagMode::NoParsing)
		{
			for (const FName& Tag : FAuthoredTags::OfActor(Actor))
			{
				Append(Tag);
			}
		}
		else if (Mode == EPCGExValueTagMode::ParseAndKeep)
		{
			const FParsedTags Parsed = ParseActorTags(Actor, nullptr);
			for (const FName& Tag : Parsed.PlainTags)
			{
				Append(Tag);
			}
			for (const TPair<FName, TSharedPtr<PCGExData::IDataValue>>& VT : Parsed.ValueTags)
			{
				Append(VT.Key);
			}
		}
		return TagsStr;
	}
}

#endif // WITH_EDITOR
