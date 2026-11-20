// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"

#include "PCGExSplitJoinInteger.generated.h"

class UPCGData;

UENUM(BlueprintType)
enum class EPCGExSplitJoinIntegerMode : uint8
{
	Split = 0 UMETA(DisplayName = "Split", ToolTip="Read the packed attribute and write one int32 attribute per named layout field."),
	Join  = 1 UMETA(DisplayName = "Join", ToolTip="Read the layout's field attributes and write the packed attribute."),
};

UENUM(BlueprintType)
enum class EPCGExPackedIntegerType : uint8
{
	Int32 = 0 UMETA(DisplayName = "Int32", ToolTip="32-bit integer. The layout may take up to 32 bits."),
	Int64 = 1 UMETA(DisplayName = "Int64", ToolTip="64-bit integer. The layout may take up to 64 bits."),
};

USTRUCT(BlueprintType)
struct PCGEXELEMENTSMETA_API FPCGExIntegerLayoutField
{
	GENERATED_BODY()

	FPCGExIntegerLayoutField() = default;

	FPCGExIntegerLayoutField(const FName InName, const int32 InNumBits)
		: Name(InName), NumBits(InNumBits)
	{
	}

	/** Attribute holding this field. None reserves the bits without reading or writing any attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName Name = NAME_None;

	/** Number of bits this field takes in the packed integer. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ClampMin=1, ClampMax=32, UIMin=1, UIMax=32))
	int32 NumBits = 32;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(Keywords = "split join pack unpack bits integer int32 int64 gpu", PCGExNodeLibraryDoc="metadata/modify/split-join-integer"))
class UPCGExSplitJoinIntegerSettings : public UPCGExSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(SplitJoinInteger, "Split / Join Integer", "Split an integer attribute into smaller integer attributes following a bit layout, or join them back.", FName(GetDisplayName()));

	virtual TArray<FText> GetNodeTitleAliases() const override;

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscWrite);
	}
#endif

	virtual bool HasDynamicPins() const override
	{
		return true;
	}

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Whether to split the packed attribute into its fields, or join the fields into the packed attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExSplitJoinIntegerMode Mode = EPCGExSplitJoinIntegerMode::Split;

	/** The integer attribute that holds every field: read by Split (int32 or int64), written by Join. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName PackedAttribute = FName("PCGEx/CollectionEntry");

	/** Type of the packed attribute Join writes. An attribute already using that name is replaced. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="Mode == EPCGExSplitJoinIntegerMode::Join", EditConditionHides))
	EPCGExPackedIntegerType PackedType = EPCGExPackedIntegerType::Int64;

	/** Fields of the packed integer. The first entry takes the most significant bits, the last one ends on bit 0. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	TArray<FPCGExIntegerLayoutField> Layout = {FPCGExIntegerLayoutField(FName("CollectionEntryHi"), 32), FPCGExIntegerLayoutField(FName("CollectionEntryLo"), 32)};

#if WITH_EDITOR
	/** Node title for the current Mode. */
	FString GetDisplayName() const;
#endif
};

namespace PCGExSplitJoinInteger
{
	constexpr int32 MinFieldBits = 1;
	constexpr int32 MaxFieldBits = 32;

	/** A named layout field, resolved to its position in the packed integer. Mask covers the field's width from bit 0. */
	struct FField
	{
		FName Name = NAME_None;
		uint64 Mask = 0;
		int32 Shift = 0;
	};

	/** Sum of the bits every entry takes, unnamed ones included. */
	PCGEXELEMENTSMETA_API int32 GetTotalBits(TConstArrayView<FPCGExIntegerLayoutField> InLayout);

	/** Resolves the named entries of InLayout: the first entry takes the most significant bits, the last one ends on bit 0.
	 *  False, with OutError set, when an entry is not 1..32 bits, the total exceeds InMaxBits, a name repeats or none is set. */
	PCGEXELEMENTSMETA_API bool ResolveLayout(TConstArrayView<FPCGExIntegerLayoutField> InLayout, int32 InMaxBits, TArray<FField>& OutFields, FText& OutError);

	/** The field's value, zero-extended. A 32-bit field keeps its bit pattern. */
	FORCEINLINE int32 Extract(const uint64 InPacked, const FField& InField)
	{
		return static_cast<int32>(static_cast<uint32>((InPacked >> InField.Shift) & InField.Mask));
	}

	/** InValue cut to the field's width and moved to the field's position. */
	FORCEINLINE uint64 Insert(const uint64 InValue, const FField& InField)
	{
		return (InValue & InField.Mask) << InField.Shift;
	}

	/** A duplicate of InData with every field of its int32 or int64 InPackedName written as an int32 attribute.
	 *  Null when InData is to be forwarded as is: the packed attribute is missing, not an integer, or could not be read. */
	PCGEXELEMENTSMETA_API UPCGData* Split(FPCGExContext* InContext, FName InPackedName, TConstArrayView<FField> InFields, const UPCGData* InData, int32 InInputIndex);

	/** A duplicate of InData with InPackedName written as an int64, or as an int32 that keeps bits 31..0 only.
	 *  Null when InData is to be forwarded as is: a field attribute is missing, not an integer, or could not be read. */
	PCGEXELEMENTSMETA_API UPCGData* Join(FPCGExContext* InContext, FName InPackedName, TConstArrayView<FField> InFields, bool bInToInt64, const UPCGData* InData, int32 InInputIndex);
}

struct FPCGExSplitJoinIntegerContext final : FPCGExContext
{
	friend class FPCGExSplitJoinIntegerElement;

	/** Named fields of the layout. Resolved in Boot. */
	TArray<PCGExSplitJoinInteger::FField> Fields;

	bool bJoin = false;
	bool bJoinToInt64 = true;
};

class FPCGExSplitJoinIntegerElement final : public IPCGExElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(SplitJoinInteger)
	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
