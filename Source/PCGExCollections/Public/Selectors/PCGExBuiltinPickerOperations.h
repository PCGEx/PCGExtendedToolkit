// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGExStagingDetails.h"
#include "Selectors/PCGExEntryPickerOperation.h"
#include "Selectors/PCGExMicroEntryPickerOperation.h"

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

/**
 * Built-in entry and micro picker operations. Shared between:
 *   - The three concrete built-in factories (PCGExDistributionIndex/Random/WeightedRandom)
 *   - UPCGExSelectorFactoryData::CreateMicroOperation which dispatches on
 *     BaseConfig.EntryDistribution.Distribution
 *
 * These operations are internal implementation; they intentionally have no reflection
 * (non-UObject, plain C++) so they stay out of the hot-path virtual-dispatch bill.
 */

// Entry pickers --------------------------------------------------------------

/** Weighted random pick on the bound FCategory target. */
class FPCGExEntryWeightedRandomPickerOp : public FPCGExEntryPickerOperation
{
public:
	virtual int32 Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch = nullptr) const override;
	virtual int32 PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch = nullptr) const override;
};

/** Uniform random pick on the bound FCategory target. */
class FPCGExEntryRandomPickerOp : public FPCGExEntryPickerOperation
{
public:
	virtual int32 Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch = nullptr) const override;
	virtual int32 PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch = nullptr) const override;
};

/**
 * Attribute-driven index pick. Reads a per-point index from IndexConfig.IndexSource,
 * optionally remaps to collection size, then sanitizes and applies PickMode on the target.
 *
 * No PickFiltered override BY DESIGN: an explicitly-indexed entry that is quota-exhausted is
 * correctly "no pick" -- the inherited veto fallback detects the deterministic miss and
 * returns -1 rather than substituting a neighbor the user didn't ask for.
 */
class FPCGExEntryIndexPickerOp : public FPCGExEntryPickerOperation
{
public:
	FPCGExAssetDistributionIndexDetails IndexConfig;

	TSharedPtr<PCGExDetails::TSettingValue<double>> IndexGetter;
	double MaxInputIndex = 0.0;

	virtual bool PrepareForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade, PCGExAssetCollection::FCategory* InTarget, const UPCGExAssetCollection* InOwningCollection) override;
	virtual int32 Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch = nullptr) const override;
};

// Micro pickers --------------------------------------------------------------

/** Weighted random pick on a given FMicroCache (target passed per call). */
class FPCGExMicroWeightedRandomPickerOp : public FPCGExMicroEntryPickerOperation
{
public:
	virtual int32 Pick(const PCGExAssetCollection::FMicroCache* InMicroCache, int32 PointIndex, int32 Seed) const override;
};

/** Uniform random pick on a given FMicroCache. */
class FPCGExMicroRandomPickerOp : public FPCGExMicroEntryPickerOperation
{
public:
	virtual int32 Pick(const PCGExAssetCollection::FMicroCache* InMicroCache, int32 PointIndex, int32 Seed) const override;
};

/** Attribute-driven index pick on a given FMicroCache. */
class FPCGExMicroIndexPickerOp : public FPCGExMicroEntryPickerOperation
{
public:
	FPCGExAssetDistributionIndexDetails IndexConfig;

	TSharedPtr<PCGExDetails::TSettingValue<double>> IndexGetter;
	double MaxInputIndex = 0.0;

	virtual bool PrepareForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade) override;
	virtual int32 Pick(const PCGExAssetCollection::FMicroCache* InMicroCache, int32 PointIndex, int32 Seed) const override;
};
