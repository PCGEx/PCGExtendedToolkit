// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGProjectionParams.h"
#include "Utils/PCGExCurveLookup.h"
#include "Utils/PCGExWeightCurve.h"

#include "PCGExDensitySampling.generated.h"

class FPCGMetadataAttributeBase;
class UPCGMetadata;
class UPCGSpatialData;
class UPCGSurfaceData;
struct FPCGContext;
struct FPCGExContext;
struct FPCGPoint;

namespace PCGExData
{
	class FPointIO;
}

UENUM()
enum class EPCGExDensityDistribution : uint8
{
	BlueNoise    = 0 UMETA(DisplayName = "Blue Noise", ToolTip="Evenly spread at every density, no grid look. Seam-free across partitions."),
	JitteredGrid = 1 UMETA(DisplayName = "Jittered Grid", ToolTip="One point per cell of a grid at the Max Density spacing, thinned by density. Seam-free across partitions."),
	PoissonDisk  = 2 UMETA(DisplayName = "Poisson Disk", ToolTip="Maximal packing with a density-driven radius. Runs on one thread and is not seam-free across partitions."),
};

UENUM()
enum class EPCGExDensityGridThinning : uint8
{
	Random    = 0 UMETA(DisplayName = "Random", ToolTip="Each cell survives at random, like a Surface Sampler followed by a density filter. Clumps when sparse."),
	BlueNoise = 1 UMETA(DisplayName = "Blue Noise", ToolTip="Surviving cells stay evenly spread at every density."),
};

UENUM()
enum class EPCGExDensityEvaluation : uint8
{
	Exact      = 0 UMETA(DisplayName = "Exact", ToolTip="Projects every candidate onto the surface to read its density."),
	DensityMap = 1 UMETA(DisplayName = "Density Map", ToolTip="Reads the density on a lattice and interpolates it. Fewer projections; edges follow the lattice."),
};

UENUM()
enum class EPCGExDensityDriverSource : uint8
{
	Constant     = 0 UMETA(DisplayName = "Constant", ToolTip="Density 1 everywhere; modifiers still apply."),
	Attribute    = 1 UMETA(DisplayName = "Attribute", ToolTip="A numeric attribute written by the surface projection, such as a landscape layer."),
	PointDensity = 2 UMETA(DisplayName = "Point Density", ToolTip="The density of the projected point."),
};

UENUM()
enum class EPCGExDensityModifierOp : uint8
{
	Multiply     = 0 UMETA(DisplayName = "Multiply", ToolTip="Multiplies the density by the remapped attribute."),
	KeepAbove    = 1 UMETA(DisplayName = "Keep Above", ToolTip="Keeps points only where the attribute is above the threshold."),
	ExcludeAbove = 2 UMETA(DisplayName = "Exclude Above", ToolTip="Removes points where the attribute is above the threshold."),
};

USTRUCT(BlueprintType)
struct PCGEXELEMENTSSAMPLING_API FPCGExDensityModifier
{
	GENERATED_BODY()

	/** Applies this modifier. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bEnabled = true;

	/** Attribute read from the surface projection, such as a landscape layer name. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName Attribute = NAME_None;

	/** How the attribute changes the density. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExDensityModifierOp Operation = EPCGExDensityModifierOp::KeepAbove;

	/** The attribute must be strictly above this value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Operation != EPCGExDensityModifierOp::Multiply", EditConditionHides))
	float Threshold = 0;

	/** Remaps the attribute, clamped to 0-1, before it multiplies the density. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Operation == EPCGExDensityModifierOp::Multiply", EditConditionHides))
	FPCGExWeightCurve Curve = FPCGExWeightCurve(0, 1);
};

/** Density-driven surface sampling: density model, candidate layouts, spacing passes and the per-surface job. */
namespace PCGExDensitySampling
{
	/** Radii are derived from at least this density, so they stay within 16x the spacing at max density. */
	inline constexpr double MinRadiusDensity = 1.0 / 256.0;

#pragma region Density model

	enum class EChannelSource : uint8
	{
		Attribute,
		PointDensity,
	};

	enum class EFactorOp : uint8
	{
		Multiply,
		KeepAbove,
		ExcludeAbove,
	};

	/** One value the model reads per sample. */
	struct PCGEXELEMENTSSAMPLING_API FChannel
	{
		EChannelSource Source = EChannelSource::Attribute;
		FName Attribute = NAME_None;
	};

	/** Max of a curve lookup over a sub-range of [0, 1], with values clamped to [0, 1] as FDensityModel::Evaluate reads them. */
	class PCGEXELEMENTSSAMPLING_API FRangeMax
	{
	public:
		void Init(const PCGExFloatLUT& InCurve);

