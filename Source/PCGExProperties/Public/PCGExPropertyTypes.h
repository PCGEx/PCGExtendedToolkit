// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExEnumSelector.h"
#include "PCGExProperty.h"
#include "PCGSettings.h"

#include "PCGExPropertyTypes.generated.h"

// Note: AssetCollection property type is defined in modules that depend on both
// PCGExProperties and PCGExCollections (e.g., PCGExElementsValency).
// This demonstrates that custom property types CAN live in other modules -
// they just need to derive from FPCGExProperty and will be discovered by UHT.

// ============================================================================
// BUILT-IN PROPERTY TYPES
// ============================================================================
//
// All built-in types follow the same pattern:
//   - Value field:    the user-editable value (UPROPERTY)
//   - OutputBuffer:   TSharedPtr<TBuffer<OutputType>> for point attribute output
//   - Virtual overrides: InitializeOutput, WriteOutput, WriteOutputFrom,
//                        CopyValueFrom, SupportsOutput, GetOutputType, GetTypeName,
//                        CreateMetadataAttribute, WriteMetadataValue
//
// ADDING A NEW SIMPLE PROPERTY TYPE:
//
// For types where Value type == Output type (most cases):
//   1. Add the USTRUCT here following the pattern below
//   2. Add PCGEX_PROPERTY_IMPL(ValueType, StructSuffix) in PCGExPropertyTypes.cpp
//   That's it - the macro generates all method implementations.
//
// For types where Value type != Output type (like Color: FLinearColor -> FVector4):
//   1. Add the USTRUCT here
//   2. Implement all methods manually in PCGExPropertyTypes.cpp
//   See FPCGExProperty_Color and FPCGExProperty_Enum for examples.
//
// USTRUCT META TAGS:
//   - meta=(PCGExInlineValue): Optional. Controls FInstancedStruct picker display.
//   - BlueprintType: Required for UPROPERTY visibility.
//
// ============================================================================

/**
 * Editor-only numeric range hint used by Float/Double/Int32/Int64 property types.
 *
 * Pure UX sugar: when bClampMin/bClampMax are set, the override-row numeric picker is
 * built with the corresponding ClampMin/ClampMax + UIMin/UIMax slider attributes so
 * the user can't drag out of range. Schema-edit view exposes the toggles + values as
 * a compact strip via FPCGExNumericRangeCustomization.
 *
 * Zero runtime cost: all members live inside WITH_EDITORONLY_DATA, so the struct
 * has no memory footprint in cooked builds. The struct itself remains defined so
 * Range field declarations don't need to be conditionally compiled away -- they're
 * already wrapped in WITH_EDITORONLY_DATA on the owning property type.
 *
 * UX-only: TryReadValue does NOT clamp programmatic writes against Min/Max -- those
 * are picker hints, not invariants.
 */
USTRUCT()
struct PCGEXPROPERTIES_API FPCGExNumericRange
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
	// EditCondition intentionally omitted on Min/Max: the compact strip lives in a custom
	// widget row (FPCGExNumericRangeCustomization::CustomizeHeader) where the engine's
	// EditCondition refresh hook isn't wired up, so toggling bClampMin wouldn't re-enable
	// the Min widget. Keeping values always-editable is the clean fix -- the toggles
	// only gate whether the bounds are PROPAGATED to the picker as clamp attributes.
	UPROPERTY(EditAnywhere, Category = "Range")
	bool bClampMin = false;

	UPROPERTY(EditAnywhere, Category = "Range")
	double Min = 0.0;

	UPROPERTY(EditAnywhere, Category = "Range")
	bool bClampMax = false;

	UPROPERTY(EditAnywhere, Category = "Range")
	double Max = 1.0;

	bool operator==(const FPCGExNumericRange& Other) const
	{
		return bClampMin == Other.bClampMin && Min == Other.Min
			&& bClampMax == Other.bClampMax && Max == Other.Max;
	}
	bool operator!=(const FPCGExNumericRange& Other) const { return !(*this == Other); }
#endif
};

#pragma region Atomic Typed Properties

/**
 * String property - outputs as FString attribute.
 */
USTRUCT(BlueprintType, meta=(PCGExInlineValue), DisplayName="String")
struct PCGEXPROPERTIES_API FPCGExProperty_String : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	FString Value;

protected:
	TSharedPtr<PCGExData::TBuffer<FString>> OutputBuffer;

public:
	virtual bool InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName) override;
	virtual void WriteOutput(int32 PointIndex) const override;
	virtual void WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const override;
	virtual void CopyValueFrom(const FPCGExProperty* Source) override;

	virtual bool SupportsOutput() const override
	{
		return true;
	}

	virtual EPCGMetadataTypes GetOutputType() const override
	{
		return EPCGMetadataTypes::String;
	}

	virtual FName GetTypeName() const override
	{
		return FName("String");
	}

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
};

