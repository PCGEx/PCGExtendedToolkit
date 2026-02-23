// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "Data/PCGExDataCommon.h"
#include "Details/PCGExSettingsMacros.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGExCompare.generated.h"

#define PCGEX_UNSUPPORTED_STRING_TYPES(MACRO)\
MACRO(FString)\
MACRO(FName)\
MACRO(FSoftObjectPath)\
MACRO(FSoftClassPath)

#define PCGEX_UNSUPPORTED_PATH_TYPES(MACRO)\
MACRO(FSoftObjectPath)\
MACRO(FSoftClassPath)

struct FPCGExContext;

namespace PCGExData
{
	class FFacade;
	class FTags;
	class IDataValue;
	class FFacadePreloader;
}

UENUM()
enum class EPCGExIndexMode : uint8
{
	Pick   = 0 UMETA(DisplayName = "Pick", ToolTip="Index value represent a specific pick"),
	Offset = 1 UMETA(DisplayName = "Offset", ToolTip="Index value represent an offset from current point' index"),
};

UENUM()
enum class EPCGExAngularDomain : uint8
{
	Scalar  = 0 UMETA(DisplayName = "Scalar", Tooltip="Read the value as the result of a normalized dot product"),
	Degrees = 1 UMETA(DisplayName = "Degrees", Tooltip="Read the value as degrees"),
};

UENUM(BlueprintType)
enum class EPCGExComparison : uint8
{
	StrictlyEqual    = 0 UMETA(DisplayName = " == ", Tooltip="Operand A Strictly Equal to Operand B"),
	StrictlyNotEqual = 1 UMETA(DisplayName = " != ", Tooltip="Operand A Strictly Not Equal to Operand B"),
	EqualOrGreater   = 2 UMETA(DisplayName = " >= ", Tooltip="Operand A Equal or Greater to Operand B"),
	EqualOrSmaller   = 3 UMETA(DisplayName = " <= ", Tooltip="Operand A Equal or Smaller to Operand B"),
	StrictlyGreater  = 4 UMETA(DisplayName = " > ", Tooltip="Operand A Strictly Greater to Operand B"),
	StrictlySmaller  = 5 UMETA(DisplayName = " < ", Tooltip="Operand A Strictly Smaller to Operand B"),
	NearlyEqual      = 6 UMETA(DisplayName = " ~= ", Tooltip="Operand A Nearly Equal to Operand B"),
	NearlyNotEqual   = 7 UMETA(DisplayName = " !~= ", Tooltip="Operand A Nearly Not Equal to Operand B"),
};

UENUM()
enum class EPCGExEquality : uint8
{
	Equal    = 0 UMETA(DisplayName = " == ", Tooltip="Operand A Equal to Operand B"),
	NotEqual = 1 UMETA(DisplayName = " != ", Tooltip="Operand A Not Equal to Operand B"),
};

UENUM(BlueprintType)
enum class EPCGExStringComparison : uint8
{
	StrictlyEqual         = 0 UMETA(DisplayName = " == ", Tooltip="Operand A Strictly Equal to Operand B"),
	StrictlyNotEqual      = 1 UMETA(DisplayName = " != ", Tooltip="Operand A Strictly Not Equal to Operand B"),
	LengthStrictlyEqual   = 2 UMETA(DisplayName = " == (Length) ", Tooltip="Operand A Strictly Equal to Operand B"),
	LengthStrictlyUnequal = 3 UMETA(DisplayName = " != (Length) ", Tooltip="Operand A Strictly Not Equal to Operand B"),
	LengthEqualOrGreater  = 4 UMETA(DisplayName = " >= (Length)", Tooltip="Operand A Equal or Greater to Operand B"),
	LengthEqualOrSmaller  = 5 UMETA(DisplayName = " <= (Length)", Tooltip="Operand A Equal or Smaller to Operand B"),
	StrictlyGreater       = 6 UMETA(DisplayName = " > (Length)", Tooltip="Operand A Strictly Greater to Operand B"),
	StrictlySmaller       = 7 UMETA(DisplayName = " < (Length)", Tooltip="Operand A Strictly Smaller to Operand B"),
	LocaleStrictlyGreater = 8 UMETA(DisplayName = " > (Locale)", Tooltip="Operand A Locale Strictly Greater to Operand B Locale"),
	LocaleStrictlySmaller = 9 UMETA(DisplayName = " < (Locale)", Tooltip="Operand A Locale Strictly Smaller to Operand B Locale"),
	Contains              = 10 UMETA(DisplayName = " Contains ", Tooltip="Operand A contains Operand B"),
	StartsWith            = 11 UMETA(DisplayName = " Starts With ", Tooltip="Operand A starts with Operand B"),
	EndsWith              = 12 UMETA(DisplayName = " Ends With ", Tooltip="Operand A ends with Operand B"),
};

