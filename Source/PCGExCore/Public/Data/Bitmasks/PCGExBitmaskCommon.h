// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExBitmaskCommon.generated.h"

struct FPCGExSimpleBitmask;
struct FPCGExBitmaskRef;

UENUM()
enum class EPCGExBitOp : uint8
{
	Set = 0 UMETA(DisplayName = "=", ToolTip="SET (Flags = Mask) Set the bit with the specified value."),
	AND = 1 UMETA(DisplayName = "AND", ToolTip="AND (Flags &= Mask) Output true if boths bits == 1, otherwise false."),
	OR  = 2 UMETA(DisplayName = "OR", ToolTip="OR (Flags |= Mask) Output true if any of the bits == 1, otherwise false."),
	NOT = 3 UMETA(DisplayName = "NOT", ToolTip="NOT (Flags &= ~Mask) Like AND, but inverts the masks."),
	XOR = 4 UMETA(DisplayName = "XOR", ToolTip="XOR (Flags ^= Mask) Invert the flag bit where the mask == 1."),
};

UENUM()
enum class EPCGExBitOp_OR : uint8
{
	OR  = 0 UMETA(DisplayName = "OR", ToolTip="OR (Flags |= Mask) Output true if any of the bits == 1, otherwise false."),
	Set = 1 UMETA(DisplayName = "SET", ToolTip="SET (Flags = Mask) Set the bit with the specified value."),
	AND = 2 UMETA(DisplayName = "AND", ToolTip="AND (Flags &= Mask) Output true if boths bits == 1, otherwise false."),
	NOT = 3 UMETA(DisplayName = "NOT", ToolTip="NOT (Flags &= ~Mask) Like AND, but inverts the masks."),
	XOR = 4 UMETA(DisplayName = "XOR", ToolTip="XOR (Flags ^= Mask) Invert the flag bit where the mask == 1."),
};

UENUM()
enum class EPCGExBitmaskMode : uint8
{
	Direct     = 0 UMETA(DisplayName = "Direct", ToolTip="Used for easy override mostly. Will use the value of the bitmask as-is", ActionIcon="Bit_Direct"),
	Individual = 1 UMETA(DisplayName = "Mutations", ToolTip="Use an array to mutate the bits of the incoming bitmask (will modify the constant value on output)", ActionIcon="Bit_Mutations"),
	Composite  = 2 UMETA(Hidden),
};

UENUM()
enum class EPCGExBitflagComparison : uint8
{
	MatchPartial   = 0 UMETA(DisplayName = "Match (any)", Tooltip="Value & Mask != 0 (At least some flags in the mask are set)"),
	MatchFull      = 1 UMETA(DisplayName = "Match (all)", Tooltip="Value & Mask == Mask (All the flags in the mask are set)"),
	MatchStrict    = 2 UMETA(DisplayName = "Match (strict)", Tooltip="Value == Mask (Flags strictly equals mask)"),
	NoMatchPartial = 3 UMETA(DisplayName = "No match (any)", Tooltip="Value & Mask == 0 (Flags does not contains any from mask)"),
	NoMatchFull    = 4 UMETA(DisplayName = "No match (all)", Tooltip="Value & Mask != Mask (Flags does not contains the mask)"),
};

namespace PCGExBitmask
{
	FORCEINLINE static void Do(const EPCGExBitOp Op, int64& Flags, const int64 Mask)
	{
		switch (Op)
		{
		default: ;
		case EPCGExBitOp::Set:
			Flags = Mask;
			break;
		case EPCGExBitOp::AND:
			Flags &= Mask;
			break;
		case EPCGExBitOp::OR:
			Flags |= Mask;
			break;
		case EPCGExBitOp::NOT:
			Flags &= ~Mask;
			break;
		case EPCGExBitOp::XOR:
			Flags ^= Mask;
			break;
		}
	}

	PCGEXCORE_API FString ToString(const EPCGExBitflagComparison Comparison);

	constexpr EPCGExBitOp OR_Ops[5] = {EPCGExBitOp::OR, EPCGExBitOp::Set, EPCGExBitOp::AND, EPCGExBitOp::NOT, EPCGExBitOp::XOR,};

	FORCEINLINE constexpr EPCGExBitOp GetBitOp(EPCGExBitOp_OR BitOp)
	{
		return OR_Ops[static_cast<uint8>(BitOp)];
	}

	PCGEXCORE_API bool Compare(const EPCGExBitflagComparison Method, const int64& Flags, const int64& Mask);

	FORCEINLINE static void Mutate(const EPCGExBitOp Operation, int64& Flags, const int64 Mask)
	{
		switch (Operation)
		{
		case EPCGExBitOp::Set:
			Flags = Mask;
			break;
		case EPCGExBitOp::AND:
			Flags &= Mask;
			break;
		case EPCGExBitOp::OR:
			Flags |= Mask;
			break;
		case EPCGExBitOp::NOT:
			Flags &= ~Mask;
			break;
		case EPCGExBitOp::XOR:
			Flags ^= Mask;
			break;
		default: ;
		}
	}

	PCGEXCORE_API void Mutate(const TArray<FPCGExBitmaskRef>& Compositions, int64& Flags);

	PCGEXCORE_API void Mutate(const TArray<FPCGExSimpleBitmask>& Compositions, int64& Flags);

	struct PCGEXCORE_API FCachedRef
	{
		FCachedRef() = default;

		FName Identifier = NAME_None;
		int64 Bitmask = 0;
		FVector Direction = FVector::UpVector;
	};
}
