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
			PossibleOverrides.Add(Identity.Name, ParamData->Metadata->GetMutableAttribute(Identity.GetIdentifier()));
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

	for (const TPair<FName, FPCGMetadataAttributeBase*>& PossibleOverride : PossibleOverrides)
	{
		// Find the property by name.
		FProperty* Property = ObjectClass->FindPropertyByName(PossibleOverride.Key);

		// A migrated triplet's old flat name resolves to its _DEPRECATED stub, so reroute the override into
		// the matching input-shorthand's Constant. Input stays untouched: the legacy flat override only
		// ever affected the constant.
		void* Container = this;
		if (!Property || Property->HasAnyPropertyFlags(CPF_Deprecated))
		{
			Property = nullptr;

			auto TryShorthandConstant = [&](const FString& MemberName) -> bool
			{
				const FStructProperty* ShorthandProp = CastField<FStructProperty>(ObjectClass->FindPropertyByName(FName(MemberName)));
				if (!ShorthandProp || !ShorthandProp->Struct || !ShorthandProp->Struct->IsChildOf(FPCGExInputShorthandBase::StaticStruct()))
				{
					return false;
				}
				FProperty* ConstantProp = ShorthandProp->Struct->FindPropertyByName(FName("Constant"));
				if (!ConstantProp)
				{
					return false;
				}
				Property = ConstantProp;
				Container = ShorthandProp->ContainerPtrToValuePtr<void>(this);
				return true;
			};

			// Both migration namings are in use: "<Key>" -> "<Key>Value", and "<X>Constant" -> "<X>Value".
			const FString KeyStr = PossibleOverride.Key.ToString();
			if (!TryShorthandConstant(KeyStr + TEXT("Value")) && KeyStr.EndsWith(TEXT("Constant")))
			{
				TryShorthandConstant(KeyStr.LeftChop(8) + TEXT("Value"));
			}

			if (!Property)
			{
				continue;
			}
		}

		// Container/extended source attributes can't drive the typed FProperty setter -- skip silently
		// (the override is a best-effort name-match anyway).
		PCGExMetaHelpers::ExecuteWithRightType(PossibleOverride.Value, [&](auto DummyValue)
		{
			using T = decltype(DummyValue);
			PCGExPropertyHelpers::TrySetFPropertyValueCoerced<T>(Container, Property, PossibleOverride.Value->GetValueFromItemKey<T>(0));
		});
	}
}

void UPCGExInstancedFactory::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	BindContext(Other->Context);
	PCGExPropertyHelpers::CopyProperties(this, Other);
}

void UPCGExInstancedFactory::RegisterAssetDependencies(FPCGExContext* InContext)
{
	//InContext->AddAssetDependency();
}
