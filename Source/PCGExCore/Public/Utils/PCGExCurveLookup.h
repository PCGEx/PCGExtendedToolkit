// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <functional>

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
//#include "Templates/SharedPointer.h"
//#include "Templates/SharedPointerFwd.h"
#include "UObject/SoftObjectPath.h"

#include "PCGExCurveLookup.generated.h"

struct FStreamableHandle;
class FPCGExCurveFloatLookup;

using PCGExFloatLUT = TSharedPtr<FPCGExCurveFloatLookup, ESPMode::ThreadSafe>;

UENUM(BlueprintType)
enum class EPCGExCurveLUTMode : uint8
{
	Direct = 0 UMETA(DisplayName = "Direct"),
	Lookup = 1 UMETA(DisplayName = "Lookup"),
};

namespace PCGExCurves
{
	using FInitCurveDataDefaults = std::function<void(FRichCurve& CurveData)>;

	const FSoftObjectPath DefaultDotOverDistanceCurve = FSoftObjectPath(TEXT("/PCGExtendedToolkit/Curves/FC_PCGExGraphBalance_DistanceOnly.FC_PCGExGraphBalance_DistanceOnly"));
	const FSoftObjectPath WeightDistributionLinearInv = FSoftObjectPath(TEXT("/PCGExtendedToolkit/Curves/FC_PCGExWeightDistribution_Linear_Inv.FC_PCGExWeightDistribution_Linear_Inv"));
	const FSoftObjectPath WeightDistributionLinear = FSoftObjectPath(TEXT("/PCGExtendedToolkit/Curves/FC_PCGExWeightDistribution_Linear.FC_PCGExWeightDistribution_Linear"));
	const FSoftObjectPath WeightDistributionExpoInv = FSoftObjectPath(TEXT("/PCGExtendedToolkit/Curves/FC_PCGExWeightDistribution_Expo_Inv.FC_PCGExWeightDistribution_Expo_Inv"));
	const FSoftObjectPath WeightDistributionExpo = FSoftObjectPath(TEXT("/PCGExtendedToolkit/Curves/FC_PCGExWeightDistribution_Expo.FC_PCGExWeightDistribution_Expo"));
	const FSoftObjectPath SteepnessWeightCurve = FSoftObjectPath(TEXT("/PCGExtendedToolkit/Curves/FC_PCGExSteepness_Default.FC_PCGExSteepness_Default"));
}

struct FPCGExCurveLookupDetails; // Force-forward so it doesn't get captured as part of namespace when declared as friend later on

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExCurveLookupDetails
{
	GENERATED_BODY()

	FPCGExCurveLookupDetails() = default;

	explicit FPCGExCurveLookupDetails(const EPCGExCurveLUTMode InMode, const int32 InNumSamples = 256)
		: Mode(InMode)
		  , Samples(InNumSamples)
	{
	}

	virtual ~FPCGExCurveLookupDetails() = default;

#pragma region boilerplate

	// Note : we don't expose these members just yet, but we will in a future refactor.
	// They will supersede members that already exist in the lookup hosts

	/** Whether to use in-editor curve or an external asset. */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bUseLocalCurve = false;

	// TODO: DirtyCache for OnDependencyChanged when this float curve is an external asset
	/** Curve the weight value will be remapped over. */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, DisplayName="Curve", EditCondition = "bUseLocalCurve", EditConditionHides))
	FRuntimeFloatCurve LocalCurve;

	/** Curve the weight value will be remapped over. */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Curve", EditCondition="!bUseLocalCurve", EditConditionHides))
	TSoftObjectPtr<UCurveFloat> ExternalCurve = TSoftObjectPtr<UCurveFloat>(PCGExCurves::WeightDistributionLinear);

#pragma endregion

	/** Curve lookup mode */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExCurveLUTMode Mode = EPCGExCurveLUTMode::Direct;

	/** Number of samples in the lookup table. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="Mode == EPCGExCurveLUTMode::Lookup", ClampMin=32))
	int32 Samples = 512;

	PCGExFloatLUT MakeFloatLookup(const FRuntimeFloatCurve& InCurve) const;
	PCGExFloatLUT MakeLookup(const bool InUseLocalCurve, FRuntimeFloatCurve RuntimeCurve, TSoftObjectPtr<UCurveFloat> InExternalCurve, const PCGExCurves::FInitCurveDataDefaults& InitFn) const;
	PCGExFloatLUT MakeLookup(const bool InUseLocalCurve, const FRuntimeFloatCurve& RuntimeCurve, TSoftObjectPtr<UCurveFloat> InExternalCurve) const;
	PCGExFloatLUT MakeFloatLookup(const PCGExCurves::FInitCurveDataDefaults& InitFn) const;
	PCGExFloatLUT MakeFloatLookup() const;
};

#pragma region FPCGExCurveFloatLookup

/**
 * Drop-in curve evaluator with optional LUT acceleration.
 * Samples the curve over its natural time range [MinTime, MaxTime].
 */