		/** Upper bound over [A, B]. Never below: it spans one knot past each end, and is 1 for a curve no table bounds. */
		double Get(double A, double B) const;

		double GetGlobal() const { return Global; }

	private:
		static constexpr int32 NumSteps = 256;

		/** Knot of time T: Clamp((T - TimeMin) * TimeScale, 0, 1) * MaxIndex. */
		double TimeMin = 0;
		double TimeScale = 1;
		int32 MaxIndex = 0;

		/** Sparse table over the knots; empty for a curve that may peak between samples. */
		TArray<TArray<float>> Levels;
		double Global = 1;
	};

	struct PCGEXELEMENTSSAMPLING_API FFactor
	{
		EFactorOp Op = EFactorOp::Multiply;
		int32 Channel = INDEX_NONE;
		double Threshold = 0;

		/** Multiply only; null multiplies by the raw value clamped to [0, 1]. */
		PCGExFloatLUT Curve;
		FRangeMax CurveRangeMax;
	};

	/** The local density in [0, 1]: the product of every factor. No factor means density 1. */
	class PCGEXELEMENTSSAMPLING_API FDensityModel
	{
	public:
		TArray<FChannel> Channels;
		TArray<FFactor> Factors;

		/** Returns the index of the channel, adding it on first use. */
		int32 AddChannel(EChannelSource InSource, FName InAttribute);
		void AddFactor(EFactorOp InOp, int32 InChannel, double InThreshold, const PCGExFloatLUT& InCurve);

		int32 NumChannels() const { return Channels.Num(); }
		bool ReadsMetadata() const;

		double Evaluate(const float* Values) const;

		/** Upper bound of Evaluate over any values within [Mins, Maxs], per channel. */
		double UpperBound(const float* Mins, const float* Maxs) const;

		/** Upper bound of Evaluate over any values. */
		double GlobalUpperBound() const;
	};

	/** Driver lookup for Max Spacing, as a density: the curve's output (clamped to 0-1) blends the spacing linearly from
	 *  InSpacingRatio x the spacing at max density at 0, down to that spacing at 1. Ratios below 1 act as 1. */
	PCGEXELEMENTSSAMPLING_API PCGExFloatLUT MakeSpacingLookup(const FPCGExWeightCurve& InCurve, double InSpacingRatio);

	/** Reads the model's channels off projected points. Not thread-safe: one per scope, it caches attribute lookups. */
	class PCGEXELEMENTSSAMPLING_API FChannelReader
	{
	public:
		explicit FChannelReader(const FDensityModel& InModel);

		/** Missing or non-numeric attributes read 0. */
		void Read(const UPCGMetadata* Metadata, const FPCGPoint& Point, float* OutValues);

		/** Names of the attributes that were never found. */
		void GetMissingAttributes(TSet<FName>& OutMissing) const;

	private:
		const FDensityModel& Model;
		const UPCGMetadata* CachedMetadata = nullptr;
		TArray<const FPCGMetadataAttributeBase*> Attributes;
		TArray<bool> bUnsupported;
	};

#pragma endregion

#pragma region Layouts

	/** Lower wins: rank, then world-space keys, so every partition orders shared candidates the same way. */
	struct FPriority
	{
		float Rank = 0;
		int32 KeyA = 0;
		int32 KeyB = 0;

		bool operator<(const FPriority& Other) const
		{
			if (Rank != Other.Rank) { return Rank < Other.Rank; }
			if (KeyA != Other.KeyA) { return KeyA < Other.KeyA; }
			return KeyB < Other.KeyB;
		}
	};

	/** World-aligned tiling of the progressive blue-noise tile. */
	struct PCGEXELEMENTSSAMPLING_API FBlueNoiseTiling
	{
		double TileSize = 1;
		FVector2D Origin = FVector2D::ZeroVector;

		/** Mirror / swap applied to every tile alike, so the tiling stays seamless. */
		int32 Symmetry = 0;

		/** Sizes the tile so it holds MaxDensity points per square meter, and picks the seed's offset and symmetry. */
		void Init(double InMaxDensity, int32 InSeed);

		void GetTileRange(const FBox2D& InBox, FIntPoint& OutMin, FIntPoint& OutMax) const;
		FBox2D GetTileBox(const FIntPoint& InTile) const;
		FVector2D GetPosition(const FIntPoint& InTile, int32 InRank) const;

		static double GetThreshold(int32 InRank);

		/** Number of ranks whose threshold is below InBound. */
		static int32 CountBelow(double InBound);

	private:
		TConstArrayView<uint16> Table;
	};

