// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "Metadata/PCGMetadataCommon.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGExBlendingCommon.generated.h"

UENUM(BlueprintType)
enum class EPCGExBlendingType : uint8
{
	None             = 0 UMETA(DisplayName = "None", ToolTip="No blending is applied, keep the original value."),
	Average          = 1 UMETA(DisplayName = "Average", ToolTip="Average all sampled values."),
	Weight           = 2 UMETA(DisplayName = "Weight", ToolTip="Weights based on distance to blend targets. If the results are unexpected, try 'Lerp' instead"),
	Min              = 3 UMETA(DisplayName = "Min", ToolTip="Component-wise MIN operation"),
	Max              = 4 UMETA(DisplayName = "Max", ToolTip="Component-wise MAX operation"),
	Copy             = 5 UMETA(DisplayName = "Copy (Target)", ToolTip = "Copy target data (second value)"),
	Sum              = 6 UMETA(DisplayName = "Sum", ToolTip = "Sum"),
	WeightedSum      = 7 UMETA(DisplayName = "Weighted Sum", ToolTip = "Sum of all the data, weighted"),
	Lerp             = 8 UMETA(DisplayName = "Lerp", ToolTip="Uses weight as lerp. If the results are unexpected, try 'Weight' instead."),
	Subtract         = 9 UMETA(DisplayName = "Subtract", ToolTip="Subtract."),
	UnsignedMin      = 10 UMETA(DisplayName = "Unsigned Min", ToolTip="Component-wise MIN on unsigned value, but keeps the sign on written data."),
	UnsignedMax      = 11 UMETA(DisplayName = "Unsigned Max", ToolTip="Component-wise MAX on unsigned value, but keeps the sign on written data."),
	AbsoluteMin      = 12 UMETA(DisplayName = "Absolute Min", ToolTip="Component-wise MIN of absolute value."),
	AbsoluteMax      = 13 UMETA(DisplayName = "Absolute Max", ToolTip="Component-wise MAX of absolute value."),
	WeightedSubtract = 14 UMETA(DisplayName = "Weighted Subtract", ToolTip="Substraction of all the data, weighted"),
	CopyOther        = 15 UMETA(DisplayName = "Copy (Source)", ToolTip="Copy source data (first value)"),
	Hash             = 16 UMETA(DisplayName = "Hash", ToolTip="Combine the values into a hash"),
	UnsignedHash     = 17 UMETA(DisplayName = "Hash (Sorted)", ToolTip="Combine the values into a hash but sort the values first to create an order-independent hash."),
	WeightNormalize  = 18 UMETA(DisplayName = "Weight (Normalize)", ToolTip="Weights based on distance to blend targets and force normalized."),
	Unset            = 200 UMETA(Hidden),
};

#define PCGEX_FOREACH_BLEND_POINTPROPERTY(MACRO)\
MACRO(Density, float, float) \
MACRO(BoundsMin,FVector, FVector) \
MACRO(BoundsMax, FVector, FVector) \
MACRO(Color,FVector4, FVector4) \
MACRO(Position,FTransform, FVector) \
MACRO(Rotation, FTransform, FQuat) \
MACRO(Scale, FTransform, FVector) \
MACRO(Steepness,float, float) \
MACRO(Seed,int32, int32)

#define PCGEX_FOREACH_DATABLENDMODE(MACRO)\
MACRO(None) \
MACRO(Average) \
MACRO(Weight) \
MACRO(Min) \
MACRO(Max) \
MACRO(Copy) \
MACRO(Sum) \
MACRO(WeightedSum) \
MACRO(Lerp) \
MACRO(Subtract) \
MACRO(UnsignedMin) \
MACRO(UnsignedMax) \
MACRO(AbsoluteMin) \
MACRO(AbsoluteMax) \
MACRO(WeightedSubtract) \
MACRO(CopyOther) \
MACRO(Hash) \
MACRO(UnsignedHash) \
MACRO(WeightNormalize)

