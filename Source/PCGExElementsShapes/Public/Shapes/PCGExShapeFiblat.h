// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExShape.h"
#include "Core/PCGExShapeBuilderFactoryProvider.h"
#include "Core/PCGExShapeBuilderOperation.h"
#include "Core/PCGExShapeConfigBase.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Math/PCGExMathAxis.h"

#include "PCGExShapeFiblat.generated.h"

UENUM()
enum class EPCGExFibPhiConstant : uint8
{
	GoldenRatio    = 0 UMETA(DisplayName = "Golden Ratio"),
	SqRootOfTwo    = 1 UMETA(DisplayName = "Sqrt 2"),
	Irrational     = 2 UMETA(DisplayName = "Irrational"),
	SqRootOfTthree = 3 UMETA(DisplayName = "Sqrt 3"),
	Ln2            = 4 UMETA(DisplayName = "Ln2"),
	Custom         = 5 UMETA(DisplayName = "Custom"),
};

USTRUCT(BlueprintType)
struct FPCGExShapeFiblatConfig : public FPCGExShapeConfigBase
{
	GENERATED_BODY()

	FPCGExShapeFiblatConfig()
		: FPCGExShapeConfigBase()
	{
	}

	/** Irrational step between consecutive lattice points; the golden ratio gives the most even cover. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExFibPhiConstant PhiConstant = EPCGExFibPhiConstant::GoldenRatio;

	/** Custom lattice step; only its fractional part matters. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="PhiConstant == EPCGExFibPhiConstant::Custom", EditConditionHides))
	FPCGExInputShorthandNameDouble Phi = FPCGExInputShorthandNameDouble(FName("Phi"), 0.724592938, false);

	/** Pulls the first and last lattice rows inward from the ends of the range; 0 puts them on the rim or poles. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	double Epsilon = 0;
};

namespace PCGExShapes
{
	/** Fibonacci lattice over the unit square, with the sphere / disc / rect projections from https://observablehq.com/@meetamit/fibonacci-lattices */
	class FFiblat : public FShape
	{
	public:
		double Phi = (FMath::Sqrt(5.0) - 1.0) * 0.5;
		double Epsilon = 0;

		explicit FFiblat(const PCGExData::FConstPoint& InPointRef)
			: FShape(InPointRef)
		{
		}

		/** x = (i * phi) % 1 */
		FORCEINLINE double LatticeX(const int32 Index) const
		{
			return FMath::Fmod(Index * Phi, 1.0);
		}

		/** y = (i + ε) / (n - 1 + 2ε), clamped to [0, 1] so a negative ε cannot feed Sqrt; a single-point lattice sits mid-range */
		FORCEINLINE double LatticeY(const int32 Index, const int32 Count) const
		{
			const double Denominator = Count - 1 + 2.0 * Epsilon;
			return Denominator > 0 ? FMath::Clamp((Index + Epsilon) / Denominator, 0.0, 1.0) : 0.5;
		}

		/** Unit sphere, Y up. Float PI on purpose: a more precise theta would move every existing sphere point and flip its spatial seed */
		FORCEINLINE FVector ComputeSpherePoint(const int32 Index, const int32 Count) const
		{
			const double Theta = LatticeX(Index) * 2.0 * PI;
			const double PhiAngle = FMath::Acos(1.0 - 2.0 * LatticeY(Index, Count));
			const double SinPhi = FMath::Sin(PhiAngle);
			return FVector(FMath::Cos(Theta) * SinPhi, FMath::Cos(PhiAngle), FMath::Sin(Theta) * SinPhi);
		}

		/** Unit disc in the XY plane; r = sqrt(y) keeps the area density uniform */
		FORCEINLINE FVector ComputeDiscPoint(const int32 Index, const int32 Count) const
		{
			const double Theta = LatticeX(Index) * UE_DOUBLE_TWO_PI;
			const double R = FMath::Sqrt(LatticeY(Index, Count));
			return FVector(R * FMath::Cos(Theta), R * FMath::Sin(Theta), 0);
		}

		/** Unit square in the XY plane, remapped to [-1, 1] */
		FORCEINLINE FVector ComputeRectPoint(const int32 Index, const int32 Count) const
		{
			return FVector(LatticeX(Index) * 2.0 - 1.0, LatticeY(Index, Count) * 2.0 - 1.0, 0);
		}
	};
}

/**
 * Shared by the φ shapes: resolves the φ constant once per seed set, stamps it on each lattice, sizes point extents.
 */
class FPCGExShapeFiblatBuilderBase : public FPCGExShapeBuilderOperation
{
public:
	FPCGExShapeFiblatConfig Config;

	virtual bool PrepareForSeeds(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InSeedDataFacade) override;

protected:
	void PrepareLattice(const TSharedPtr<PCGExShapes::FFiblat>& InFiblat, const PCGExData::FConstPoint& Seed) const;

	/** Point extents from an even spread of NumPoints over Area, or the constant extents */
	void ComputeExtents(const TSharedPtr<PCGExShapes::FFiblat>& InFiblat, const double Area) const;

	/** Points needed to cover Area at Spacing; 0 (an invalid shape) when Spacing is not positive or the count does not fit int32 */
	static int32 CountFromSpacing(const double Area, const double Spacing);

	/** Rotation facing the seed; skips the matrix build when Dir is degenerate (a lattice point on the center) */
	static FQuat LookAtRotation(const FVector& Dir, const EPCGExAxisAlign LookAtAxis);

	TSharedPtr<PCGExDetails::TSettingValue<double>> Phi;
	double ConstantPhi = 0;
};

/**
 *
 */
class FPCGExShapeFiblatBuilder : public FPCGExShapeFiblatBuilderBase
{
public:
	virtual void PrepareShape(const PCGExData::FConstPoint& Seed) override;
	virtual void BuildShape(TSharedPtr<PCGExShapes::FShape> InShape, TSharedPtr<PCGExData::FFacade> InDataFacade, const PCGExData::FScope& Scope, bool bOwnsData = false) override;
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExShapeFiblatFactory : public UPCGExShapeBuilderFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExShapeFiblatConfig Config;

	virtual TSharedPtr<FPCGExShapeBuilderOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Builder|Params", meta=(PCGExNodeLibraryDoc="shapes/shape-f-sphere"))
class UPCGExCreateShapeFiblatSettings : public UPCGExShapeBuilderFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ShapeBuilderFiblat, "Shape : φ Sphere", "Create a Fibonacci Lattice sphere.", FName(TEXT("φ Sphere")))

#endif
	//~End UPCGSettings

	/** Shape properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExShapeFiblatConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