	/** The dither mask tiled over the jittered grid, at a per-seed offset. */
	struct PCGEXELEMENTSSAMPLING_API FDitherMask
	{
		FIntPoint Offset = FIntPoint::ZeroValue;

		/** Picks the seed's offset. */
		void Init(int32 InSeed);

		/** Blue-noise threshold of a jittered-grid cell. */
		double GetThreshold(int32 InCellX, int32 InCellY) const;

	private:
		TConstArrayView<uint16> Ranks;
	};

	/** Where a 2D candidate starts before projection, as the engine Surface Sampler does it. */
	struct PCGEXELEMENTSSAMPLING_API FPreProjection
	{
		bool bNeedsLocalTransformation = false;
		FMatrix Transform = FMatrix::Identity;
		double Z = 0;

		void Init(const FBox& InGridBounds, const FTransform& InSurfaceTransform);
		FVector Get(const FVector2D& InPosition) const;
	};

#pragma endregion

#pragma region Density map

	/** Channel values on a fixed lattice, interpolated bilinearly. Node (I, J) sits at Origin + I * StepU * AxisU + J * StepV * AxisV. */
	class PCGEXELEMENTSSAMPLING_API FDensityMap
	{
	public:
		FVector2D Origin = FVector2D::ZeroVector;
		FVector2D AxisU = FVector2D(1, 0);
		FVector2D AxisV = FVector2D(0, 1);
		double StepU = 100;
		double StepV = 100;

		/** Sizes the lattice to cover InCoverage. False when it would exceed InMaxNodes. */
		bool Init(const FDensityModel& InModel, const FBox2D& InCoverage, int64 InMaxNodes);

		int32 NumNodes() const { return NumU * NumV; }
		int32 GetNumU() const { return NumU; }
		int32 GetNumV() const { return NumV; }
		FVector2D GetNodePosition(int32 InNodeIndex) const;
		float* GetNodeValues(int32 InNodeIndex) { return Values.GetData() + static_cast<int64>(InNodeIndex) * NumChannels; }

		/** Call once every node value is written. */
		void ComputeCellBounds(const FDensityModel& InModel);

		/** Upper bound of the density over the cells overlapping InBox. */
		double UpperBound(const FBox2D& InBox) const;

		/** Upper bound of the density over the cell holding InPosition. */
		double GetCellBound(const FVector2D& InPosition) const;

		void Sample(const FVector2D& InPosition, float* OutValues) const;

	private:
		int32 NumChannels = 0;
		int32 I0 = 0;
		int32 J0 = 0;
		int32 NumU = 0;
		int32 NumV = 0;
		TArray<float> Values;
		TArray<float> CellBounds;

		FVector2D ToLattice(const FVector2D& InPosition) const;
		void GetCell(const FVector2D& InPosition, int32& OutI, int32& OutJ, double& OutFracU, double& OutFracV) const;
	};

#pragma endregion

#pragma region Spacing

	/** Uniform 2D grid over a box, capped in cell count. */
	struct PCGEXELEMENTSSAMPLING_API FGrid2D
	{
		FVector2D Origin = FVector2D::ZeroVector;
		double CellSize = 1;
		double InvCellSize = 1;
		int32 NumX = 0;
		int32 NumY = 0;

		void Init(const FBox2D& InBox, double InCellSize, int64 InMaxCells);
		FIntPoint GetCell(const FVector2D& InPosition) const;
		int32 GetIndex(int32 InX, int32 InY) const { return InY * NumX + InX; }
		int32 NumCells() const { return NumX * NumY; }
	};

	/** Drops a candidate when a higher-priority candidate lies within its own radius. Order-independent and parallel. */
	PCGEXELEMENTSSAMPLING_API void ResolveStrictSpacing(TConstArrayView<FVector2D> InPositions, TConstArrayView<double> InRadii, TConstArrayView<FPriority> InPriorities, TArray<int8>& OutKeep);

	/** Visits candidates in order and accepts each one with no accepted point within its own radius. Sequential. */
	PCGEXELEMENTSSAMPLING_API void ResolvePoisson(TConstArrayView<FVector2D> InPositions, TConstArrayView<double> InRadii, TArray<int8>& OutAccepted);

#pragma endregion

#pragma region Job

	/** Everything a surface job reads; built once per execution. */
	struct PCGEXELEMENTSSAMPLING_API FConfig
	{
		EPCGExDensityDistribution Distribution = EPCGExDensityDistribution::BlueNoise;
		EPCGExDensityGridThinning GridThinning = EPCGExDensityGridThinning::BlueNoise;
		EPCGExDensityEvaluation Evaluation = EPCGExDensityEvaluation::Exact;

