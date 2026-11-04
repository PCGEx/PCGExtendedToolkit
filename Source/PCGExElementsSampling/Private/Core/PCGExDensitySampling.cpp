// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExDensitySampling.h"

#include "LandscapeProxy.h"
#include "PCGElement.h"
#include "PCGPoint.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExBlueNoiseTables.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExMTCommon.h"
#include "Curves/RichCurve.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGLandscapeData.h"
#include "Data/PCGSpatialData.h"
#include "Data/PCGSurfaceData.h"
#include "Elements/PCGSurfaceSampler.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"
#include "Helpers/PCGHelpers.h"
#include "Math/RandomStream.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

#define LOCTEXT_NAMESPACE "PCGExDensitySampling"

namespace PCGExDensitySampling
{
	namespace Local
	{
		/** Seed salts keep the layout offset and the mask offset independent of each other. */
		constexpr int32 TilingSalt = 0x2F1B3C5D;
		constexpr int32 MaskSalt = 0x5C4E1D2B;

		/** Density map node budget; past it the spacing is too fine for the area. */
		constexpr int64 MaxDensityMapNodes = 1ll << 26;

		constexpr int32 DensityMapBlockSize = 4096;
		constexpr int32 GridBlockSize = 64;

		bool ReadAsFloat(const FPCGMetadataAttributeBase* InAttribute, const PCGMetadataEntryKey InKey, float& OutValue)
		{
			switch (static_cast<EPCGMetadataTypes>(InAttribute->GetTypeId()))
			{
			case EPCGMetadataTypes::Float:
				OutValue = static_cast<const FPCGMetadataAttribute<float>*>(InAttribute)->GetValueFromItemKey(InKey);
				return true;
			case EPCGMetadataTypes::Double:
				OutValue = static_cast<float>(static_cast<const FPCGMetadataAttribute<double>*>(InAttribute)->GetValueFromItemKey(InKey));
				return true;
			case EPCGMetadataTypes::Integer32:
				OutValue = static_cast<float>(static_cast<const FPCGMetadataAttribute<int32>*>(InAttribute)->GetValueFromItemKey(InKey));
				return true;
			case EPCGMetadataTypes::Integer64:
				OutValue = static_cast<float>(static_cast<const FPCGMetadataAttribute<int64>*>(InAttribute)->GetValueFromItemKey(InKey));
				return true;
			case EPCGMetadataTypes::Boolean:
				OutValue = static_cast<const FPCGMetadataAttribute<bool>*>(InAttribute)->GetValueFromItemKey(InKey) ? 1.0f : 0.0f;
				return true;
			default:
				// Non-numeric: reported as missing by the reader.
				return false;
			}
		}

		double GetRadius(const double InFactor, const double InSpacing, const double InDensity)
		{
			return InFactor * InSpacing / FMath::Sqrt(FMath::Max(InDensity, MinRadiusDensity));
		}

		bool IsInsideHalfOpen(const FBox2D& InBox, const FVector2D& InPosition)
		{
			return InPosition.X >= InBox.Min.X && InPosition.X < InBox.Max.X && InPosition.Y >= InBox.Min.Y && InPosition.Y < InBox.Max.Y;
		}

		/** Constant or a ramp, so the samples bracketing a range bound it. FRichCurve::Eval returns a lone key's value at every
		 *  time; a pair ramps only when joined linearly or by a step and held flat past both ends (RCIM_None evaluates as cubic). */
		bool IsMonotone(const FRichCurve* InCurve)
		{
			if (!InCurve || InCurve->GetNumKeys() < 2)
			{
				return true;
			}

			if (InCurve->GetNumKeys() > 2)
			{
				return false;
			}

			const ERichCurveInterpMode Interp = InCurve->Keys[0].InterpMode;
			return (Interp == RCIM_Linear || Interp == RCIM_Constant)
				&& InCurve->PreInfinityExtrap == RCCE_Constant
				&& InCurve->PostInfinityExtrap == RCCE_Constant;
		}
	}

#pragma region FRangeMax

	void FRangeMax::Init(const PCGExFloatLUT& InCurve)
	{
		Levels.Reset();
		Global = 1;

		TArray<float> Base;
		if (InCurve->UsesLUT() && InCurve->GetLUTSize() >= 2)
		{
			// Lookup mode is linear between its knots, so the knots a range touches bound it exactly.
			// FPCGExCurveFloatLookup::EvalLUT maps normalized time onto knots [0, GetLUTSize() - 2].
			MaxIndex = InCurve->GetLUTSize() - 2;
			TimeMin = InCurve->GetMinTime();
			const double TimeDelta = static_cast<double>(InCurve->GetMaxTime()) - TimeMin;
			TimeScale = FMath::IsNearlyZero(TimeDelta) ? 1.0 : 1.0 / TimeDelta;

			const float* Knots = InCurve->GetRawLUT();
			Base.SetNumUninitialized(MaxIndex + 1);
			for (int32 i = 0; i <= MaxIndex; i++)
			{
				Base[i] = FMath::Clamp(Knots[i], 0.0f, 1.0f);
			}
		}
		else if (Local::IsMonotone(InCurve->GetCurve()))
		{
			MaxIndex = NumSteps;
			TimeMin = 0;
			TimeScale = 1;
			Base.SetNumUninitialized(NumSteps + 1);
			for (int32 i = 0; i <= NumSteps; i++)
			{
				Base[i] = static_cast<float>(FMath::Clamp(InCurve->Eval(static_cast<double>(i) / NumSteps), 0.0, 1.0));
			}
		}
		else
		{
			// Any other direct curve can peak between samples: no table, so every bound is 1.
			return;
		}

		Levels.Add(MoveTemp(Base));

		// Sparse table: level k holds the max over [i, i + 2^k).
		for (int32 Width = 2; Width <= MaxIndex + 1; Width *= 2)
		{
			const int32 Half = Width / 2;
			const int32 Count = MaxIndex + 2 - Width;

			TArray<float> Next;
			Next.SetNumUninitialized(Count);
			const TArray<float>& Previous = Levels.Last();
			for (int32 i = 0; i < Count; i++)
			{
				Next[i] = FMath::Max(Previous[i], Previous[i + Half]);
			}

			Levels.Add(MoveTemp(Next));
		}

		Global = Get(0, 1);
	}

	double FRangeMax::Get(double A, double B) const
	{
		if (Levels.IsEmpty())
		{
			return 1;
		}

		double KnotA = FMath::Clamp((FMath::Clamp(A, 0.0, 1.0) - TimeMin) * TimeScale, 0.0, 1.0) * MaxIndex;
		double KnotB = FMath::Clamp((FMath::Clamp(B, 0.0, 1.0) - TimeMin) * TimeScale, 0.0, 1.0) * MaxIndex;
		if (KnotA > KnotB)
		{
			Swap(KnotA, KnotB);
		}

		// The extra knot on each side absorbs the float rounding of the lookup's own index math.
		const int32 Lo = FMath::Clamp(FMath::FloorToInt32(KnotA) - 1, 0, MaxIndex);
		const int32 Hi = FMath::Clamp(FMath::CeilToInt32(KnotB) + 1, 0, MaxIndex);
		const int32 Level = FMath::FloorLog2(static_cast<uint32>(Hi - Lo + 1));
		const TArray<float>& Table = Levels[Level];

		return FMath::Max(Table[Lo], Table[Hi - (1 << Level) + 1]);
	}

#pragma endregion

#pragma region FDensityModel