UENUM()
enum class EPCGExStringMatchMode : uint8
{
	Equals     = 0 UMETA(DisplayName = "Equals", ToolTip=""),
	Contains   = 1 UMETA(DisplayName = "Contains", ToolTip=""),
	StartsWith = 2 UMETA(DisplayName = "Starts with", ToolTip=""),
	EndsWith   = 3 UMETA(DisplayName = "Ends with", ToolTip=""),
};

UENUM()
enum class EPCGExComparisonDataType : uint8
{
	Numeric = 0 UMETA(DisplayName = "Numeric", Tooltip="Compare numeric values", ActionIcon="Numeric"),
	String  = 1 UMETA(DisplayName = "String", Tooltip="Compare string values", ActionIcon="Text"),
};

namespace PCGExCompare
{
	PCGEXCORE_API
	FString ToString(const EPCGExComparison Comparison);

	PCGEXCORE_API
	FString ToString(const EPCGExStringComparison Comparison);

	PCGEXCORE_API
	FString ToString(const EPCGExStringMatchMode MatchMode);

	PCGEXCORE_API
	bool Compare(const EPCGExStringComparison Method, const FString& A, const FString& B);

#pragma region Numeric comparisons ops

	template <typename T>
	FORCEINLINE static bool StrictlyEqual(const T& A, const T& B)
	{
		if constexpr (std::is_same_v<T, FVector2D>)
		{
			return A.X == B.X && A.Y == B.Y;
		}
		else if constexpr (std::is_same_v<T, FVector>)
		{
			return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
		}
		else if constexpr (std::is_same_v<T, FVector4>)
		{
			return A.X == B.X && A.Y == B.Y && A.Z == B.Z && A.W == B.W;
		}
		else if constexpr (std::is_same_v<T, FQuat>)
		{
			return A.Equals(B);
		}
		else if constexpr (std::is_same_v<T, FRotator>)
		{
			return A.Equals(B);
		}
		else if constexpr (std::is_same_v<T, FTransform>)
		{
			return A.Equals(B);
		}
		else
		{
			return A == B;
		}
	}

	template <typename T>
	FORCEINLINE static bool StrictlyNotEqual(const T& A, const T& B) { return !StrictlyEqual(A, B); }

	template <typename T>
	FORCEINLINE static bool EqualOrGreater(const T& A, const T& B)
	{
		if constexpr (std::is_same_v<T, bool>)
		{
			return A == B || A;
		}
		else if constexpr (std::is_same_v<T, FVector2D> || std::is_same_v<T, FVector>)
		{
			return A.SquaredLength() >= B.SquaredLength();
		}
		else if constexpr (std::is_same_v<T, FVector4>)
		{
			return EqualOrGreater(FVector(A), FVector(B));
		}
		else if constexpr (std::is_same_v<T, FQuat> || std::is_same_v<T, FRotator>)
		{
			return EqualOrGreater(FVector(A.Euler()), FVector(B.Euler()));
		}
		else if constexpr (std::is_same_v<T, FTransform>)
		{
			return (EqualOrGreater(A.GetLocation(), B.GetLocation()) && EqualOrGreater(A.GetRotation(), B.GetRotation()) && EqualOrGreater(A.GetScale3D(), B.GetScale3D()));
		}
		else if constexpr (std::is_same_v<T, FString>)
		{
			return A.Compare(B, ESearchCase::IgnoreCase) >= 0;
		}
		else if constexpr (std::is_same_v<T, FName> || std::is_same_v<T, FSoftObjectPath> || std::is_same_v<T, FSoftClassPath>)
		{
			return EqualOrGreater(A.ToString(), B.ToString());
		}
		else
		{
			return A >= B;
		}
	}

