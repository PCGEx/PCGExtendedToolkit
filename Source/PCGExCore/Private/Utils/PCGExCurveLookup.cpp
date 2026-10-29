// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Utils/PCGExCurveLookup.h"

#include <atomic>

#include "PCGExLog.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "UObject/PackageReload.h"
#endif

#pragma region PCGExCurves

namespace PCGExCurveLookup
{
	struct FDefaultCurve
	{
		explicit FDefaultCurve(const FSoftObjectPath& InAssetPath)
			: AssetPath(InAssetPath)
		{
			checkf(IsInGameThread(), TEXT("PCGExCurves::GetDefaultCurve must first run on the game thread: it loads the shipped curves."));

			UCurveFloat* LoadedCurve = LoadObject<UCurveFloat>(nullptr, *AssetPath.ToString());
			if (LoadedCurve)
			{
				// Rooted like the engine's default materials: GC never traces the cooked CDOs that point at it.
				LoadedCurve->AddToRoot();
			}
			else
			{
				UE_LOG(LogPCGEx, Error, TEXT("Missing shipped default curve '%s'."), *AssetPath.ToString());
			}
			Curve.store(LoadedCurve, std::memory_order_relaxed);
		}

		const FSoftObjectPath AssetPath;

		// Swapped on the game thread by an editor package reload while any thread may read it.
		std::atomic<UCurveFloat*> Curve;
	};

	/** The shipped default curves, loaded on the game thread at the first call. */
	TArrayView<FDefaultCurve> GetDefaultCurves()
	{
		static FDefaultCurve DefaultCurves[] = {FDefaultCurve(PCGExCurves::WeightDistributionLinear), FDefaultCurve(PCGExCurves::WeightDistributionLinearInv)};
		return MakeArrayView(DefaultCurves);
	}
}

namespace PCGExCurves
{
	UCurveFloat* GetDefaultCurve(const FSoftObjectPath& Path)
	{
		for (const PCGExCurveLookup::FDefaultCurve& Entry : PCGExCurveLookup::GetDefaultCurves())
		{
			if (Entry.AssetPath == Path) { return Entry.Curve.load(std::memory_order_acquire); }
		}

		ensureMsgf(false, TEXT("'%s' is not a shipped default curve."), *Path.ToString());
		return nullptr;
	}

#if WITH_EDITOR
	void EDITOR_RepointDefaultCurves(const EPackageReloadPhase InPhase, FPackageReloadedEvent* InEvent)
	{
		// Batch phases carry no event.
		if (InPhase != EPackageReloadPhase::OnPackageFixup || !InEvent) { return; }

		for (PCGExCurveLookup::FDefaultCurve& Entry : PCGExCurveLookup::GetDefaultCurves())
		{
			// The engine roots the replacement of a rooted object (GeneratePackageReloadEvent).
			UObject* Replacement = nullptr;
			if (InEvent->GetRepointedObject(Entry.Curve.load(std::memory_order_relaxed), Replacement))
			{
				Entry.Curve.store(Cast<UCurveFloat>(Replacement), std::memory_order_release);
			}
		}
	}
#endif
}

#pragma endregion

PCGExFloatLUT FPCGExCurveLookupDetails::MakeFloatLookup(const FRuntimeFloatCurve& InCurve) const
{
	return FPCGExCurveFloatLookup::Make(InCurve, Mode, Samples);
}

PCGExFloatLUT FPCGExCurveLookupDetails::MakeLookup(
	const bool InUseLocalCurve, FRuntimeFloatCurve RuntimeCurve,
	const UCurveFloat* InExternalCurve, const PCGExCurves::FInitCurveDataDefaults& InitFn) const
{
	if (!InUseLocalCurve)
	{
		// The external curve wins; without one, the local curve seeded by InitFn stands in.
		if (InExternalCurve)
		{
			RuntimeCurve.EditorCurveData = InExternalCurve->FloatCurve;
		}
		else
		{
			InitFn(RuntimeCurve.EditorCurveData);
		}
		RuntimeCurve.ExternalCurve = nullptr;
	}

	PCGExFloatLUT NewLUT = MakeShared<FPCGExCurveFloatLookup>();
	NewLUT->Init(RuntimeCurve, Mode, Samples);
	return NewLUT;
}

PCGExFloatLUT FPCGExCurveLookupDetails::MakeLookup(
	const bool InUseLocalCurve, const FRuntimeFloatCurve& RuntimeCurve,
	const UCurveFloat* InExternalCurve) const
{
	return MakeLookup(
		InUseLocalCurve, RuntimeCurve, InExternalCurve,
		[](FRichCurve& CurveData)
		{
		});
}

PCGExFloatLUT FPCGExCurveLookupDetails::MakeFloatLookup(const PCGExCurves::FInitCurveDataDefaults& InitFn) const
{
	static_assert("NOT IMPLEMENTED YET");
	return MakeLookup(bUseLocalCurve, LocalCurve, ExternalCurve, InitFn);
}