	int32 FDensityModel::AddChannel(const EChannelSource InSource, const FName InAttribute)
	{
		const FName Attribute = InSource == EChannelSource::Attribute ? InAttribute : NAME_None;
		for (int32 i = 0; i < Channels.Num(); i++)
		{
			if (Channels[i].Source == InSource && Channels[i].Attribute == Attribute)
			{
				return i;
			}
		}

		FChannel& Channel = Channels.Emplace_GetRef();
		Channel.Source = InSource;
		Channel.Attribute = Attribute;
		return Channels.Num() - 1;
	}

	void FDensityModel::AddFactor(const EFactorOp InOp, const int32 InChannel, const double InThreshold, const PCGExFloatLUT& InCurve)
	{
		check(Channels.IsValidIndex(InChannel));

		FFactor& Factor = Factors.Emplace_GetRef();
		Factor.Op = InOp;
		Factor.Channel = InChannel;
		Factor.Threshold = InThreshold;

		if (InOp == EFactorOp::Multiply && InCurve)
		{
			Factor.Curve = InCurve;
			Factor.CurveRangeMax.Init(InCurve);
		}
	}

	bool FDensityModel::ReadsMetadata() const
	{
		for (const FChannel& Channel : Channels)
		{
			if (Channel.Source == EChannelSource::Attribute)
			{
				return true;
			}
		}
		return false;
	}

	double FDensityModel::Evaluate(const float* Values) const
	{
		double Density = 1;

		for (const FFactor& Factor : Factors)
		{
			const double Value = Values[Factor.Channel];

			switch (Factor.Op)
			{
			case EFactorOp::Multiply:
				{
					const double Time = FMath::Clamp(Value, 0.0, 1.0);
					Density *= Factor.Curve ? FMath::Clamp(Factor.Curve->Eval(Time), 0.0, 1.0) : Time;
				}
				break;
			case EFactorOp::KeepAbove:
				if (Value <= Factor.Threshold) { return 0; }
				break;
			case EFactorOp::ExcludeAbove:
				if (Value > Factor.Threshold) { return 0; }
				break;
			default:
				checkNoEntry();
				return 0;
			}
		}

		return Density;
	}

	double FDensityModel::UpperBound(const float* Mins, const float* Maxs) const
	{
		double Bound = 1;

		for (const FFactor& Factor : Factors)
		{
			const double Lo = Mins[Factor.Channel];
			const double Hi = Maxs[Factor.Channel];

			switch (Factor.Op)
			{
			case EFactorOp::Multiply:
				{
					const double A = FMath::Clamp(Lo, 0.0, 1.0);
					const double B = FMath::Clamp(Hi, 0.0, 1.0);
					Bound *= Factor.Curve ? Factor.CurveRangeMax.Get(A, B) : B;
				}
				break;
			case EFactorOp::KeepAbove:
				if (Hi <= Factor.Threshold) { return 0; }
				break;
			case EFactorOp::ExcludeAbove:
				if (Lo > Factor.Threshold) { return 0; }
				break;
			default:
				checkNoEntry();
				return 0;
			}
		}

		return Bound;
	}

	double FDensityModel::GlobalUpperBound() const
	{
		double Bound = 1;
		for (const FFactor& Factor : Factors)
		{
			if (Factor.Op == EFactorOp::Multiply && Factor.Curve)
			{
				Bound *= Factor.CurveRangeMax.GetGlobal();
			}
		}
		return Bound;
	}

#pragma endregion

#pragma region FChannelReader

	FChannelReader::FChannelReader(const FDensityModel& InModel)
		: Model(InModel)
	{
		Attributes.Init(nullptr, Model.NumChannels());
		bUnsupported.Init(false, Model.NumChannels());
	}

	void FChannelReader::Read(const UPCGMetadata* Metadata, const FPCGPoint& Point, float* OutValues)
	{
		if (Metadata != CachedMetadata)
		{
			CachedMetadata = Metadata;
			for (const FPCGMetadataAttributeBase*& Attribute : Attributes) { Attribute = nullptr; }
		}

		for (int32 i = 0; i < Model.Channels.Num(); i++)
		{
			const FChannel& Channel = Model.Channels[i];

			if (Channel.Source == EChannelSource::PointDensity)
			{
				OutValues[i] = Point.Density;
				continue;
			}

			OutValues[i] = 0;
			if (!Metadata || bUnsupported[i])
			{
				continue;
			}

			// Resolved lazily: some surfaces only create their attributes on the first projection.
			const FPCGMetadataAttributeBase*& Attribute = Attributes[i];
			if (!Attribute)
			{
				Attribute = Metadata->GetConstAttribute(FPCGAttributeIdentifier(Channel.Attribute));
				if (!Attribute) { continue; }
			}

			if (!Local::ReadAsFloat(Attribute, Point.MetadataEntry, OutValues[i]))
			{
				bUnsupported[i] = true;
			}
		}
	}

	void FChannelReader::GetMissingAttributes(TSet<FName>& OutMissing) const
	{
		if (!CachedMetadata)
		{
			return;
		}

		for (int32 i = 0; i < Model.Channels.Num(); i++)
		{
			if (Model.Channels[i].Source == EChannelSource::Attribute && (!Attributes[i] || bUnsupported[i]))
			{
				OutMissing.Add(Model.Channels[i].Attribute);
			}
		}
	}

#pragma endregion

#pragma region FBlueNoiseTiling

	void FBlueNoiseTiling::Init(const double InMaxDensity, const int32 InSeed)
	{
		Table = PCGExBlueNoise::GetTileXY();
		TileSize = 100.0 * FMath::Sqrt(static_cast<double>(PCGExBlueNoise::TileNumPoints) / InMaxDensity);

		FRandomStream Random(PCGHelpers::ComputeSeed(InSeed, Local::TilingSalt));
		const double OffsetU = Random.FRand();
		const double OffsetV = Random.FRand();
		Origin = FVector2D(OffsetU, OffsetV) * TileSize;
		Symmetry = Random.RandRange(0, 7);
	}

	void FBlueNoiseTiling::GetTileRange(const FBox2D& InBox, FIntPoint& OutMin, FIntPoint& OutMax) const
	{
		OutMin = FIntPoint(FMath::FloorToInt32((InBox.Min.X - Origin.X) / TileSize), FMath::FloorToInt32((InBox.Min.Y - Origin.Y) / TileSize));
		OutMax = FIntPoint(FMath::FloorToInt32((InBox.Max.X - Origin.X) / TileSize), FMath::FloorToInt32((InBox.Max.Y - Origin.Y) / TileSize));
	}

	FBox2D FBlueNoiseTiling::GetTileBox(const FIntPoint& InTile) const
	{
		const FVector2D Min = Origin + FVector2D(InTile.X, InTile.Y) * TileSize;
		return FBox2D(Min, Min + FVector2D(TileSize, TileSize));
	}

	FVector2D FBlueNoiseTiling::GetPosition(const FIntPoint& InTile, const int32 InRank) const
	{
		constexpr double Units = PCGExBlueNoise::TileUnits;

		double U = (Table[InRank * 2] + 0.5) / Units;
		double V = (Table[InRank * 2 + 1] + 0.5) / Units;
		if (Symmetry & 1) { U = 1.0 - U; }
		if (Symmetry & 2) { V = 1.0 - V; }
		if (Symmetry & 4) { Swap(U, V); }

		return Origin + FVector2D(InTile.X + U, InTile.Y + V) * TileSize;
	}

	double FBlueNoiseTiling::GetThreshold(const int32 InRank)
	{
		return (InRank + 0.5) / PCGExBlueNoise::TileNumPoints;
	}

	int32 FBlueNoiseTiling::CountBelow(const double InBound)
	{
		return FMath::Clamp(FMath::CeilToInt32(InBound * PCGExBlueNoise::TileNumPoints - 0.5), 0, PCGExBlueNoise::TileNumPoints);
	}

#pragma endregion

#pragma region FDitherMask

