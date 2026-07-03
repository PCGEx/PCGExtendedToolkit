// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExData.h"

#include "PCGExLog.h"
#include "PCGExSettingsCacheBody.h"
#include "Data/PCGExPointIO.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Types/PCGExTypes.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

namespace PCGExData
{
#pragma region FPropertyBuffer

	FPropertyBuffer::FPropertyBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier)
		: IBuffer(InSource, InIdentifier)
	{
		SetType(EPCGMetadataTypes::Unknown);
	}

	FPropertyBuffer::~FPropertyBuffer()
	{
		delete CachedInnerProperty;
		CachedInnerProperty = nullptr;
	}

	FProperty* FPropertyBuffer::CreateInnerPropertyFromDesc(const FPCGMetadataAttributeDesc& Desc, FFieldVariant PropertyScope)
	{
		// Construct an FProperty matching a metadata attribute's full descriptor.
		// Mirrors PCG::Private::CreatePropertyFromDesc -- supports containers (Array/Set/Map),
		// nested containers (TArray<TArray<T>>), all scalar legacy types, struct/enum, and
		// the Object family (Object/SoftObject/Class/SoftClass).
		// The constructed property passes SameType() against the attribute's internal property.

		// Container handling: walk ContainerTypes outermost-to-innermost, wrapping each layer.
		// The innermost wrapper holds the leaf scalar/struct property.
		if (Desc.ContainerTypes.Num() > 0)
		{
			FProperty* Prop = nullptr;            // The outermost wrapper (returned at the end)
			FProperty** ValuePropertyPtr = &Prop; // Slot to fill on each iteration (Inner / ElementProp / ValueProp)
			FFieldVariant PropertyOwner = PropertyScope;

			for (EPCGMetadataAttributeContainerTypes ContainerType : Desc.ContainerTypes)
			{
				switch (ContainerType)
				{
				case EPCGMetadataAttributeContainerTypes::Array:
				{
					FArrayProperty* ArrayProperty = new FArrayProperty(PropertyOwner, Desc.Name);
					*ValuePropertyPtr = ArrayProperty;
					ValuePropertyPtr = &ArrayProperty->Inner;
					PropertyOwner = ArrayProperty;
					break;
				}
				case EPCGMetadataAttributeContainerTypes::Set:
				{
					FSetProperty* SetProperty = new FSetProperty(PropertyOwner, Desc.Name);
					*ValuePropertyPtr = SetProperty;
					ValuePropertyPtr = &SetProperty->ElementProp;
					PropertyOwner = SetProperty;
					break;
				}
				case EPCGMetadataAttributeContainerTypes::Map:
				{
					FMapProperty* MapProperty = new FMapProperty(PropertyOwner, Desc.Name);
					*ValuePropertyPtr = MapProperty;
					ValuePropertyPtr = &MapProperty->ValueProp;
					PropertyOwner = MapProperty;

					// Map key -- container types are not supported for keys.
					FPCGMetadataAttributeDesc KeyDesc;
					KeyDesc.ValueType = Desc.KeyType;
					KeyDesc.ValueTypeObject = Desc.KeyTypeObject;
					KeyDesc.Name = Desc.Name;
					FProperty* KeyProp = CreateInnerPropertyFromDesc(KeyDesc, PropertyOwner);
					if (!KeyProp)
					{
						delete MapProperty; // Recursive delete cascades through nested wrappers + Inner via ~FProperty.
						return nullptr;
					}
					MapProperty->KeyProp = KeyProp;
					break;
				}
				default:
					ensureMsgf(false, TEXT("Unsupported container type %d"), static_cast<int32>(ContainerType));
					delete Prop;
					return nullptr;
				}
			}

			// Recurse for the leaf type (containers stripped).
			FPCGMetadataAttributeDesc InnerDesc = Desc;
			InnerDesc.ContainerTypes.Reset();
			*ValuePropertyPtr = CreateInnerPropertyFromDesc(InnerDesc, PropertyOwner);

			if (*ValuePropertyPtr == nullptr)
			{
				delete Prop;
				return nullptr;
			}
			return Prop;
		}

		const UScriptStruct* ScriptStruct = nullptr;

		switch (Desc.ValueType)
		{
		case EPCGMetadataTypes::Boolean:
		{
			FBoolProperty* Prop = new FBoolProperty(PropertyScope, Desc.Name);
			Prop->SetBoolSize(sizeof(bool), true);
			Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
			return Prop;
		}
		case EPCGMetadataTypes::Byte:
		{
			FByteProperty* Prop = new FByteProperty(PropertyScope, Desc.Name);
			Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
			return Prop;
		}
		case EPCGMetadataTypes::Integer32:
		{
			FIntProperty* Prop = new FIntProperty(PropertyScope, Desc.Name);
			Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
			return Prop;
		}
		case EPCGMetadataTypes::Integer64:
		{
			FInt64Property* Prop = new FInt64Property(PropertyScope, Desc.Name);
			Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
			return Prop;
		}
		case EPCGMetadataTypes::Float:
		{
			FFloatProperty* Prop = new FFloatProperty(PropertyScope, Desc.Name);
			Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
			return Prop;
		}
		case EPCGMetadataTypes::Double:
		{
			FDoubleProperty* Prop = new FDoubleProperty(PropertyScope, Desc.Name);
			Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
			return Prop;
		}
		case EPCGMetadataTypes::Name:
		{
			FNameProperty* Prop = new FNameProperty(PropertyScope, Desc.Name);
			Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
			return Prop;
		}
		case EPCGMetadataTypes::String:
		{
			FStrProperty* Prop = new FStrProperty(PropertyScope, Desc.Name);
			Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
			return Prop;
		}
		case EPCGMetadataTypes::Text:
		{
			FTextProperty* Prop = new FTextProperty(PropertyScope, Desc.Name);
			return Prop;
		}
		case EPCGMetadataTypes::Enum:
			if (const UEnum* Enum = Cast<UEnum>(Desc.ValueTypeObject))
			{
				FEnumProperty* Prop = new FEnumProperty(PropertyScope, Desc.Name);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				FNumericProperty* UnderlyingProp = new FByteProperty(Prop, TEXT("UnderlyingType"));
				UnderlyingProp->SetPropertyFlags(CPF_HasGetValueTypeHash | CPF_IsPlainOldData);
				Prop->SetEnum(const_cast<UEnum*>(Enum));
				Prop->AddCppProperty(UnderlyingProp);
				return Prop;
			}
			break;
		// Object family (Object/SoftObject/Class/SoftClass).
		// IMPORTANT: PCGEx attribute storage is NOT GC-tracked. UObject pointers stored here
		// stay valid only as long as something else keeps the referent alive (typically the
		// short PCG processing window). Do not persist these to long-lived UObject-owned storage
		// without re-rooting. Soft variants (SoftObject/SoftClass) are safer (path-based).
		// Runtime-constructed FProperty subclasses skip LinkInternal, so ElementSize
		// must be set explicitly -- otherwise GetSize() returns 0 and FScopedTypedValue
		// allocates a zero-byte buffer, tripping size checks in the property buffers.
		case EPCGMetadataTypes::Object:
		{
			FObjectProperty* Prop = new FObjectProperty(PropertyScope, Desc.Name);
			UClass* Class = const_cast<UClass*>(Cast<UClass>(Desc.ValueTypeObject));
			if (!Class)
			{
				Class = UObject::StaticClass();
			}
			if (Class->HasAnyClassFlags(CLASS_DefaultToInstanced))
			{
				Prop->SetPropertyFlags(CPF_InstancedReference);
			}
			Prop->SetPropertyClass(Class);
			Prop->SetPropertyFlags(CPF_HasGetValueTypeHash | CPF_TObjectPtr);
			Prop->SetElementSize(sizeof(FObjectPtr));
			return Prop;
		}
		case EPCGMetadataTypes::SoftObject:
		{
			FSoftObjectProperty* Prop = new FSoftObjectProperty(PropertyScope, Desc.Name);
			UClass* Class = const_cast<UClass*>(Cast<UClass>(Desc.ValueTypeObject));
			if (!Class)
			{
				Class = UObject::StaticClass();
			}
			Prop->SetPropertyClass(Class);
			Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
			Prop->SetElementSize(sizeof(FSoftObjectPtr));
			return Prop;
		}
		case EPCGMetadataTypes::Class:
		{
			FClassProperty* Prop = new FClassProperty(PropertyScope, Desc.Name);
			UClass* MetaClass = const_cast<UClass*>(Cast<UClass>(Desc.ValueTypeObject));
			if (!MetaClass)
			{
				MetaClass = UObject::StaticClass();
			}
#if WITH_EDITORONLY_DATA
			Prop->SetMetaClass(MetaClass);
#else
			Prop->SetMetaClass(MetaClass);
#endif
			Prop->PropertyClass = UClass::StaticClass();
			Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
			Prop->SetElementSize(sizeof(FObjectPtr));
			return Prop;
		}
		case EPCGMetadataTypes::SoftClass:
		{
			FSoftClassProperty* Prop = new FSoftClassProperty(PropertyScope, Desc.Name);
			UClass* MetaClass = const_cast<UClass*>(Cast<UClass>(Desc.ValueTypeObject));
			if (!MetaClass)
			{
				MetaClass = UObject::StaticClass();
			}
#if WITH_EDITORONLY_DATA
			Prop->SetMetaClass(MetaClass);
#else
			Prop->SetMetaClass(MetaClass);
#endif
			Prop->PropertyClass = UClass::StaticClass();
			Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
			Prop->SetElementSize(sizeof(FSoftObjectPtr));
			return Prop;
		}
		case EPCGMetadataTypes::SoftObjectPath:
			ScriptStruct = TBaseStructure<FSoftObjectPath>::Get();
			break;
		case EPCGMetadataTypes::SoftClassPath:
			ScriptStruct = TBaseStructure<FSoftClassPath>::Get();
			break;
		case EPCGMetadataTypes::Vector2:
			ScriptStruct = TBaseStructure<FVector2D>::Get();
			break;
		case EPCGMetadataTypes::Vector:
			ScriptStruct = TBaseStructure<FVector>::Get();
			break;
		case EPCGMetadataTypes::Vector4:
			ScriptStruct = TBaseStructure<FVector4>::Get();
			break;
		case EPCGMetadataTypes::Quaternion:
			ScriptStruct = TBaseStructure<FQuat>::Get();
			break;
		case EPCGMetadataTypes::Rotator:
			ScriptStruct = TBaseStructure<FRotator>::Get();
			break;
		case EPCGMetadataTypes::Transform:
			ScriptStruct = TBaseStructure<FTransform>::Get();
			break;
		case EPCGMetadataTypes::Struct:
			ScriptStruct = Cast<UScriptStruct>(Desc.ValueTypeObject);
			break;
		default:
			break;
		}

		if (ScriptStruct)
		{
			FStructProperty* Prop = new FStructProperty(PropertyScope, Desc.Name);
			Prop->Struct = const_cast<UScriptStruct*>(ScriptStruct);
			Prop->SetElementSize(ScriptStruct->GetStructureSize());

			if (ScriptStruct->GetCppStructOps() && ScriptStruct->GetCppStructOps()->HasGetTypeHash())
			{
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
			}

			if (ScriptStruct->StructFlags & STRUCT_HasInstancedReference)
			{
				Prop->SetPropertyFlags(CPF_ContainsInstancedReference);
			}

			return Prop;
		}

		return nullptr;
	}

	int32 FPropertyBuffer::GetElementSizeFromDesc(const FPCGMetadataAttributeDesc& Desc)
	{
		// For non-container types, fall back to the type/VTO based sizing -- cheap, no allocations.
		if (Desc.ContainerTypes.IsEmpty())
		{
			return PCGExTypes::GetElementSizeFromType(Desc.ValueType, Desc.ValueTypeObject);
		}

		// For containers, the storage footprint is fixed (FScriptArray / FScriptSet / FScriptMap)
		// but depends on the wrapper. Construct a transient property and ask it for its element size.
		// This is a one-time cost per attribute description; result is cached on the buffer.
		FProperty* Prop = CreateInnerPropertyFromDesc(Desc);
		if (!Prop)
		{
			return 0;
		}
		const int32 Size = Prop->GetSize();
		delete Prop;
		return Size;
	}

	int32 FPropertyBuffer::GetElementAlignmentFromDesc(const FPCGMetadataAttributeDesc& Desc)
	{
		if (Desc.ContainerTypes.IsEmpty())
		{
			return PCGExTypes::GetElementAlignmentFromType(Desc.ValueType, Desc.ValueTypeObject);
		}

		FProperty* Prop = CreateInnerPropertyFromDesc(Desc);
		if (!Prop)
		{
			return 1;
		}
		const int32 Alignment = Prop->GetMinAlignment();
		delete Prop;
		return Alignment;
	}

	//
	// Container-accessor helpers
	//

	int32 FPropertyBuffer::GetInnerElementSizeFromDesc(const FPCGMetadataAttributeDesc& Desc)
	{
		// Non-container: inner == outer. Just forward.
		if (Desc.ContainerTypes.IsEmpty())
		{
			return GetElementSizeFromDesc(Desc);
		}

		// Strip the outermost ContainerType entry and ask recursively. For
		// TArray<FVector>: ContainerTypes=[Array] -> inner desc has ContainerTypes=[]
		// + ValueType=Vector -> returns 24. For TArray<TArray<FVector>>:
		// ContainerTypes=[Array,Array] -> inner has [Array] -> returns sizeof(FScriptArray).
		// (Nested container element-reads aren't meaningful via FContainerIndexAccessor
		// yet because the accessor hot path only walks one container level --
		// Stripping one layer is correct
		// for the single-step container-index case.)
		FPCGMetadataAttributeDesc InnerDesc = Desc;
		InnerDesc.ContainerTypes.RemoveAt(0);
		return GetElementSizeFromDesc(InnerDesc);
	}

	int32 FPropertyBuffer::GetContainerNum(const void* ContainerBytes)
	{
		if (!ContainerBytes)
		{
			return 0;
		}
		// TArray, TSet, and TMap all store their element count at a
		// layout-compatible binary offset. We cast to FScriptArray as the
		// canonical representative -- FScriptSet and FScriptMap place Num()
		// at the same offset.
		const FScriptArray* Arr = static_cast<const FScriptArray*>(ContainerBytes);
		return Arr->Num();
	}

	const void* FPropertyBuffer::GetArrayElementAt(const void* ArrayBytes, int32 Index, int32 ElementSize)
	{
		if (!ArrayBytes || ElementSize <= 0)
		{
			return nullptr;
		}
		const FScriptArray* Arr = static_cast<const FScriptArray*>(ArrayBytes);
		const int32 Num = Arr->Num();
		if (Index < 0 || Index >= Num)
		{
			return nullptr;
		}
		// GetData() on FScriptArray returns a raw const void* into the contiguous
		// element storage; stride by ElementSize to reach element [Index].
		return static_cast<const uint8*>(Arr->GetData()) + static_cast<SIZE_T>(Index) * static_cast<SIZE_T>(ElementSize);
	}

	void* FPropertyBuffer::GetMutableArrayElementAt(void* ArrayBytes, int32 Index, int32 ElementSize)
	{
		if (!ArrayBytes || ElementSize <= 0)
		{
			return nullptr;
		}
		FScriptArray* Arr = static_cast<FScriptArray*>(ArrayBytes);
		const int32 Num = Arr->Num();
		if (Index < 0 || Index >= Num)
		{
			return nullptr;
		}
		return static_cast<uint8*>(Arr->GetData()) + static_cast<SIZE_T>(Index) * static_cast<SIZE_T>(ElementSize);
	}

	bool FPropertyBuffer::InitProperty(const FPCGMetadataAttributeBase* InGenericAttribute)
	{
		if (!InGenericAttribute)
		{
			return false;
		}

		// Cache the real attribute descriptor onto the IBuffer member -- replaces the empty
		// default-constructed Desc from the IBuffer base, so GetDesc() now reflects the actual
		// attribute shape (ContainerTypes, ValueTypeObject, KeyType).
		Desc = InGenericAttribute->GetAttributeDesc();

		// Build the FULL property -- including container wrapping, so reads/writes/copies
		// of TArray/TSet/TMap attributes route through FArrayProperty/FSetProperty/FMapProperty.
		CachedInnerProperty = CreateInnerPropertyFromDesc(Desc);
		if (!CachedInnerProperty)
		{
			return false;
		}

		ElementSize = GetElementSizeFromDesc(Desc);
		return ElementSize > 0;
	}

