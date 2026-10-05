// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Factories/PCGExInstancedFactory.h"

#include "UObject/Object.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include "PCGParamData.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Helpers/PCGExPropertyHelpers.h"
#include "Helpers/PCGPropertyHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "StructUtils/UserDefinedStruct.h"

namespace PCGExInstancedFactory
{
	using FPropertyChain = TArray<const FProperty*, TInlineAllocator<4>>;

	// Shallow paths before deep ones; at equal depth, whole-struct attributes before basic ones, so the most specific override lands last.
	int32 OverrideRank(const int32 InNumSegments, const FPCGMetadataAttributeBase* InAttribute)
	{
		return InNumSegments * 2 + (PCGExMetaHelpers::IsBasicSingleValue(InAttribute->GetAttributeDesc()) ? 1 : 0);
	}

	// FNAME_Find, not Add: a name absent from the name table matches no native property, and interning every
	// unrelated attribute name would grow the table for good. UDS members match by authored name, which may not be interned yet.
	const FProperty* FindSegment(const UStruct* InStruct, const FStringView InSegment)
	{
		FName Name(InSegment, FNAME_Find);
		if (Name.IsNone() && InStruct->IsA<UUserDefinedStruct>()) { Name = FName(InSegment); }
		return Name.IsNone() ? nullptr : PCGPropertyHelpers::FindPropertyByName(InStruct, Name);
	}

	// Object hops are refused: the factory copy shares referenced and instanced objects with its template.
	int32 ResolvePath(const UClass* InClass, FStringView InPath, FPropertyChain& OutChain)
	{
		const UStruct* Current = InClass;
		int32 NumSegments = 0;

		while (true)
		{
			int32 Separator = INDEX_NONE;
			const bool bLast = !InPath.FindChar(TEXT('/'), Separator);
			const FStringView Segment = bLast ? InPath : InPath.Left(Separator);
			if (!bLast) { InPath.RightChopInline(Separator + 1); }

			if (!Segment.IsEmpty())
			{
				const FProperty* Property = Current ? FindSegment(Current, Segment) : nullptr;
				if (!Property) { return 0; }

				OutChain.Add(Property);
				NumSegments++;

				const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
				Current = StructProperty ? StructProperty->Struct : nullptr;
			}

			if (bLast) { break; }
		}

		return NumSegments > 0 && !OutChain.Last()->HasAnyPropertyFlags(CPF_Deprecated) ? NumSegments : 0;
	}

	bool TryShorthandConstant(const UClass* InClass, const FStringView InStem, FPropertyChain& OutChain)
	{
		TStringBuilder<FName::StringBufferSize> MemberName;
		MemberName << InStem << TEXT("Value");

		const FName Name(MemberName.ToView(), FNAME_Find);
		const FStructProperty* ShorthandProp = Name.IsNone() ? nullptr : CastField<FStructProperty>(InClass->FindPropertyByName(Name));
		if (!ShorthandProp || !ShorthandProp->Struct || !ShorthandProp->Struct->IsChildOf(FPCGExInputShorthandBase::StaticStruct()))
		{
			return false;
		}

		const FProperty* ConstantProp = ShorthandProp->Struct->FindPropertyByName(FName("Constant"));
		if (!ConstantProp)
		{
			return false;
		}

		OutChain.Add(ShorthandProp);
		OutChain.Add(ConstantProp);
		return true;
	}

	// Resolves an override key to its property chain and returns the key's segment count, 0 when it does not resolve.
	// Unknown names resolve to nothing without logging: override attribute sets routinely carry unrelated attributes.
	int32 ResolveOverride(const UClass* InClass, const FName InKey, FPropertyChain& OutChain)
	{
		// Flat names are the common case and need no string work.
		const FProperty* FlatProperty = PCGPropertyHelpers::FindPropertyByName(InClass, InKey);
		if (FlatProperty && !FlatProperty->HasAnyPropertyFlags(CPF_Deprecated))
		{
			OutChain.Add(FlatProperty);
			return 1;
		}

		const FNameBuilder KeyBuilder(InKey);
		const FStringView Key = KeyBuilder.ToView();

		int32 Separator = INDEX_NONE;
		if (Key.FindChar(TEXT('/'), Separator))
		{
			const int32 NumSegments = ResolvePath(InClass, Key, OutChain);
			if (!NumSegments) { OutChain.Reset(); }
			return NumSegments;
		}

		// A migrated triplet's old flat name resolves to its _DEPRECATED stub, so reroute the override into
		// the matching input-shorthand's Constant. Input stays untouched: the legacy flat override only
		// ever affected the constant. Both migration namings are in use: "<Key>" -> "<Key>Value", and "<X>Constant" -> "<X>Value".
		if (TryShorthandConstant(InClass, Key, OutChain)) { return 1; }
		if (Key.EndsWith(TEXT("Constant")) && TryShorthandConstant(InClass, Key.LeftChop(8), OutChain)) { return 1; }
		return 0;
	}

	// Walks the struct chain down to the leaf's container.
	void* ResolveContainer(void* InRoot, const TConstArrayView<const FProperty*> InChain)
	{
		void* Container = InRoot;
		for (int32 i = 0; i < InChain.Num() - 1; i++)
		{
			Container = CastFieldChecked<FStructProperty>(InChain[i])->ContainerPtrToValuePtr<void>(Container);
		}
		return Container;
	}
}

