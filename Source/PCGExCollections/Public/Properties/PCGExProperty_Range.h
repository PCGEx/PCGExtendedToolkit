// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExProperty.h"

#include "PCGExProperty_Range.generated.h"

/**
 * Range property: a range picked inside schema-owned bounds, output as an FVector2D attribute
 * (X = start, Y = end).
 *
 * CONVERTING type: Value holds the two ends as positions between Min (0) and Max (1), and every output path
 * writes them remapped through GetRange(). Editing the bounds rescales an authored range, it never clips it.
 *
 * Min / Max are structural: the schema owns them and overrides mirror them (SyncStructuralFromSchema).
 * Positions are not clamped to 0..1 -- a programmatic write outside the bounds extrapolates.
 */
USTRUCT(BlueprintType, meta=(PCGExInlineValue), DisplayName="Range")
struct PCGEXCOLLECTIONS_API FPCGExProperty_Range : public FPCGExProperty
{
	GENERATED_BODY()

	/** Range start (X) and end (Y), as positions between Min (0) and Max (1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	FVector2D Value = FVector2D(0.0, 1.0);

	/** Output value at the slider's left end. */
	UPROPERTY(EditAnywhere, Category = "Property")
	double Min = 0.0;

	/** Output value at the slider's right end. */
	UPROPERTY(EditAnywhere, Category = "Property")
	double Max = 1.0;

	/** Output value at a position. Exact at both ends: 0 is Min, 1 is Max. */
	double PositionToValue(const double Position) const
	{
		return FMath::LerpStable(Min, Max, Position);
	}

	/** Position of an output value. False when Min == Max leaves no span to invert. */
	bool ValueToPosition(const double InValue, double& OutPosition) const
	{
		if (Min == Max)
		{
			return false;
		}
		const double Position = (InValue - Min) / (Max - Min);
		if (!FMath::IsFinite(Position))
		{
			return false;
		}
		OutPosition = Position;
		return true;
	}

	/** Value remapped to Min..Max: what every output path writes. */
	FVector2D GetRange() const
	{
		return FVector2D(PositionToValue(Value.X), PositionToValue(Value.Y));
	}

protected:
	TSharedPtr<PCGExData::TBuffer<FVector2D>> OutputBuffer;

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
		return EPCGMetadataTypes::Vector2;
	}

	virtual FName GetTypeName() const override
	{
		return FName("Range");
	}

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;

	/** Projects GetRange(), like every other output path. */
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;

	/** Takes an output-space range and stores its positions; fails and leaves Value untouched when Min == Max. */
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
};