/**
 * Name property - outputs as FName attribute.
 */
USTRUCT(BlueprintType, meta=(PCGExInlineValue), DisplayName="Name")
struct PCGEXPROPERTIES_API FPCGExProperty_Name : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	FName Value;

protected:
	TSharedPtr<PCGExData::TBuffer<FName>> OutputBuffer;

public:
	virtual bool InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName) override;
	virtual void WriteOutput(int32 PointIndex) const override;
	virtual void WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const override;
	virtual void CopyValueFrom(const FPCGExProperty* Source) override;

	virtual bool SupportsOutput() const override
	{
		return true;
	}

	virtual EPCGMetadataTypes GetOutputType() const override
	{
		return EPCGMetadataTypes::Name;
	}

	virtual FName GetTypeName() const override
	{
		return FName("Name");
	}

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
};

/**
 * Int32 property - outputs as int32 attribute.
 */
USTRUCT(BlueprintType, meta=(PCGExInlineValue), DisplayName="Integer 32")
struct PCGEXPROPERTIES_API FPCGExProperty_Int32 : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	int32 Value = 0;

#if WITH_EDITORONLY_DATA
	/** Editor-only picker constraint, schema-owned (synced to overrides as read-only). */
	UPROPERTY(EditAnywhere, Category = "Property")
	FPCGExNumericRange Range;
#endif

protected:
	TSharedPtr<PCGExData::TBuffer<int32>> OutputBuffer;

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
		return EPCGMetadataTypes::Integer32;
	}

	virtual FName GetTypeName() const override
	{
		return FName("Int32");
	}

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
};

/**
 * Int64 property - outputs as int64 attribute.
 */
USTRUCT(BlueprintType, meta=(PCGExInlineValue), DisplayName="Integer 64")
struct PCGEXPROPERTIES_API FPCGExProperty_Int64 : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	int64 Value = 0;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Property")
	FPCGExNumericRange Range;
#endif

protected:
	TSharedPtr<PCGExData::TBuffer<int64>> OutputBuffer;

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
		return EPCGMetadataTypes::Integer64;
	}

	virtual FName GetTypeName() const override
	{
		return FName("Int64");
	}

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
};

/**
 * Float property - outputs as float attribute.
 */
USTRUCT(BlueprintType, meta=(PCGExInlineValue), DisplayName="Float")
struct PCGEXPROPERTIES_API FPCGExProperty_Float : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	float Value = 0.0f;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Property")
	FPCGExNumericRange Range;
#endif

protected:
	TSharedPtr<PCGExData::TBuffer<float>> OutputBuffer;

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
		return EPCGMetadataTypes::Float;
	}

	virtual FName GetTypeName() const override
	{
		return FName("Float");
	}

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
};

/**
 * Double property - outputs as double attribute.
 */
USTRUCT(BlueprintType, meta=(PCGExInlineValue), DisplayName="Double")
struct PCGEXPROPERTIES_API FPCGExProperty_Double : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	double Value = 0.0;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Property")
	FPCGExNumericRange Range;
#endif

protected:
	TSharedPtr<PCGExData::TBuffer<double>> OutputBuffer;

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
		return EPCGMetadataTypes::Double;
	}

	virtual FName GetTypeName() const override
	{
		return FName("Double");
	}

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
};

/**
 * Bool property - outputs as bool attribute.
 */
USTRUCT(BlueprintType, meta=(PCGExInlineValue), DisplayName="Bool")
struct PCGEXPROPERTIES_API FPCGExProperty_Bool : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	bool Value = false;

protected:
	TSharedPtr<PCGExData::TBuffer<bool>> OutputBuffer;

public:
	virtual bool InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName) override;
	virtual void WriteOutput(int32 PointIndex) const override;
	virtual void WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const override;
	virtual void CopyValueFrom(const FPCGExProperty* Source) override;

	virtual bool SupportsOutput() const override
	{
		return true;
	}

	virtual EPCGMetadataTypes GetOutputType() const override
	{
		return EPCGMetadataTypes::Boolean;
	}

	virtual FName GetTypeName() const override
	{
		return FName("Bool");
	}

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
};

/**
 * Vector property - outputs as FVector attribute.
 */
USTRUCT(BlueprintType, DisplayName="Vector", meta=(PCGExInlineValue))
struct PCGEXPROPERTIES_API FPCGExProperty_Vector : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	FVector Value = FVector::ZeroVector;

protected:
	TSharedPtr<PCGExData::TBuffer<FVector>> OutputBuffer;

