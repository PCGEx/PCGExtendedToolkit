// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "PCGExSettingsMacros.h"
#include "Data/PCGExDataCommon.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGExInputShorthandsDetails.generated.h"

struct FPCGExContext;
class UPCGSettings;
class UPCGNode;

namespace PCGExData
{
	class FPointIO;
	class FFacadePreloader;
}

#if WITH_EDITOR
namespace PCGExDeprecation
{
	/**
	 * Rewires the serialized override pin of a removed property onto the current override param whose
	 * PropertiesNames end with [InMemberName, InLeafName]. New labels are resolved from the settings'
	 * freshly gathered OverridableParams — the exact source UpdatePins builds pins from — so this stays
	 * correct across bare/member-path/Config-path label variants (see .claude/Shorthand_Migration_Plan.md).
	 * The old pin is matched by exact label, else by unique segment-qualified ".../InOldName", else by
	 * legacy display-name labels (explicit InOldDisplayName, then the engine-default prettified name) —
	 * very old assets carry pins labeled by GetDisplayNameText from before PCG switched to authored names.
	 * Call from PCGExApplyDeprecationBeforeUpdatePins; safely no-ops when either side is absent.
	 */
	PCGEXCORE_API void RenameShorthandOverridePin(const UPCGSettings* InSettings, UPCGNode* InOutNode, FName InOldName, FName InMemberName, FName InLeafName, FName InOldDisplayName = NAME_None);

	/** Suffix-explicit variant for ambiguous embeddings (same member/leaf tail reachable through two paths). */
	PCGEXCORE_API void RenameShorthandOverridePin(const UPCGSettings* InSettings, UPCGNode* InOutNode, FName InOldName, TArrayView<const FName> InNewPathSuffix, FName InOldDisplayName = NAME_None);
}
#endif

/**
 * Base struct for input shorthands that can read from constant or attribute.
 * Derived types add typed Constant and Attribute fields.
 */
USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandBase
{
	GENERATED_BODY()

	FPCGExInputShorthandBase() = default;

	/** Whether to use the constant value or read from an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExInputValueType Input = EPCGExInputValueType::Constant;

	/** Allow this attribute to be registered as consumable, i.e. deleted from the consuming node's outputs when its 'Cleanup Consumable Attributes' toggle is enabled. Disable to always preserve the attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="Input == EPCGExInputValueType::Attribute"))
	bool bCleanupAttribute = true;

	void RegisterBufferDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
	{
	}
};

#define PCGEX_ATTRIBUTE_TOGGLE_DECL const bool bDefaultToAttribute = false
#define PCGEX_ATTRIBUTE_TOGGLE Input = bDefaultToAttribute ? EPCGExInputValueType::Attribute : EPCGExInputValueType::Constant;

#define PCGEX_SHORTHAND_UPDATE_DECL(_NAME, _TYPE)\
void Update(EPCGExInputValueType InInputType, const FPCGAttributePropertyInputSelector& InSelector, _TYPE InConstant);\
void Update(EPCGExInputValueType InInputType, FName InSelector, _TYPE InConstant);\
bool CanSupportDataOnly() const;

// Renames old separate override pins to new shorthand struct pins during deprecation.
// Target labels are resolved from the live OverridableParams (never hardcoded — nested/Config
// wrapping and clash disambiguation make static labels unreliable). Expects to expand inside
// PCGExApplyDeprecationBeforeUpdatePins (uses `this` and `InOutNode`).
// _OLD_ATTR:     C++ name of the old attribute property  (e.g. RangeMinAttribute)
// _OLD_CONST:    C++ name of the old constant property    (e.g. RangeMin)
// _NEW_MEMBER:   C++ name of the new shorthand struct member (e.g. MinRange)
#define PCGEX_SHORTHAND_RENAME_PIN(_OLD_ATTR, _OLD_CONST, _NEW_MEMBER)\
PCGExDeprecation::RenameShorthandOverridePin(this, InOutNode, FName(TEXT(#_OLD_ATTR)), FName(TEXT(#_NEW_MEMBER)), FName(TEXT("Attribute")));\
PCGExDeprecation::RenameShorthandOverridePin(this, InOutNode, FName(TEXT(#_OLD_CONST)), FName(TEXT(#_NEW_MEMBER)), FName(TEXT("Constant")));

#pragma region Name

#define PCGEX_SHORTHAND_NAME_CTR(_NAME, _TYPE) \
FPCGExInputShorthandName##_NAME() = default; \
explicit FPCGExInputShorthandName##_NAME(const FName DefaultName, PCGEX_ATTRIBUTE_TOGGLE_DECL) { Attribute = DefaultName; PCGEX_ATTRIBUTE_TOGGLE  } \
FPCGExInputShorthandName##_NAME(const FName DefaultName, const _TYPE DefaultValue, PCGEX_ATTRIBUTE_TOGGLE_DECL){	Attribute = DefaultName; Constant = DefaultValue; PCGEX_ATTRIBUTE_TOGGLE } \
bool TryReadDataValue(const TSharedPtr<PCGExData::FPointIO>& IO, _TYPE& OutValue, const bool bQuiet = false) const;\
bool TryReadDataValue(FPCGExContext* InContext, const UPCGData* InData, _TYPE& OutValue, const bool bQuiet = false) const;\
PCGEX_SETTING_VALUE_DECL(, _TYPE)\
PCGEX_SETTING_DATA_VALUE_DECL(, _TYPE)\
void RegisterBufferDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const;\
PCGEX_SHORTHAND_UPDATE_DECL(_NAME, _TYPE)

/** Name-based shorthand where attribute is specified by FName. */
USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameBase : public FPCGExInputShorthandBase
{
	GENERATED_BODY()

	FPCGExInputShorthandNameBase() = default;

	/** Attribute name to read from when Input is set to Attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FName Attribute = NAME_None;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameBoolean : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(Boolean, bool)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool Constant = false;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameFloat : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(Float, float)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	float Constant = 0;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameDouble : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(Double, double)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	double Constant = 0;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameDoubleAbs : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(DoubleAbs, double)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin=0, UIMin=0))
	double Constant = 0;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameDouble01 : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(Double01, double)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin=0, UIMin=0, ClampMax=1, UIMax=1))
	double Constant = 0;
};

/** Signed unit-range shorthand (-1..1). */
USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameDouble11 : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(Double11, double)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin=-1, UIMin=-1, ClampMax=1, UIMax=1))
	double Constant = 0;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameInteger32 : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(Integer32, int32)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	int32 Constant = 0;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameInteger32Abs : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(Integer32Abs, int32)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin=0, UIMin=0))
	int32 Constant = 0;
};


USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameInteger3201 : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(Integer3201, int32)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin=0, UIMin=0, ClampMax=1, UIMax=1))
	int32 Constant = 0;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameInteger64 : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(Integer64, int64)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	int64 Constant = 0;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameVector2 : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(Vector2, FVector2D)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FVector2D Constant = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameVector : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(Vector, FVector)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FVector Constant = FVector::ZeroVector;
};

/** Direction vector shorthand with optional inversion. */
USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameDirection : public FPCGExInputShorthandNameVector
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(Direction, FVector)

	/** Negate the direction vector after reading. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bFlip = false;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameVector4 : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(Vector4, FVector4)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FVector4 Constant = FVector4(0, 0, 0, 1);
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameRotator : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(Rotator, FRotator)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FRotator Constant = FRotator::ZeroRotator;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameTransform : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(Transform, FTransform)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FTransform Constant = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameString : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(String, FString)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FString Constant = TEXT("");
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameName : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(Name, FName)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FName Constant = NAME_None;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandNameSoftObjectPath : public FPCGExInputShorthandNameBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_NAME_CTR(SoftObjectPath, FSoftObjectPath)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FSoftObjectPath Constant;
};

#pragma endregion

#pragma region Selector

#define PCGEX_SHORTHAND_SELECTOR_CTR(_NAME, _TYPE) \
FPCGExInputShorthandSelector##_NAME() = default;\
explicit FPCGExInputShorthandSelector##_NAME(const FString& DefaultSelection, PCGEX_ATTRIBUTE_TOGGLE_DECL) { Attribute.Update(DefaultSelection); PCGEX_ATTRIBUTE_TOGGLE } \
explicit FPCGExInputShorthandSelector##_NAME(const FName& DefaultSelection, PCGEX_ATTRIBUTE_TOGGLE_DECL) { Attribute.SetAttributeName(DefaultSelection); PCGEX_ATTRIBUTE_TOGGLE } \
explicit FPCGExInputShorthandSelector##_NAME(const FPCGAttributePropertyInputSelector& DefaultSelector, const _TYPE DefaultValue, PCGEX_ATTRIBUTE_TOGGLE_DECL) { Attribute = DefaultSelector; Constant = DefaultValue; PCGEX_ATTRIBUTE_TOGGLE } \
FPCGExInputShorthandSelector##_NAME(const FString& DefaultSelection, const _TYPE DefaultValue, PCGEX_ATTRIBUTE_TOGGLE_DECL){ Attribute.Update(DefaultSelection);	Constant = DefaultValue; PCGEX_ATTRIBUTE_TOGGLE } \
FPCGExInputShorthandSelector##_NAME(const FName& DefaultSelection, const _TYPE DefaultValue, PCGEX_ATTRIBUTE_TOGGLE_DECL){ Attribute.Update(DefaultSelection.ToString()); Constant = DefaultValue; PCGEX_ATTRIBUTE_TOGGLE  } \
bool TryReadDataValue(const TSharedPtr<PCGExData::FPointIO>& IO, _TYPE& OutValue, const bool bQuiet = false) const;\
bool TryReadDataValue(FPCGExContext* InContext, const UPCGData* InData, _TYPE& OutValue, const bool bQuiet = false) const;\
PCGEX_SETTING_VALUE_DECL(, _TYPE)\
PCGEX_SETTING_DATA_VALUE_DECL(, _TYPE)\
void RegisterBufferDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const;\
PCGEX_SHORTHAND_UPDATE_DECL(_NAME, _TYPE)


/** Selector-based shorthand where attribute is specified by full PCG selector (supports properties). */
USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorBase : public FPCGExInputShorthandBase
{
	GENERATED_BODY()

	FPCGExInputShorthandSelectorBase() = default;

	/** Attribute or property selector to read from when Input is set to Attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGAttributePropertyInputSelector Attribute;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorBoolean : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(Boolean, bool)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool Constant = false;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorFloat : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(Float, float)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	float Constant = 0;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorDouble : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(Double, double)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	double Constant = 0;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorDoubleAbs : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(DoubleAbs, double)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin=0, UIMin=0))
	double Constant = 0;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorDouble01 : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(Double01, double)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin=0, UIMin=0, ClampMax=1, UIMax=1))
	double Constant = 0;
};

/** Signed unit-range shorthand (-1..1). */
USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorDouble11 : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(Double11, double)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin=-1, UIMin=-1, ClampMax=1, UIMax=1))
	double Constant = 0;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorInteger32 : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(Integer32, int32)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	int32 Constant = 0;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorInteger32Abs : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(Integer32Abs, int32)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin=0, UIMin=0))
	int32 Constant = 0;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorInteger3201 : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(Integer3201, int32)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin=0, UIMin=0, ClampMax=1, UIMax=1))
	int32 Constant = 0;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorInteger64 : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(Integer64, int64)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	int64 Constant = 0;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorVector2 : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(Vector2, FVector2D)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FVector2D Constant = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorVector : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(Vector, FVector)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FVector Constant = FVector::ZeroVector;
};

/** Direction vector shorthand with optional inversion. */
USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorDirection : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(Direction, FVector)

	/** Direction vector when using constant input. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FVector Constant = FVector::ZeroVector;

	/** Negate the direction vector after reading. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bFlip = false;
};


USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorVector4 : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(Vector4, FVector4)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FVector4 Constant = FVector4(0, 0, 0, 1);
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorRotator : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(Rotator, FRotator)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FRotator Constant = FRotator::ZeroRotator;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorTransform : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(Transform, FTransform)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FTransform Constant = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorString : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(String, FString)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FString Constant = TEXT("");
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorName : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(Name, FName)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FName Constant = NAME_None;
};

#pragma region DUMMIES

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputShorthandSelectorSoftObjectPath : public FPCGExInputShorthandSelectorBase
{
	GENERATED_BODY()

	PCGEX_SHORTHAND_SELECTOR_CTR(SoftObjectPath, FSoftObjectPath)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FSoftObjectPath Constant;
};

#pragma endregion

#pragma endregion

#undef PCGEX_ATTRIBUTE_TOGGLE
#undef PCGEX_SHORTHAND_NAME_CTR
#undef PCGEX_SHORTHAND_SELECTOR_CTR