#pragma endregion

#pragma region FPropertyArrayBuffer

	FPropertyArrayBuffer::FPropertyArrayBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier)
		: FPropertyBuffer(InSource, InIdentifier)
	{
		check(InIdentifier.MetadataDomain.Flag != EPCGMetadataDomainFlag::Data)
		UnderlyingDomain = EDomainType::Elements;
	}

	FPropertyArrayBuffer::~FPropertyArrayBuffer()
	{
		// Each element in InBytes/OutBytes was initialized via CachedInnerProperty->InitializeValue
		// (and possibly CopyCompleteValue), so we owe a matching DestroyValue to release any
		// allocated state -- array allocator memory, FString character storage, FText shared data, etc.
		// Without this, container-typed and string-typed attributes would leak per-element heap on every Flush.
		DestroyAllElements(InBytes);
		DestroyAllElements(OutBytes);
	}

	void FPropertyArrayBuffer::DestroyAllElements(const TSharedPtr<TArray<uint8>>& Bytes) const
	{
		if (!Bytes || !CachedInnerProperty || ElementSize <= 0)
		{
			return;
		}
		const int32 NumElements = Bytes->Num() / ElementSize;
		uint8* Data = Bytes->GetData();
		for (int32 i = 0; i < NumElements; i++)
		{
			CachedInnerProperty->DestroyValue(Data + i * ElementSize);
		}
	}

	int32 FPropertyArrayBuffer::GetNumValues(const EIOSide InSide)
	{
		if (InSide == EIOSide::In)
		{
			return InBytes ? InBytes->Num() / FMath::Max(1, ElementSize) : 0;
		}
		return OutBytes ? OutBytes->Num() / FMath::Max(1, ElementSize) : 0;
	}

	bool FPropertyArrayBuffer::IsWritable()
	{
		return OutBytes != nullptr;
	}

	bool FPropertyArrayBuffer::IsReadable()
	{
		return InBytes != nullptr;
	}

	bool FPropertyArrayBuffer::ReadsFromOutput()
	{
		return InBytes == OutBytes;
	}

	bool FPropertyArrayBuffer::EnsureReadable()
	{
		{
			FReadScopeLock ReadLock(BufferLock);
			if (InBytes)
			{
				return true;
			}
		}
		{
			FWriteScopeLock WriteLock(BufferLock);
			if (InBytes)
			{
				return true;
			}
			if (OutBytes)
			{
				InBytes = OutBytes;
				return true;
			}
		}
		// InitForRead acquires its own WriteScopeLock -- must not hold ours when calling it.
		return InitForRead(EIOSide::In);
	}

	void FPropertyArrayBuffer::ReadVoid(const int32 Index, PCGExTypes::FScopedTypedValue& OutValue) const
	{
		check(InBytes && ElementSize > 0);
		check(OutValue.GetValueSize() == ElementSize);
		const int32 Offset = Index * ElementSize;
		check(Offset + ElementSize <= InBytes->Num());

		if (const FProperty* Prop = OutValue.GetProperty())
		{
			// Property-backed: use reflection for deep copy (handles TArray, TSet, etc.)
			Prop->CopyCompleteValue(OutValue.GetRaw(), InBytes->GetData() + Offset);
		}
		else
		{
			FMemory::Memcpy(OutValue.GetRaw(), InBytes->GetData() + Offset, ElementSize);
		}
	}

	void FPropertyArrayBuffer::SetVoid(const int32 Index, const PCGExTypes::FScopedTypedValue& Value)
	{
		check(OutBytes && ElementSize > 0);
		check(Value.GetValueSize() == ElementSize);
		const int32 Offset = Index * ElementSize;
		check(Offset + ElementSize <= OutBytes->Num());

		if (const FProperty* Prop = Value.GetProperty())
		{
			Prop->CopyCompleteValue(OutBytes->GetData() + Offset, Value.GetRaw());
		}
		else
		{
			FMemory::Memcpy(OutBytes->GetData() + Offset, Value.GetRaw(), ElementSize);
		}
	}

	void FPropertyArrayBuffer::GetVoid(const int32 Index, PCGExTypes::FScopedTypedValue& OutValue)
	{
		if (OutBytes)
		{
			check(OutValue.GetValueSize() == ElementSize);
			const int32 Offset = Index * ElementSize;
			check(Offset + ElementSize <= OutBytes->Num());

			if (const FProperty* Prop = OutValue.GetProperty())
			{
				Prop->CopyCompleteValue(OutValue.GetRaw(), OutBytes->GetData() + Offset);
			}
			else
			{
				FMemory::Memcpy(OutValue.GetRaw(), OutBytes->GetData() + Offset, ElementSize);
			}
			return;
		}
		ReadVoid(Index, OutValue);
	}

	PCGExValueHash FPropertyArrayBuffer::ReadValueHash(const int32 Index)
	{
		if (!InBytes || ElementSize <= 0)
		{
			return 0;
		}
		return FCrc::MemCrc32(InBytes->GetData() + Index * ElementSize, ElementSize);
	}

	PCGExValueHash FPropertyArrayBuffer::GetValueHash(const int32 Index)
	{
		if (OutBytes && ElementSize > 0)
		{
			return FCrc::MemCrc32(OutBytes->GetData() + Index * ElementSize, ElementSize);
		}
		return ReadValueHash(Index);
	}

	bool FPropertyArrayBuffer::InitForRead(const EIOSide InSide)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (InBytes)
		{
			return true;
		}

		if (ElementSize <= 0)
		{
			return false;
		}

		const UPCGBasePointData* PointData = (InSide == EIOSide::In) ? Source->GetIn() : Source->GetOut();
		if (!PointData)
		{
			return false;
		}

		const FPCGMetadataAttributeBase* Attr = Source->FindConstAttribute(Identifier, InSide);
		if (!Attr)
		{
			return false;
		}

		GenericInAttribute = Attr;
		InAttribute = Attr;

		if (!CachedInnerProperty)
		{
			InitProperty(GenericInAttribute);
		}
		if (!CachedInnerProperty || ElementSize <= 0)
		{
			return false;
		}

		const int32 NumPoints = PointData->GetNumPoints();

		InBytes = MakeShared<TArray<uint8>>();
		InBytes->SetNumZeroed(NumPoints * ElementSize);

		auto EntryKeys = PointData->GetConstMetadataEntryValueRange();

		// Deep-copy each source value into our owned storage. Memcpy would shallow-alias container
		// allocators / FString data / etc. -- fine while source lives, but bytes we own here would
		// either dangle or double-free on cleanup. CopyCompleteValue gives us independent ownership.
		uint8* Data = InBytes->GetData();
		for (int32 i = 0; i < NumPoints; i++)
		{
			uint8* Slot = Data + i * ElementSize;
			CachedInnerProperty->InitializeValue(Slot);
			if (const void* ReadAddr = GenericInAttribute->GetReadAddressFromEntryKey_Unsafe(EntryKeys[i]))
			{
				CachedInnerProperty->CopyCompleteValue(Slot, ReadAddr);
			}
		}

		bReadComplete = true;
		return true;
	}

	bool FPropertyArrayBuffer::InitForWrite(const FPCGMetadataAttributeBase* SourceAttribute, EBufferInit Init)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (OutBytes)
		{
			return true;
		}

		if (ElementSize <= 0)
		{
			return false;
		}

		const UPCGBasePointData* OutData = Source->GetOut();
		if (!OutData)
		{
			return false;
		}

		// Resolve the output generic attribute
		if (SourceAttribute)
		{
			// Prefer an existing matching attribute on output (typically present from data duplication).
			FPCGMetadataAttributeBase* MutableAttr = Source->FindMutableAttribute(Identifier, EIOSide::Out);

			// Otherwise create it on-the-fly using the source attribute's desc -- mirrors what TBuffer<T>
			// does via FindOrCreateAttribute<T>. Required for the data forward path where target starts
			// without the attribute.
			if (!MutableAttr && OutData->Metadata)
			{
				MutableAttr = OutData->Metadata->CreateAttribute(
					Identifier,
					SourceAttribute->GetAttributeDesc(),
					SourceAttribute->AllowsInterpolation(),
					/*bOverrideParent=*/true);
			}

			if (MutableAttr)
			{
				GenericOutAttribute = MutableAttr;
				OutAttribute = GenericOutAttribute;
			}

			if (!CachedInnerProperty)
			{
				InitProperty(SourceAttribute);
			}
		}

		if (!CachedInnerProperty || ElementSize <= 0)
		{
			return false;
		}

		const int32 NumPoints = OutData->GetNumPoints();

		OutBytes = MakeShared<TArray<uint8>>();
		OutBytes->SetNumZeroed(NumPoints * ElementSize);

		// Each slot must start in a property-valid state so subsequent CopyCompleteValue / DestroyValue
		// behave correctly. Zero-init suffices for most types, but containers (FScriptArray etc.)
		// and types with construction-time invariants need explicit InitializeValue.
		uint8* OutBytesData = OutBytes->GetData();
		for (int32 i = 0; i < NumPoints; i++)
		{
			CachedInnerProperty->InitializeValue(OutBytesData + i * ElementSize);
		}

		// If inheriting, deep-copy input values into our owned output storage.
		if (Init == EBufferInit::Inherit && SourceAttribute)
		{
			auto EntryKeys = OutData->GetConstMetadataEntryValueRange();

			for (int32 i = 0; i < NumPoints; i++)
			{
				if (const void* ReadAddr = SourceAttribute->GetReadAddressFromEntryKey_Unsafe(EntryKeys[i]))
				{
					CachedInnerProperty->CopyCompleteValue(OutBytesData + i * ElementSize, ReadAddr);
				}
			}
		}

		bIsNewOutput = (Init == EBufferInit::New);
		return true;
	}

	void FPropertyArrayBuffer::Write(const bool bEnsureValidKeys)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPropertyArrayBuffer::Write);

		if (!IsWritable() || !IsEnabled() || !OutBytes)
		{
			return;
		}
		if (!GenericOutAttribute || !CachedInnerProperty)
		{
			return;
		}

		const UPCGBasePointData* OutData = Source->GetOut();
		if (!OutData)
		{
			return;
		}

		PCGEX_SHARED_CONTEXT_VOID(Source->GetContextHandle())
		SharedContext.Get()->AddProtectedAttributeName(GenericOutAttribute->Name);

		// Without GetOutKeys, fresh points share PCGDefaultValueKey (-1) and per-point
		// SetValueFromProperty collapses into the default slot. Typed TBuffer<T> gets this
		// via PCGAttributeAccessorHelpers; the property path has to trigger it manually.
		Source->GetOutKeys(bEnsureValidKeys);

		auto EntryKeys = OutData->GetConstMetadataEntryValueRange();
		const int32 NumPoints = FMath::Min(EntryKeys.Num(), OutBytes->Num() / FMath::Max(1, ElementSize));

		for (int32 i = 0; i < NumPoints; i++)
		{
			GenericOutAttribute->SetValueFromProperty(
				EntryKeys[i],
				OutBytes->GetData() + i * ElementSize,
				CachedInnerProperty);
		}
	}

	void FPropertyArrayBuffer::Flush()
	{
		// Property-aware cleanup: release per-element allocator state before dropping the byte storage.
		DestroyAllElements(InBytes);
		DestroyAllElements(OutBytes);
		InBytes.Reset();
		OutBytes.Reset();
	}

	void FPropertyArrayBuffer::SetFromVoidProperty(const int32 Index, const void* SrcPtr)
	{
		if (!OutBytes || !CachedInnerProperty || !SrcPtr || ElementSize <= 0)
		{
			return;
		}
		const int32 Offset = Index * ElementSize;
		check(Offset + ElementSize <= OutBytes->Num());
		// Slot was InitializeValue'd in InitForWrite, so it's in a property-valid state and
		// CopyCompleteValue can safely overwrite it (releasing any previous allocations as needed).
		CachedInnerProperty->CopyCompleteValue(OutBytes->GetData() + Offset, SrcPtr);
	}