	void FDitherMask::Init(const int32 InSeed)
	{
		Ranks = PCGExBlueNoise::GetDitherMask();

		FRandomStream Random(PCGHelpers::ComputeSeed(InSeed, Local::MaskSalt));
		Offset = FIntPoint(Random.RandRange(0, PCGExBlueNoise::DitherMaskSize - 1), Random.RandRange(0, PCGExBlueNoise::DitherMaskSize - 1));
	}

	double FDitherMask::GetThreshold(const int32 InCellX, const int32 InCellY) const
	{
		constexpr int64 Size = PCGExBlueNoise::DitherMaskSize;
		const int64 X = ((static_cast<int64>(InCellX) + Offset.X) % Size + Size) % Size;
		const int64 Y = ((static_cast<int64>(InCellY) + Offset.Y) % Size + Size) % Size;
		return (Ranks[static_cast<int32>(Y * Size + X)] + 0.5) / static_cast<double>(Size * Size);
	}

#pragma endregion

#pragma region FPreProjection

	void FPreProjection::Init(const FBox& InGridBounds, const FTransform& InSurfaceTransform)
	{
		bNeedsLocalTransformation = !InSurfaceTransform.Rotator().IsNearlyZero();
		Transform = FMatrix::Identity;

		if (bNeedsLocalTransformation)
		{
			const FTransform TranslationTransform(InSurfaceTransform.GetTranslation());
			Transform = TranslationTransform.Inverse().ToMatrixNoScale();
			Transform *= FQuat::FindBetweenNormals(FVector::UpVector, InSurfaceTransform.GetRotation().GetUpVector().GetSafeNormal()).ToMatrix();
			Transform *= TranslationTransform.ToMatrixNoScale();
		}

		// Slightly below the top so one-sided bounds tests keep the start point.
		constexpr double HeightModifier = 1.0 - UE_DOUBLE_SMALL_NUMBER;
		Z = FMath::Abs(InGridBounds.Max.Z) > UE_DOUBLE_SMALL_NUMBER ? InGridBounds.Max.Z * HeightModifier : -UE_DOUBLE_SMALL_NUMBER;
		Z = FMath::Max(Z, InGridBounds.Min.Z);
	}

	FVector FPreProjection::Get(const FVector2D& InPosition) const
	{
		const FVector Location(InPosition.X, InPosition.Y, Z);
		return bNeedsLocalTransformation ? FVector(Transform.TransformPosition(Location)) : Location;
	}

#pragma endregion

#pragma region FDensityMap

	FVector2D FDensityMap::ToLattice(const FVector2D& InPosition) const
	{
		const FVector2D Delta = InPosition - Origin;
		return FVector2D(FVector2D::DotProduct(Delta, AxisU) / StepU - I0, FVector2D::DotProduct(Delta, AxisV) / StepV - J0);
	}

	bool FDensityMap::Init(const FDensityModel& InModel, const FBox2D& InCoverage, const int64 InMaxNodes)
	{
		NumChannels = InModel.NumChannels();
		I0 = 0;
		J0 = 0;

		const FVector2D Corners[4] = {InCoverage.Min, FVector2D(InCoverage.Max.X, InCoverage.Min.Y), FVector2D(InCoverage.Min.X, InCoverage.Max.Y), InCoverage.Max};

		double MinU = MAX_dbl;
		double MaxU = -MAX_dbl;
		double MinV = MAX_dbl;
		double MaxV = -MAX_dbl;
		for (const FVector2D& Corner : Corners)
		{
			const FVector2D L = ToLattice(Corner);
			MinU = FMath::Min(MinU, L.X);
			MaxU = FMath::Max(MaxU, L.X);
			MinV = FMath::Min(MinV, L.Y);
			MaxV = FMath::Max(MaxV, L.Y);
		}

		// One node of margin on every side, so any covered position has its four corners.
		const double SpanU = FMath::CeilToDouble(MaxU) - FMath::FloorToDouble(MinU) + 3;
		const double SpanV = FMath::CeilToDouble(MaxV) - FMath::FloorToDouble(MinV) + 3;
		if (SpanU * SpanV > static_cast<double>(InMaxNodes) || SpanU * SpanV * FMath::Max(1, NumChannels) >= static_cast<double>(MAX_int32))
		{
			return false;
		}

		I0 = FMath::FloorToInt32(MinU) - 1;
		J0 = FMath::FloorToInt32(MinV) - 1;
		NumU = static_cast<int32>(SpanU);
		NumV = static_cast<int32>(SpanV);

		Values.SetNumZeroed(NumU * NumV * NumChannels);
		CellBounds.SetNumZeroed((NumU - 1) * (NumV - 1));
		return true;
	}

	FVector2D FDensityMap::GetNodePosition(const int32 InNodeIndex) const
	{
		const int32 I = I0 + InNodeIndex % NumU;
		const int32 J = J0 + InNodeIndex / NumU;
		return Origin + AxisU * (StepU * I) + AxisV * (StepV * J);
	}

	void FDensityMap::GetCell(const FVector2D& InPosition, int32& OutI, int32& OutJ, double& OutFracU, double& OutFracV) const
	{
		const FVector2D L = ToLattice(InPosition);
		OutI = FMath::Clamp(FMath::FloorToInt32(L.X), 0, NumU - 2);
		OutJ = FMath::Clamp(FMath::FloorToInt32(L.Y), 0, NumV - 2);
		OutFracU = FMath::Clamp(L.X - OutI, 0.0, 1.0);
		OutFracV = FMath::Clamp(L.Y - OutJ, 0.0, 1.0);
	}

	void FDensityMap::ComputeCellBounds(const FDensityModel& InModel)
	{
		const int32 CellsU = NumU - 1;
		const int32 CellsV = NumV - 1;

		PCGExMT::ParallelOrSequential(CellsV, [&](const int32 J)
		{
			TArray<float, TInlineAllocator<16>> Mins;
			TArray<float, TInlineAllocator<16>> Maxs;
			Mins.SetNumUninitialized(NumChannels);
			Maxs.SetNumUninitialized(NumChannels);

			for (int32 I = 0; I < CellsU; I++)
			{
				const int32 N00 = J * NumU + I;
				const int32 Corners[4] = {N00, N00 + 1, N00 + NumU, N00 + NumU + 1};

				for (int32 c = 0; c < NumChannels; c++)
				{
					float Lo = MAX_flt;
					float Hi = -MAX_flt;
					for (const int32 Node : Corners)
					{
						const float Value = Values[Node * NumChannels + c];
						Lo = FMath::Min(Lo, Value);
						Hi = FMath::Max(Hi, Value);
					}
					Mins[c] = Lo;
					Maxs[c] = Hi;
				}

				CellBounds[J * CellsU + I] = static_cast<float>(InModel.UpperBound(Mins.GetData(), Maxs.GetData()));
			}
		}, 16);
	}

	double FDensityMap::UpperBound(const FBox2D& InBox) const
	{
		const FVector2D Corners[4] = {InBox.Min, FVector2D(InBox.Max.X, InBox.Min.Y), FVector2D(InBox.Min.X, InBox.Max.Y), InBox.Max};

		int32 MinI = MAX_int32;
		int32 MaxI = MIN_int32;
		int32 MinJ = MAX_int32;
		int32 MaxJ = MIN_int32;
		for (const FVector2D& Corner : Corners)
		{
			const FVector2D L = ToLattice(Corner);
			MinI = FMath::Min(MinI, FMath::FloorToInt32(L.X));
			MaxI = FMath::Max(MaxI, FMath::FloorToInt32(L.X));
			MinJ = FMath::Min(MinJ, FMath::FloorToInt32(L.Y));
			MaxJ = FMath::Max(MaxJ, FMath::FloorToInt32(L.Y));
		}

		const int32 CellsU = NumU - 1;
		MinI = FMath::Clamp(MinI, 0, NumU - 2);
		MaxI = FMath::Clamp(MaxI, 0, NumU - 2);
		MinJ = FMath::Clamp(MinJ, 0, NumV - 2);
		MaxJ = FMath::Clamp(MaxJ, 0, NumV - 2);

		float Bound = 0;
		for (int32 J = MinJ; J <= MaxJ; J++)
		{
			for (int32 I = MinI; I <= MaxI; I++)
			{
				Bound = FMath::Max(Bound, CellBounds[J * CellsU + I]);
			}
		}

		return Bound;
	}