class PCGEXCORE_API FPCGExCurveFloatLookup : public TSharedFromThis<FPCGExCurveFloatLookup>
{
	friend struct FPCGExCurveLookupDetails;

public:
	FPCGExCurveFloatLookup() = default;
	~FPCGExCurveFloatLookup();

	void Init(const FRuntimeFloatCurve& InCurve, const EPCGExCurveLUTMode InMode = EPCGExCurveLUTMode::Lookup, const int32 InNumSamples = 256);

	// Evaluate at time T - same signature as FRichCurve::Eval()
	FORCEINLINE double Eval(const double InTime) const
	{
		if (bFastLinear)
		{
			return EvalLinear(InTime);
		}
		if (Mode == EPCGExCurveLUTMode::Direct)
		{
			return CurvePtr ? CurvePtr->Eval(static_cast<float>(InTime)) : 0.0f;
		}
		return EvalLUT(InTime);
	}

	FORCEINLINE void EvalInPlace(TArrayView<double> InTimes) const
	{
		const int32 NumSamples = InTimes.Num();
		if (bFastLinear)
		{
			for (double& InTime : InTimes)
			{
				InTime = EvalLinear(InTime);
			}
		}
		else if (Mode == EPCGExCurveLUTMode::Direct)
		{
			if (CurvePtr)
			{
				for (double& InTime : InTimes)
				{
					InTime = CurvePtr->Eval(static_cast<float>(InTime));
				}
			}
			else
			{
				for (double& InTime : InTimes)
				{
					InTime = 0;
				}
			}
		}
		else
		{
			for (double& InTime : InTimes)
			{
				InTime = EvalLUT(InTime);
			}
		}
	}

	FORCEINLINE bool IsValid() const
	{
		return CurvePtr != nullptr;
	}

	FORCEINLINE bool UsesLUT() const
	{
		return Mode != EPCGExCurveLUTMode::Direct && LUT.Num() > 0;
	}

	FORCEINLINE float GetMinTime() const
	{
		return TimeMin;
	}

	FORCEINLINE float GetMaxTime() const
	{
		return TimeMax;
	}

	FORCEINLINE const FRichCurve* GetCurve() const
	{
		return CurvePtr;
	}

	FORCEINLINE const float* GetRawLUT() const
	{
		return LUT.GetData();
	}

	FORCEINLINE int32 GetLUTSize() const
	{
		return LUT.Num();
	}

	static PCGExFloatLUT Make(const FRuntimeFloatCurve& InCurve, const EPCGExCurveLUTMode InMode = EPCGExCurveLUTMode::Lookup, const int32 InNumSamples = 256)
	{
		PCGExFloatLUT NewLUT = MakeShared<FPCGExCurveFloatLookup>();
		NewLUT->Init(InCurve, InMode, InNumSamples);
		return NewLUT;
	}

protected:
	TSharedPtr<FStreamableHandle> ExternalCurveHandle = nullptr;

	/** Clamped lerp equivalent of a 2-key linear curve with constant extrapolation -- what the
	 * default weight-distribution ramps are. Set up by Init, bypasses FRichCurve::Eval entirely. */
	FORCEINLINE double EvalLinear(const double InTime) const
	{
		return LinearBase + LinearSlope * (FMath::Clamp(InTime, static_cast<double>(TimeMin), static_cast<double>(TimeMax)) - TimeMin);
	}

	FORCEINLINE float EvalLUT(const float InTime) const
	{
		// Normalize time to [0, 1] within curve's range
		const float NormalizedTime = (InTime - TimeMin) * TimeToNormalized;
		const float ClampedIdx = FMath::Clamp(NormalizedTime, 0.0f, 1.0f) * LUTMaxIdx;

		const int32 Lo = static_cast<int32>(ClampedIdx);
		const int32 Hi = Lo + 1;
		const float Frac = ClampedIdx - static_cast<float>(Lo);

		// Hi is safe because LUT has Count+1 entries
		return LUT[Lo] + (LUT[Hi] - LUT[Lo]) * Frac;
	}

	TArray<float> LUT;
	FRuntimeFloatCurve Curve;
	const FRichCurve* CurvePtr = nullptr;
	EPCGExCurveLUTMode Mode = EPCGExCurveLUTMode::Direct;

	float TimeMin = 0.0f;
	float TimeMax = 1.0f;
	float TimeToNormalized = 1.0f; // 1.0 / (TimeMax - TimeMin)
	float LUTMaxIdx = 0.0f;        // LUT.Num() - 2 (because we have Count+1 entries)

	// Fast-linear path (see EvalLinear); takes precedence over Mode when set.
	bool bFastLinear = false;
	double LinearBase = 0;  // First key value
	double LinearSlope = 0; // (V1 - V0) / (T1 - T0)
};

#pragma endregion
