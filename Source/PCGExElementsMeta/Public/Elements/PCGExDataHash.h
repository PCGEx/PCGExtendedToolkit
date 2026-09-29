// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGElement.h"
#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "PCGExVersion.h"
#include "Core/PCGExSettings.h"

#include "PCGExDataHash.generated.h"

UENUM(BlueprintType)
enum class EPCGExDataHashType : uint8
{
	Bool       = 0 UMETA(DisplayName = "Bool"),
	Int32      = 1 UMETA(DisplayName = "Int32"),
	Int64      = 2 UMETA(DisplayName = "Int64"),
	Float      = 3 UMETA(DisplayName = "Float"),
	Double     = 4 UMETA(DisplayName = "Double"),
	Vector2    = 5 UMETA(DisplayName = "Vector2"),
	Vector     = 6 UMETA(DisplayName = "Vector"),
	Vector4    = 7 UMETA(DisplayName = "Vector4"),
	Quaternion = 8 UMETA(DisplayName = "Quaternion"),
	Rotator    = 9 UMETA(DisplayName = "Rotator"),
	Transform  = 10 UMETA(DisplayName = "Transform"),
};

namespace PCGExDataHash
{
	const FName OutputValueLabel = FName("Value");
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), meta=(PCGExNodeLibraryDoc="metadata/analyze/data-hash"))
class UPCGExDataHashSettings : public UPCGExSettings
{
	GENERATED_BODY()

	friend class FPCGExDataHashElement;

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(DataHash, "Data Hash", "Generates a single deterministic random value from any combination of input data (count, type, bounds).", FName(GetEnumDisplayName()));

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Param;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Constant);
	}

	virtual bool ShouldDrawNodeCompact() const override
	{
		return true;
	}

	FString GetEnumDisplayName() const;
#endif

#if PCGEX_ENGINE_VERSION >= 507
	virtual FPCGDataTypeIdentifier GetCurrentPinTypesID(const UPCGPin* InPin) const override;
#endif

protected:
	virtual bool HasDynamicPins() const override
	{
		return true;
	}

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Output value type. Drives the output pin's metadata type. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExDataHashType OutputType = EPCGExDataHashType::Int32;

	/** Name of the attribute the random value is written to on the output Param Data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName OutputAttributeName = FName("Value");

	/** Extra integer salt mixed into the hash. Change this to re-roll the value without touching upstream inputs. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	int32 Salt = 0;

	/** If enabled, clamps the random value to the user-defined [Min,Max] range. If disabled, defaults to [-MAX,MAX] for integer types, [0,1] for Transform scale, and [-1,1] for everything else. Ignored for Bool. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bUseRange = false;

	/** Minimum value (inclusive). For vector / quat / rotator / transform types, applied per-component. For Quaternion, treated as Euler degrees on each axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bUseRange"))
	double RangeMin = -1.0;

	/** Maximum value (inclusive). For vector / quat / rotator / transform types, applied per-component. For Quaternion, treated as Euler degrees on each axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bUseRange"))
	double RangeMax = 1.0;
};

class FPCGExDataHashElement final : public IPCGElement
{
public:
	// IPCGElement defaults to false, and PrepareData then converts point inputs to legacy UPCGPointData.
	virtual bool SupportsBasePointDataInputs(FPCGContext* InContext) const override
	{
		return true;
	}

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