	double FDensityMap::GetCellBound(const FVector2D& InPosition) const
	{
		int32 I;
		int32 J;
		double FracU;
		double FracV;
		GetCell(InPosition, I, J, FracU, FracV);
		return CellBounds[J * (NumU - 1) + I];
	}

	void FDensityMap::Sample(const FVector2D& InPosition, float* OutValues) const
	{
		int32 I;
		int32 J;
		double FracU;
		double FracV;
		GetCell(InPosition, I, J, FracU, FracV);

		const float AlphaU = static_cast<float>(FracU);
		const float AlphaV = static_cast<float>(FracV);
		const float* V00 = Values.GetData() + (J * NumU + I) * NumChannels;
		const float* V10 = V00 + NumChannels;
		const float* V01 = V00 + NumU * NumChannels;
		const float* V11 = V01 + NumChannels;

		// Lerp along U first, then V: the order the landscape cache interpolates in.
		for (int32 c = 0; c < NumChannels; c++)
		{
			const float Y0 = FMath::Lerp(V00[c], V10[c], AlphaU);
			const float Y1 = FMath::Lerp(V01[c], V11[c], AlphaU);
			OutValues[c] = FMath::Lerp(Y0, Y1, AlphaV);
		}
	}

#pragma endregion

#pragma region Spacing

	void FGrid2D::Init(const FBox2D& InBox, const double InCellSize, const int64 InMaxCells)
	{
		Origin = InBox.Min;
		CellSize = FMath::Max(InCellSize, UE_KINDA_SMALL_NUMBER);

		const FVector2D Size = InBox.GetSize();
		const double MaxCells = static_cast<double>(FMath::Max<int64>(1, InMaxCells));
		while (FMath::Max(1.0, FMath::CeilToDouble(Size.X / CellSize)) * FMath::Max(1.0, FMath::CeilToDouble(Size.Y / CellSize)) > MaxCells)
		{
			CellSize *= 2;
		}

		NumX = FMath::Max(1, FMath::CeilToInt32(Size.X / CellSize));
		NumY = FMath::Max(1, FMath::CeilToInt32(Size.Y / CellSize));
		InvCellSize = 1.0 / CellSize;
	}

	FIntPoint FGrid2D::GetCell(const FVector2D& InPosition) const
	{
		return FIntPoint(
			FMath::Clamp(FMath::FloorToInt32((InPosition.X - Origin.X) * InvCellSize), 0, NumX - 1),
			FMath::Clamp(FMath::FloorToInt32((InPosition.Y - Origin.Y) * InvCellSize), 0, NumY - 1));
	}

	void ResolveStrictSpacing(const TConstArrayView<FVector2D> InPositions, const TConstArrayView<double> InRadii, const TConstArrayView<FPriority> InPriorities, TArray<int8>& OutKeep)
	{
		const int32 Num = InPositions.Num();
		check(InRadii.Num() == Num && InPriorities.Num() == Num);

		OutKeep.Init(1, Num);
		if (Num < 2)
		{
			return;
		}

		FBox2D Box(EForceInit::ForceInit);
		double MinRadius = MAX_dbl;
		for (int32 i = 0; i < Num; i++)
		{
			Box += InPositions[i];
			MinRadius = FMath::Min(MinRadius, InRadii[i]);
		}

		FGrid2D Grid;
		Grid.Init(Box.ExpandBy(1.0), FMath::Max(MinRadius, 1.0), static_cast<int64>(Num) * 4);

		// Static cell lists (counting sort), read concurrently below.
		TArray<int32> CellOf;
		CellOf.SetNumUninitialized(Num);
		TArray<int32> Start;
		Start.Init(0, Grid.NumCells() + 1);
		for (int32 i = 0; i < Num; i++)
		{
			const FIntPoint Cell = Grid.GetCell(InPositions[i]);
			CellOf[i] = Grid.GetIndex(Cell.X, Cell.Y);
			Start[CellOf[i] + 1]++;
		}

		for (int32 c = 0; c < Grid.NumCells(); c++)
		{
			Start[c + 1] += Start[c];
		}

		TArray<int32> Items;
		Items.SetNumUninitialized(Num);
		{
			TArray<int32> Cursor(Start);
			for (int32 i = 0; i < Num; i++)
			{
				Items[Cursor[CellOf[i]]++] = i;
			}
		}

		PCGExMT::ParallelOrSequential(Num, [&](const int32 i)
		{
			const FVector2D Position = InPositions[i];
			const double Radius = InRadii[i];
			const double RadiusSquared = Radius * Radius;
			const FPriority& Priority = InPriorities[i];

			const FIntPoint C0 = Grid.GetCell(Position - FVector2D(Radius, Radius));
			const FIntPoint C1 = Grid.GetCell(Position + FVector2D(Radius, Radius));

			for (int32 Y = C0.Y; Y <= C1.Y; Y++)
			{
				for (int32 X = C0.X; X <= C1.X; X++)
				{
					const int32 Cell = Grid.GetIndex(X, Y);
					for (int32 k = Start[Cell]; k < Start[Cell + 1]; k++)
					{
						const int32 j = Items[k];
						if (j == i || !(InPriorities[j] < Priority))
						{
							continue;
						}

						if (FVector2D::DistSquared(Position, InPositions[j]) < RadiusSquared)
						{
							OutKeep[i] = 0;
							return;
						}
					}
				}
			}
		});
	}

	void ResolvePoisson(const TConstArrayView<FVector2D> InPositions, const TConstArrayView<double> InRadii, TArray<int8>& OutAccepted)
	{
		const int32 Num = InPositions.Num();
		check(InRadii.Num() == Num);

		OutAccepted.Init(0, Num);
		if (Num == 0)
		{
			return;
		}

		FBox2D Box(EForceInit::ForceInit);
		double MinRadius = MAX_dbl;
		for (int32 i = 0; i < Num; i++)
		{
			Box += InPositions[i];
			MinRadius = FMath::Min(MinRadius, InRadii[i]);
		}

		FGrid2D Grid;
		Grid.Init(Box.ExpandBy(1.0), FMath::Max(MinRadius, 1.0), static_cast<int64>(Num) * 4);

		TArray<int32> Head;
		Head.Init(INDEX_NONE, Grid.NumCells());
		TArray<int32> Next;
		Next.Init(INDEX_NONE, Num);

		auto IsBlocked = [&](const FVector2D& Position, const double Radius)
		{
			const double RadiusSquared = Radius * Radius;
			const FIntPoint C0 = Grid.GetCell(Position - FVector2D(Radius, Radius));
			const FIntPoint C1 = Grid.GetCell(Position + FVector2D(Radius, Radius));

			for (int32 Y = C0.Y; Y <= C1.Y; Y++)
			{
				for (int32 X = C0.X; X <= C1.X; X++)
				{
					for (int32 j = Head[Grid.GetIndex(X, Y)]; j != INDEX_NONE; j = Next[j])
					{
						if (FVector2D::DistSquared(Position, InPositions[j]) < RadiusSquared)
						{
							return true;
						}
					}
				}
			}

			return false;
		};

		for (int32 i = 0; i < Num; i++)
		{
			if (IsBlocked(InPositions[i], InRadii[i]))
			{
				continue;
			}

			OutAccepted[i] = 1;
			const FIntPoint Cell = Grid.GetCell(InPositions[i]);
			const int32 CellIndex = Grid.GetIndex(Cell.X, Cell.Y);
			Next[i] = Head[CellIndex];
			Head[CellIndex] = i;
		}
	}

#pragma endregion

#pragma region FConfig