public:
	virtual bool InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName) override;
	virtual void WriteOutput(int32 PointIndex) const override;
	virtual void WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const override;
	virtual void CopyValueFrom(const FPCGExProperty* Source) override;

	virtual bool SupportsOutput() const override
	{
		return true;
	}

	virtual EPCGMetadataTypes GetOutputType() const override
	{
		return EPCGMetadataTypes::Vector;
	}

	virtual FName GetTypeName() const override
	{
		return FName("Vector");
	}

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
};

/**
 * Vector2D property - outputs as FVector2D attribute.
 */
USTRUCT(BlueprintType, DisplayName="Vector2", meta=(PCGExInlineValue))
struct PCGEXPROPERTIES_API FPCGExProperty_Vector2 : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	FVector2D Value = FVector2D::ZeroVector;

protected:
	TSharedPtr<PCGExData::TBuffer<FVector2D>> OutputBuffer;

public:
	virtual bool InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName) override;
	virtual void WriteOutput(int32 PointIndex) const override;
	virtual void WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const override;
	virtual void CopyValueFrom(const FPCGExProperty* Source) override;

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
		return FName("Vector2D");
	}

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
};

/**
 * Vector4 property - outputs as FVector4 attribute.
 */
USTRUCT(BlueprintType, DisplayName="Vector4")
struct PCGEXPROPERTIES_API FPCGExProperty_Vector4 : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	FVector4 Value = FVector4::Zero();

protected:
	TSharedPtr<PCGExData::TBuffer<FVector4>> OutputBuffer;

public:
	virtual bool InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName) override;
	virtual void WriteOutput(int32 PointIndex) const override;
	virtual void WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const override;
	virtual void CopyValueFrom(const FPCGExProperty* Source) override;

	virtual bool SupportsOutput() const override
	{
		return true;
	}

	virtual EPCGMetadataTypes GetOutputType() const override
	{
		return EPCGMetadataTypes::Vector4;
	}

	virtual FName GetTypeName() const override
	{
		return FName("Vector4");
	}

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
};

/**
 * Color property - authored as FLinearColor, outputs as FVector4 attribute.
 *
 * This is an example of a CONVERTING property type:
 * - Value is FLinearColor (gives the user a color picker in the editor)
 * - OutputBuffer is TBuffer<FVector4> (PCG doesn't have a native color attribute type)
 * - All output methods convert FLinearColor -> FVector4 before writing
 *
 * Use this pattern when your authored type differs from the PCG attribute type.
 * The methods are implemented manually in PCGExPropertyTypes.cpp (not via macro).
 */
USTRUCT(BlueprintType, DisplayName="Color")
struct PCGEXPROPERTIES_API FPCGExProperty_Color : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	FLinearColor Value = FLinearColor::White;

protected:
	TSharedPtr<PCGExData::TBuffer<FVector4>> OutputBuffer;

public:
	virtual bool InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName) override;
	virtual void WriteOutput(int32 PointIndex) const override;
	virtual void WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const override;
	virtual void CopyValueFrom(const FPCGExProperty* Source) override;

	virtual bool SupportsOutput() const override
	{
		return true;
	}

	virtual EPCGMetadataTypes GetOutputType() const override
	{
		return EPCGMetadataTypes::Vector4;
	}

	virtual FName GetTypeName() const override
	{
		return FName("Color");
	}

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
};

/**
 * Rotator property - outputs as FRotator attribute.
 */
USTRUCT(BlueprintType, DisplayName="Rotator", meta=(PCGExInlineValue))
struct PCGEXPROPERTIES_API FPCGExProperty_Rotator : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	FRotator Value = FRotator::ZeroRotator;

protected:
	TSharedPtr<PCGExData::TBuffer<FRotator>> OutputBuffer;

public:
	virtual bool InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName) override;
	virtual void WriteOutput(int32 PointIndex) const override;
	virtual void WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const override;
	virtual void CopyValueFrom(const FPCGExProperty* Source) override;

	virtual bool SupportsOutput() const override
	{
		return true;
	}

	virtual EPCGMetadataTypes GetOutputType() const override
	{
		return EPCGMetadataTypes::Rotator;
	}

	virtual FName GetTypeName() const override
	{
		return FName("Rotator");
	}

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
};

/**
 * Quaternion property - outputs as FQuat attribute.
 */
USTRUCT(BlueprintType, DisplayName="Quat")
struct PCGEXPROPERTIES_API FPCGExProperty_Quat : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	FQuat Value = FQuat::Identity;

protected:
	TSharedPtr<PCGExData::TBuffer<FQuat>> OutputBuffer;