#pragma endregion

#pragma region FPropertySingleValueBuffer

	FPropertySingleValueBuffer::FPropertySingleValueBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier)
		: FPropertyBuffer(InSource, InIdentifier)
	{
		check(InIdentifier.MetadataDomain.Flag == EPCGMetadataDomainFlag::Data)
		UnderlyingDomain = EDomainType::Data;
	}

	FPropertySingleValueBuffer::~FPropertySingleValueBuffer()
	{
		// Symmetric with FPropertyArrayBuffer: release per-slot allocator state before the inline
		// TArray<uint8>'s storage is freed. Without this, FString/container-typed single values leak
		// their per-element heap on every buffer drop.
		if (CachedInnerProperty && ElementSize > 0)
		{
			if (bReadInitialized && InValue.Num() >= ElementSize)
			{
				CachedInnerProperty->DestroyValue(InValue.GetData());
			}
			if (bWriteInitialized && OutValue.Num() >= ElementSize && !bReadFromOutput)
			{
				CachedInnerProperty->DestroyValue(OutValue.GetData());
			}
		}
	}

	int32 FPropertySingleValueBuffer::GetNumValues(const EIOSide InSide)
	{
		return 1;
	}

	bool FPropertySingleValueBuffer::IsWritable()
	{
		return bWriteInitialized;
	}

	bool FPropertySingleValueBuffer::IsReadable()
	{
		return bReadInitialized;
	}

	bool FPropertySingleValueBuffer::ReadsFromOutput()
	{
		return bReadFromOutput;
	}

	bool FPropertySingleValueBuffer::EnsureReadable()
	{
		{
			FReadScopeLock ReadLock(BufferLock);
			if (bReadInitialized)
			{
				return true;
			}
		}
		{
			FWriteScopeLock WriteLock(BufferLock);
			if (bReadInitialized)
			{
				return true;
			}
			if (bWriteInitialized && OutValue.Num() > 0)
			{
				InValue = OutValue;
				bReadFromOutput = true;
				bReadInitialized = true;
				return true;
			}
		}
		// InitForRead acquires its own WriteScopeLock -- must not hold ours when calling it.
		return InitForRead(EIOSide::In);
	}

	void FPropertySingleValueBuffer::ReadVoid(const int32 Index, PCGExTypes::FScopedTypedValue& OutVal) const
	{
		check(InValue.Num() >= ElementSize && ElementSize > 0);
		check(OutVal.GetValueSize() == ElementSize);

		if (const FProperty* Prop = OutVal.GetProperty())
		{
			Prop->CopyCompleteValue(OutVal.GetRaw(), InValue.GetData());
		}
		else
		{
			FMemory::Memcpy(OutVal.GetRaw(), InValue.GetData(), ElementSize);
		}
	}

	void FPropertySingleValueBuffer::SetVoid(const int32 Index, const PCGExTypes::FScopedTypedValue& Value)
	{
		check(OutValue.Num() >= ElementSize && ElementSize > 0);
		check(Value.GetValueSize() == ElementSize);

		if (const FProperty* Prop = Value.GetProperty())
		{
			Prop->CopyCompleteValue(OutValue.GetData(), Value.GetRaw());
		}
		else
		{
			FMemory::Memcpy(OutValue.GetData(), Value.GetRaw(), ElementSize);
		}
	}

	void FPropertySingleValueBuffer::GetVoid(const int32 Index, PCGExTypes::FScopedTypedValue& OutVal)
	{
		if (OutValue.Num() >= ElementSize)
		{
			check(OutVal.GetValueSize() == ElementSize);

			if (const FProperty* Prop = OutVal.GetProperty())
			{
				Prop->CopyCompleteValue(OutVal.GetRaw(), OutValue.GetData());
			}
			else
			{
				FMemory::Memcpy(OutVal.GetRaw(), OutValue.GetData(), ElementSize);
			}
			return;
		}
		ReadVoid(Index, OutVal);
	}

	PCGExValueHash FPropertySingleValueBuffer::ReadValueHash(const int32 Index)
	{
		if (InValue.Num() < ElementSize || ElementSize <= 0)
		{
			return 0;
		}
		return FCrc::MemCrc32(InValue.GetData(), ElementSize);
	}

	PCGExValueHash FPropertySingleValueBuffer::GetValueHash(const int32 Index)
	{
		if (OutValue.Num() >= ElementSize && ElementSize > 0)
		{
			return FCrc::MemCrc32(OutValue.GetData(), ElementSize);
		}
		return ReadValueHash(Index);
	}

	bool FPropertySingleValueBuffer::InitForRead(const EIOSide InSide)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (bReadInitialized)
		{
			return true;
		}

		if (ElementSize <= 0)
		{
			return false;
		}

		const FPCGMetadataAttributeBase* Attr = Source->FindConstAttribute(Identifier, InSide);
		if (!Attr)
		{
			return false;
		}

		GenericInAttribute = Attr;
		InAttribute = Attr;

		if (!CachedInnerProperty)
		{
			InitProperty(GenericInAttribute);
		}
		if (!CachedInnerProperty || ElementSize <= 0)
		{
			return false;
		}

		InValue.SetNumZeroed(ElementSize);
		// Property-aware deep copy: see FPropertyArrayBuffer::InitForRead for rationale.
		CachedInnerProperty->InitializeValue(InValue.GetData());
		if (const void* ReadAddr = GenericInAttribute->GetReadAddressFromEntryKey_Unsafe(PCGDefaultValueKey))
		{
			CachedInnerProperty->CopyCompleteValue(InValue.GetData(), ReadAddr);
		}

		bReadInitialized = true;
		bReadComplete = true;
		return true;
	}

	bool FPropertySingleValueBuffer::InitForWrite(const FPCGMetadataAttributeBase* SourceAttribute, EBufferInit Init)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (bWriteInitialized)
		{
			return true;
		}

		if (ElementSize <= 0)
		{
			return false;
		}

		if (SourceAttribute)
		{
			FPCGMetadataAttributeBase* MutableAttr = Source->FindMutableAttribute(Identifier, EIOSide::Out);

			// Create-on-the-fly when not already present (data forward path).
			if (!MutableAttr)
			{
				if (const UPCGBasePointData* OutData = Source->GetOut();
					OutData && OutData->Metadata)
				{
					MutableAttr = OutData->Metadata->CreateAttribute(
						Identifier,
						SourceAttribute->GetAttributeDesc(),
						SourceAttribute->AllowsInterpolation(),
						/*bOverrideParent=*/true);
				}
			}

			if (MutableAttr)
			{
				GenericOutAttribute = MutableAttr;
				OutAttribute = GenericOutAttribute;
			}

			if (!CachedInnerProperty)
			{
				InitProperty(SourceAttribute);
			}
		}

		if (!CachedInnerProperty || ElementSize <= 0)
		{
			return false;
		}

		OutValue.SetNumZeroed(ElementSize);
		CachedInnerProperty->InitializeValue(OutValue.GetData());

		if (Init == EBufferInit::Inherit && SourceAttribute)
		{
			if (const void* ReadAddr = SourceAttribute->GetReadAddressFromEntryKey_Unsafe(PCGDefaultValueKey))
			{
				CachedInnerProperty->CopyCompleteValue(OutValue.GetData(), ReadAddr);
			}
		}

		bWriteInitialized = true;
		bIsNewOutput = (Init == EBufferInit::New);
		return true;
	}

	void FPropertySingleValueBuffer::Write(const bool bEnsureValidKeys)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPropertySingleValueBuffer::Write);

		if (!IsWritable() || !IsEnabled())
		{
			return;
		}
		if (!GenericOutAttribute || !CachedInnerProperty)
		{
			return;
		}

		PCGEX_SHARED_CONTEXT_VOID(Source->GetContextHandle())
		SharedContext.Get()->AddProtectedAttributeName(GenericOutAttribute->Name);

		// @Data values live in the default-value slot -- engine-conformant, no entry materialization
		// (see Helpers::SetDataValue/ReadDataValue for the model).
		// NOTE on API asymmetry vs TSingleValueBuffer<T>::Write:
		//   - The typed path goes through SetDefaultValue<T> because SetValue<T>(PCGDefaultValueKey, ...)
		//     silently bails inside the engine (PCGDefaultValueKey == -1 == PCGInvalidEntryKey;
		//     SetValueFromValueKey_Unsafe early-returns for invalid entries).
		//   - The property-based path here is fine: SetValueFromProperty checks ItemKey ==
		//     PCGInvalidEntryKey and routes to ExistingIndexForDefaultValue internally, so passing
		//     PCGDefaultValueKey actually does what we want. Different code path, different behavior.
		// If you ever switch this away from SetValueFromProperty, audit the new API the same way.
		GenericOutAttribute->SetValueFromProperty(
			PCGDefaultValueKey,
			OutValue.GetData(),
			CachedInnerProperty);
	}

#pragma endregion
}
