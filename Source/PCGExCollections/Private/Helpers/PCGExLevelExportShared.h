// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "PCGExLog.h"
#include "Data/PCGExDataValue.h"
#include "Data/PCGPointArrayData.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGExDefaultLevelDataExporter.h"
#include "Helpers/PCGExMetaHelpersMacros.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"
#include "Metadata/PCGMetadata.h"

/**
 * Point-data and actor-tag helpers shared by the default exporter and the built-in export handlers.
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

		bool Register(const FName& Name, const EPCGMetadataTypes NewType, const FString& SourceActorName)
		{
			if (const EPCGMetadataTypes* Existing = TypeMap.Find(Name))
			{
				if (*Existing != NewType)
				{
					UE_LOG(LogPCGEx, Warning,
					       TEXT("Value tag type conflict: '%s' on actor '%s'. Attribute was already registered with a different type; this actor's value will be discarded."),
					       *Name.ToString(), *SourceActorName);
					return false;
				}
				return true;
			}
			TypeMap.Add(Name, NewType);
			return true;
		}
	};

	/** PlainTags become bool=true attributes; ValueTags (Name:Value) become typed attributes. */
	struct FParsedActorTags
	{
		TArray<FName> PlainTags;
		TArray<TPair<FName, TSharedPtr<PCGExData::IDataValue>>> ValueTags;
	};

	/** Null registry parses silently (no type bookkeeping) -- for tag SETS, never for attribute writes. */
	inline FParsedActorTags ParseActorTags(const AActor* Actor, FValueTagRegistry* Registry)
	{
		FParsedActorTags Result;
		const FString ActorName = Actor->GetActorNameOrLabel();

		for (const FName& Tag : Actor->Tags)
		{
			FString Key;
			const TSharedPtr<PCGExData::IDataValue> DataValue = PCGExData::TryGetValueFromTag(Tag.ToString(), Key);

			if (DataValue.IsValid())
			{
				const FName AttrName(Key);
				if (!Registry || Registry->Register(AttrName, DataValue->GetTypeId(), ActorName))
				{
					Result.ValueTags.Add(TPair<FName, TSharedPtr<PCGExData::IDataValue>>(AttrName, DataValue));
				}
			}
			else if (!Registry || Registry->Register(Tag, EPCGMetadataTypes::Boolean, ActorName))
			{
				Result.PlainTags.Add(Tag);
			}
		}
		return Result;
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

	inline void SetValueTagAttributes(const TMap<FName, FPCGMetadataAttributeBase*>& AttrMap, const int64 Entry, const FParsedActorTags& Parsed)
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

	/** The tag set an actor contributes under the parse mode: raw tags, plain tags, or plain + value-tag names. */
	inline TSet<FName> BuildEffectiveTags(const AActor* Actor, const EPCGExValueTagMode Mode)
	{
		TSet<FName> Tags;
		if (Mode == EPCGExValueTagMode::NoParsing)
		{
			for (const FName& Tag : Actor->Tags)
			{
				Tags.Add(Tag);
			}
			return Tags;
		}

		const FParsedActorTags Parsed = ParseActorTags(Actor, nullptr);
		for (const FName& Tag : Parsed.PlainTags)
		{
			Tags.Add(Tag);
		}
		if (Mode == EPCGExValueTagMode::ParseAndKeep)
		{
			for (const TPair<FName, TSharedPtr<PCGExData::IDataValue>>& VT : Parsed.ValueTags)
			{
				Tags.Add(VT.Key);
			}
		}
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
			for (const FName& Tag : Actor->Tags)
			{
				Append(Tag);
			}
		}
		else if (Mode == EPCGExValueTagMode::ParseAndKeep)
		{
			const FParsedActorTags Parsed = ParseActorTags(Actor, nullptr);
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
