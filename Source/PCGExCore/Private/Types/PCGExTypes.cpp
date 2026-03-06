// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Types/PCGExTypes.h"

#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeGeneric.h"
#include "Metadata/PCGMetadataCommon.h"

namespace PCGExTypes
{
	// FScopedTypedValue: a type-erased value stored inline or on the heap.
	// Known types (up to 96 bytes) use the inline stack buffer.
	// Generic/unknown types with runtime-determined sizes heap-allocate when needed.

	FScopedTypedValue::FScopedTypedValue(EPCGMetadataTypes InType)
		: ActiveStorage(Storage), Type(InType), bConstructed(false)
	{
		ValueSize = GetTypeSize(Type);
		InitializeValue();
	}

	FScopedTypedValue::FScopedTypedValue(EPCGMetadataTypes InType, int32 InSize, int32 InAlignment)
		: ActiveStorage(Storage), Type(InType), bConstructed(false)
	{
		ValueSize = InSize;
		AllocateStorage(InSize, InAlignment);
		InitializeValue();
	}

	FScopedTypedValue::~FScopedTypedValue()
	{
		Destruct();
		FreeHeapStorage();
	}

	FScopedTypedValue::FScopedTypedValue(FScopedTypedValue&& Other) noexcept
		: Type(Other.Type)
		, ValueSize(Other.ValueSize)
		, bConstructed(Other.bConstructed)
		, bHeapAllocated(Other.bHeapAllocated)
	{
		if (bHeapAllocated)
		{
			// Take ownership of heap allocation
			ActiveStorage = Other.ActiveStorage;
			Other.ActiveStorage = Other.Storage;
			Other.bHeapAllocated = false;
		}
		else
		{
			ActiveStorage = Storage;

			if (bConstructed && NeedsLifecycleManagement(Type))
			{
				// Move construct complex types
				switch (Type)
				{
				case EPCGMetadataTypes::String: new(Storage) FString(MoveTemp(*reinterpret_cast<FString*>(Other.Storage)));
					break;
				case EPCGMetadataTypes::Name: new(Storage) FName(MoveTemp(*reinterpret_cast<FName*>(Other.Storage)));
					break;
				case EPCGMetadataTypes::SoftObjectPath: new(Storage) FSoftObjectPath(MoveTemp(*reinterpret_cast<FSoftObjectPath*>(Other.Storage)));
					break;
				case EPCGMetadataTypes::SoftClassPath: new(Storage) FSoftClassPath(MoveTemp(*reinterpret_cast<FSoftClassPath*>(Other.Storage)));
					break;
				case EPCGMetadataTypes::Text: new(Storage) FText(MoveTemp(*reinterpret_cast<FText*>(Other.Storage)));
					break;
				default: FMemory::Memcpy(Storage, Other.Storage, ValueSize > 0 ? ValueSize : BufferSize);
					break;
				}
			}
			else
			{
				const int32 CopySize = ValueSize > 0 ? ValueSize : BufferSize;
				FMemory::Memcpy(Storage, Other.Storage, FMath::Min(CopySize, BufferSize));
			}
		}

		// Mark other as not constructed to prevent double destruction
		Other.bConstructed = false;
		Other.Type = EPCGMetadataTypes::Unknown;
		Other.ValueSize = 0;
	}

	void FScopedTypedValue::Destruct()
	{
		if (bConstructed && NeedsLifecycleManagement(Type))
		{
			switch (Type)
			{
			case EPCGMetadataTypes::String: reinterpret_cast<FString*>(ActiveStorage)->~FString();
				break;
			case EPCGMetadataTypes::Name: reinterpret_cast<FName*>(ActiveStorage)->~FName();
				break;
			case EPCGMetadataTypes::SoftObjectPath: reinterpret_cast<FSoftObjectPath*>(ActiveStorage)->~FSoftObjectPath();
				break;
			case EPCGMetadataTypes::SoftClassPath: reinterpret_cast<FSoftClassPath*>(ActiveStorage)->~FSoftClassPath();
				break;
			case EPCGMetadataTypes::Text: reinterpret_cast<FText*>(ActiveStorage)->~FText();
				break;
			default: break;
			}
		}
		bConstructed = false;
	}

	void FScopedTypedValue::Initialize(EPCGMetadataTypes NewType)
	{
		Destruct();
		FreeHeapStorage();

		Type = NewType;
		ValueSize = GetTypeSize(Type);
		ActiveStorage = Storage;
		InitializeValue();
	}

	void FScopedTypedValue::Initialize(EPCGMetadataTypes NewType, int32 InSize, int32 InAlignment)
	{
		Destruct();
		FreeHeapStorage();

		Type = NewType;
		ValueSize = InSize;
		AllocateStorage(InSize, InAlignment);
		InitializeValue();
	}

	void FScopedTypedValue::AllocateStorage(int32 InSize, int32 InAlignment)
	{
		if (InSize > BufferSize)
		{
			ActiveStorage = FMemory::Malloc(InSize, FMath::Max(InAlignment, static_cast<int32>(BufferAlignment)));
			bHeapAllocated = true;
		}
		else
		{
			ActiveStorage = Storage;
			bHeapAllocated = false;
		}
	}