	double FConfig::GetHalo(const double InSpacing) const
	{
		if (!bStrictSpacing || Distribution == EPCGExDensityDistribution::PoissonDisk)
		{
			return 0;
		}

		return Local::GetRadius(SpacingFactor, InSpacing, 0);
	}

	void FConfig::Finalize()
	{
		Tiling.Init(MaxDensity, Seed);
		Dither.Init(Seed);
		GlobalUpperBound = Model.GlobalUpperBound();
	}

#pragma endregion

#pragma region FSurfaceJob

	struct FCandidate
	{
		FVector2D Position = FVector2D::ZeroVector;
		FPriority Priority;
		float Density = 0;
		int32 Seed = 0;
		int32 Rank = 0;
		bool bOwned = false;
	};

	struct FFinalPoint
	{
		FTransform Transform = FTransform::Identity;
		FVector4 Color = FVector4(1, 1, 1, 1);
		int64 MetadataEntry = PCGInvalidEntryKey;
		float Density = 1;
		float Rank = 0;
		int32 Seed = 0;
	};

	struct FScopeResult
	{
		TArray<FCandidate> Candidates;

		/** Exact mode without surface attributes: the first projection is the final one. Index-aligned with Candidates. */
		TArray<FTransform> Projected;
		TArray<FVector4> Colors;

		TArray<int8> Keep;
		TArray<FFinalPoint> Final;
		int32 OutputOffset = 0;
	};

	bool FSurfaceJob::IsOwned(const FVector2D& InPosition) const
	{
		return Local::IsInsideHalfOpen(DomainXY, InPosition);
	}

	bool FSurfaceJob::IsGenerated(const FVector2D& InPosition) const
	{
		return Local::IsInsideHalfOpen(Generation, InPosition);
	}

	void FSurfaceJob::InitializeOutputData() const
	{
		check(Config && Surface && Output);

		FPCGInitializeFromDataParams Params(Surface);
		Params.bInheritMetadata = Config->bOutputSurfaceAttributes;
		Output->GetOut()->InitializeFromDataWithParams(Params);
	}

	bool FSurfaceJob::Prepare(FPCGExContext* InContext, const FBox& InDomain)
	{
		bPrepared = false;
		Domain = InDomain;

		if (!Config || !Surface || !Output || !Domain.IsValid || Domain.Max.X <= Domain.Min.X || Domain.Max.Y <= Domain.Min.Y)
		{
			return false;
		}

		PreProjection.Init(Domain, Surface->GetTransform());

		switch (Config->Distribution)
		{
		case EPCGExDensityDistribution::BlueNoise:
		case EPCGExDensityDistribution::PoissonDisk:
			CandidateSpacing = Config->GetBlueNoiseSpacing();
			break;
		case EPCGExDensityDistribution::JitteredGrid:
			{
				// The engine's own grid math, so positions match the Surface Sampler. Also applies its cell-count and memory guards.
				PCGSurfaceSampler::FSurfaceSamplerData SamplerData;
				SamplerData.Params.bUseLegacyGridCreationMethod = false;
				SamplerData.Params.PointsPerSquaredMeter = static_cast<float>(Config->MaxDensity);
				SamplerData.Params.PointExtents = Config->PointExtents;
				SamplerData.Params.Looseness = Config->Looseness;
				SamplerData.Params.bApplyDensityToPoints = false;
				SamplerData.Params.PointSteepness = Config->PointSteepness;
				if (!SamplerData.Initialize(InContext, Domain, Surface->GetTransform()))
				{
					return false;
				}

				GridCellSize = FVector2D(SamplerData.CellSize.X, SamplerData.CellSize.Y);
				GridInnerCellSize = FVector2D(SamplerData.InnerCellSize.X, SamplerData.InnerCellSize.Y);
				GridInnerCellOffset = FVector2D(SamplerData.InnerCellOffset.X, SamplerData.InnerCellOffset.Y);
				CandidateSpacing = FMath::Sqrt(GridCellSize.X * GridCellSize.Y);
			}
			break;
		default:
			checkNoEntry();
			return false;
		}

		const double Halo = Config->GetHalo(CandidateSpacing);
		DomainXY = FBox2D(FVector2D(Domain.Min), FVector2D(Domain.Max));
		Generation = DomainXY.ExpandBy(Halo);

		if (Config->Evaluation == EPCGExDensityEvaluation::DensityMap)
		{
			InitMapFrame(InContext);
		}

		bPrepared = true;
		return true;
	}

	FBox FSurfaceJob::GetQueryBounds() const
	{
		double Margin = 0;
		if (Config && Config->Evaluation == EPCGExDensityEvaluation::DensityMap)
		{
			// Interpolation reads nodes up to one step along each lattice axis past the generation area.
			Margin = MapStep.X + MapStep.Y;
		}

		const FBox Bounds(
			FVector(Generation.Min.X - Margin, Generation.Min.Y - Margin, Domain.Min.Z),
			FVector(Generation.Max.X + Margin, Generation.Max.Y + Margin, Domain.Max.Z));

		return PreProjection.bNeedsLocalTransformation ? Bounds.TransformBy(PreProjection.Transform) : Bounds;
	}

	UPCGMetadata* FSurfaceJob::CreateScratchMetadata(FPCGExContext* InContext) const
	{
		UPCGMetadata* Metadata = InContext->ManagedObjects->New<UPCGMetadata>();
		if (!Metadata)
		{
			return nullptr;
		}

		Metadata->SetupDomainsFromPCGDataType<UPCGBasePointData>();

		// Creates the surface's attributes up front, so the projections only ever find them.
		Surface->InitializeTargetMetadata(FPCGInitializeFromDataParams(Surface), Metadata);
		return Metadata;
	}

	void FSurfaceJob::ReleaseScratchMetadata(FPCGExContext* InContext, UPCGMetadata* InMetadata, const FChannelReader& InReader)
	{
		{
			FScopeLock Lock(&MissingLock);
			InReader.GetMissingAttributes(MissingAttributes);
		}

		// Destroy only un-roots: deleting the attributes frees the projected values now instead of at the next GC.
		TArray<FPCGAttributeIdentifier> Identifiers;
		TArray<EPCGMetadataTypes> Types;
		InMetadata->GetAllAttributes(Identifiers, Types);
		for (const FPCGAttributeIdentifier& Identifier : Identifiers)
		{
			InMetadata->DeleteAttribute(Identifier);
		}

		InContext->ManagedObjects->Destroy(InMetadata);
	}

	void FSurfaceJob::Run(FPCGExContext* InContext)
	{
		if (!bPrepared || !InContext || InContext->IsWorkCancelled())
		{
			return;
		}

		TSharedPtr<FDensityMap> Map;
		if (Config->Evaluation == EPCGExDensityEvaluation::DensityMap)
		{
			Map = BuildDensityMap(InContext);
			if (!Map)
			{
				return;
			}
		}

		TArray<FScopeResult> Scopes;
		bool bGenerated = false;

		switch (Config->Distribution)
		{
		case EPCGExDensityDistribution::BlueNoise:
		case EPCGExDensityDistribution::PoissonDisk:
			bGenerated = GenerateBlueNoise(InContext, Map.Get(), Scopes);
			break;
		case EPCGExDensityDistribution::JitteredGrid:
			bGenerated = GenerateGrid(InContext, Map.Get(), Scopes);
			break;
		default:
			checkNoEntry();
			break;
		}

		if (!bGenerated || InContext->IsWorkCancelled())
		{
			return;
		}

		ResolveSpacing(Scopes);
		Finalize(InContext, Scopes);

		if (InContext->IsWorkCancelled())
		{
			return;
		}

		Write(Scopes);
	}

