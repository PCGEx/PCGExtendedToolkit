// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGExInputShorthandsDetails.h"

#include "PCGExNormalToDensityDetails.generated.h"

namespace PCGExMT
{
	struct FScope;
}

namespace PCGExData
{
	class FFacade;
}

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

UENUM()
enum class EPCGExNormalToDensityMode : uint8
{
	Set      = 0 UMETA(DisplayName = "Set", ToolTip="Density = Value"),
	Minimum  = 1 UMETA(DisplayName = "Minimum", ToolTip="Density = Min(Density, Value)"),
	Maximum  = 2 UMETA(DisplayName = "Maximum", ToolTip="Density = Max(Density, Value)"),
	Add      = 3 UMETA(DisplayName = "Add", ToolTip="Density = Density + Value"),
	Subtract = 4 UMETA(DisplayName = "Subtract", ToolTip="Density = Density - Value"),
	Multiply = 5 UMETA(DisplayName = "Multiply", ToolTip="Density = Density * Value"),
	Divide   = 6 UMETA(DisplayName = "Divide", ToolTip="Density = Density / Value, 0 when Value is 0"),
};

/** Turns the alignment between a sampled normal and a reference direction into a density value. */
USTRUCT(BlueprintType)
struct PCGEXBLENDING_API FPCGExNormalToDensityDetails
{
	GENERATED_BODY()

	FPCGExNormalToDensityDetails() = default;

	/** Direction the sampled normal is compared against : 1 when aligned, 0 when perpendicular or opposed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExInputShorthandSelectorDirection Normal = FPCGExInputShorthandSelectorDirection(FName("Normal"), FVector::UpVector);

	/** Added to the alignment before it is clamped to 0..1; biases the value toward or away from the direction. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExInputShorthandSelectorDouble Offset = FPCGExInputShorthandSelectorDouble(FName("Offset"), 0.0);

	/** Shapes the falloff, Value = Value^(1/Strength) : above 1 widens the dense area, below 1 tightens it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExInputShorthandSelectorDoubleAbs Strength = FPCGExInputShorthandSelectorDoubleAbs(FName("Strength"), 1.0);

	/** How the computed value is combined with the existing point density. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExNormalToDensityMode Mode = EPCGExNormalToDensityMode::Set;
};

namespace PCGExSampling
{
	/**
	 * Runtime side of FPCGExNormalToDensityDetails, bound to one facade.
	 * Init once, PrepareScope once per scope after the facade's Fetch, then Apply per point.
	 */
	class PCGEXBLENDING_API FNormalToDensity
	{
	public:
		/** Per-scope operands, indexed by Index - Scope.Start. Left empty when every operand is constant. */
		struct FScopeView
		{
			TArray<FVector> Normals;
			TArray<double> Offsets;
			TArray<double> InvStrengths;
		};

	private:
		static constexpr double MinStrength = 0.0001;

		TSharedPtr<PCGExDetails::TSettingValue<FVector>> Normal;
		TSharedPtr<PCGExDetails::TSettingValue<double>> Offset;
		TSharedPtr<PCGExDetails::TSettingValue<double>> Strength;

		EPCGExNormalToDensityMode Mode = EPCGExNormalToDensityMode::Set;

		// Constant and @Data operands resolve once in Init; scopes then skip the per-point reads.
		bool bConstant = false;
		FVector ConstantNormal = FVector::UpVector;
		double ConstantOffset = 0;
		double ConstantInvStrength = 1;

	public:
		bool Init(const FPCGExNormalToDensityDetails& InDetails, const TSharedPtr<PCGExData::FFacade>& InDataFacade);

		void PrepareScope(const PCGExMT::FScope& Scope, FScopeView& OutView) const;

		/** InNormal must be normalized. */
		FORCEINLINE double GetValue(const FScopeView& View, const int32 ScopeIndex, const FVector& InNormal) const
		{
			if (bConstant)
			{
				return FMath::Pow(FMath::Clamp(InNormal.Dot(ConstantNormal) + ConstantOffset, 0.0, 1.0), ConstantInvStrength);
			}

			return FMath::Pow(FMath::Clamp(InNormal.Dot(View.Normals[ScopeIndex]) + View.Offsets[ScopeIndex], 0.0, 1.0), View.InvStrengths[ScopeIndex]);
		}

		/** InNormal must be normalized. */
		FORCEINLINE void Apply(const FScopeView& View, const int32 ScopeIndex, const FVector& InNormal, float& InOutDensity) const
		{
			const float Value = static_cast<float>(GetValue(View, ScopeIndex, InNormal));

			switch (Mode)
			{
			case EPCGExNormalToDensityMode::Set:
				InOutDensity = Value;
				break;
			case EPCGExNormalToDensityMode::Minimum:
				InOutDensity = FMath::Min(InOutDensity, Value);
				break;
			case EPCGExNormalToDensityMode::Maximum:
				InOutDensity = FMath::Max(InOutDensity, Value);
				break;
			case EPCGExNormalToDensityMode::Add:
				InOutDensity += Value;
				break;
			case EPCGExNormalToDensityMode::Subtract:
				InOutDensity -= Value;
				break;
			case EPCGExNormalToDensityMode::Multiply:
				InOutDensity *= Value;
				break;
			case EPCGExNormalToDensityMode::Divide:
				InOutDensity = Value != 0.0f ? InOutDensity / Value : 0.0f;
				break;
			default:
				// Init rejects unknown modes
				checkNoEntry();
				break;
			}
		}
	};
}
