// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Math/PCGExMathAxis.h"

#include "PCGExLayout.generated.h"

UENUM()
enum class EPCGExBinSeedMode : uint8
{
	UVWConstant       = 0 UMETA(DisplayName = "UVW (Constant)", ToolTip="A constant bound-relative position"),
	UVWAttribute      = 1 UMETA(DisplayName = "UVW", ToolTip="A per-bin bound-relative position"),
	PositionConstant  = 2 UMETA(DisplayName = "Position (Constant)", ToolTip="A constant world position"),
	PositionAttribute = 3 UMETA(DisplayName = "Position (Attribute)", ToolTip="A per-bin world position"),
};

UENUM()
enum class EPCGExSpaceSplitMode : uint8
{
	Minimal      = 0 UMETA(DisplayName = "Minimal", ToolTip="Six partitions: the space directly before and after the item along the main axis, then four slabs around it. Fewest candidate spaces to search."),
	MinimalCross = 1 UMETA(DisplayName = "Minimal (Cross Axis)", ToolTip="The same six-partition split as Minimal with the two side axes swapped, so the slabs that ran full width now run narrow and the narrow ones run full width."),
	EqualSplit   = 2 UMETA(DisplayName = "Equal Split", ToolTip="Up to twenty-six partitions, one per cell of the 3x3x3 grid around the item. The most candidate spaces, and the most to search."),
	Cone         = 3 UMETA(Hidden, DisplayName = "Cone", ToolTip="Not implemented. The splitter emits no partitions, so the space around a placed item is discarded and nothing else can go in it."),
	ConeCross    = 4 UMETA(Hidden, DisplayName = "Cone (Cross Axis)", ToolTip="Not implemented. The splitter emits no partitions, so the space around a placed item is discarded and nothing else can go in it."),
};

namespace PCGExLayout
{
	const FName SourceBinsLabel = TEXT("Bins");
	const FName OutputBinsLabel = TEXT("Bins");

	struct FItem
	{
		int32 Index = 0;
		FBox Box = FBox(ForceInit);
		FVector Padding = FVector::ZeroVector;
	};

	struct FSpace
	{
		FBox Box = FBox(ForceInit);
		FVector Size = FVector::ZeroVector;
		FVector CoG = FVector::ZeroVector;
		double DistanceScore = 0;
		double Volume = 0;

		explicit FSpace(const FBox& InBox, const FVector& InSeed)
			: Box(InBox)
		{
			Volume = Box.GetVolume();
			Size = Box.GetSize();

			for (int C = 0; C < 3; C++)
			{
				CoG[C] = FMath::Clamp(InSeed[C], Box.Min[C], Box.Max[C]);
			}
			DistanceScore = FVector::DistSquared(InSeed, CoG);
		}

		~FSpace() = default;

		bool CanFit(const FVector& InTestSize) const;
		void Expand(FBox& InBox, const FVector& Expansion) const;
		FVector Inflate(FBox& InBox, const FVector& Thresholds) const;
	};