	template <typename T>
	FORCEINLINE static bool EqualOrSmaller(const T& A, const T& B)
	{
		if constexpr (std::is_same_v<T, bool>)
		{
			return A == B || !A;
		}
		else if constexpr (std::is_same_v<T, FVector2D> || std::is_same_v<T, FVector>)
		{
			return A.SquaredLength() <= B.SquaredLength();
		}
		else if constexpr (std::is_same_v<T, FVector4>)
		{
			return EqualOrSmaller(FVector(A), FVector(B));
		}
		else if constexpr (std::is_same_v<T, FQuat> || std::is_same_v<T, FRotator>)
		{
			return EqualOrSmaller(FVector(A.Euler()), FVector(B.Euler()));
		}
		else if constexpr (std::is_same_v<T, FTransform>)
		{
			return (EqualOrSmaller(A.GetLocation(), B.GetLocation()) && EqualOrSmaller(A.GetRotation(), B.GetRotation()) && EqualOrSmaller(A.GetScale3D(), B.GetScale3D()));
		}
		else if constexpr (std::is_same_v<T, FString>)
		{
			return A.Compare(B, ESearchCase::IgnoreCase) <= 0;
		}
		else if constexpr (std::is_same_v<T, FName> || std::is_same_v<T, FSoftObjectPath> || std::is_same_v<T, FSoftClassPath>)
		{
			return EqualOrSmaller(A.ToString(), B.ToString());
		}
		else
		{
			return A <= B;
		}
	}

	template <typename T>
	FORCEINLINE static bool StrictlyGreater(const T& A, const T& B)
	{
		if constexpr (std::is_same_v<T, bool>)
		{
			return A && !B;
		}
		else if constexpr (std::is_same_v<T, FVector2D> || std::is_same_v<T, FVector>)
		{
			return A.SquaredLength() > B.SquaredLength();
		}
		else if constexpr (std::is_same_v<T, FVector4>)
		{
			return StrictlyGreater(FVector(A), FVector(B));
		}
		else if constexpr (std::is_same_v<T, FQuat> || std::is_same_v<T, FRotator>)
		{
			return StrictlyGreater(FVector(A.Euler()), FVector(B.Euler()));
		}
		else if constexpr (std::is_same_v<T, FTransform>)
		{
			return (StrictlyGreater(A.GetLocation(), B.GetLocation()) && StrictlyGreater(A.GetRotation(), B.GetRotation()) && StrictlyGreater(A.GetScale3D(), B.GetScale3D()));
		}
		else if constexpr (std::is_same_v<T, FString>)
		{
			return A.Compare(B, ESearchCase::IgnoreCase) > 0;
		}
		else if constexpr (std::is_same_v<T, FName> || std::is_same_v<T, FSoftObjectPath> || std::is_same_v<T, FSoftClassPath>)
		{
			return StrictlyGreater(A.ToString(), B.ToString());
		}
		else
		{
			return A > B;
		}
	}