// This is a different blending list that makes more sense for AxB blending
// and also includes extra modes that don't make sense in regular multi-source data blending
UENUM(BlueprintType)
enum class EPCGExABBlendingType : uint8
{
	None             = 0 UMETA(Hidden, DisplayName = "None", ToolTip="No blending is applied, keep the original value.", ActionIcon="PCGEx.Pin.OUT_BlendOp"),
	Average          = 1 UMETA(DisplayName = "Average", ToolTip="(A + B) / 2", ActionIcon="PCGEx.Pin.OUT_BlendOp", SearchHints = "Average"),
	Weight           = 2 UMETA(DisplayName = "Weight", ToolTip="(A + B) / Weight. Values are normalize if weight > 1", ActionIcon="PCGEx.Pin.OUT_BlendOp", SearchHints = "Weight"),
	Multiply         = 3 UMETA(DisplayName = "Multiply", ToolTip="A * B", ActionIcon="PCGEx.Pin.OUT_BlendOp", SearchHints = "*"),
	Divide           = 4 UMETA(DisplayName = "Divide", ToolTip="A / B", ActionIcon="PCGEx.Pin.OUT_BlendOp", SearchHints = "Average"),
	Min              = 5 UMETA(DisplayName = "Min", ToolTip="Min(A, B)", ActionIcon="PCGEx.Pin.OUT_BlendOp"),
	Max              = 6 UMETA(DisplayName = "Max", ToolTip="Max(A, B)", ActionIcon="PCGEx.Pin.OUT_BlendOp"),
	CopyTarget       = 7 UMETA(DisplayName = "Copy (Target)", ToolTip = "= B", ActionIcon="PCGEx.Pin.OUT_BlendOp"),
	CopySource       = 8 UMETA(DisplayName = "Copy (Source)", ToolTip="= A", ActionIcon="PCGEx.Pin.OUT_BlendOp"),
	Add              = 9 UMETA(DisplayName = "Add", ToolTip = "A + B", ActionIcon="PCGEx.Pin.OUT_BlendOp", SearchHints = "+"),
	Subtract         = 10 UMETA(DisplayName = "Subtract", ToolTip="A - B", ActionIcon="PCGEx.Pin.OUT_BlendOp", SearchHints = "-"),
	WeightedAdd      = 11 UMETA(DisplayName = "Weighted Add", ToolTip = "A + (B * Weight)", ActionIcon="PCGEx.Pin.OUT_BlendOp"),
	WeightedSubtract = 12 UMETA(DisplayName = "Weighted Subtract", ToolTip="A - (B * Weight)", ActionIcon="PCGEx.Pin.OUT_BlendOp"),
	Lerp             = 13 UMETA(DisplayName = "Lerp", ToolTip="Lerp(A, B, Weight)", ActionIcon="PCGEx.Pin.OUT_BlendOp"),
	UnsignedMin      = 14 UMETA(DisplayName = "Unsigned Min", ToolTip="Min(A, B) * Sign", ActionIcon="PCGEx.Pin.OUT_BlendOp"),
	UnsignedMax      = 15 UMETA(DisplayName = "Unsigned Max", ToolTip="Max(A, B) * Sign", ActionIcon="PCGEx.Pin.OUT_BlendOp"),
	AbsoluteMin      = 16 UMETA(DisplayName = "Absolute Min", ToolTip="+Min(A, B)", ActionIcon="PCGEx.Pin.OUT_BlendOp"),
	AbsoluteMax      = 17 UMETA(DisplayName = "Absolute Max", ToolTip="+Max(A, B)", ActionIcon="PCGEx.Pin.OUT_BlendOp"),
	Hash             = 18 UMETA(DisplayName = "Hash", ToolTip="Hash(A, B)", ActionIcon="PCGEx.Pin.OUT_BlendOp"),
	UnsignedHash     = 19 UMETA(DisplayName = "Hash (Sorted)", ToolTip="Hash(Min(A, B), Max(A, B))", ActionIcon="PCGEx.Pin.OUT_BlendOp"),
	Mod              = 20 UMETA(DisplayName = "Modulo (Simple)", ToolTip="FMod(A, cast(B))", ActionIcon="PCGEx.Pin.OUT_BlendOp", SearchHints = "%"),
	ModCW            = 21 UMETA(DisplayName = "Modulo (Component Wise)", ToolTip="FMod(A, B)", ActionIcon="PCGEx.Pin.OUT_BlendOp", SearchHints = "%"),
	WeightNormalize  = 22 UMETA(DisplayName = "Weight (Always Normalize)", ToolTip="(A + B) / Weight. Always normalize final values.", ActionIcon="PCGEx.Pin.OUT_BlendOp", SearchHints = "Weight"),
	GeometricMean    = 23 UMETA(Hidden, DisplayName = "Geometrical Mean", ToolTip="A * B .. Pow(Acc, 1/Count).", ActionIcon="PCGEx.Pin.OUT_BlendOp", SearchHints = "Geometrical Mean"),
	HarmonicMean     = 24 UMETA(Hidden, DisplayName = "Harmonic Mean", ToolTip="Acc + 1/B .. Count/Acc", ActionIcon="PCGEx.Pin.OUT_BlendOp", SearchHints = "Harmonic Mean"),
	RMS              = 25 UMETA(Hidden, DisplayName = "RMS (Signal Magnitude)", ToolTip="Acc + Src² .. Sqrt(Acc/Count)", ActionIcon="PCGEx.Pin.OUT_BlendOp", SearchHints = "RMS Signal Magnitude"),
	Step             = 26 UMETA(Hidden, DisplayName = "Step", ToolTip="Step", ActionIcon="PCGEx.Pin.OUT_BlendOp", SearchHints = "Step"),
};

