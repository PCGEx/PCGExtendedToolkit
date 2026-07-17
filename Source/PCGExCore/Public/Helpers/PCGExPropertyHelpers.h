// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Types/PCGExTypes.h"
#include "UObject/SoftObjectPath.h"

namespace PCGExPropertyHelpers
{
#define PCGEX_FOREACH_SUPPORTEDFPROPERTY(MACRO)\
MACRO(FBoolProperty, bool) \
MACRO(FIntProperty, int32) \
MACRO(FInt64Property, int64) \
MACRO(FFloatProperty, float) \
MACRO(FDoubleProperty, double) \
MACRO(FStrProperty, FString) \
MACRO(FNameProperty, FName)

#define PCGEX_FOREACH_SUPPORTEDFSTRUCT(MACRO)\
MACRO(FStructProperty, FVector2D) \
MACRO(FStructProperty, FVector) \
MACRO(FStructProperty, FVector4) \
MACRO(FStructProperty, FQuat) \
MACRO(FStructProperty, FRotator) \
MACRO(FStructProperty, FTransform)

	/** Resolves InPath (worker-safe: off the game thread the load marshals to it and blocks, via
	 *  PCGExHelpers::LoadBlocking_AnyThread) and writes the loaded object to the property if
	 *  class-compatible. Never call raw FSoftObjectPath::TryLoad() here -- it is game-thread-only. */
	PCGEXCORE_API bool TrySetObjectPropertyFromPath(void* InContainer, FObjectPropertyBase* InProperty, const FSoftObjectPath& InPath);

	template <typename T>
	static bool TrySetFPropertyValue(void* InContainer, FProperty* InProperty, T InValue)
	{
		const PCGExTypeOps::ITypeOpsBase* TypeOps = PCGExTypeOps::FTypeOpsRegistry::Get<T>();

		if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(InProperty))
		{
			// If input type is a soft object path, check if the target property is an object type
			// and resolve the object path
			FSoftObjectPath Path;
			TypeOps->ConvertTo(&InValue, PCGExTypes::TTraits<FSoftObjectPath>::Type, &Path);

			if (TrySetObjectPropertyFromPath(InContainer, ObjectProperty, Path))
			{
				return true;
			}
		}

#define PCGEX_TRY_SET_FPROPERTY(_PTYPE, _TYPE)\
	if(InProperty->IsA<_PTYPE>()){\
		_PTYPE* P = CastField<_PTYPE>(InProperty);\
		_TYPE V;\
		TypeOps->ConvertTo(&InValue, PCGExTypes::TTraits<_TYPE>::Type, &V);\
		P->SetPropertyValue_InContainer(InContainer, V);\
		return true;\
		}
		PCGEX_FOREACH_SUPPORTEDFPROPERTY(PCGEX_TRY_SET_FPROPERTY)
#undef PCGEX_TRY_SET_FPROPERTY

		if (FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
		{
			if (StructProperty->Struct == TBaseStructure<FPCGAttributePropertyInputSelector>::Get())
			{
				FPCGAttributePropertyInputSelector NewSelector = FPCGAttributePropertyInputSelector();
				FString S;
				TypeOps->ConvertTo(&InValue, PCGExTypes::TTraits<FString>::Type, &S);\
				NewSelector.Update(S);
				void* StructContainer = StructProperty->ContainerPtrToValuePtr<void>(InContainer);
				*reinterpret_cast<FPCGAttributePropertyInputSelector*>(StructContainer) = NewSelector;
				return true;
			}

#define PCGEX_TRY_SET_FSTRUCT(_PTYPE, _TYPE) \
	else if (StructProperty->Struct == TBaseStructure<_TYPE>::Get()){\
		void* StructContainer = StructProperty->ContainerPtrToValuePtr<void>(InContainer);\
		_TYPE V;\
		TypeOps->ConvertTo(&InValue, PCGExTypes::TTraits<_TYPE>::Type, &V);\
		*reinterpret_cast<_TYPE*>(StructContainer) = V;\
		return true;\
		}
			PCGEX_FOREACH_SUPPORTEDFSTRUCT(PCGEX_TRY_SET_FSTRUCT)
#undef PCGEX_TRY_SET_FSTRUCT
		}

		return false;
	}

	/** Coerced single-value property write: the engine property accessor first (broadcast + construct
	 *  covers targets TrySetFPropertyValue does not -- FLinearColor, FColor, ... -- plus every standard
	 *  numeric/vector conversion), TrySetFPropertyValue as fallback for targets the accessor doesn't
	 *  support. Hard object/class targets skip the accessor: FPCGPropertyObjectPtrAccessor::Set does a
	 *  raw TryLoad(), unsafe off the game thread -- the fallback resolves them through
	 *  TrySetObjectPropertyFromPath instead. Soft object/class targets keep the accessor (never loads). */
	template <typename T>
	static bool TrySetFPropertyValueCoerced(void* InContainer, const FProperty* InProperty, const T& InValue)
	{
		if (!InProperty->IsA<FObjectProperty>())
		{
			if (const TUniquePtr<IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreatePropertyAccessor(InProperty))
			{
				void* Containers[1] = {InContainer};
				FPCGAttributeAccessorKeysGenericPtrs Keys(Containers);
				if (Accessor->Set<T>(InValue, Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
				{
					return true;
				}
			}
		}

		return TrySetFPropertyValue<T>(InContainer, const_cast<FProperty*>(InProperty), InValue);
	}

	PCGEXCORE_API void CopyStructProperties(const void* SourceStruct, void* TargetStruct, const UStruct* SourceStructType, const UStruct* TargetStructType);

	PCGEXCORE_API bool CopyProperties(UObject* Target, const UObject* Source, const TSet<FString>* Exclusions = nullptr);
}
