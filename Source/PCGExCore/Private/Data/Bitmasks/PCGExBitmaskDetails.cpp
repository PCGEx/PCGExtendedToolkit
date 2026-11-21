// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Bitmasks/PCGExBitmaskDetails.h"

#include "Core/PCGExContext.h"
#include "Data/Bitmasks/PCGExBitmaskCollection.h"

void FPCGExClampedBitOp::Mutate(int64& Flags) const
{
	const int64 BitMask = static_cast<int64>(1) << BitIndex;
	switch (Op)
	{
	case EPCGExBitOp::Set:
		if (bValue)
		{
			Flags |= BitMask;
		}
		else
		{
			Flags &= ~BitMask;
		}
		break;
	case EPCGExBitOp::AND:
		if (!bValue)
		{
			Flags &= ~BitMask;
		} // AND false -> clear bit 
		// AND true -> leave bit as is, nothing to do
		break;
	case EPCGExBitOp::OR:
		if (bValue)
		{
			Flags |= BitMask;
		} // OR true -> set bit 
		// OR false -> leave bit as is, nothing to do
		break;
	case EPCGExBitOp::NOT:
		if (bValue)
		{
			Flags ^= BitMask;
		} // flip the bit
		break;
	case EPCGExBitOp::XOR:
		if (bValue)
		{
			Flags ^= BitMask;
		} // XOR with true flips the bit
		// XOR false -> do nothing
		break;
	default:
		break;
	}
}

FPCGExBitmaskRef::FPCGExBitmaskRef(const TObjectPtr<UPCGExBitmaskCollection> InSource, const FName InIdentifier)
	: Source(InSource)
	  , Identifier(InIdentifier)
{
}

#if WITH_EDITOR
TArray<FName> FPCGExBitmaskRef::EDITOR_GetIdentifierOptions() const
{
	if (Source)
	{
		return Source->EDITOR_GetIdentifierOptions();
	}
	return {TEXT("INVALID")};
}
#endif

void FPCGExBitmaskRef::EDITOR_RegisterTrackingKeys(FPCGExContext* Context) const
{
	if (Source)
	{
		Context->EDITOR_TrackPath(Source);
	}
}

void FPCGExBitmaskRef::Mutate(int64& Flags) const
{
	int64 Mask = 0;
	if (Source && Source->LoadCache()->TryGetBitmask(Identifier, Mask))
	{
		PCGExBitmask::Mutate(Op, Flags, Mask);
	}
}

FPCGExSimpleBitmask FPCGExBitmaskRef::GetSimpleBitmask() const
{
	FPCGExSimpleBitmask SimpleBitmask;
	if (Source && Source->LoadCache()->TryGetBitmask(Identifier, SimpleBitmask.Bitmask))
	{
		SimpleBitmask.Op = Op;
	}
	else
	{
		SimpleBitmask.Op = EPCGExBitOp::OR;
	}
	return SimpleBitmask;
}

bool FPCGExBitmaskRef::TryGetAdjacencyInfos(FVector& OutDirection, FPCGExSimpleBitmask& OutSimpleBitmask) const
{
	if (PCGExBitmask::FCachedRef Cache = PCGExBitmask::FCachedRef{};
		Source && Source->LoadCache()->TryGetBitmask(Identifier, Cache))
	{
		OutSimpleBitmask = FPCGExSimpleBitmask();
		OutSimpleBitmask.Bitmask = Cache.Bitmask;
		OutSimpleBitmask.Op = Op;

		OutDirection = Cache.Direction;

		return true;
	}

	return false;
}

// Shared by FPCGExBitmask and FPCGExBitmaskWithOperation, which declare the same members.
namespace PCGExBitmaskDetails
{
	template <typename T>
	int64 ResolveMask(const T& In)
	{
		int64 Mask = In.Bitmask;
		if (In.Mode == EPCGExBitmaskMode::Individual)
		{
			for (const FPCGExClampedBitOp& Bit : In.Mutations)
			{
				Bit.Mutate(Mask);
			}
		}
		PCGExBitmask::Mutate(In.Compositions, Mask);
		return Mask;
	}