	template <EPCGExAxis MainAxis = EPCGExAxis::Up, EPCGExSpaceSplitMode SplitMode = EPCGExSpaceSplitMode::Minimal>
	void SplitSpace(const FSpace& Space, FBox& ItemBox, TArray<FBox>& OutPartitions)
	{
		OutPartitions.Reserve(6);

#define PCGEX_BIN_SPLIT(_MIN, _MAX) \
			if (const FBox B = FBox(_MIN, _MAX); !FMath::IsNearlyZero(B.GetVolume())) { OutPartitions.Add(B); }

		const FVector S_Min = Space.Box.Min;
		const FVector S_Max = Space.Box.Max;
		const FVector I_Min = ItemBox.Min;
		const FVector I_Max = ItemBox.Max;

		if constexpr (SplitMode == EPCGExSpaceSplitMode::EqualSplit)
		{
			OutPartitions.Reserve(26);

			// Top layer
			PCGEX_BIN_SPLIT(FVector(S_Min.X, S_Min.Y, I_Max.Z), FVector(I_Min.X, I_Min.Y, S_Max.Z));
			PCGEX_BIN_SPLIT(FVector(I_Min.X, S_Min.Y, I_Max.Z), FVector(I_Max.X, I_Min.Y, S_Max.Z));
			PCGEX_BIN_SPLIT(FVector(I_Max.X, S_Min.Y, I_Max.Z), FVector(S_Max.X, I_Min.Y, S_Max.Z));

			PCGEX_BIN_SPLIT(FVector(S_Min.X, I_Min.Y, I_Max.Z), FVector(I_Min.X, I_Max.Y, S_Max.Z));
			PCGEX_BIN_SPLIT(FVector(I_Min.X, I_Min.Y, I_Max.Z), FVector(I_Max.X, I_Max.Y, S_Max.Z));
			PCGEX_BIN_SPLIT(FVector(I_Max.X, I_Min.Y, I_Max.Z), FVector(S_Max.X, I_Max.Y, S_Max.Z));

			PCGEX_BIN_SPLIT(FVector(S_Min.X, I_Max.Y, I_Max.Z), FVector(I_Min.X, S_Max.Y, S_Max.Z));
			PCGEX_BIN_SPLIT(FVector(I_Min.X, I_Max.Y, I_Max.Z), FVector(I_Max.X, S_Max.Y, S_Max.Z));
			PCGEX_BIN_SPLIT(I_Max, S_Max);

			// Middle layer
			PCGEX_BIN_SPLIT(FVector(S_Min.X, S_Min.Y, I_Min.Z), FVector(I_Min.X, I_Min.Y, I_Max.Z));
			PCGEX_BIN_SPLIT(FVector(I_Min.X, S_Min.Y, I_Min.Z), FVector(I_Max.X, I_Min.Y, I_Max.Z));
			PCGEX_BIN_SPLIT(FVector(I_Max.X, S_Min.Y, I_Min.Z), FVector(S_Max.X, I_Min.Y, I_Max.Z));

			PCGEX_BIN_SPLIT(FVector(S_Min.X, I_Min.Y, I_Min.Z), FVector(I_Min.X, I_Max.Y, I_Max.Z));
			PCGEX_BIN_SPLIT(FVector(I_Max.X, I_Min.Y, I_Min.Z), FVector(S_Max.X, I_Max.Y, I_Max.Z));

			PCGEX_BIN_SPLIT(FVector(S_Min.X, I_Max.Y, I_Min.Z), FVector(I_Min.X, S_Max.Y, I_Max.Z));
			PCGEX_BIN_SPLIT(FVector(I_Min.X, I_Max.Y, I_Min.Z), FVector(I_Max.X, S_Max.Y, I_Max.Z));
			PCGEX_BIN_SPLIT(FVector(I_Max.X, I_Max.Y, I_Min.Z), FVector(S_Max.X, S_Max.Y, I_Max.Z));

			// Bottom layer
			PCGEX_BIN_SPLIT(S_Min, I_Min);
			PCGEX_BIN_SPLIT(FVector(I_Min.X, S_Min.Y, S_Min.Z), FVector(I_Max.X, I_Min.Y, I_Min.Z));
			PCGEX_BIN_SPLIT(FVector(I_Max.X, S_Min.Y, S_Min.Z), FVector(S_Max.X, I_Min.Y, I_Min.Z));

			PCGEX_BIN_SPLIT(FVector(S_Min.X, I_Min.Y, S_Min.Z), FVector(I_Min.X, I_Max.Y, I_Min.Z));
			PCGEX_BIN_SPLIT(FVector(I_Min.X, I_Min.Y, S_Min.Z), FVector(I_Max.X, I_Max.Y, I_Min.Z));
			PCGEX_BIN_SPLIT(FVector(I_Max.X, I_Min.Y, S_Min.Z), FVector(S_Max.X, I_Max.Y, I_Min.Z));

			PCGEX_BIN_SPLIT(FVector(S_Min.X, I_Max.Y, S_Min.Z), FVector(I_Min.X, S_Max.Y, I_Min.Z));
			PCGEX_BIN_SPLIT(FVector(I_Min.X, I_Max.Y, S_Min.Z), FVector(I_Max.X, S_Max.Y, I_Min.Z));
			PCGEX_BIN_SPLIT(FVector(I_Max.X, I_Max.Y, S_Min.Z), FVector(S_Max.X, S_Max.Y, I_Min.Z));
		}
		else if constexpr (SplitMode == EPCGExSpaceSplitMode::Minimal || SplitMode == EPCGExSpaceSplitMode::MinimalCross)
		{
			if constexpr (MainAxis == EPCGExAxis::Up || MainAxis == EPCGExAxis::Down)
			{
				PCGEX_BIN_SPLIT(FVector(I_Min.X, I_Min.Y, I_Max.Z), FVector(I_Max.X, I_Max.Y, S_Max.Z));
				PCGEX_BIN_SPLIT(FVector(I_Min.X, I_Min.Y, S_Min.Z), FVector(I_Max.X, I_Max.Y, I_Min.Z));

				if constexpr (SplitMode == EPCGExSpaceSplitMode::Minimal)
				{
					PCGEX_BIN_SPLIT(FVector(I_Max.X, S_Min.Y, S_Min.Z), S_Max);
					PCGEX_BIN_SPLIT(S_Min, FVector(I_Min.X, S_Max.Y, S_Max.Z));
					PCGEX_BIN_SPLIT(FVector(I_Min.X, I_Max.Y, S_Min.Z), FVector(I_Max.X, S_Max.Y, S_Max.Z));
					PCGEX_BIN_SPLIT(FVector(I_Min.X, S_Min.Y, S_Min.Z), FVector(I_Max.X, I_Min.Y, S_Max.Z));
				}
				else
				{
					PCGEX_BIN_SPLIT(FVector(I_Max.X, I_Min.Y, S_Min.Z), FVector(S_Max.X, I_Max.Y, S_Max.Z));
					PCGEX_BIN_SPLIT(FVector(S_Min.X, I_Min.Y, S_Min.Z), FVector(I_Min.X, I_Max.Y, S_Max.Z));
					PCGEX_BIN_SPLIT(S_Min, FVector(S_Max.X, I_Min.Y, S_Max.Z))
					PCGEX_BIN_SPLIT(FVector(S_Min.X, I_Max.Y, S_Min.Z), S_Max);
				}
			}
			else if constexpr (MainAxis == EPCGExAxis::Left || MainAxis == EPCGExAxis::Right)
			{
				PCGEX_BIN_SPLIT(FVector(I_Min.X, I_Max.Y, I_Min.Z), FVector(I_Max.X, S_Max.Y, I_Max.Z));
				PCGEX_BIN_SPLIT(FVector(I_Min.X, S_Min.Y, I_Min.Z), FVector(I_Max.X, I_Min.Y, I_Max.Z));

				if constexpr (SplitMode == EPCGExSpaceSplitMode::Minimal)
				{
					PCGEX_BIN_SPLIT(FVector(S_Min.X, S_Min.Y, I_Max.Z), S_Max);
					PCGEX_BIN_SPLIT(S_Min, FVector(S_Max.X, S_Max.Y, I_Min.Z));

					PCGEX_BIN_SPLIT(FVector(I_Max.X, S_Min.Y, I_Min.Z), FVector(S_Max.X, S_Max.Y, I_Max.Z));
					PCGEX_BIN_SPLIT(FVector(S_Min.X, S_Min.Y, I_Min.Z), FVector(I_Min.X, S_Max.Y, I_Max.Z));
				}
				else
				{
					PCGEX_BIN_SPLIT(FVector(I_Min.X, S_Min.Y, I_Max.Z), FVector(I_Max.X, S_Max.Y, S_Max.Z));
					PCGEX_BIN_SPLIT(FVector(I_Min.X, S_Min.Y, S_Min.Z), FVector(I_Max.X, S_Max.Y, I_Min.Z));

					PCGEX_BIN_SPLIT(FVector(I_Max.X, S_Min.Y, S_Min.Z), S_Max);
					PCGEX_BIN_SPLIT(S_Min, FVector(I_Min.X, S_Max.Y, S_Max.Z));
				}
			}
			else if constexpr (MainAxis == EPCGExAxis::Forward || MainAxis == EPCGExAxis::Backward)
			{
				PCGEX_BIN_SPLIT(FVector(I_Max.X, I_Min.Y, I_Min.Z), FVector(S_Max.X, I_Max.Y, I_Max.Z));
				PCGEX_BIN_SPLIT(FVector(S_Min.X, I_Min.Y, I_Min.Z), FVector(I_Min.X, I_Max.Y, I_Max.Z));

				if constexpr (SplitMode == EPCGExSpaceSplitMode::Minimal)
				{
					PCGEX_BIN_SPLIT(FVector(S_Min.X, S_Min.Y, I_Max.Z), S_Max);
					PCGEX_BIN_SPLIT(S_Min, FVector(S_Max.X, S_Max.Y, I_Min.Z));

					PCGEX_BIN_SPLIT(FVector(S_Min.X, I_Max.Y, I_Min.Z), FVector(S_Max.X, S_Max.Y, I_Max.Z));
					PCGEX_BIN_SPLIT(FVector(S_Min.X, S_Min.Y, I_Min.Z), FVector(S_Max.X, I_Min.Y, I_Max.Z));
				}
				else
				{
					PCGEX_BIN_SPLIT(FVector(S_Min.X, I_Min.Y, I_Max.Z), FVector(S_Max.X, I_Max.Y, S_Max.Z));
					PCGEX_BIN_SPLIT(FVector(S_Min.X, I_Min.Y, S_Min.Z), FVector(S_Max.X, I_Max.Y, I_Min.Z));

					PCGEX_BIN_SPLIT(FVector(S_Min.X, I_Max.Y, S_Min.Z), S_Max);
					PCGEX_BIN_SPLIT(S_Min, FVector(S_Max.X, I_Min.Y, S_Max.Z));
				}
			}
		}

#undef PCGEX_BIN_SPLIT
	}
}
