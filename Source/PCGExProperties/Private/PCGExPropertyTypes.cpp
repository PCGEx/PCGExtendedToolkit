// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExPropertyTypes.h"

#include "Metadata/PCGMetadata.h"
#include "Types/PCGExTypeOps.h"

// ============================================================================
// PCGEX_PROPERTY_IMPL - Code generation macro for simple property types
// ============================================================================
//
// Use this macro when your property's Value type matches its output buffer type
// (i.e., no type conversion needed). It generates all 6 required method implementations:
//
//   InitializeOutput   - Creates a writable buffer on the facade
//   WriteOutput        - Writes this->Value to buffer at PointIndex
//   WriteOutputFrom    - Writes Source->Value to buffer (thread-safe, no mutation of 'this')
//   CopyValueFrom      - Copies Source->Value into this->Value
//   CreateMetadataAttribute - Creates a typed metadata attribute with default = Value
//   WriteMetadataValue - Writes Value to a metadata entry
//
// For CONVERTING types (Value type != output type), implement manually instead.
// See FPCGExProperty_Color and FPCGExProperty_Enum below for examples.
//
// ADDING A NEW SIMPLE TYPE:
//   1. Define the USTRUCT in PCGExPropertyTypes.h (copy any existing type as template)
//   2. Add one line here: PCGEX_PROPERTY_IMPL(YourValueType, YourStructSuffix)
//      e.g., PCGEX_PROPERTY_IMPL(FVector, Vector) for FPCGExProperty_Vector
//
// NOTE: OutputBuffer validity is guaranteed by InitializeOutput returning true.
// Callers must exclude properties that failed initialization from processing.
//
// The generated TryWriteValue dispatches Value through FConversionTable, giving
// free N×N conversion across all supported PCG metadata types. Converting types
// must override manually to project to their output type first.
#define PCGEX_PROPERTY_IMPL(_TYPE, _NAME) \
bool FPCGExProperty_##_NAME::InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName) \
{ \
	OutputBuffer = OutputFacade->GetWritable<_TYPE>(OutputName, Value, true, PCGExData::EBufferInit::Inherit); \
	return OutputBuffer.IsValid(); \
} \
void FPCGExProperty_##_NAME::WriteOutput(int32 PointIndex) const \
{ \
	check(OutputBuffer); \
	OutputBuffer->SetValue(PointIndex, Value); \
} \
void FPCGExProperty_##_NAME::WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const \
{ \
	check(OutputBuffer); \
	const FPCGExProperty_##_NAME* Typed = static_cast<const FPCGExProperty_##_NAME*>(Source); \
	OutputBuffer->SetValue(PointIndex, Typed->Value); \
} \
void FPCGExProperty_##_NAME::CopyValueFrom(const FPCGExProperty* Source) \
{ \
	const FPCGExProperty_##_NAME* Typed = static_cast<const FPCGExProperty_##_NAME*>(Source); \
	Value = Typed->Value; \
} \
FPCGMetadataAttributeBase* FPCGExProperty_##_NAME::CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const \
{ \
	return Metadata->CreateAttribute<_TYPE>(AttributeName, Value, true, true); \
} \
void FPCGExProperty_##_NAME::WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const \
{ \
	static_cast<FPCGMetadataAttribute<_TYPE>*>(Attribute)->SetValue(EntryKey, Value); \
} \
bool FPCGExProperty_##_NAME::TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const \
{ \
	PCGExTypeOps::FConversionTable::Convert(PCGExTypes::TTraits<_TYPE>::Type, &Value, TargetType, OutBuffer); \
	return true; \
} \
bool FPCGExProperty_##_NAME::TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer) \
{ \
	PCGExTypeOps::FConversionTable::Convert(SourceType, InBuffer, PCGExTypes::TTraits<_TYPE>::Type, &Value); \
	return true; \
}

#pragma region Standard Types

PCGEX_PROPERTY_IMPL(FString, String)
PCGEX_PROPERTY_IMPL(FName, Name)
PCGEX_PROPERTY_IMPL(int32, Int32)
PCGEX_PROPERTY_IMPL(int64, Int64)
PCGEX_PROPERTY_IMPL(float, Float)
PCGEX_PROPERTY_IMPL(double, Double)
PCGEX_PROPERTY_IMPL(bool, Bool)
PCGEX_PROPERTY_IMPL(FVector, Vector)
PCGEX_PROPERTY_IMPL(FVector2D, Vector2)
PCGEX_PROPERTY_IMPL(FVector4, Vector4)
PCGEX_PROPERTY_IMPL(FRotator, Rotator)
PCGEX_PROPERTY_IMPL(FQuat, Quat)
PCGEX_PROPERTY_IMPL(FTransform, Transform)
PCGEX_PROPERTY_IMPL(FSoftObjectPath, SoftObjectPath)
PCGEX_PROPERTY_IMPL(FSoftClassPath, SoftClassPath)