		/** Points per square meter where the density is 1. */
		double MaxDensity = 0.1;
		FVector PointExtents = FVector(50);
		float Looseness = 1;
		float PointSteepness = 1;

		double DensityMapSpacing = 400;
		bool bAlignToLandscapeVertices = true;

		bool bStrictSpacing = false;
		double SpacingFactor = 0.75;
		double PoissonRadiusFactor = 0.83;

		bool bOutputSurfaceAttributes = true;
		bool bWriteLocalDensity = false;
		bool bWriteRank = false;
		FName RankAttributeName = FName("Rank");

		int32 Seed = 0;

		FDensityModel Model;
		FPCGProjectionParams ProjectionParams;

		/** Set by Finalize. */
		FBlueNoiseTiling Tiling;
		FDitherMask Dither;
		double GlobalUpperBound = 1;

		/** Spacing at max density, in cm: the blue-noise spacing and the jittered grid's cell size. */
		double GetSpacingAtMaxDensity() const { return 100.0 / FMath::Sqrt(MaxDensity); }

		/** Halo around the domain that strict spacing reads, in cm (for a candidate spacing at max density). */
		double GetHalo(double InSpacing) const;

		/** Derives the tiling, the dither mask and the global bound from the fields above. */
		void Finalize();
	};

	struct FCandidate;
	struct FScopeResult;

	/** Samples one surface into its output point data. Run() is self-contained and may run on any thread. */
	class PCGEXELEMENTSSAMPLING_API FSurfaceJob : public TSharedFromThis<FSurfaceJob>
	{
	public:
		TSharedPtr<const FConfig> Config;
		const UPCGSurfaceData* Surface = nullptr;
		const UPCGSpatialData* BoundingShape = nullptr;
		TSharedPtr<PCGExData::FPointIO> Output;

		/** Sampling area in grid space; a candidate belongs to this job when its XY lies in [Min, Max). */
		FBox Domain = FBox(EForceInit::ForceInit);

		/** Initializes the output from the surface; with surface attributes off, none of its metadata is inherited. */
		void InitializeOutputData() const;

		/** Sets up the generation area and the pre-projection. False leaves the output empty. */
		bool Prepare(FPCGExContext* InContext, const FBox& InDomain);

		bool IsPrepared() const { return bPrepared; }

		/** Box to prime the surface's spatial queries over before Run(). */
		FBox GetQueryBounds() const;

		void Run(FPCGExContext* InContext);

		/** Attributes the density model asked for and never found, gathered by Run(). */
		TSet<FName> MissingAttributes;

	private:
		/** Domain in XY: ownership is tested against it. */
		FBox2D DomainXY = FBox2D(EForceInit::ForceInit);

		/** Where candidates are generated and evaluated: the domain plus the strict-spacing halo. */
		FBox2D Generation = FBox2D(EForceInit::ForceInit);
		FPreProjection PreProjection;

		/** Spacing between candidates at max density, in cm; also the jittered grid's cell size. */
		double CandidateSpacing = 0;

		/** Jittered grid only: on each axis, a point lands in [Offset, Offset + Range) of its cell. */
		double GridJitterOffset = 0;
		double GridJitterRange = 0;

		/** Density map only: the lattice frame, set once so the priming margin and the map agree on the steps. */
		FVector2D MapOrigin = FVector2D::ZeroVector;
		FVector2D MapAxisU = FVector2D(1, 0);
		FVector2D MapAxisV = FVector2D(0, 1);
		FVector2D MapStep = FVector2D::ZeroVector;

		bool bPrepared = false;

		mutable FCriticalSection MissingLock;

		bool IsOwned(const FVector2D& InPosition) const;
		bool IsGenerated(const FVector2D& InPosition) const;

		void InitMapFrame(FPCGExContext* InContext);
		TSharedPtr<FDensityMap> BuildDensityMap(FPCGExContext* InContext);
		bool GenerateBlueNoise(FPCGExContext* InContext, const FDensityMap* InMap, TArray<FScopeResult>& OutScopes);
		bool GenerateGrid(FPCGExContext* InContext, const FDensityMap* InMap, TArray<FScopeResult>& OutScopes);
		void ResolveSpacing(TArray<FScopeResult>& InOutScopes) const;
		void Finalize(FPCGExContext* InContext, TArray<FScopeResult>& InOutScopes) const;
		void Write(TArray<FScopeResult>& InScopes) const;

		UPCGMetadata* CreateScratchMetadata(FPCGExContext* InContext) const;

		/** Gathers missing attributes, then frees the scratch's attributes: InReader must not read it afterwards. */
		void ReleaseScratchMetadata(FPCGExContext* InContext, UPCGMetadata* InMetadata, const FChannelReader& InReader);
	};

#pragma endregion
}
