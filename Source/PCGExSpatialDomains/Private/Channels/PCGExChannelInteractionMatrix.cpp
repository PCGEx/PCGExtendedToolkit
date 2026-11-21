// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Channels/PCGExChannelInteractionMatrix.h"

DEFINE_LOG_CATEGORY_STATIC(LogPCGExChannelMatrix, Log, All);

static_assert(static_cast<uint8>(EPCGExChannelResponse::Block) == 0,
              "FMemory::Memset relies on Block having underlying byte value 0 -- "
              "see ResetResponsesToBlock().");

namespace PCGExChannelInteractionMatrix
{
	FORCEINLINE void ResetResponsesToBlock(EPCGExChannelResponse* Cells, size_t NumBytes)
	{
		// Single memset to fill the entire 32x32 table with Block -- relies
		// on the static_assert above pinning Block's representation to 0.
		FMemory::Memset(Cells, 0, NumBytes);
	}
}

FPCGExChannelInteractionMatrix::FPCGExChannelInteractionMatrix()
{
	PCGExChannelInteractionMatrix::ResetResponsesToBlock(&Responses[0][0], sizeof(Responses));
}

void FPCGExChannelInteractionMatrix::Compile(
	TConstArrayView<FName> InChannelKeys,
	TConstArrayView<FPCGExChannelProfile> InProfiles)
{
	PCGExChannelInteractionMatrix::ResetResponsesToBlock(&Responses[0][0], sizeof(Responses));

	// Snapshot channel keys (capped at MaxChannels -- excess registered
	// channels can't be addressed by the uint32 mask anyway; if we ever
	// need more, widen the mask).
	ChannelKeys.Reset();
	KeyToBit.Reset();

	const int32 Count = FMath::Min(InChannelKeys.Num(), MaxChannels);
	ChannelKeys.Reserve(Count);
	KeyToBit.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		const FName& Key = InChannelKeys[i];
		ChannelKeys.Add(Key);
		KeyToBit.Add(Key, i);
	}

	if (InChannelKeys.Num() > MaxChannels)
	{
		UE_LOG(LogPCGExChannelMatrix, Warning,
		       TEXT("Channel registry exceeds MaxChannels (%d > %d); excess channels are unaddressable in the runtime mask. Widen the mask type or trim the registry."),
		       InChannelKeys.Num(), MaxChannels);
	}

	// Apply each profile's response overrides. Drop profiles or entries
	// referring to unknown channels with a warning; the safe direction is
	// "leave the cell at Block".
	for (const FPCGExChannelProfile& Profile : InProfiles)
	{
		const int32 CandidateBit = GetChannelBit(Profile.ChannelKey);
		if (CandidateBit == INDEX_NONE)
		{
			UE_LOG(LogPCGExChannelMatrix, Warning,
			       TEXT("Channel profile references unknown channel '%s'; dropping."),
			       *Profile.ChannelKey.ToString());
			continue;
		}

		for (const FPCGExChannelResponseEntry& Entry : Profile.Responses)
		{
			const int32 StoredBit = GetChannelBit(Entry.StoredChannel);
			if (StoredBit == INDEX_NONE)
			{
				UE_LOG(LogPCGExChannelMatrix, Warning,
				       TEXT("Channel profile '%s' references unknown stored channel '%s'; dropping that response entry."),
				       *Profile.ChannelKey.ToString(), *Entry.StoredChannel.ToString());
				continue;
			}
			Responses[CandidateBit][StoredBit] = Entry.Response;
		}
	}
}

int32 FPCGExChannelInteractionMatrix::GetChannelBit(FName ChannelKey) const
{
	if (ChannelKey.IsNone())
	{
		return INDEX_NONE;
	}
	const int32* Found = KeyToBit.Find(ChannelKey);
	return Found ? *Found : INDEX_NONE;
}

bool FPCGExChannelInteractionMatrix::ShouldRunNarrowPhase(uint32 CandidateMask, uint32 StoredMask) const
{
	// Safe default: no channel info on either side means we run narrow phase
	// (preserves pre-channel-matrix behavior for un-channeled entries / tests).
	if (CandidateMask == 0 || StoredMask == 0)
	{
		return true;
	}

	// Walk the cross-product of set bits. First Block wins -- as soon as any
	// (candidate, stored) channel pair is Block, the narrow phase has to run.
	// Only the all-Ignore case lets us skip.
	uint32 CMask = CandidateMask;
	while (CMask != 0)
	{
		const uint32 CBit = FMath::CountTrailingZeros(CMask);
		CMask &= CMask - 1; // clear lowest set bit

		uint32 SMask = StoredMask;
		while (SMask != 0)
		{
			const uint32 SBit = FMath::CountTrailingZeros(SMask);
			SMask &= SMask - 1;

			if (Responses[CBit][SBit] == EPCGExChannelResponse::Block)
			{
				return true;
			}
		}
	}

	return false;
}
