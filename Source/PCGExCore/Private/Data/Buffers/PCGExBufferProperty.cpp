// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExData.h"

#include "PCGExLog.h"
#include "PCGExSettingsCacheBody.h"
#include "Data/PCGExPointIO.h"
#include "Types/PCGExTypes.h"
#include "Metadata/PCGMetadataAttributeGeneric.h"
#include "Metadata/PCGMetadataDomain.h"
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

	FProperty* FPropertyBuffer::CreateInnerPropertyFromDesc(const FPCGMetadataAttributeDesc& Desc)
	{
		// Construct an FProperty matching the inner element type of a generic attribute.
		// This mirrors PCG::Private::CreatePropertyFromDesc for non-container leaf types.
		// The constructed property passes SameType() against the attribute's internal property.

		const UScriptStruct* ScriptStruct = nullptr;

		switch (Desc.ValueType)
		{
		case EPCGMetadataTypes::Boolean:
			{
				FBoolProperty* Prop = new FBoolProperty(FFieldVariant{nullptr}, Desc.Name);
				Prop->SetBoolSize(sizeof(bool), true);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
		case EPCGMetadataTypes::Byte:
			{
				FByteProperty* Prop = new FByteProperty(FFieldVariant{nullptr}, Desc.Name);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
		case EPCGMetadataTypes::Integer32:
			{
				FIntProperty* Prop = new FIntProperty(FFieldVariant{nullptr}, Desc.Name);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
		case EPCGMetadataTypes::Integer64:
			{
				FInt64Property* Prop = new FInt64Property(FFieldVariant{nullptr}, Desc.Name);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
		case EPCGMetadataTypes::Float:
			{
				FFloatProperty* Prop = new FFloatProperty(FFieldVariant{nullptr}, Desc.Name);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
		case EPCGMetadataTypes::Double:
			{
				FDoubleProperty* Prop = new FDoubleProperty(FFieldVariant{nullptr}, Desc.Name);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
		case EPCGMetadataTypes::Name:
			{
				FNameProperty* Prop = new FNameProperty(FFieldVariant{nullptr}, Desc.Name);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
		case EPCGMetadataTypes::String:
			{
				FStrProperty* Prop = new FStrProperty(FFieldVariant{nullptr}, Desc.Name);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				return Prop;
			}
		case EPCGMetadataTypes::Text:
			{
				FTextProperty* Prop = new FTextProperty(FFieldVariant{nullptr}, Desc.Name);
				return Prop;
			}
		case EPCGMetadataTypes::Enum:
			if (const UEnum* Enum = Cast<UEnum>(Desc.ValueTypeObject))
			{
				FEnumProperty* Prop = new FEnumProperty(FFieldVariant{nullptr}, Desc.Name);
				Prop->SetPropertyFlags(CPF_HasGetValueTypeHash);
				FNumericProperty* UnderlyingProp = new FByteProperty(Prop, TEXT("UnderlyingType"));
				UnderlyingProp->SetPropertyFlags(CPF_HasGetValueTypeHash | CPF_IsPlainOldData);
				Prop->SetEnum(const_cast<UEnum*>(Enum));
				Prop->AddCppProperty(UnderlyingProp);
				return Prop;
			}
			break;
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
			FStructProperty* Prop = new FStructProperty(FFieldVariant{nullptr}, Desc.Name);
			Prop->Struct = const_cast<UScriptStruct*>(ScriptStruct);

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

	bool FPropertyBuffer::InitProperty(const FPCGMetadataAttributeGeneric* InGenericAttribute)
	{
		if (!InGenericAttribute) { return false; }

		const FPCGMetadataAttributeDesc& Desc = InGenericAttribute->GetAttributeDesc();

		// For container types, we'd need to handle the inner element type.
		// For now, construct the inner property from the desc (ignoring container wrappers).
		FPCGMetadataAttributeDesc InnerDesc = Desc;
		InnerDesc.ContainerTypes.Reset();

		CachedInnerProperty = CreateInnerPropertyFromDesc(InnerDesc);
		if (!CachedInnerProperty) { return false; }

		ElementSize = PCGExTypes::GetElementSizeFromAttribute(InGenericAttribute);
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

	int32 FPropertyArrayBuffer::GetNumValues(const EIOSide InSide)
	{
		if (InSide == EIOSide::In) { return InBytes ? InBytes->Num() / FMath::Max(1, ElementSize) : 0; }
		return OutBytes ? OutBytes->Num() / FMath::Max(1, ElementSize) : 0;
	}

	bool FPropertyArrayBuffer::IsWritable() { return OutBytes != nullptr; }
	bool FPropertyArrayBuffer::IsReadable() { return InBytes != nullptr; }
	bool FPropertyArrayBuffer::ReadsFromOutput() { return InBytes == OutBytes; }

	bool FPropertyArrayBuffer::EnsureReadable()
	{
		if (InBytes) { return true; }

		if (OutBytes)
		{
			InBytes = OutBytes;
			return true;
		}

		return InitForRead(EIOSide::In);
	}

	void FPropertyArrayBuffer::ReadVoid(const int32 Index, void* OutValue) const
	{
		check(InBytes && ElementSize > 0);
		const int32 Offset = Index * ElementSize;
		check(Offset + ElementSize <= InBytes->Num());
		FMemory::Memcpy(OutValue, InBytes->GetData() + Offset, ElementSize);
	}

	void FPropertyArrayBuffer::SetVoid(const int32 Index, const void* Value)
	{
		check(OutBytes && ElementSize > 0);
		const int32 Offset = Index * ElementSize;
		check(Offset + ElementSize <= OutBytes->Num());
		FMemory::Memcpy(OutBytes->GetData() + Offset, Value, ElementSize);
	}

	void FPropertyArrayBuffer::GetVoid(const int32 Index, void* OutValue)
	{
		if (OutBytes)
		{
			const int32 Offset = Index * ElementSize;
			check(Offset + ElementSize <= OutBytes->Num());
			FMemory::Memcpy(OutValue, OutBytes->GetData() + Offset, ElementSize);
			return;
		}
		ReadVoid(Index, OutValue);
	}

	PCGExValueHash FPropertyArrayBuffer::ReadValueHash(const int32 Index)
	{
		if (!InBytes || ElementSize <= 0) { return 0; }
		return FCrc::MemCrc32(InBytes->GetData() + Index * ElementSize, ElementSize);
	}

	PCGExValueHash FPropertyArrayBuffer::GetValueHash(const int32 Index)
	{
		if (OutBytes && ElementSize > 0) { return FCrc::MemCrc32(OutBytes->GetData() + Index * ElementSize, ElementSize); }
		return ReadValueHash(Index);
	}

	bool FPropertyArrayBuffer::InitForRead(const EIOSide InSide)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (InBytes) { return true; }

		if (ElementSize <= 0) { return false; }

		const UPCGBasePointData* PointData = (InSide == EIOSide::In) ? Source->GetIn() : Source->GetOut();
		if (!PointData) { return false; }

		const FPCGMetadataAttributeBase* Attr = Source->FindConstAttribute(Identifier, InSide);
		if (!Attr || !Attr->IsGeneric()) { return false; }

		GenericInAttribute = static_cast<const FPCGMetadataAttributeGeneric*>(Attr);
		InAttribute = Attr;

		if (!CachedInnerProperty) { InitProperty(GenericInAttribute); }
		if (!CachedInnerProperty || ElementSize <= 0) { return false; }

		const int32 NumPoints = PointData->GetNumPoints();

		InBytes = MakeShared<TArray<uint8>>();
		InBytes->SetNumZeroed(NumPoints * ElementSize);

		auto EntryKeys = PointData->GetConstMetadataEntryValueRange();

		for (int32 i = 0; i < NumPoints; i++)
		{
			const void* ReadAddr = GenericInAttribute->GetReadAddressFromEntryKey_Unsafe(EntryKeys[i]);
			if (ReadAddr)
			{
				FMemory::Memcpy(InBytes->GetData() + i * ElementSize, ReadAddr, ElementSize);
			}
		}

		bReadComplete = true;
		return true;
	}

	bool FPropertyArrayBuffer::InitForWrite(const FPCGMetadataAttributeBase* SourceAttribute, EBufferInit Init)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (OutBytes) { return true; }

		if (ElementSize <= 0) { return false; }

		const UPCGBasePointData* OutData = Source->GetOut();
		if (!OutData) { return false; }

		// Resolve the output generic attribute
		if (SourceAttribute && SourceAttribute->IsGeneric())
		{
			// Look for existing attribute on output, or it should already exist from duplication
			FPCGMetadataAttributeBase* MutableAttr = Source->FindMutableAttribute(Identifier, EIOSide::Out);
			if (MutableAttr && MutableAttr->IsGeneric())
			{
				GenericOutAttribute = static_cast<FPCGMetadataAttributeGeneric*>(MutableAttr);
				OutAttribute = GenericOutAttribute;
			}

			if (!CachedInnerProperty)
			{
				const FPCGMetadataAttributeGeneric* GenericSrc = static_cast<const FPCGMetadataAttributeGeneric*>(SourceAttribute);
				InitProperty(GenericSrc);
			}
		}

		if (!CachedInnerProperty || ElementSize <= 0) { return false; }

		const int32 NumPoints = OutData->GetNumPoints();

		OutBytes = MakeShared<TArray<uint8>>();
		OutBytes->SetNumZeroed(NumPoints * ElementSize);

		// If inheriting, copy input values to output
		if (Init == EBufferInit::Inherit && SourceAttribute && SourceAttribute->IsGeneric())
		{
			const FPCGMetadataAttributeGeneric* GenericSrc = static_cast<const FPCGMetadataAttributeGeneric*>(SourceAttribute);
			auto EntryKeys = OutData->GetConstMetadataEntryValueRange();

			for (int32 i = 0; i < NumPoints; i++)
			{
				const void* ReadAddr = GenericSrc->GetReadAddressFromEntryKey_Unsafe(EntryKeys[i]);
				if (ReadAddr)
				{
					FMemory::Memcpy(OutBytes->GetData() + i * ElementSize, ReadAddr, ElementSize);
				}
			}
		}

		bIsNewOutput = (Init == EBufferInit::New);
		return true;
	}

	void FPropertyArrayBuffer::Write(const bool bEnsureValidKeys)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPropertyArrayBuffer::Write);

		if (!IsWritable() || !IsEnabled() || !OutBytes) { return; }
		if (!GenericOutAttribute || !CachedInnerProperty) { return; }

		const UPCGBasePointData* OutData = Source->GetOut();
		if (!OutData) { return; }

		PCGEX_SHARED_CONTEXT_VOID(Source->GetContextHandle())
		SharedContext.Get()->AddProtectedAttributeName(GenericOutAttribute->Name);

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
		InBytes.Reset();
		OutBytes.Reset();
	}

#pragma endregion

#pragma region FPropertySingleValueBuffer

	FPropertySingleValueBuffer::FPropertySingleValueBuffer(const TSharedRef<FPointIO>& InSource, const FPCGAttributeIdentifier& InIdentifier)
		: FPropertyBuffer(InSource, InIdentifier)
	{
		check(InIdentifier.MetadataDomain.Flag == EPCGMetadataDomainFlag::Data)
		UnderlyingDomain = EDomainType::Data;
	}

	int32 FPropertySingleValueBuffer::GetNumValues(const EIOSide InSide) { return 1; }

	bool FPropertySingleValueBuffer::IsWritable() { return bWriteInitialized; }
	bool FPropertySingleValueBuffer::IsReadable() { return bReadInitialized; }
	bool FPropertySingleValueBuffer::ReadsFromOutput() { return bReadFromOutput; }

	bool FPropertySingleValueBuffer::EnsureReadable()
	{
		if (bReadInitialized) { return true; }

		if (bWriteInitialized && OutValue.Num() > 0)
		{
			InValue = OutValue;
			bReadFromOutput = true;
			bReadInitialized = true;
			return true;
		}

		return InitForRead(EIOSide::In);
	}

	void FPropertySingleValueBuffer::ReadVoid(const int32 Index, void* OutVal) const
	{
		check(InValue.Num() >= ElementSize && ElementSize > 0);
		FMemory::Memcpy(OutVal, InValue.GetData(), ElementSize);
	}

	void FPropertySingleValueBuffer::SetVoid(const int32 Index, const void* Value)
	{
		check(OutValue.Num() >= ElementSize && ElementSize > 0);
		FMemory::Memcpy(OutValue.GetData(), Value, ElementSize);
	}

	void FPropertySingleValueBuffer::GetVoid(const int32 Index, void* OutVal)
	{
		if (OutValue.Num() >= ElementSize)
		{
			FMemory::Memcpy(OutVal, OutValue.GetData(), ElementSize);
			return;
		}
		ReadVoid(Index, OutVal);
	}

	PCGExValueHash FPropertySingleValueBuffer::ReadValueHash(const int32 Index)
	{
		if (InValue.Num() < ElementSize || ElementSize <= 0) { return 0; }
		return FCrc::MemCrc32(InValue.GetData(), ElementSize);
	}

	PCGExValueHash FPropertySingleValueBuffer::GetValueHash(const int32 Index)
	{
		if (OutValue.Num() >= ElementSize && ElementSize > 0) { return FCrc::MemCrc32(OutValue.GetData(), ElementSize); }
		return ReadValueHash(Index);
	}

	bool FPropertySingleValueBuffer::InitForRead(const EIOSide InSide)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (bReadInitialized) { return true; }

		if (ElementSize <= 0) { return false; }

		const FPCGMetadataAttributeBase* Attr = Source->FindConstAttribute(Identifier, InSide);
		if (!Attr || !Attr->IsGeneric()) { return false; }

		GenericInAttribute = static_cast<const FPCGMetadataAttributeGeneric*>(Attr);
		InAttribute = Attr;

		if (!CachedInnerProperty) { InitProperty(GenericInAttribute); }
		if (!CachedInnerProperty || ElementSize <= 0) { return false; }

		const void* ReadAddr = GenericInAttribute->GetReadAddressFromEntryKey_Unsafe(PCGDefaultValueKey);
		InValue.SetNumZeroed(ElementSize);
		if (ReadAddr) { FMemory::Memcpy(InValue.GetData(), ReadAddr, ElementSize); }

		bReadInitialized = true;
		bReadComplete = true;
		return true;
	}

	bool FPropertySingleValueBuffer::InitForWrite(const FPCGMetadataAttributeBase* SourceAttribute, EBufferInit Init)
	{
		FWriteScopeLock WriteScopeLock(BufferLock);

		if (bWriteInitialized) { return true; }

		if (ElementSize <= 0) { return false; }

		if (SourceAttribute && SourceAttribute->IsGeneric())
		{
			FPCGMetadataAttributeBase* MutableAttr = Source->FindMutableAttribute(Identifier, EIOSide::Out);
			if (MutableAttr && MutableAttr->IsGeneric())
			{
				GenericOutAttribute = static_cast<FPCGMetadataAttributeGeneric*>(MutableAttr);
				OutAttribute = GenericOutAttribute;
			}

			if (!CachedInnerProperty)
			{
				const FPCGMetadataAttributeGeneric* GenericSrc = static_cast<const FPCGMetadataAttributeGeneric*>(SourceAttribute);
				InitProperty(GenericSrc);
			}
		}

		if (!CachedInnerProperty || ElementSize <= 0) { return false; }

		OutValue.SetNumZeroed(ElementSize);

		if (Init == EBufferInit::Inherit && SourceAttribute && SourceAttribute->IsGeneric())
		{
			const FPCGMetadataAttributeGeneric* GenericSrc = static_cast<const FPCGMetadataAttributeGeneric*>(SourceAttribute);
			const void* ReadAddr = GenericSrc->GetReadAddressFromEntryKey_Unsafe(PCGDefaultValueKey);
			if (ReadAddr) { FMemory::Memcpy(OutValue.GetData(), ReadAddr, ElementSize); }
		}

		bWriteInitialized = true;
		bIsNewOutput = (Init == EBufferInit::New);
		return true;
	}

	void FPropertySingleValueBuffer::Write(const bool bEnsureValidKeys)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPropertySingleValueBuffer::Write);

		if (!IsWritable() || !IsEnabled()) { return; }
		if (!GenericOutAttribute || !CachedInnerProperty) { return; }

		PCGEX_SHARED_CONTEXT_VOID(Source->GetContextHandle())
		SharedContext.Get()->AddProtectedAttributeName(GenericOutAttribute->Name);

		GenericOutAttribute->SetValueFromProperty(
			PCGDefaultValueKey,
			OutValue.GetData(),
			CachedInnerProperty);
	}

#pragma endregion
}