public:
	virtual bool InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName) override;
	virtual void WriteOutput(int32 PointIndex) const override;
	virtual void WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const override;
	virtual void CopyValueFrom(const FPCGExProperty* Source) override;

	virtual bool SupportsOutput() const override
	{
		return true;
	}

	virtual EPCGMetadataTypes GetOutputType() const override
	{
		return EPCGMetadataTypes::Quaternion;
	}

	virtual FName GetTypeName() const override
	{
		return FName("Quat");
	}

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
};

/**
 * Transform property - outputs as FTransform attribute.
 */
USTRUCT(BlueprintType, DisplayName="Transform")
struct PCGEXPROPERTIES_API FPCGExProperty_Transform : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	FTransform Value = FTransform::Identity;

protected:
	TSharedPtr<PCGExData::TBuffer<FTransform>> OutputBuffer;

public:
	virtual bool InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName) override;
	virtual void WriteOutput(int32 PointIndex) const override;
	virtual void WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const override;
	virtual void CopyValueFrom(const FPCGExProperty* Source) override;

	virtual bool SupportsOutput() const override
	{
		return true;
	}

	virtual EPCGMetadataTypes GetOutputType() const override
	{
		return EPCGMetadataTypes::Transform;
	}

	virtual FName GetTypeName() const override
	{
		return FName("Transform");
	}

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
};

/**
 * SoftObjectPath property - outputs as FSoftObjectPath attribute.
 */
USTRUCT(BlueprintType, meta=(PCGExInlineValue), DisplayName="Soft Object Path")
struct PCGEXPROPERTIES_API FPCGExProperty_SoftObjectPath : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	FSoftObjectPath Value;

#if WITH_EDITORONLY_DATA
	/**
	 * Editor-only picker constraint, schema-owned (synced to overrides as read-only).
	 * Null = generic UObject picker (legacy behavior). Set to a specific class to narrow
	 * the picker to assets of that class (and its subclasses).
	 */
	UPROPERTY(EditAnywhere, Category = "Property", meta=(DisplayName="Allowed Class", AllowAbstract="true"))
	TSubclassOf<UObject> AllowedClass;
#endif

protected:
	TSharedPtr<PCGExData::TBuffer<FSoftObjectPath>> OutputBuffer;

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
		return EPCGMetadataTypes::SoftObjectPath;
	}

	virtual FName GetTypeName() const override
	{
		return FName("SoftObjectPath");
	}

	virtual FName GetDisplayTypeName() const override;

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;

	virtual void GatherSoftObjectPaths(TSet<FSoftObjectPath>& OutPaths) const override;
};

/**
 * SoftClassPath property - outputs as FSoftClassPath attribute.
 */
USTRUCT(BlueprintType, meta=(PCGExInlineValue), DisplayName="Soft Class Path")
struct PCGEXPROPERTIES_API FPCGExProperty_SoftClassPath : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	FSoftClassPath Value;

#if WITH_EDITORONLY_DATA
	/**
	 * Editor-only picker constraint, schema-owned (synced to overrides as read-only).
	 * Null = generic UClass picker. Set to a base class to narrow to its subclasses.
	 */
	UPROPERTY(EditAnywhere, Category = "Property", meta=(DisplayName="Allowed Base Class", AllowAbstract="true"))
	TSubclassOf<UObject> AllowedClass;
#endif

protected:
	TSharedPtr<PCGExData::TBuffer<FSoftClassPath>> OutputBuffer;

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
		return EPCGMetadataTypes::SoftClassPath;
	}

	virtual FName GetTypeName() const override
	{
		return FName("SoftClassPath");
	}

	virtual FName GetDisplayTypeName() const override;

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;

	virtual void GatherSoftObjectPaths(TSet<FSoftObjectPath>& OutPaths) const override;
};

/**
 * Enum property - uses FPCGExEnumSelector for type-safe enum selection, outputs as int64 attribute.
 *
 * Another example of a CONVERTING property type:
 * - Value is FPCGExEnumSelector (gives the user a type-safe enum picker)
 * - OutputBuffer is TBuffer<int64> (enum values stored as integer)
 * - Output methods extract Value.Value (the int64) before writing
 */
USTRUCT(BlueprintType, meta=(PCGExInlineValue), DisplayName="Enum")
struct PCGEXPROPERTIES_API FPCGExProperty_Enum : public FPCGExProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Property", meta=(NoResetToDefault))
	FPCGExEnumSelector Value;

protected:
	TSharedPtr<PCGExData::TBuffer<int64>> OutputBuffer;

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
		return EPCGMetadataTypes::Integer64;
	}

	virtual FName GetTypeName() const override
	{
		return FName("Enum");
	}

	virtual FPCGMetadataAttributeBase* CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const override;
	virtual void WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const override;
	virtual bool TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const override;
	virtual bool TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) override;
};

#pragma endregion