#define PCGEX_FOREACH_AB_BLENDMODE(MACRO)\
MACRO(None) \
MACRO(Average) \
MACRO(Weight) \
MACRO(Multiply) \
MACRO(Divide) \
MACRO(Min) \
MACRO(Max) \
MACRO(CopyTarget) \
MACRO(CopySource) \
MACRO(Add) \
MACRO(Subtract) \
MACRO(WeightedAdd) \
MACRO(WeightedSubtract) \
MACRO(Lerp) \
MACRO(UnsignedMin) \
MACRO(UnsignedMax) \
MACRO(AbsoluteMin) \
MACRO(AbsoluteMax) \
MACRO(Hash) \
MACRO(UnsignedHash) \
MACRO(Mod) \
MACRO(ModCW) \
MACRO(WeightNormalize) \
MACRO(GeometricMean) \
MACRO(HarmonicMean) \
MACRO(RMS) \
MACRO(Step)

UENUM()
enum class EPCGExBlendOver : uint8
{
	Distance = 0 UMETA(DisplayName = "Distance", ToolTip="Blend is based on distance over max distance"),
	Index    = 1 UMETA(DisplayName = "Count", ToolTip="Blend is based on index over total count"),
	Fixed    = 2 UMETA(DisplayName = "Fixed", ToolTip="Fixed blend lerp/weight value"),
};

UENUM()
enum class EPCGExBlendingInterface : uint8
{
	Individual = 0 UMETA(DisplayName = "Individual", ToolTip="Uses individual blend operation subnodes to get full control. Best if you're looking to pick only a few specific things."),
	Monolithic = 1 UMETA(DisplayName = "Monolithic", ToolTip="Blend attributes & properties using monolithic settings. Best if you want to grab everything, or only select things to leave out."),
};

UENUM()
enum class EPCGExOperandSource : uint8
{
	Constant  = 0 UMETA(DisplayName = "Constant", Tooltip="Use a constant, user-defined value.", ActionIcon="Constant"),
	Attribute = 1 UMETA(DisplayName = "Attribute", Tooltip="Read the value from the input data.", ActionIcon="Attribute"),
};

UENUM()
enum class EPCGExOperandAuthority : uint8
{
	A      = 0 UMETA(DisplayName = "Operand A", ToolTip="Type of operand A will drive the output type, thus converting operand B to the same type for the operation."),
	B      = 1 UMETA(DisplayName = "Operand B", ToolTip="Type of operand B will drive the output type, thus converting operand A to the same type for the operation."),
	Custom = 2 UMETA(DisplayName = "Custom", ToolTip="Select a specific type to output the result to."),
	Auto   = 3 UMETA(DisplayName = "Auto", ToolTip="Takes an informed guess based on settings & existing data. Usually works well, but not fool-proof."),
};

UENUM()
enum class EPCGExBlendOpOutputMode : uint8
{
	SameAsA   = 0 UMETA(DisplayName = "Same as A", ToolTip="Will write the output value to Operand A' selector."),
	SameAsB   = 1 UMETA(DisplayName = "Same as B", ToolTip="Will write the output value to Operand B' selector. (Will default to A if B is not set)"),
	New       = 2 UMETA(DisplayName = "New", ToolTip="Will write the output value to new selector."),
	Transient = 3 UMETA(DisplayName = "New (Transient)", ToolTip="Will write the output value to a new selector that will only exist for the duration of the blend, but can be referenced by other blend ops."),
};

namespace PCGExBlending
{
	namespace Labels
	{
		const FName SourceOverridesBlendingOps = TEXT("Overrides : Blending");

		const FName SourceConstantA = FName("A");
		const FName SourceConstantB = FName("B");

		const FName SourceBlendingLabel = TEXT("Blend Ops");
		const FName OutputBlendingLabel = TEXT("Blend Op");
	}

	PCGEXBLENDING_API void ConvertBlending(const EPCGExBlendingType From, EPCGExABBlendingType& OutTo);

	struct FBlendingParam
	{
		FBlendingParam() = default;

		FPCGAttributeIdentifier Identifier;
		FPCGAttributePropertyInputSelector Selector;
		EPCGExABBlendingType Blending = EPCGExABBlendingType::None;
		bool bIsNewAttribute = false;

		void SelectFromString(const FString& Selection);
		void Select(const FPCGAttributeIdentifier& InIdentifier);

		// Conversion from old blendmode to extended list
		void SetBlending(EPCGExBlendingType InBlending);
	};
}