	template <typename T>
	FORCEINLINE static bool StrictlySmaller(const T& A, const T& B)
	{
		if constexpr (std::is_same_v<T, bool>)
		{
			return !A && B;
		}
		else if constexpr (std::is_same_v<T, FVector2D> || std::is_same_v<T, FVector>)
		{
			return A.SquaredLength() < B.SquaredLength();
		}
		else if constexpr (std::is_same_v<T, FVector4>)
		{
			return StrictlySmaller(FVector(A), FVector(B));
		}
		else if constexpr (std::is_same_v<T, FQuat> || std::is_same_v<T, FRotator>)
		{
			return StrictlySmaller(FVector(A.Euler()), FVector(B.Euler()));
		}
		else if constexpr (std::is_same_v<T, FTransform>)
		{
			return (StrictlySmaller(A.GetLocation(), B.GetLocation()) && StrictlySmaller(A.GetRotation(), B.GetRotation()) && StrictlySmaller(A.GetScale3D(), B.GetScale3D()));
		}
		else if constexpr (std::is_same_v<T, FString>)
		{
			return A.Compare(B, ESearchCase::IgnoreCase) < 0;
		}
		else if constexpr (std::is_same_v<T, FName> || std::is_same_v<T, FSoftObjectPath> || std::is_same_v<T, FSoftClassPath>)
		{
			return StrictlySmaller(A.ToString(), B.ToString());
		}
		else
		{
			return A < B;
		}
	}

	template <typename T>
	FORCEINLINE static bool NearlyEqual(const T& A, const T& B, const double Tolerance = DBL_COMPARE_TOLERANCE)
	{
		if constexpr (std::is_same_v<T, bool>)
		{
			return A == B;
		}
		else if constexpr (std::is_same_v<T, FVector2D>)
		{
			return NearlyEqual(A.X, B.X, Tolerance) && NearlyEqual(A.Y, B.Y, Tolerance);
		}
		else if constexpr (std::is_same_v<T, FVector>)
		{
			return NearlyEqual(A.X, B.X, Tolerance) && NearlyEqual(A.Y, B.Y, Tolerance) && NearlyEqual(A.Z, B.Z, Tolerance);
		}
		else if constexpr (std::is_same_v<T, FVector4>)
		{
			return NearlyEqual(A.X, B.X, Tolerance) && NearlyEqual(A.Y, B.Y, Tolerance) && NearlyEqual(A.Z, B.Z, Tolerance) && NearlyEqual(A.W, B.W, Tolerance);
		}
		else if constexpr (std::is_same_v<T, FQuat> || std::is_same_v<T, FRotator>)
		{
			return NearlyEqual(FVector(A.Euler()), FVector(B.Euler()), Tolerance);
		}
		else if constexpr (std::is_same_v<T, FTransform>)
		{
			return NearlyEqual(A.GetLocation(), B.GetLocation(), Tolerance) && NearlyEqual(A.GetRotation(), B.GetRotation(), Tolerance) && NearlyEqual(A.GetScale3D(), B.GetScale3D(), Tolerance);
		}
		else if constexpr (std::is_same_v<T, FString>)
		{
			return FMath::IsNearlyEqual(A.Len(), B.Len(), Tolerance);
		}
		else if constexpr (std::is_same_v<T, FName> || std::is_same_v<T, FSoftObjectPath> || std::is_same_v<T, FSoftClassPath>)
		{
			return NearlyEqual(A.ToString(), B.ToString(), Tolerance);
		}
		else
		{
			return FMath::IsNearlyEqual(A, B, Tolerance);
		}
	}

	template <typename T>
	FORCEINLINE static bool NearlyNotEqual(const T& A, const T& B, const double Tolerance = DBL_COMPARE_TOLERANCE) { return !NearlyEqual(A, B, Tolerance); }

#pragma endregion

	template <typename T>
	FORCEINLINE static bool Compare(const EPCGExComparison Method, const T& A, const T& B, const double Tolerance = DBL_COMPARE_TOLERANCE)
	{
		switch (Method)
		{
		case EPCGExComparison::StrictlyEqual: return StrictlyEqual(A, B);
		case EPCGExComparison::StrictlyNotEqual: return StrictlyNotEqual(A, B);
		case EPCGExComparison::EqualOrGreater: return EqualOrGreater(A, B);
		case EPCGExComparison::EqualOrSmaller: return EqualOrSmaller(A, B);
		case EPCGExComparison::StrictlyGreater: return StrictlyGreater(A, B);
		case EPCGExComparison::StrictlySmaller: return StrictlySmaller(A, B);
		case EPCGExComparison::NearlyEqual: return NearlyEqual(A, B, Tolerance);
		case EPCGExComparison::NearlyNotEqual: return NearlyNotEqual(A, B, Tolerance);
		default: return false;
		}
	}