	void FSurfaceJob::InitMapFrame(FPCGExContext* InContext)
	{
		MapOrigin = FVector2D::ZeroVector;
		MapAxisU = FVector2D(1, 0);
		MapAxisV = FVector2D(0, 1);
		MapStep = FVector2D(Config->DensityMapSpacing, Config->DensityMapSpacing);

		if (!Config->bAlignToLandscapeVertices)
		{
			return;
		}

		const UPCGLandscapeData* LandscapeData = Cast<UPCGLandscapeData>(Surface);
		const ALandscapeProxy* Proxy = LandscapeData && !LandscapeData->Landscapes.IsEmpty() ? InContext->ResolveObject_AnyThreadTpl(LandscapeData->Landscapes[0]) : nullptr;
		if (!Proxy)
		{
			return;
		}

		// Vertex (I, J) of the landscape sits at LandscapeActorToWorld applied to (I, J, 0): scale is the quad size.
		const FTransform LandscapeTransform = Proxy->LandscapeActorToWorld();
		const FVector Scale = LandscapeTransform.GetScale3D();
		if (Scale.X <= UE_KINDA_SMALL_NUMBER || Scale.Y <= UE_KINDA_SMALL_NUMBER)
		{
			return;
		}

		MapOrigin = FVector2D(LandscapeTransform.GetLocation());
		MapAxisU = FVector2D(LandscapeTransform.GetUnitAxis(EAxis::X)).GetSafeNormal();
		MapAxisV = FVector2D(LandscapeTransform.GetUnitAxis(EAxis::Y)).GetSafeNormal();
		MapStep = FVector2D(
			Scale.X * FMath::Max(1.0, FMath::RoundToDouble(Config->DensityMapSpacing / Scale.X)),
			Scale.Y * FMath::Max(1.0, FMath::RoundToDouble(Config->DensityMapSpacing / Scale.Y)));
	}

