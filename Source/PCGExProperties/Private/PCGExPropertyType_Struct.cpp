// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExPropertyType_Struct.h"

#include "PCGExLog.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Buffers/PCGExBufferProperty.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"

bool FPCGExProperty_Struct::InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName)
{
	if (!Value.IsValid())
	{
		return false;
	}

	UPCGBasePointData* OutData = OutputFacade->GetOut();
	if (!OutData || !OutData->Metadata)
	{
		return false;
	}

	const FPCGAttributeIdentifier Identifier = PCGExMetaHelpers::MakeElementIdentifier(OutputName);

	FPCGMetadataAttributeBase* Attr = OutData->Metadata->GetMutableAttribute(Identifier);
	if (!Attr)
	{
		FPCGMetadataAttributeDesc Desc;
		Desc.ValueType = EPCGMetadataTypes::Struct;
		Desc.ValueTypeObject = const_cast<UScriptStruct*>(Value.GetScriptStruct());
		Desc.Name = OutputName;
		Attr = OutData->Metadata->CreateAttribute(Identifier, Desc, /*bAllowsInterp=*/true, /*bOverrideParent=*/true);
	}

	if (!Attr)
	{
		return false;
	}

	const TSharedPtr<PCGExData::IBuffer> Buf = OutputFacade->GetWritableFromAttribute(Attr, PCGExData::EBufferInit::Inherit);
	if (!Buf.IsValid() || !Buf->IsPropertyBacked())
	{
		return false;
	}

	OutputBuffer = StaticCastSharedPtr<PCGExData::FPropertyArrayBuffer>(Buf);
	return OutputBuffer.IsValid();
}

void FPCGExProperty_Struct::WriteOutput(int32 PointIndex) const
{
	check(OutputBuffer);
	OutputBuffer->SetFromVoidProperty(PointIndex, Value.GetMemory());
}

void FPCGExProperty_Struct::WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const
{
	check(OutputBuffer);
	const FPCGExProperty_Struct* Typed = static_cast<const FPCGExProperty_Struct*>(Source);
	// Layout-compatible by SyncStructuralFromSchema invariant.
	OutputBuffer->SetFromVoidProperty(PointIndex, Typed->Value.GetMemory());
}

void FPCGExProperty_Struct::CopyValueFrom(const FPCGExProperty* Source)
{
	const FPCGExProperty_Struct* Typed = static_cast<const FPCGExProperty_Struct*>(Source);
	Value = Typed->Value;
}

bool FPCGExProperty_Struct::SyncStructuralFromSchema(const FPCGExProperty& Schema)
{
	// Drops user-authored payload on type swap (matches Enum's documented policy).
	const FPCGExProperty_Struct& Typed = static_cast<const FPCGExProperty_Struct&>(Schema);
	const UScriptStruct* SchemaStruct = Typed.Value.GetScriptStruct();
	const UScriptStruct* CurrentStruct = Value.GetScriptStruct();

	if (CurrentStruct == SchemaStruct)
	{
		return false;
	}

	if (!SchemaStruct)
	{
		Value.Reset();
	}
	else
	{
		Value.InitializeAs(SchemaStruct);
	}
	return true;
}

FName FPCGExProperty_Struct::GetDisplayTypeName() const
{
	if (const UScriptStruct* ScriptStruct = Value.GetScriptStruct())
	{
		return ScriptStruct->GetFName();
	}
	return GetTypeName();
}

FPCGMetadataAttributeBase* FPCGExProperty_Struct::CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const
{
	if (!Metadata || !Value.IsValid())
	{
		return nullptr;
	}

	FPCGMetadataAttributeDesc Desc;
	Desc.ValueType = EPCGMetadataTypes::Struct;
	Desc.ValueTypeObject = const_cast<UScriptStruct*>(Value.GetScriptStruct());
	Desc.Name = AttributeName;

	const FPCGAttributeIdentifier Identifier = PCGExMetaHelpers::MakeElementIdentifier(AttributeName);
	return Metadata->CreateAttribute(Identifier, Desc, /*bAllowsInterp=*/true, /*bOverrideParent=*/true);
}

void FPCGExProperty_Struct::WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const
{
	if (!Value.IsValid())
	{
		return;
	}

	// PERF: per-call FProperty alloc -- OK at metadata-entry granularity, not per-point.
	const FPCGMetadataAttributeDesc& Desc = Attribute->GetAttributeDesc();
	FProperty* TransientProp = PCGExData::FPropertyBuffer::CreateInnerPropertyFromDesc(Desc);
	if (!TransientProp)
	{
		return;
	}

	Attribute->SetValueFromProperty(EntryKey, Value.GetMemory(), TransientProp);
	delete TransientProp;
}

bool FPCGExProperty_Struct::TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const
{
	// Same-struct only: caller asserts OutBuffer matches our UScriptStruct layout.
	if (TargetType != EPCGMetadataTypes::Struct || !OutBuffer || !Value.IsValid())
	{
		return false;
	}
	Value.GetScriptStruct()->CopyScriptStruct(OutBuffer, Value.GetMemory());
	return true;
}

bool FPCGExProperty_Struct::TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer)
{
	if (SourceType != EPCGMetadataTypes::Struct || !InBuffer || !Value.IsValid())
	{
		return false;
	}
	Value.GetScriptStruct()->CopyScriptStruct(Value.GetMutableMemory(), InBuffer);
	return true;
}
