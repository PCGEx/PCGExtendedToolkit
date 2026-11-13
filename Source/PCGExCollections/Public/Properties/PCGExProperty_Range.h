// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExProperty.h"

#include "PCGExProperty_Range.generated.h"

class UTexture;

/** How FPCGExProperty_Range::BackgroundTexture is laid along the slider. */
UENUM()
enum class EPCGExRangeBackgroundOrientation : uint8
{
	U         = 0 UMETA(DisplayName = "U", ToolTip="The texture's left edge sits at the range start."),
	UReversed = 1 UMETA(DisplayName = "U (Reversed)", ToolTip="The texture's right edge sits at the range start (rotated 180)."),
	V         = 2 UMETA(DisplayName = "V", ToolTip="The texture's top edge sits at the range start (rotated -90)."),
	VReversed = 3 UMETA(DisplayName = "V (Reversed)", ToolTip="The texture's bottom edge sits at the range start (rotated 90)."),
};

/**
 * Range property: a range picked inside schema-owned bounds, output as an FVector2D attribute
 * (X = start, Y = end).
 *
 * CONVERTING type: Value holds the two ends as positions between Min (0) and Max (1), and every output path
 * writes them remapped through GetRange(). Editing the bounds rescales an authored range, it never clips it.
 *
 * Min / Max are structural: the schema owns them and overrides mirror them (SyncStructuralFromSchema).
 * Positions are not clamped to 0..1 -- a programmatic write outside the bounds extrapolates.
 *
 * The background texture is editor-only decoration for the slider, mirrored the same way; no output reads it.
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

#if WITH_EDITORONLY_DATA
	/** Texture stretched behind the slider, e.g. the heatmap a debug material reads this range against. */
	UPROPERTY(EditAnywhere, Category = "Property")
	TSoftObjectPtr<UTexture> BackgroundTexture;

	/** Which way Background Texture runs along the slider. */
	UPROPERTY(EditAnywhere, Category = "Property")
	EPCGExRangeBackgroundOrientation BackgroundOrientation = EPCGExRangeBackgroundOrientation::U;
#endif

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
