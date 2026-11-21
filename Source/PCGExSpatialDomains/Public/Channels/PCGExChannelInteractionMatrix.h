// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExChannelInteractionMatrix.generated.h"

/**
 * Per-pair response in the channel-interaction matrix. UE-collision-style:
 * Block = run the narrow-phase overlap test (the candidate-vs-stored pair is
 * "interested" in each other); Ignore = skip the narrow phase entirely, the
 * pair has no spatial relationship.
 *
 * Block and Ignore only -- the middle "Overlap" state from UE collision
 * (record but don't reject) doesn't fit placement's binary accept/reject;
 * we'd add it if a trigger-style use case appears.
 */
UENUM(BlueprintType)
enum class EPCGExChannelResponse : uint8
{
	/** Pair runs the narrow-phase overlap test (default). */
	Block = 0,
	/** Pair is skipped entirely; never runs narrow phase. */
	Ignore = 1,
};

/**
 * One channel-response override in a profile. "When THIS profile's channel
 * is the candidate, and the stored entry's channel is StoredChannel, the
 * response is Response." Asymmetric by design: the directional read matters;
 * `(A → B)` may differ from `(B → A)`.
 */
USTRUCT(BlueprintType)
struct PCGEXSPATIALDOMAINS_API FPCGExChannelResponseEntry
{
	GENERATED_BODY()

	/** The stored channel the candidate is testing against. */
	UPROPERTY(EditAnywhere, Category = "Response",
		meta = (GetOptions = "/Script/PCGExSpatialDomains.PCGExSpatialDomainsSettings.GetChannelKeyOptions"))
	FName StoredChannel = NAME_None;

	/** Response when a candidate of THIS profile's channel hits a stored entry on StoredChannel. */
	UPROPERTY(EditAnywhere, Category = "Response")
	EPCGExChannelResponse Response = EPCGExChannelResponse::Block;
};

/**
 * Per-channel response profile. One profile per registered channel
 * declares how that channel interacts with each of the others (as the
 * CANDIDATE -- the "moving thing" being placed). Unlisted entries default
 * to Block; the runtime matrix initialises all pairs to Block before
 * applying these overrides.
 */
USTRUCT(BlueprintType)
struct PCGEXSPATIALDOMAINS_API FPCGExChannelProfile
{
	GENERATED_BODY()

	/** The candidate channel this profile describes. */
	UPROPERTY(EditAnywhere, Category = "Profile",
		meta = (GetOptions = "/Script/PCGExSpatialDomains.PCGExSpatialDomainsSettings.GetChannelKeyOptions"))
	FName ChannelKey = NAME_None;

	/** Per-stored-channel response overrides. Missing entries default to Block. */
	UPROPERTY(EditAnywhere, Category = "Profile")
	TArray<FPCGExChannelResponseEntry> Responses;
};

/**
 * Compiled runtime form of the channel-interaction matrix. Built from
 * `(ChannelKeys, Profiles)` once at editor-time (or module-init); read-only
 * during placement queries -- the broadphase consults it before invoking
 * the narrow phase to skip Ignored pairs at the cheapest possible step.
 *
 * Storage:
 *   - ChannelKeys[i] = the channel name at bit index i (max 32).
 *   - KeyToBit[key]  = O(1) reverse lookup from name to bit index.
 *   - Responses[from][to] = the response when a candidate at bit `from`
 *     hits a stored entry at bit `to`.
 *
 * Hot-path lookup: ShouldRunNarrowPhase(CandidateMask, StoredMask) ORs across
 * the cross-product of set bits; first Block returns true. For the dominant
 * single-channel-per-side case this is two CountTrailingZeros + one table
 * read.
 *
 * Lives outside USTRUCT reflection (plain C++ class) -- it's runtime-only
 * derived state, never serialised. The authoring (ChannelKeys + Profiles)
 * lives on UPCGExSpatialDomainsSettings; this is the compiled output.
 */
class PCGEXSPATIALDOMAINS_API FPCGExChannelInteractionMatrix
{
public:
	/** Maximum number of registered channels. Cap from uint32 mask width. */
	static constexpr int32 MaxChannels = 32;

	FPCGExChannelInteractionMatrix();

	/**
	 * Compile from authoring inputs. Each ChannelKey entry's array position
	 * becomes its bit index. All response cells default to Block; each
	 * Profile's entries overlay specific (candidate, stored) pairs with the
	 * authored response. Profiles or entries referring to unknown channel
	 * keys are dropped with a warning.
	 *
	 * Idempotent: safe to call from PostInitProperties / PostLoad /
	 * PostEditChangeProperty without worrying about residual state.
	 */
	void Compile(
		TConstArrayView<FName> InChannelKeys,
		TConstArrayView<FPCGExChannelProfile> InProfiles);

	/** Resolve a channel name to its bit index. INDEX_NONE if not registered. O(1). */
	int32 GetChannelBit(FName ChannelKey) const;

	/**
	 * Build a single-channel mask. Returns 0 when the key isn't registered --
	 * callers should treat a 0 mask as "no channel info" and fall back to
	 * the safe default (run narrow phase / Block).
	 */
	FORCEINLINE uint32 MakeMask(FName ChannelKey) const
	{
		const int32 Bit = GetChannelBit(ChannelKey);
		return Bit == INDEX_NONE ? 0u : (1u << Bit);
	}

	/**
	 * Hot-path query. Returns true when at least one (candidate-bit, stored-bit)
	 * pair across the cross-product of set bits resolves to Block -- the narrow
	 * phase runs. Returns false only when every pair is Ignored.
	 *
	 * Safe-default semantic: if either mask is 0 (no channel info), returns
	 * true. This keeps placements working even when channel plumbing hasn't
	 * caught up to a new shape kind or test fixture.
	 */
	bool ShouldRunNarrowPhase(uint32 CandidateMask, uint32 StoredMask) const;

	/** Inspection -- count of registered channels (= ChannelKeys.Num(), capped to MaxChannels). */
	int32 GetNumChannels() const
	{
		return ChannelKeys.Num();
	}

private:
	/** Channel name at each bit index. Index = bit position. Bounded by MaxChannels. */
	TArray<FName> ChannelKeys;

	/** Reverse lookup name -> bit. Rebuilt in Compile() alongside ChannelKeys; case-insensitive. */
	TMap<FName, int32> KeyToBit;

	/**
	 * Response table. Responses[CandidateBit][StoredBit] is the response when
	 * the candidate's channel is at bit CandidateBit and the stored entry's
	 * channel is at bit StoredBit. Asymmetric: [a][b] may differ from [b][a].
	 */
	EPCGExChannelResponse Responses[MaxChannels][MaxChannels];
};
