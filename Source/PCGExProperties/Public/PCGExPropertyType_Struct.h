// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExProperty.h"
#include "StructUtils/InstancedStruct.h"

#include "PCGExPropertyType_Struct.generated.h"

namespace PCGExData
{
	class FPropertyArrayBuffer;
}

// Intentionally not PCGExInlineValue: requires the AddComplexValueRows fallback path so the
// editor customization can bypass the redundant FInstancedStruct wrapper row.
USTRUCT(BlueprintType, DisplayName="Struct")
struct PCGEXPROPERTIES_API FPCGExProperty_Struct : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property")
	FInstancedStruct Value;

protected:
	TSharedPtr<PCGExData::FPropertyArrayBuffer> OutputBuffer;

public:
	virtual bool InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName) override;
	virtual void WriteOutput(int32 PointIndex) const override;
	virtual void WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const override;
	virtual void CopyValueFrom(const FPCGExProperty* Source) override;
	virtual bool SyncStructuralFromSchema(const FPCGExProperty& Schema) override;

	virtual bool SupportsOutput() const override
	{
		return true;
	}

	virtual EPCGMetadataTypes GetOutputType() const override
	{
		return EPCGMetadataTypes::Struct;
	}

	virtual FName GetTypeName() const override
	{
		return FName("Struct");
	}

	virtual FName GetDisplayTypeName() const override;

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;

	/** Writes Value as a struct-typed @Data attribute; the default only dispatches the 15 legacy types. */
	virtual bool WriteDataDomainValue(UPCGData* OutData, FName OutName) const override;

	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
};
