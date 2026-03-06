// Copyright 2026 Edward Boucher and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "PCGExConstantsDefinitions.generated.h"

// An enum used to identify the constants in the namespace below. This is perhaps a slightly messy way of doing things,
// but it means we can use PCGMetadataElementCommon::FillPreconfiguredSettingsInfoFromEnum to create different nodes
// for each.
UENUM(BlueprintType)
enum class EPCGExConstantListID : uint8
{
	// Numeric constants
	// These go before the others, as there's more of them, and that way we can just access them by index.
	ZeroAndOne  = 0 UMETA(DisplayName="0 and 1"),
	MinusOne    = 1 UMETA(DisplayName="-1"),
	Twos        = 2 UMETA(DisplayName="0.5 and 2"),
	Tens        = 3 UMETA(DisplayName="Powers of 10"),
	Irrationals = 4,
	Angles      = 5,
	Zero        = 6 UMETA(DisplayName="0"),
	One         = 7 UMETA(DisplayName="1"),

	// Vectors
	Vectors = 8 UMETA(DisplayName="Axes"),

	// Booleans
	Booleans  = 9 UMETA(DisplayName = "True and False"),
	TrueBool  = 10 UMETA(DisplayName = "True"),
	FalseBool = 11 UMETA(DisplayName = "False"),
	MAX_BOOL  = 12 UMETA(Hidden),

	// Additional vectors
	ADDITIONAL_VECTORS = 2 << 3 UMETA(Hidden),
	OneVector          = 17 UMETA(DisplayName="Unit Vector"),
	ZeroVector         = 18 UMETA(DisplayName="Zero Vector"),
	HalfVector         = 19 UMETA(DisplayName="Half Vector"),
	UpVector           = 20 UMETA(DisplayName="Up Vector"),
	RightVector        = 21 UMETA(DisplayName="Right Vector"),
	ForwardVector      = 22 UMETA(DisplayName="Forward Vector"),

	// Additional numerics
	ADDITIONAL_NUMERICS = 2 << 4 UMETA(Hidden),
	Two                 = 33 UMETA(DisplayName="2"),
	Half                = 34 UMETA(DisplayName="0.5")
};

ENUM_CLASS_FLAGS(EPCGExConstantListID)

UENUM(BlueprintType)
enum class EPCGExConstantType : uint8
{
	Number,
	Vector,
	Bool
};

namespace PCGExConstants
{
	template <typename T>
	struct TDescriptor
	{
		FName Name;
		T Value;
	};

	template <typename T>
	struct TDescriptorList
	{
		FName GroupName;
		TArray<TDescriptor<T>> Constants;
	};

	template <typename T>
	struct TDescriptorListGroup
	{
		FName DisplayName; // Not currently needed, but helps identify each in the namespace below
		TArray<TDescriptorList<T>> ExportedConstants;
	};

	inline TDescriptorListGroup<double> Numbers = {"Numeric Constants", {{"Zero and One", {{"0", 0.0}, {"1", 1.0}}}, {"Minus One", {{"-1", -1.0}}}, {"Twos", {{"Half", 0.5}, {"2", 2.0}}}, {"Tens", {{"10", 10.0}, {"100", 100.0}, {"1000", 1000.0},}}, {"Irrationals", {{"Pi", DOUBLE_PI}, {"Tau", DOUBLE_PI * 2.0}, {"E", DOUBLE_EULERS_NUMBER}, {"Root 2", DOUBLE_UE_SQRT_2}, {"Golden Ratio", UE_DOUBLE_GOLDEN_RATIO}}}, {"Angles", {{"90", 90.0}, {"180", 180.0}, {"270", 270.0}, {"360", 360.0}, {"45", 45.0}}}, {"Zero", {{"0", 0.0}}}, {"One", {{"1", 1.0}}}}};

	inline TDescriptorListGroup<double> AdditionalNumbers{"Additional Numbers", {{"Two", {{"2", 2.0}}}, {"Half", {{"Half", 0.5}}}}};

	inline TDescriptorListGroup<FVector> Vectors{"Vector Constants", {{"Axes", {{"Up", FVector::UpVector}, {"Right", FVector::RightVector}, {"Forward", FVector::ForwardVector}}}}};

	inline TDescriptorListGroup<FVector> AdditionalVectors{"Unit Vector Constants", {{"Unit Vector", {{"Unit Vector", FVector::OneVector}}}, {"Zero Vector", {{"Zero Vector", FVector::ZeroVector}}}, {"Half Vector", {{"Half Vector", FVector(.5, .5, .5)}}}, {"Up Vector", {{"Up Vector", FVector::UpVector}}}, {"Right Vector", {{"Right Vector", FVector::RightVector}}}, {"Forward Vector", {{"Forward Vector", FVector::ForwardVector}}}}};

	inline TArray<TDescriptor<bool>> Booleans = {{"True", true}, {"False", false}};
}