	void FScopedTypedValue::FreeHeapStorage()
	{
		if (bHeapAllocated)
		{
			FMemory::Free(ActiveStorage);
			ActiveStorage = Storage;
			bHeapAllocated = false;
		}
	}

	void FScopedTypedValue::InitializeValue()
	{
		if (NeedsLifecycleManagement(Type))
		{
			switch (Type)
			{
			case EPCGMetadataTypes::String: new(ActiveStorage) FString();
				break;
			case EPCGMetadataTypes::Name: new(ActiveStorage) FName();
				break;
			case EPCGMetadataTypes::SoftObjectPath: new(ActiveStorage) FSoftObjectPath();
				break;
			case EPCGMetadataTypes::SoftClassPath: new(ActiveStorage) FSoftClassPath();
				break;
			case EPCGMetadataTypes::Text: new(ActiveStorage) FText();
				break;
			default: break;
			}
			bConstructed = true;
		}
		else
		{
			// Zero-initialize POD types
			const int32 ZeroSize = ValueSize > 0 ? ValueSize : BufferSize;
			FMemory::Memzero(ActiveStorage, ZeroSize);
			bConstructed = true;
		}
	}

	bool FScopedTypedValue::NeedsLifecycleManagement(EPCGMetadataTypes InType)
	{
		switch (InType)
		{
		case EPCGMetadataTypes::String:
		case EPCGMetadataTypes::Name:
		case EPCGMetadataTypes::SoftObjectPath:
		case EPCGMetadataTypes::SoftClassPath:
		case EPCGMetadataTypes::Text: return true;
		default: return false;
		}
	}

	int32 FScopedTypedValue::GetTypeSize(EPCGMetadataTypes InType)
	{
#define PCGEX_TPL(_TYPE, _NAME, ...) case EPCGMetadataTypes::_NAME: return sizeof(_TYPE);
		switch (InType)
		{
		PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
		default: return 0;
		}
#undef PCGEX_TPL
	}

	int32 GetElementSizeFromType(EPCGMetadataTypes InType, const UObject* ValueTypeObject)
	{
		// First try known types (covers legacy 0-14)
		const int32 KnownSize = FScopedTypedValue::GetTypeSize(InType);
		if (KnownSize > 0) { return KnownSize; }

		// Handle new 5.8 hidden types
		switch (InType)
		{
		case EPCGMetadataTypes::Byte: return sizeof(uint8);
		case EPCGMetadataTypes::Text: return sizeof(FText);
		case EPCGMetadataTypes::Enum: return sizeof(uint8); // UE enums backed by uint8
		case EPCGMetadataTypes::Struct:
			if (const UScriptStruct* Struct = Cast<UScriptStruct>(ValueTypeObject))
			{
				return Struct->GetStructureSize();
			}
			return 0;
		case EPCGMetadataTypes::Object:
		case EPCGMetadataTypes::Class:
			return sizeof(TObjectPtr<UObject>);
		case EPCGMetadataTypes::SoftObject: return sizeof(FSoftObjectPath);
		case EPCGMetadataTypes::SoftClass: return sizeof(FSoftClassPath);
		default: return 0;
		}
	}

	int32 GetElementAlignmentFromType(EPCGMetadataTypes InType, const UObject* ValueTypeObject)
	{
		switch (InType)
		{
		case EPCGMetadataTypes::Byte:
		case EPCGMetadataTypes::Enum: return alignof(uint8);
		case EPCGMetadataTypes::Text: return alignof(FText);
		case EPCGMetadataTypes::Struct:
			if (const UScriptStruct* Struct = Cast<UScriptStruct>(ValueTypeObject))
			{
				return Struct->GetMinAlignment();
			}
			return 1;
		case EPCGMetadataTypes::Object:
		case EPCGMetadataTypes::Class: return alignof(TObjectPtr<UObject>);
		case EPCGMetadataTypes::SoftObject: return alignof(FSoftObjectPath);
		case EPCGMetadataTypes::SoftClass: return alignof(FSoftClassPath);
		default:
			{
				// For known types, use their natural alignment
				const int32 Size = FScopedTypedValue::GetTypeSize(InType);
				return Size > 0 ? FMath::Min(Size, 16) : 1;
			}
		}
	}

	int32 GetElementSizeFromAttribute(const FPCGMetadataAttributeBase* InAttribute)
	{
		if (!InAttribute) { return 0; }

		const EPCGMetadataTypes Type = static_cast<EPCGMetadataTypes>(InAttribute->GetTypeId());

		// Known types: use compile-time size
		const int32 KnownSize = FScopedTypedValue::GetTypeSize(Type);
		if (KnownSize > 0) { return KnownSize; }

		// Generic attributes: extract size from descriptor
		if (InAttribute->IsGeneric())
		{
			const FPCGMetadataAttributeGeneric* GenericAttr = static_cast<const FPCGMetadataAttributeGeneric*>(InAttribute);
			const FPCGMetadataAttributeDesc& Desc = GenericAttr->GetAttributeDesc();
			return GetElementSizeFromType(Desc.ValueType, Desc.ValueTypeObject);
		}

		return 0;
	}
}