	TSharedPtr<FDensityMap> FSurfaceJob::BuildDensityMap(FPCGExContext* InContext)
	{
		PCGEX_MAKE_SHARED(Map, FDensityMap)
		Map->Origin = MapOrigin;
		Map->AxisU = MapAxisU;
		Map->AxisV = MapAxisV;
		Map->StepU = MapStep.X;
		Map->StepV = MapStep.Y;

		if (!Map->Init(Config->Model, Generation, Local::MaxDensityMapNodes))
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, LOCTEXT("DensityMapTooLarge", "The density map would need too many nodes for this area: raise Density Map Spacing."));
			return nullptr;
		}

		if (Config->Model.NumChannels() > 0)
		{
			const int32 NumNodes = Map->NumNodes();
			const int32 NumBlocks = FMath::DivideAndRoundUp(NumNodes, Local::DensityMapBlockSize);
			const bool bNeedsMetadata = Config->Model.ReadsMetadata();
			const FBox LocalBounds(-Config->PointExtents, Config->PointExtents);

			PCGExMT::ParallelOrSequential(NumBlocks, [&](const int32 Block)
			{
				if (InContext->IsWorkCancelled())
				{
					return;
				}

				FChannelReader Reader(Config->Model);
				UPCGMetadata* Scratch = bNeedsMetadata ? CreateScratchMetadata(InContext) : nullptr;
				if (bNeedsMetadata && !Scratch)
				{
					return;
				}

				const int32 End = FMath::Min(NumNodes, (Block + 1) * Local::DensityMapBlockSize);
				for (int32 Node = Block * Local::DensityMapBlockSize; Node < End; Node++)
				{
					FPCGPoint Point;
					if (Surface->ProjectPoint(FTransform(PreProjection.Get(Map->GetNodePosition(Node))), LocalBounds, Config->ProjectionParams, Point, Scratch))
					{
						Reader.Read(Scratch, Point, Map->GetNodeValues(Node));
					}
				}

				if (Scratch)
				{
					ReleaseScratchMetadata(InContext, Scratch, Reader);
				}
			}, 1, EParallelForFlags::Unbalanced);

			if (InContext->IsWorkCancelled())
			{
				return nullptr;
			}
		}

		Map->ComputeCellBounds(Config->Model);
		return Map;
	}

	bool FSurfaceJob::GenerateBlueNoise(FPCGExContext* InContext, const FDensityMap* InMap, TArray<FScopeResult>& OutScopes)
	{
		const FBlueNoiseTiling& Tiling = Config->Tiling;

		FIntPoint TileMin;
		FIntPoint TileMax;
		Tiling.GetTileRange(Generation, TileMin, TileMax);

		const int64 NumTilesX = static_cast<int64>(TileMax.X) - TileMin.X + 1;
		const int64 NumTilesY = static_cast<int64>(TileMax.Y) - TileMin.Y + 1;
		const int64 NumTiles = NumTilesX * NumTilesY;
		if (NumTiles <= 0)
		{
			return true;
		}

		if (NumTiles * PCGExBlueNoise::TileNumPoints >= MAX_int32)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, LOCTEXT("TooManyCandidates", "Too many candidates for this area: lower Max Density or sample a smaller area."));
			return false;
		}

		OutScopes.SetNum(static_cast<int32>(NumTiles));

		const bool bPool = Config->Distribution == EPCGExDensityDistribution::PoissonDisk;
		const int32 GlobalLimit = bPool ? PCGExBlueNoise::TileNumPoints : FBlueNoiseTiling::CountBelow(Config->GlobalUpperBound);
		if (GlobalLimit <= 0)
		{
			return true;
		}

		const bool bExact = InMap == nullptr;
		const bool bNeedsMetadata = bExact && Config->Model.ReadsMetadata();
		const bool bKeepProjection = bExact && !Config->bOutputSurfaceAttributes;
		const FBox LocalBounds(-Config->PointExtents, Config->PointExtents);

		PCGExMT::ParallelOrSequential(static_cast<int32>(NumTiles), [&](const int32 TileIndex)
		{
			if (InContext->IsWorkCancelled())
			{
				return;
			}

			const FIntPoint Tile(TileMin.X + static_cast<int32>(TileIndex % NumTilesX), TileMin.Y + static_cast<int32>(TileIndex / NumTilesX));

			int32 Limit = GlobalLimit;
			if (InMap)
			{
				const double TileBound = InMap->UpperBound(Tiling.GetTileBox(Tile));
				Limit = bPool ? (TileBound > 0 ? Limit : 0) : FMath::Min(Limit, FBlueNoiseTiling::CountBelow(TileBound));
			}

			if (Limit <= 0)
			{
				return;
			}

			FChannelReader Reader(Config->Model);
			UPCGMetadata* Scratch = bNeedsMetadata ? CreateScratchMetadata(InContext) : nullptr;
			if (bNeedsMetadata && !Scratch)
			{
				return;
			}

			TArray<float, TInlineAllocator<16>> Values;
			Values.SetNumZeroed(Config->Model.NumChannels());

			FScopeResult& Scope = OutScopes[TileIndex];
			const int32 TileSeed = PCGHelpers::ComputeSeed(Config->Seed, Tile.X, Tile.Y);

			// Ranks ascend, so the scope lists its candidates in rank order (the Poisson pass relies on it).
			for (int32 Rank = 0; Rank < Limit; Rank++)
			{
				const FVector2D Position = Tiling.GetPosition(Tile, Rank);
				if (!IsGenerated(Position))
				{
					continue;
				}

				const double Threshold = FBlueNoiseTiling::GetThreshold(Rank);
				FPCGPoint Point;

				if (bExact)
				{
					if (!Surface->ProjectPoint(FTransform(PreProjection.Get(Position)), LocalBounds, Config->ProjectionParams, Point, Scratch))
					{
						continue;
					}

					Reader.Read(Scratch, Point, Values.GetData());
				}
				else
				{
					const double CellBound = InMap->GetCellBound(Position);
					if (bPool ? CellBound <= 0 : Threshold >= CellBound)
					{
						continue;
					}

					InMap->Sample(Position, Values.GetData());
				}

				const double Density = Config->Model.Evaluate(Values.GetData());
				if (bPool ? Density <= 0 : Threshold >= Density)
				{
					continue;
				}

				FCandidate& Candidate = Scope.Candidates.Emplace_GetRef();
				Candidate.Position = Position;
				Candidate.Priority = FPriority{static_cast<float>(Threshold), Tile.Y, Tile.X};
				Candidate.Density = static_cast<float>(Density);
				Candidate.Seed = PCGHelpers::ComputeSeed(TileSeed, Rank);
				Candidate.Rank = Rank;
				Candidate.bOwned = IsOwned(Position);

				if (bKeepProjection)
				{
					Scope.Projected.Add(Point.Transform);
					Scope.Colors.Add(Point.Color);
				}
			}

			if (Scratch)
			{
				ReleaseScratchMetadata(InContext, Scratch, Reader);
			}
		}, 1, EParallelForFlags::Unbalanced);

		return !InContext->IsWorkCancelled();
	}

	bool FSurfaceJob::GenerateGrid(FPCGExContext* InContext, const FDensityMap* InMap, TArray<FScopeResult>& OutScopes)
	{
		bool bBlueNoiseThinning = false;
		switch (Config->GridThinning)
		{
		case EPCGExDensityGridThinning::Random:
			break;
		case EPCGExDensityGridThinning::BlueNoise:
			bBlueNoiseThinning = true;
			break;
		default:
			checkNoEntry();
			return false;
		}

		// Every cell whose jitter can land in the generation area: straddling cells are generated on both sides of a border.
		const int64 X0 = FMath::FloorToInt64(Generation.Min.X / GridCellSize.X);
		const int64 X1 = FMath::FloorToInt64(Generation.Max.X / GridCellSize.X);
		const int64 Y0 = FMath::FloorToInt64(Generation.Min.Y / GridCellSize.Y);
		const int64 Y1 = FMath::FloorToInt64(Generation.Max.Y / GridCellSize.Y);
		const int64 NumCellsX = X1 - X0 + 1;
		const int64 NumCellsY = Y1 - Y0 + 1;

		if (NumCellsX <= 0 || NumCellsY <= 0)
		{
			return true;
		}

		if (NumCellsX * NumCellsY >= MAX_int32 || X0 < MIN_int32 || X1 > MAX_int32 || Y0 < MIN_int32 || Y1 > MAX_int32)
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, LOCTEXT("TooManyCells", "Too many cells for this area: lower Max Density or sample a smaller area."));
			return false;
		}

		const int64 NumBlocksX = (NumCellsX + Local::GridBlockSize - 1) / Local::GridBlockSize;
		const int64 NumBlocksY = (NumCellsY + Local::GridBlockSize - 1) / Local::GridBlockSize;
		OutScopes.SetNum(static_cast<int32>(NumBlocksX * NumBlocksY));

		const bool bExact = InMap == nullptr;
		const bool bNeedsMetadata = bExact && Config->Model.ReadsMetadata();
		const bool bKeepProjection = bExact && !Config->bOutputSurfaceAttributes;
		const FBox LocalBounds(-Config->PointExtents, Config->PointExtents);

		PCGExMT::ParallelOrSequential(OutScopes.Num(), [&](const int32 BlockIndex)
		{
			if (InContext->IsWorkCancelled())
			{
				return;
			}

			const int64 BlockX = BlockIndex % NumBlocksX;
			const int64 BlockY = BlockIndex / NumBlocksX;
			const int64 CellX0 = X0 + BlockX * Local::GridBlockSize;
			const int64 CellY0 = Y0 + BlockY * Local::GridBlockSize;
			const int64 CellX1 = FMath::Min(X1, CellX0 + Local::GridBlockSize - 1);
			const int64 CellY1 = FMath::Min(Y1, CellY0 + Local::GridBlockSize - 1);

			FChannelReader Reader(Config->Model);
			UPCGMetadata* Scratch = nullptr;
			TArray<float, TInlineAllocator<16>> Values;
			Values.SetNumZeroed(Config->Model.NumChannels());

			FScopeResult& Scope = OutScopes[BlockIndex];

			for (int64 CellY = CellY0; CellY <= CellY1; CellY++)
			{
				for (int64 CellX = CellX0; CellX <= CellX1; CellX++)
				{
					const int32 X = static_cast<int32>(CellX);
					const int32 Y = static_cast<int32>(CellY);

					// Same draws, in the same order, as the engine Surface Sampler.
					FRandomStream RandomSource(PCGHelpers::ComputeSeed(Config->Seed, X, Y));
					const float Chance = RandomSource.FRand();
					const float RandX = RandomSource.FRand();
					const float RandY = RandomSource.FRand();

					const double Threshold = bBlueNoiseThinning ? Config->Dither.GetThreshold(X, Y) : static_cast<double>(Chance);
					if (Threshold >= Config->GlobalUpperBound)
					{
						continue;
					}

					const FVector2D Position(
						X * GridCellSize.X + GridInnerCellOffset.X + RandX * GridInnerCellSize.X,
						Y * GridCellSize.Y + GridInnerCellOffset.Y + RandY * GridInnerCellSize.Y);

					if (!IsGenerated(Position))
					{
						continue;
					}

					FPCGPoint Point;

					if (bExact)
					{
						if (bNeedsMetadata && !Scratch)
						{
							Scratch = CreateScratchMetadata(InContext);
							if (!Scratch)
							{
								return;
							}
						}

						if (!Surface->ProjectPoint(FTransform(PreProjection.Get(Position)), LocalBounds, Config->ProjectionParams, Point, Scratch))
						{
							continue;
						}

						Reader.Read(Scratch, Point, Values.GetData());
					}
					else
					{
						if (Threshold >= InMap->GetCellBound(Position))
						{
							continue;
						}

						InMap->Sample(Position, Values.GetData());
					}

					const double Density = Config->Model.Evaluate(Values.GetData());
					if (Threshold >= Density)
					{
						continue;
					}

					FCandidate& Candidate = Scope.Candidates.Emplace_GetRef();
					Candidate.Position = Position;
					Candidate.Priority = FPriority{static_cast<float>(Threshold), Y, X};
					Candidate.Density = static_cast<float>(Density);
					Candidate.Seed = RandomSource.GetCurrentSeed();
					Candidate.bOwned = IsOwned(Position);

					if (bKeepProjection)
					{
						Scope.Projected.Add(Point.Transform);
						Scope.Colors.Add(Point.Color);
					}
				}
			}

			if (Scratch)
			{
				ReleaseScratchMetadata(InContext, Scratch, Reader);
			}
		}, 1, EParallelForFlags::Unbalanced);

		return !InContext->IsWorkCancelled();
	}

	void FSurfaceJob::ResolveSpacing(TArray<FScopeResult>& InOutScopes) const
	{
		int32 Total = 0;
		for (FScopeResult& Scope : InOutScopes)
		{
			Scope.Keep.Init(1, Scope.Candidates.Num());
			Total += Scope.Candidates.Num();
		}

		const bool bPoisson = Config->Distribution == EPCGExDensityDistribution::PoissonDisk;
		if ((!bPoisson && !Config->bStrictSpacing) || Total == 0)
		{
			return;
		}

		const double Factor = bPoisson ? Config->PoissonRadiusFactor : Config->SpacingFactor;

		// (scope, candidate) per visited entry.
		TArray<FIntPoint> Order;
		Order.SetNumUninitialized(Total);

		if (bPoisson)
		{
			// Rank-major across tiles, tiles in row order: a stable counting sort over the per-scope rank-ordered lists.
			TArray<int32> Offsets;
			Offsets.Init(0, PCGExBlueNoise::TileNumPoints + 1);
			for (const FScopeResult& Scope : InOutScopes)
			{
				for (const FCandidate& Candidate : Scope.Candidates)
				{
					Offsets[Candidate.Rank + 1]++;
				}
			}

			for (int32 r = 0; r < PCGExBlueNoise::TileNumPoints; r++)
			{
				Offsets[r + 1] += Offsets[r];
			}

			for (int32 s = 0; s < InOutScopes.Num(); s++)
			{
				const TArray<FCandidate>& Candidates = InOutScopes[s].Candidates;
				for (int32 i = 0; i < Candidates.Num(); i++)
				{
					Order[Offsets[Candidates[i].Rank]++] = FIntPoint(s, i);
				}
			}
		}
		else
		{
			int32 Write = 0;
			for (int32 s = 0; s < InOutScopes.Num(); s++)
			{
				for (int32 i = 0; i < InOutScopes[s].Candidates.Num(); i++)
				{
					Order[Write++] = FIntPoint(s, i);
				}
			}
		}

		TArray<FVector2D> Positions;
		TArray<double> Radii;
		Positions.SetNumUninitialized(Total);
		Radii.SetNumUninitialized(Total);
		for (int32 k = 0; k < Total; k++)
		{
			const FCandidate& Candidate = InOutScopes[Order[k].X].Candidates[Order[k].Y];
			Positions[k] = Candidate.Position;
			Radii[k] = Local::GetRadius(Factor, CandidateSpacing, Candidate.Density);
		}

		TArray<int8> Keep;
		if (bPoisson)
		{
			ResolvePoisson(Positions, Radii, Keep);
		}
		else
		{
			TArray<FPriority> Priorities;
			Priorities.SetNumUninitialized(Total);
			for (int32 k = 0; k < Total; k++)
			{
				Priorities[k] = InOutScopes[Order[k].X].Candidates[Order[k].Y].Priority;
			}

			ResolveStrictSpacing(Positions, Radii, Priorities, Keep);
		}

		for (int32 k = 0; k < Total; k++)
		{
			InOutScopes[Order[k].X].Keep[Order[k].Y] = Keep[k];
		}
	}

	void FSurfaceJob::Finalize(FPCGExContext* InContext, TArray<FScopeResult>& InOutScopes) const
	{
		UPCGMetadata* OutMetadata = Config->bOutputSurfaceAttributes ? Output->GetOut()->Metadata.Get() : nullptr;
		const FBox LocalBounds(-Config->PointExtents, Config->PointExtents);

		PCGExMT::ParallelOrSequential(InOutScopes.Num(), [&](const int32 ScopeIndex)
		{
			if (InContext->IsWorkCancelled())
			{
				return;
			}

			FScopeResult& Scope = InOutScopes[ScopeIndex];
			const bool bHasProjection = !Scope.Projected.IsEmpty();

			for (int32 i = 0; i < Scope.Candidates.Num(); i++)
			{
				const FCandidate& Candidate = Scope.Candidates[i];
				if (!Scope.Keep[i] || !Candidate.bOwned)
				{
					continue;
				}

				FPCGPoint Point;
				if (bHasProjection)
				{
					Point.Transform = Scope.Projected[i];
					Point.Color = Scope.Colors[i];
				}
				else if (!Surface->ProjectPoint(FTransform(PreProjection.Get(Candidate.Position)), LocalBounds, Config->ProjectionParams, Point, OutMetadata))
				{
					continue;
				}

				float Density = Config->bWriteLocalDensity ? Candidate.Density : 1.0f;

				if (BoundingShape)
				{
					FPCGPoint BoundingShapeSample;
					if (!BoundingShape->SamplePoint(Point.Transform, LocalBounds, BoundingShapeSample, nullptr))
					{
						continue;
					}

					Density *= BoundingShapeSample.Density;
				}

				FFinalPoint& Final = Scope.Final.Emplace_GetRef();
				Final.Transform = Point.Transform;
				Final.Color = Point.Color;
				Final.MetadataEntry = OutMetadata ? Point.MetadataEntry : PCGInvalidEntryKey;
				Final.Density = Density;
				Final.Rank = Candidate.Priority.Rank;
				Final.Seed = Candidate.Seed;
			}
		}, 1, EParallelForFlags::Unbalanced);
	}

	void FSurfaceJob::Write(TArray<FScopeResult>& InScopes) const
	{
		int32 Total = 0;
		for (FScopeResult& Scope : InScopes)
		{
			Scope.OutputOffset = Total;
			Total += Scope.Final.Num();
		}

		UPCGBasePointData* OutData = Output->GetOut();
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(
			OutData, Total,
			EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::Density | EPCGPointNativeProperties::Seed |
			EPCGPointNativeProperties::MetadataEntry | EPCGPointNativeProperties::Color);

		OutData->SetBoundsMin(-Config->PointExtents);
		OutData->SetBoundsMax(Config->PointExtents);
		OutData->SetSteepness(Config->PointSteepness);

		if (Total == 0)
		{
			return;
		}

		TPCGValueRange<FTransform> Transforms = OutData->GetTransformValueRange(false);
		TPCGValueRange<float> Densities = OutData->GetDensityValueRange(false);
		TPCGValueRange<int32> Seeds = OutData->GetSeedValueRange(false);
		TPCGValueRange<int64> MetadataEntries = OutData->GetMetadataEntryValueRange(false);
		TPCGValueRange<FVector4> Colors = OutData->GetColorValueRange(false);

		PCGExMT::ParallelOrSequential(InScopes.Num(), [&](const int32 ScopeIndex)
		{
			const FScopeResult& Scope = InScopes[ScopeIndex];
			for (int32 k = 0; k < Scope.Final.Num(); k++)
			{
				const FFinalPoint& Final = Scope.Final[k];
				const int32 Index = Scope.OutputOffset + k;
				Transforms[Index] = Final.Transform;
				Densities[Index] = Final.Density;
				Seeds[Index] = Final.Seed;
				MetadataEntries[Index] = Final.MetadataEntry;
				Colors[Index] = Final.Color;
			}
		}, 1);

		if (Config->bWriteRank)
		{
			PCGEX_MAKE_SHARED(Facade, PCGExData::FFacade, Output.ToSharedRef())
			const TSharedPtr<PCGExData::TBuffer<float>> RankWriter = Facade->GetWritable<float>(FPCGAttributeIdentifier(Config->RankAttributeName), 0.0f, true, PCGExData::EBufferInit::New);
			if (RankWriter)
			{
				for (const FScopeResult& Scope : InScopes)
				{
					for (int32 k = 0; k < Scope.Final.Num(); k++)
					{
						RankWriter->SetValue(Scope.OutputOffset + k, Scope.Final[k].Rank);
					}
				}

				Facade->WriteSynchronous();
			}
		}
	}

#pragma endregion
}

#undef LOCTEXT_NAMESPACE