PCGExFloatLUT FPCGExCurveLookupDetails::MakeFloatLookup() const
{
	static_assert("NOT IMPLEMENTED YET");
	return MakeLookup(
		bUseLocalCurve, LocalCurve, ExternalCurve,
		PCGExCurves::InitLinearRamp);
}

#pragma region FPCGExCurveFloatLookup

FPCGExCurveFloatLookup::~FPCGExCurveFloatLookup()
{
	
}

void FPCGExCurveFloatLookup::SnapshotCurve(const FRuntimeFloatCurve& InCurve)
{
	// GetRichCurveConst never returns null: without an external curve it returns the editor data.
	Curve.EditorCurveData = *InCurve.GetRichCurveConst();
	Curve.ExternalCurve = nullptr;
	CurvePtr = &Curve.EditorCurveData;
}

void FPCGExCurveFloatLookup::Init(const FRuntimeFloatCurve& InCurve, const EPCGExCurveLUTMode InMode, const int32 InNumSamples)
{
	SnapshotCurve(InCurve);
	Mode = InMode;
	LUT.Reset();
	bFastLinear = false;

	if (!CurvePtr || CurvePtr->GetNumKeys() == 0)
	{
		// No LUT is built here, so Lookup mode would index an empty array; Direct returns the
		// curve's default value (0 for a keyless FRichCurve).
		Mode = EPCGExCurveLUTMode::Direct;
		TimeMin = 0.0f;
		TimeMax = 1.0f;
		TimeToNormalized = 1.0f;
		LUTMaxIdx = 0.0f;
		return;
	}

	// Get curve's natural time range
	float TMin, TMax;
	CurvePtr->GetTimeRange(TMin, TMax);

	TimeMin = TMin;
	TimeMax = TMax;

	const float TimeDelta = TimeMax - TimeMin;
	TimeToNormalized = FMath::IsNearlyZero(TimeDelta) ? 1.0f : 1.0f / TimeDelta;

	// A 2-key linear segment with constant extrapolation (the default weight-distribution ramps,
	// including identity) reduces to a clamped lerp. Detect it and bypass both FRichCurve::Eval
	// (branchy key search, called every edge relaxation in pathfinding) and the LUT, whichever
	// mode was requested -- the closed form is faster and exact.
	if (CurvePtr->GetNumKeys() == 2
		&& CurvePtr->Keys[0].InterpMode == RCIM_Linear
		&& CurvePtr->PreInfinityExtrap == RCCE_Constant
		&& CurvePtr->PostInfinityExtrap == RCCE_Constant
		&& TimeDelta > 0.0f)
	{
		bFastLinear = true;
		LinearBase = CurvePtr->Keys[0].Value;
		LinearSlope = (static_cast<double>(CurvePtr->Keys[1].Value) - CurvePtr->Keys[0].Value) / TimeDelta;
		LUTMaxIdx = 0.0f;
		return;
	}

	if (Mode == EPCGExCurveLUTMode::Direct)
	{
		LUTMaxIdx = 0.0f;
		return;
	}

	const int32 Count = FMath::Max(InNumSamples, 32);

	// Allocate Count + 1 so Hi index is always safe without branch
	LUT.SetNumUninitialized(Count + 1);
	LUTMaxIdx = static_cast<float>(Count - 1);

	for (int32 i = 0; i <= Count; i++)
	{
		const float T = TimeMin + (static_cast<float>(i) / static_cast<float>(Count)) * TimeDelta;
		LUT[i] = CurvePtr->Eval(T);
	}
}

void FPCGExCurveFloatLookup::Init(const FRuntimeFloatCurve& InCurve, const TFunctionRef<float(float)> InSampleTransform, const int32 InNumSamples)
{
	SnapshotCurve(InCurve);
	Mode = EPCGExCurveLUTMode::Lookup;
	bFastLinear = false;
	LUT.Reset();

	float TMin = 0.0f;
	float TMax = 1.0f;
	const bool bHasKeys = CurvePtr && CurvePtr->GetNumKeys() > 0;
	if (bHasKeys)
	{
		CurvePtr->GetTimeRange(TMin, TMax);
	}
	TimeMin = TMin;
	TimeMax = TMax;

	const float TimeDelta = TimeMax - TimeMin;
	TimeToNormalized = FMath::IsNearlyZero(TimeDelta) ? 1.0f : 1.0f / TimeDelta;

	// Count + 2 entries with a duplicated tail: LUTMaxIdx = Count maps t=1 exactly onto the last
	// real sample (no endpoint skew), and Hi = Lo + 1 stays in range without a branch.
	const int32 Count = FMath::Max(InNumSamples, 32);
	LUT.SetNumUninitialized(Count + 2);
	LUTMaxIdx = static_cast<float>(Count);

	for (int32 i = 0; i <= Count; i++)
	{
		const float T = TimeMin + (static_cast<float>(i) / static_cast<float>(Count)) * TimeDelta;
		LUT[i] = InSampleTransform(bHasKeys ? CurvePtr->Eval(T) : 0.0f);
	}
	LUT[Count + 1] = LUT[Count];
}

#pragma endregion
