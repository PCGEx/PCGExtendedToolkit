// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Properties/PCGExProperty_Range.h"

#include "Metadata/PCGMetadata.h"
#include "Types/PCGExTypeOps.h"

#pragma region FPCGExProperty_Range

bool FPCGExProperty_Range::InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, const FName OutputName)
{
	OutputBuffer = OutputFacade->GetWritable<FVector2D>(OutputName, GetRange(), true, PCGExData::EBufferInit::Inherit);
	return OutputBuffer.IsValid();
}

void FPCGExProperty_Range::WriteOutput(const int32 PointIndex) const
{
	check(OutputBuffer);
	OutputBuffer->SetValue(PointIndex, GetRange());
}

void FPCGExProperty_Range::WriteOutputFrom(const int32 PointIndex, const FPCGExProperty* Source) const
{
	check(OutputBuffer);
	OutputBuffer->SetValue(PointIndex, static_cast<const FPCGExProperty_Range*>(Source)->GetRange());
}

void FPCGExProperty_Range::CopyValueFrom(const FPCGExProperty* Source)
{
	// The bounds travel with the positions: a writer clone has to remap exactly as its source would, and a
	// source from another host can carry different bounds than the prototype the clone was built from.
	const FPCGExProperty_Range* Typed = static_cast<const FPCGExProperty_Range*>(Source);
	Value = Typed->Value;
	Min = Typed->Min;
	Max = Typed->Max;
}

bool FPCGExProperty_Range::SyncStructuralFromSchema(const FPCGExProperty& Schema)
{
	const FPCGExProperty_Range& Typed = static_cast<const FPCGExProperty_Range&>(Schema);
	if (Min == Typed.Min && Max == Typed.Max)
	{
		return false;
	}
	Min = Typed.Min;
	Max = Typed.Max;
	return true;
}

FPCGMetadataAttributeBase* FPCGExProperty_Range::CreateMetadataAttribute(UPCGMetadata* Metadata, const FName AttributeName) const
{
	return Metadata->CreateAttribute<FVector2D>(AttributeName, GetRange(), true, true);
}

void FPCGExProperty_Range::WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, const int64 EntryKey) const
{
	static_cast<FPCGMetadataAttribute<FVector2D>*>(Attribute)->SetValue(EntryKey, GetRange());
}

bool FPCGExProperty_Range::TryWriteValue(const EPCGMetadataTypes TargetType, void* OutBuffer) const
{
	const FVector2D Projected = GetRange();
	PCGExTypeOps::FConversionTable::Convert(EPCGMetadataTypes::Vector2, &Projected, TargetType, OutBuffer);
	return true;
}

bool FPCGExProperty_Range::TryReadValue(const EPCGMetadataTypes SourceType, const void* InBuffer)
{
	FVector2D Projected = FVector2D::ZeroVector;
	PCGExTypeOps::FConversionTable::Convert(SourceType, InBuffer, EPCGMetadataTypes::Vector2, &Projected);

	FVector2D Positions = FVector2D::ZeroVector;
	if (!ValueToPosition(Projected.X, Positions.X) || !ValueToPosition(Projected.Y, Positions.Y))
	{
		return false;
	}
	Value = Positions;
	return true;
}

#pragma endregion