// Soft-path properties surface their value for cook dependencies AND runtime preloading.
// FSoftClassPath derives from FSoftObjectPath, so a single FSoftObjectPath set absorbs both.
// Runtime-safe (a plain value read), hence defined here rather than in the editor-only block below.
#define PCGEX_GATHER_SOFT_PATHS(_TYPE) \
	void FPCGExProperty_##_TYPE::GatherSoftObjectPaths(TSet<FSoftObjectPath>& OutPaths) const \
	{ if (!Value.IsNull()) { OutPaths.Add(Value); } }

PCGEX_GATHER_SOFT_PATHS(SoftObjectPath)
PCGEX_GATHER_SOFT_PATHS(SoftClassPath)
#undef PCGEX_GATHER_SOFT_PATHS

// Editor-only structural meta propagation. AllowedClass and Range are schema-authored
// and copied verbatim onto override entries so the override UI inherits the same picker
// constraints. The fields themselves are WITH_EDITORONLY_DATA, so cooked builds get
// no-op stubs to preserve the vtable.
#if WITH_EDITOR
#define PCGEX_SYNC_STRUCTURAL(_TYPE, _FIELD) \
	bool FPCGExProperty_##_TYPE::SyncStructuralFromSchema(const FPCGExProperty& Schema) \
	{ \
		const auto& SchemaField = static_cast<const FPCGExProperty_##_TYPE&>(Schema)._FIELD; \
		if (_FIELD == SchemaField) { return false; } \
		_FIELD = SchemaField; \
		return true; \
	}

PCGEX_SYNC_STRUCTURAL(Int32, Range)
PCGEX_SYNC_STRUCTURAL(Int64, Range)
PCGEX_SYNC_STRUCTURAL(Float, Range)
PCGEX_SYNC_STRUCTURAL(Double, Range)
PCGEX_SYNC_STRUCTURAL(SoftObjectPath, AllowedClass)
PCGEX_SYNC_STRUCTURAL(SoftClassPath, AllowedClass)
#undef PCGEX_SYNC_STRUCTURAL

// Surface the narrowed class name in UI labels when the schema author has constrained
// the picker. Returning the FName of the UClass keeps it allocation-free.
#define PCGEX_DISPLAY_TYPE_FROM_ALLOWED_CLASS(_TYPE) \
	FName FPCGExProperty_##_TYPE::GetDisplayTypeName() const \
	{ return AllowedClass ? AllowedClass->GetFName() : GetTypeName(); }

PCGEX_DISPLAY_TYPE_FROM_ALLOWED_CLASS(SoftObjectPath)
PCGEX_DISPLAY_TYPE_FROM_ALLOWED_CLASS(SoftClassPath)
#undef PCGEX_DISPLAY_TYPE_FROM_ALLOWED_CLASS

// Soft-path emit is defined as GatherSoftObjectPaths in the always-compiled section above
// (runtime-safe), so nothing soft-path-related lives in this editor-only block anymore.
#else
bool FPCGExProperty_Int32::SyncStructuralFromSchema(const FPCGExProperty&)
{
	return false;
}
bool FPCGExProperty_Int64::SyncStructuralFromSchema(const FPCGExProperty&)
{
	return false;
}
bool FPCGExProperty_Float::SyncStructuralFromSchema(const FPCGExProperty&)
{
	return false;
}
bool FPCGExProperty_Double::SyncStructuralFromSchema(const FPCGExProperty&)
{
	return false;
}
bool FPCGExProperty_SoftObjectPath::SyncStructuralFromSchema(const FPCGExProperty&)
{
	return false;
}
bool FPCGExProperty_SoftClassPath::SyncStructuralFromSchema(const FPCGExProperty&)
{
	return false;
}
FName FPCGExProperty_SoftObjectPath::GetDisplayTypeName() const
{
	return GetTypeName();
}
FName FPCGExProperty_SoftClassPath::GetDisplayTypeName() const
{
	return GetTypeName();
}
#endif

#pragma endregion

// ============================================================================
// MANUAL IMPLEMENTATIONS - Converting property types
// ============================================================================
// These types need manual implementations because the authored Value type
// differs from the output buffer type. Each method must perform the conversion.

#pragma region Color (FLinearColor -> FVector4)

bool FPCGExProperty_Color::InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName)
{
	OutputBuffer = OutputFacade->GetWritable<FVector4>(OutputName, FVector4(Value), true, PCGExData::EBufferInit::Inherit);
	return OutputBuffer.IsValid();
}

void FPCGExProperty_Color::WriteOutput(int32 PointIndex) const
{
	check(OutputBuffer);
	OutputBuffer->SetValue(PointIndex, FVector4(Value));
}