	PCGEXCORE_API
	bool Compare(const EPCGExComparison Method, const TSharedPtr<PCGExData::IDataValue>& A, const double B, const double Tolerance = DBL_COMPARE_TOLERANCE);

	PCGEXCORE_API
	bool Compare(const EPCGExStringComparison Method, const TSharedPtr<PCGExData::IDataValue>& A, const FString B);

	PCGEXCORE_API
	bool HasMatchingTags(const TSharedPtr<PCGExData::FTags>& InTags, const FString& Query, const EPCGExStringMatchMode MatchMode, const bool bStrict = true);

	PCGEXCORE_API
	bool GetMatchingValueTags(const TSharedPtr<PCGExData::FTags>& InTags, const FString& Query, const EPCGExStringMatchMode MatchMode, TArray<TSharedPtr<PCGExData::IDataValue>>& OutValues);
}

UENUM()
enum class EPCGExDirectionCheckMode : uint8
{
	Dot  = 0 UMETA(DisplayName = "Dot (Precise)", Tooltip="Extensive comparison using Dot product"),
	Hash = 1 UMETA(DisplayName = "Hash (Fast)", Tooltip="Simplified check using hash comparison with a destructive tolerance"),
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExVectorHashComparisonDetails
{
	GENERATED_BODY()

	FPCGExVectorHashComparisonDetails()
	{
	}

	explicit FPCGExVectorHashComparisonDetails(double InHashToleranceConstant)
	{
		HashToleranceConstant = InHashToleranceConstant;
	}

	/** Whether tolerance comes from a constant or per-point attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExInputValueType HashToleranceInput = EPCGExInputValueType::Constant;

	/** Attribute to read tolerance from when using per-point values. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Hash Tolerance (Attr)", EditCondition="HashToleranceInput != EPCGExInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector HashToleranceAttribute;

	/**
	 * Tolerance used when hashing vectors for comparison.
	 * Smaller values = more precise matching, larger values = more lenient.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Hash Tolerance", EditCondition="HashToleranceInput == EPCGExInputValueType::Constant", EditConditionHides, ClampMin=0.00001))
	double HashToleranceConstant = 0.001;

	TSharedPtr<PCGExDetails::TSettingValue<double>> Tolerance;

	PCGEX_SETTING_VALUE_DECL(Tolerance, double)

	bool Init(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InPrimaryDataFacade, const bool bQuiet = false);
	FVector GetCWTolerance(const int32 PointIndex) const;

	void RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const;
	bool GetOnlyUseDataDomain() const;

	bool Test(const FVector& A, const FVector& B, const int32 PointIndex) const;
};

/**
 * Dot product comparison with constant threshold only.
 * Use FPCGExDotComparisonDetails if you need per-point attribute thresholds.
 */
USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExStaticDotComparisonDetails
{
	GENERATED_BODY()

	FPCGExStaticDotComparisonDetails()
	{
	}


	/**
	 * How threshold values are interpreted.
	 * Scalar = raw dot product (-1 to 1), Degrees = angular (0-180).
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExAngularDomain Domain = EPCGExAngularDomain::Scalar;

	/** The comparison operator to use. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExComparison Comparison = EPCGExComparison::EqualOrGreater;

	/**
	 * Use absolute value of dot product before comparing.
	 * Treats opposite directions as equivalent.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bUnsignedComparison = false;

	/**
	 * Threshold for comparison in scalar domain.
	 * -1 = opposite, 0 = perpendicular, 1 = same direction.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Scalar", EditCondition="Domain == EPCGExAngularDomain::Scalar", EditConditionHides, ClampMin=-1, ClampMax=1))
	double DotConstant = 0.5;

	/** Tolerance for ~= and !~= comparisons in scalar domain. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Tolerance", EditCondition="(Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual) && Domain == EPCGExAngularDomain::Scalar", EditConditionHides, ClampMin=0, ClampMax=1))
	double DotTolerance = 0.1;

	/**
	 * Threshold for comparison in degrees.
	 * 0 = same direction, 90 = perpendicular, 180 = opposite.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Degrees", EditCondition="Domain == EPCGExAngularDomain::Degrees", EditConditionHides, ClampMin=0, ClampMax=180, Units="Degrees"))
	double DegreesConstant = 90;

	/** Tolerance for ~= and !~= comparisons in degrees. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Tolerance", EditCondition="(Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual) && Domain == EPCGExAngularDomain::Degrees", EditConditionHides, ClampMin=0, ClampMax=180, Units="Degrees"))
	double DegreesTolerance = 0.1;

	double ComparisonTolerance = 0;

	void Init();
	bool Test(const double A) const;
};

/**
 * Dot product comparison with support for per-point attribute thresholds.
 * Use FPCGExStaticDotComparisonDetails if constant threshold is sufficient.
 */
USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExDotComparisonDetails
{
	GENERATED_BODY()

	FPCGExDotComparisonDetails()
	{
	}

	/**
	 * How threshold values are interpreted.
	 * Scalar = raw dot product (-1 to 1), Degrees = angular (0-180).
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExAngularDomain Domain = EPCGExAngularDomain::Scalar;

	/** The comparison operator to use. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExComparison Comparison = EPCGExComparison::EqualOrGreater;

	/**
	 * Use absolute value of dot product before comparing.
	 * Treats opposite directions as equivalent.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bUnsignedComparison = false;

	/** Whether threshold comes from a constant or per-point attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName="Threshold Input"))
	EPCGExInputValueType ThresholdInput = EPCGExInputValueType::Constant;

	/**
	 * Attribute to read threshold from.
	 * Value interpretation depends on Domain setting.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Threshold (Attr)", EditCondition="ThresholdInput != EPCGExInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector ThresholdAttribute;

	/**
	 * Threshold for comparison in scalar domain.
	 * -1 = opposite, 0 = perpendicular, 1 = same direction.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Scalar", EditCondition="ThresholdInput == EPCGExInputValueType::Constant && Domain == EPCGExAngularDomain::Scalar", EditConditionHides, ClampMin=-1, ClampMax=1))
	double DotConstant = 0;

	/** Tolerance for ~= and !~= comparisons in scalar domain. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Tolerance", EditCondition="(Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual) && Domain == EPCGExAngularDomain::Scalar", EditConditionHides, ClampMin=0, ClampMax=1))
	double DotTolerance = 0.1;

	/**
	 * Threshold for comparison in degrees.
	 * 0 = same direction, 90 = perpendicular, 180 = opposite.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Degrees", EditCondition="ThresholdInput == EPCGExInputValueType::Constant && Domain == EPCGExAngularDomain::Degrees", EditConditionHides, ClampMin=0, ClampMax=180, Units="Degrees"))
	double DegreesConstant = 90;

	/** Tolerance for ~= and !~= comparisons in degrees. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Tolerance", EditCondition="(Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual) && Domain == EPCGExAngularDomain::Degrees", EditConditionHides, ClampMin=0, ClampMax=180, Units="Degrees"))
	double DegreesTolerance = 0.1;

	PCGEX_SETTING_VALUE_DECL(Threshold, double)
	TSharedPtr<PCGExDetails::TSettingValue<double>> ThresholdGetter;

	double ComparisonTolerance = 0;

	bool Init(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InPrimaryDataCache, const bool bQuiet = false);

	double GetComparisonThreshold(const int32 PointIndex) const;

	bool Test(const double A, const double B) const;
	bool Test(const double A, const int32 Index) const;

	void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const;
	void RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const;
	bool GetOnlyUseDataDomain() const;

#if WITH_EDITOR
	FString GetDisplayComparison() const;
#endif
};

#undef PCGEX_UNSUPPORTED_STRING_TYPES
#undef PCGEX_UNSUPPORTED_PATH_TYPES
