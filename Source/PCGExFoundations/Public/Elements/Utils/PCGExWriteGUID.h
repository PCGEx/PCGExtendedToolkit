// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExPointsProcessor.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"

#include "Misc/Guid.h"

#include "PCGExWriteGUID.generated.h"

namespace PCGExData
{
	struct FConstPoint;
}

UENUM(meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true", DisplayName="[PCGEx] GUID Uniqueness Flags"))
enum class EPCGExGUIDUniquenessFlags : uint8
{
	None     = 0,
	Index    = 1 << 0 UMETA(DisplayName = "Index", ToolTip="Uses point index as a marker of uniqueness"),
	Position = 1 << 1 UMETA(DisplayName = "Position", ToolTip="Uses point position as a marker of uniqueness"),
	Seed     = 1 << 2 UMETA(DisplayName = "Seed", ToolTip="Uses point seed as a marker of uniqueness"),
	Grid     = 1 << 3 UMETA(DisplayName = "Grid", ToolTip="Uses PCG component Grid as a marker of uniqueness"),
	All      = Position | Seed | Index | Grid UMETA(DisplayName = "All"),
};

ENUM_CLASS_FLAGS(EPCGExGUIDUniquenessFlags)
using EPCGExGUIDUniquenessFlagsBitmask = TEnumAsByte<EPCGExGUIDUniquenessFlags>;

UENUM()
enum class EPCGExGUIDOutputType : uint8
{
	Integer = 0 UMETA(DisplayName = "Integer", ToolTip="..."),
	String  = 1 UMETA(DisplayName = "String", ToolTip="..."),
};

UENUM()
enum class EPCGExGUIDFormat : uint8
{
	Digits                         = 0 UMETA(DisplayName = "Digits", ToolTip="32 digits. For example: `00000000000000000000000000000000`"),
	DigitsLower                    = 1 UMETA(DisplayName = "Digits (Lowercase)", ToolTip="32 digits in lowercase. For example: `0123abc456def789abcd123ef4a5b6c7`"),
	DigitsWithHyphens              = 2 UMETA(DisplayName = "Digits (Hyphens)", ToolTip="32 digits separated by hyphens. For example: 00000000-0000-0000-0000-000000000000"),
	DigitsWithHyphensLower         = 3 UMETA(DisplayName = "Digits (RFC 4122)", ToolTip="32 digits separated by hyphens, in lowercase as described by RFC 4122. For example: bd048ce3-358b-46c5-8cee-627c719418f8"),
	DigitsWithHyphensInBraces      = 4 UMETA(DisplayName = "{Digits}", ToolTip="32 digits separated by hyphens and enclosed in braces. For example: {00000000-0000-0000-0000-000000000000}"),
	DigitsWithHyphensInParentheses = 5 UMETA(DisplayName = "(Digits)", ToolTip="32 digits separated by hyphens and enclosed in parentheses. For example: (00000000-0000-0000-0000-000000000000)"),
	HexValuesInBraces              = 6 UMETA(DisplayName = "{Hex}", ToolTip="Comma-separated hexadecimal values enclosed in braces. For example: {0x00000000,0x0000,0x0000,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}}"),
	UniqueObjectGuid               = 7 UMETA(DisplayName = "Unique Object GUID", ToolTip="This format is currently used by the FUniqueObjectGuid class. For example: 00000000-00000000-00000000-00000000"),
	Short                          = 8 UMETA(DisplayName = "Short (Base64)", ToolTip="Base64 characters with dashes and underscores instead of pluses and slashes (respectively), For example: AQsMCQ0PAAUKCgQEBAgADQ"),
	Base36Encoded                  = 9 UMETA(DisplayName = "Short (Base36)", ToolTip="Base-36 encoded, compatible with case-insensitive OS file systems (such as Windows). For example: 1DPF6ARFCM4XH5RMWPU8TGR0J"),
};

USTRUCT(BlueprintType)
struct PCGEXFOUNDATIONS_API FPCGExGUIDDetails
{
	GENERATED_BODY()

	FPCGExGUIDDetails()
	{
	}

	/** The name of the attribute to write its index to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName OutputAttributeName = "GUID";

	/** Output type.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExGUIDOutputType OutputType = EPCGExGUIDOutputType::Integer;

	/** Output format. Still relevant for integer, as the integer value is the TypeHash of the GUID String.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExGUIDFormat Format = EPCGExGUIDFormat::Digits;

	/** What components are used for Uniqueness. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, Bitmask, BitmaskEnum="/Script/PCGExFoundations.EPCGExGUIDUniquenessFlags"))
	uint8 Uniqueness = static_cast<uint8>(EPCGExGUIDUniquenessFlags::All);

	/** A base value for the GUID. Treat it like a seed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorInteger32Abs UniqueKey = FPCGExInputShorthandSelectorInteger32Abs(FName("@Last"), 42, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType UniqueKeyInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector UniqueKeyAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	int32 UniqueKeyConstant_DEPRECATED = 42;

#pragma endregion

	EGuidFormats GUIDFormat = EGuidFormats::Digits;
	TSharedPtr<PCGExDetails::TSettingValue<int32>> UniqueKeyReader;

	uint32 GridHash = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Hashes", meta=(PCG_Overridable, ClampMin=0.000001))
	FVector GridHashCollision = FVector(0.001);
	FVector AdjustedGridHashCollision = FVector(0.001);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Hashes", meta=(PCG_Overridable, ClampMin=0.000001))
	FVector PositionHashCollision = FVector(0.001);
	FVector AdjustedPositionHashCollision = FVector(0.001);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Hashes", meta=(PCG_Overridable))
	FVector PositionHashOffset = FVector(0);

	/** Whether the created attribute allows interpolation or not.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bAllowInterpolation = true;

	bool bUseIndex = false;
	bool bUseSeed = false;
	bool bUsePosition = false;

	FGuid DefaultGUID = FGuid(0, 0, 0, 0);

	bool Init(FPCGExContext* InContext, TSharedRef<PCGExData::FFacade>& InFacade);
	void GetGUID(const int32 Index, const PCGExData::FConstPoint& InPoint, FGuid& OutGUID) const;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="metadata/keys/write-guid"))
class UPCGExWriteGUIDSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(WriteGUID, "Write GUID", "Write a GUID on the point.", Config.OutputAttributeName);

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscWrite);
	}
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

public:
	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	/** Config */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExGUIDDetails Config;
};

struct FPCGExWriteGUIDContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExWriteGUIDElement;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExWriteGUIDElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(WriteGUID)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExWriteGUID
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExWriteGUIDContext, UPCGExWriteGUIDSettings>
	{
		FPCGExGUIDDetails Config;

		TSharedPtr<PCGExData::TBuffer<FString>> StringGUIDWriter;
		TSharedPtr<PCGExData::TBuffer<int32>> IntegerGUIDWriter;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void CompleteWork() override;
	};
}