void FPCGExProperty_Color::WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const
{
	check(OutputBuffer);
	const FPCGExProperty_Color* Typed = static_cast<const FPCGExProperty_Color*>(Source);
	OutputBuffer->SetValue(PointIndex, FVector4(Typed->Value));
}

void FPCGExProperty_Color::CopyValueFrom(const FPCGExProperty* Source)
{
	const FPCGExProperty_Color* Typed = static_cast<const FPCGExProperty_Color*>(Source);
	Value = Typed->Value;
}

FPCGMetadataAttributeBase* FPCGExProperty_Color::CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const
{
	return Metadata->CreateAttribute<FVector4>(AttributeName, FVector4(Value), true, true);
}

void FPCGExProperty_Color::WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const
{
	static_cast<FPCGMetadataAttribute<FVector4>*>(Attribute)->SetValue(EntryKey, FVector4(Value));
}

bool FPCGExProperty_Color::TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const
{
	const FVector4 Projected = FVector4(Value);
	PCGExTypeOps::FConversionTable::Convert(EPCGMetadataTypes::Vector4, &Projected, TargetType, OutBuffer);
	return true;
}

bool FPCGExProperty_Color::TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer)
{
	FVector4 Projected;
	PCGExTypeOps::FConversionTable::Convert(SourceType, InBuffer, EPCGMetadataTypes::Vector4, &Projected);
	Value = FLinearColor(Projected.X, Projected.Y, Projected.Z, Projected.W);
	return true;
}

#pragma endregion

#pragma region Enum (FPCGExEnumSelector -> int64)

bool FPCGExProperty_Enum::InitializeOutput(const TSharedRef<PCGExData::FFacade>& OutputFacade, FName OutputName)
{
	OutputBuffer = OutputFacade->GetWritable<int64>(OutputName, Value.Value, true, PCGExData::EBufferInit::Inherit);
	return OutputBuffer.IsValid();
}

void FPCGExProperty_Enum::WriteOutput(int32 PointIndex) const
{
	check(OutputBuffer);
	OutputBuffer->SetValue(PointIndex, Value.Value);
}

void FPCGExProperty_Enum::WriteOutputFrom(int32 PointIndex, const FPCGExProperty* Source) const
{
	check(OutputBuffer);
	const FPCGExProperty_Enum* Typed = static_cast<const FPCGExProperty_Enum*>(Source);
	OutputBuffer->SetValue(PointIndex, Typed->Value.Value);
}

void FPCGExProperty_Enum::CopyValueFrom(const FPCGExProperty* Source)
{
	const FPCGExProperty_Enum* Typed = static_cast<const FPCGExProperty_Enum*>(Source);
	Value = Typed->Value;
}

bool FPCGExProperty_Enum::SyncStructuralFromSchema(const FPCGExProperty& Schema)
{
	// The enum class is structural -- the schema decides which UEnum the property targets,
	// and overrides must follow it. The selected int64 value is intentionally preserved
	// here; if it no longer corresponds to a member of the new class, the picker UI will
	// surface the raw integer and the user can re-pick.
	const FPCGExProperty_Enum& Typed = static_cast<const FPCGExProperty_Enum&>(Schema);
	if (Value.Class == Typed.Value.Class) { return false; }
	Value.Class = Typed.Value.Class;
	return true;
}

FPCGMetadataAttributeBase* FPCGExProperty_Enum::CreateMetadataAttribute(UPCGMetadata* Metadata, FName AttributeName) const
{
	return Metadata->CreateAttribute<int64>(AttributeName, Value.Value, true, true);
}

void FPCGExProperty_Enum::WriteMetadataValue(FPCGMetadataAttributeBase* Attribute, int64 EntryKey) const
{
	static_cast<FPCGMetadataAttribute<int64>*>(Attribute)->SetValue(EntryKey, Value.Value);
}

bool FPCGExProperty_Enum::TryWriteValue(EPCGMetadataTypes TargetType, void* OutBuffer) const
{
	const int64 Projected = Value.Value;
	PCGExTypeOps::FConversionTable::Convert(EPCGMetadataTypes::Integer64, &Projected, TargetType, OutBuffer);
	return true;
}

bool FPCGExProperty_Enum::TryReadValue(EPCGMetadataTypes SourceType, const void* InBuffer)
{
	// Project the incoming value through int64. The enum's UEnum class is structural and
	// owned by the schema -- not touched here. If the projected int64 doesn't correspond to
	// a member of the current class, Value.Value still stores it and the picker UI surfaces
	// the raw integer, matching the SyncStructuralFromSchema convention.
	int64 Projected = 0;
	PCGExTypeOps::FConversionTable::Convert(SourceType, InBuffer, EPCGMetadataTypes::Integer64, &Projected);
	Value.Value = Projected;
	return true;
}

#pragma endregion

#undef PCGEX_PROPERTY_IMPL