void UPCGExInstancedFactory::BindContext(FPCGExContext* InContext)
{
	Context = InContext;
}

void UPCGExInstancedFactory::InitializeInContext(FPCGExContext* InContext, FName InOverridesPinLabel)
{
	FindSettingsOverrides(InContext, InOverridesPinLabel);
}

void UPCGExInstancedFactory::FindSettingsOverrides(FPCGExContext* InContext, FName InPinLabel)
{
	// Reflection-based property override system: reads attributes from PCGParamData inputs
	// on the specified pin, then applies matching attribute values to UPROPERTY fields on this
	// factory by name. This enables data-driven configuration of factory behavior without subclassing.
	TArray<FPCGTaggedData> OverrideParams = InContext->InputData.GetParamsByPin(InPinLabel);
	for (FPCGTaggedData& InTaggedData : OverrideParams)
	{
		const UPCGParamData* ParamData = Cast<UPCGParamData>(InTaggedData.Data);

		if (!ParamData)
		{
			continue;
		}
		const TSharedPtr<PCGExData::FAttributesInfos> Infos = PCGExData::FAttributesInfos::Get(ParamData->Metadata);

		for (PCGExData::FAttributeIdentity& Identity : Infos->Identities)
		{
			const FPCGMetadataAttributeBase* Attribute = ParamData->Metadata->GetConstAttribute(Identity.GetIdentifier());
			if (!Attribute) { continue; }

			FOverrideSource& Source = PossibleOverrides.FindOrAdd(Identity.Name);
			Source.Attribute = Attribute;
			Source.Metadata = ParamData->Metadata;
		}
	}

	ApplyOverrides();
	PossibleOverrides.Empty();
}

#if WITH_EDITOR
void UPCGExInstancedFactory::UpdateUserFacingInfos()
{
}
#endif

void UPCGExInstancedFactory::Cleanup()
{
	Context = nullptr;
	PrimaryDataFacade.Reset();
	SecondaryDataFacade.Reset();
}

UPCGExInstancedFactory* UPCGExInstancedFactory::CreateNewInstance(PCGEx::FManagedObjects* InManagedObjects) const
{
	if (!InManagedObjects)
	{
		return nullptr;
	}
	UPCGExInstancedFactory* TypedInstance = InManagedObjects->New<UPCGExInstancedFactory>(GetTransientPackage(), this->GetClass());

	check(TypedInstance)

	TypedInstance->CopySettingsFrom(this);
	return TypedInstance;
}

void UPCGExInstancedFactory::RegisterConsumableAttributesWithFacade(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InFacade) const
{
}

void UPCGExInstancedFactory::RegisterPrimaryBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
}

void UPCGExInstancedFactory::BeginDestroy()
{
	Cleanup();
	UObject::BeginDestroy();
}

void UPCGExInstancedFactory::ApplyOverrides()
{
	UClass* ObjectClass = GetClass();

	struct FResolvedOverride
	{
		int32 Rank = 0;
		PCGExInstancedFactory::FPropertyChain Chain;
		FOverrideSource Source;
	};

	// Resolved once, before the sort, so unrelated attributes never reach it.
	TArray<FResolvedOverride> Resolved;
	Resolved.Reserve(PossibleOverrides.Num());
	for (const TPair<FName, FOverrideSource>& Pair : PossibleOverrides)
	{
		FResolvedOverride& Entry = Resolved.Emplace_GetRef();
		const int32 NumSegments = PCGExInstancedFactory::ResolveOverride(ObjectClass, Pair.Key, Entry.Chain);
		if (!NumSegments)
		{
			Resolved.Pop(EAllowShrinking::No);
			continue;
		}

		Entry.Rank = PCGExInstancedFactory::OverrideRank(NumSegments, Pair.Value.Attribute);
		Entry.Source = Pair.Value;
	}
	Resolved.StableSort([](const FResolvedOverride& A, const FResolvedOverride& B) { return A.Rank < B.Rank; });

	for (const FResolvedOverride& Override : Resolved)
	{
		void* Container = PCGExInstancedFactory::ResolveContainer(this, Override.Chain);
		const FProperty* Property = Override.Chain.Last();
		const FPCGMetadataAttributeBase* Attribute = Override.Source.Attribute;

		// Basic attributes try the typed coerced setter first (selector-from-string, off-thread-safe hard refs); anything it
		// cannot apply goes through the generic accessor. Either path is a best-effort name match: failures are silent.
		bool bTypedSet = false;
		PCGExMetaHelpers::ExecuteWithRightType(Attribute, [&](auto DummyValue)
		{
			using T = decltype(DummyValue);
			bTypedSet = PCGExPropertyHelpers::TrySetFPropertyValueCoerced<T>(Container, Property, Attribute->GetValueFromItemKey<T>(0));
		});

		if (!bTypedSet)
		{
			PCGExPropertyHelpers::TrySetFPropertyFromAttribute(Container, Property, Attribute, Override.Source.Metadata);
		}
	}
}

void UPCGExInstancedFactory::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	BindContext(Other->Context);
	PCGExPropertyHelpers::CopyProperties(this, Other);
}

void UPCGExInstancedFactory::RegisterAssetDependencies(FPCGExContext* InContext)
{
}