	template <typename T>
	void RegisterCompositionsTrackingKeys(const T& In, FPCGExContext* Context)
	{
		for (const FPCGExBitmaskRef& Comp : In.Compositions)
		{
			Comp.EDITOR_RegisterTrackingKeys(Context);
		}
	}

#if WITH_EDITOR
	template <typename T>
	void FoldLegacyRanges(T& In)
	{
		In.Bitmask = 0;
		In.Bitmask |= static_cast<int64>(In.Range_00_08_DEPRECATED) << 0;
		In.Bitmask |= static_cast<int64>(In.Range_08_16_DEPRECATED) << 8;
		In.Bitmask |= static_cast<int64>(In.Range_16_24_DEPRECATED) << 16;
		In.Bitmask |= static_cast<int64>(In.Range_24_32_DEPRECATED) << 24;
		In.Bitmask |= static_cast<int64>(In.Range_32_40_DEPRECATED) << 32;
		In.Bitmask |= static_cast<int64>(In.Range_40_48_DEPRECATED) << 40;
		In.Bitmask |= static_cast<int64>(In.Range_48_56_DEPRECATED) << 48;
		In.Bitmask |= static_cast<int64>(In.Range_56_64_DEPRECATED) << 56;
	}
#endif
}

int64 FPCGExBitmask::Get() const
{
	return PCGExBitmaskDetails::ResolveMask(*this);
}

void FPCGExBitmask::EDITOR_RegisterTrackingKeys(FPCGExContext* Context) const
{
	PCGExBitmaskDetails::RegisterCompositionsTrackingKeys(*this, Context);
}

#if WITH_EDITOR
void FPCGExBitmask::ApplyDeprecation()
{
	if (Mode == EPCGExBitmaskMode::Composite)
	{
		PCGExBitmaskDetails::FoldLegacyRanges(*this);
	}
	else if (Mode == EPCGExBitmaskMode::Individual)
	{
		Bitmask = 0;
		Mutations.Reserve(Bits.Num());
		for (const FPCGExClampedBit& Bit : Bits)
		{
			if (Bit.bValue)
			{
				Bitmask |= (static_cast<int64>(1) << Bit.BitIndex);
			}
			FPCGExClampedBitOp& Mutation = Mutations.Emplace_GetRef();
			Mutation.BitIndex = Bit.BitIndex;
			Mutation.bValue = Bit.bValue;
			Mutation.Op = EPCGExBitOp::Set;
		}
		Bits.Empty();
	}

	Mode = EPCGExBitmaskMode::Direct;
}
#endif

int64 FPCGExBitmaskWithOperation::Get() const
{
	return PCGExBitmaskDetails::ResolveMask(*this);
}

void FPCGExBitmaskWithOperation::Mutate(int64& Flags) const
{
	PCGExBitmask::Mutate(Op, Flags, Get());
	if (Mode == EPCGExBitmaskMode::Individual)
	{
		for (const FPCGExClampedBitOp& Bit : Mutations)
		{
			Bit.Mutate(Flags);
		}
	}
	else
	{
		PCGExBitmask::Mutate(Compositions, Flags);
	}
}

void FPCGExBitmaskWithOperation::EDITOR_RegisterTrackingKeys(FPCGExContext* Context) const
{
	PCGExBitmaskDetails::RegisterCompositionsTrackingKeys(*this, Context);
}

#if WITH_EDITOR
void FPCGExBitmaskWithOperation::ApplyDeprecation()
{
	if (Mode == EPCGExBitmaskMode::Composite)
	{
		PCGExBitmaskDetails::FoldLegacyRanges(*this);
		Mode = EPCGExBitmaskMode::Direct;
	}
}
#endif
