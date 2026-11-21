// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "UObject/Object.h"

#include "PCGCommon.h"
#include "PCGExFilterCommon.h"
#include "PCGExOctree.h"
#include "Core/PCGExPointFilter.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "Math/PCGExMathBounds.h"
#include "Math/OBB/PCGExOBB.h"

#include "PCGExDynamicMeshFilter.generated.h"

class UPCGDynamicMeshData;

UENUM()
enum class EPCGExMeshCheckType : uint8
{
	IsInside              = 0 UMETA(DisplayName = "Is Inside", Tooltip="Point position is inside a mesh (bounds are ignored)", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Inside Mesh"),
	Intersects            = 1 UMETA(DisplayName = "Intersects", Tooltip="Point position is outside, but the mesh surface crosses the point bounds", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Intersects Mesh"),
	IsInsideOrIntersects  = 2 UMETA(DisplayName = "Is Inside or Intersects", Tooltip="Point position is inside a mesh, or the mesh surface crosses the point bounds", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Inside Intersects Mesh"),
	IsOutsideOrIntersects = 3 UMETA(DisplayName = "Is Outside or Intersects", Tooltip="Point position is outside every mesh, or a mesh surface crosses the point bounds", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Outside Intersects Mesh"),
	IsFullyInside         = 4 UMETA(DisplayName = "Is Fully Inside", Tooltip="Point bounds lie entirely inside a mesh", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Fully Inside Mesh"),
};

namespace PCGExPointFilter
{
	struct FCachedDynamicMesh
	{
		FBox WorldBounds = FBox(ForceInit);
		FVector ProbeVertex = FVector::ZeroVector;
		const UPCGDynamicMeshData* Data = nullptr;
		// Trees point into the mesh owned by Data; the factory's data dependency keeps that data alive.
		FGeometryScriptDynamicMeshBVH BVH;
	};

	/** One point's query shape in mesh space: its position, and (unless the check is position-only) its oriented bounds. */
	struct FMeshQuery
	{
		FVector Position = FVector::ZeroVector;
		PCGExMath::OBB::FOBB Box;
		FBox WorldBounds = FBox(ForceInit);

		template <typename PointType>
		void Build(const PointType& Point, const FTransform& WorldToMesh, const EPCGExPointBoundsSource BoundsSource, const double Expansion, const bool bWithBox)
		{
			const FTransform& Transform = Point.GetTransform();
			Position = WorldToMesh.TransformPosition(Transform.GetLocation());
			WorldBounds = FBox(Position, Position);

			if (!bWithBox)
			{
				return;
			}

			// Rotation + location only: every bounds source is already in the space it means to be in.
			// WorldToMesh carries no scale (PCGExTopology::GetCoordinateSpaceTransform strips it), so extents are untouched.
			const FBox Local = PCGExMath::GetLocalBounds(Point, BoundsSource);
			const FQuat Rotation = WorldToMesh.GetRotation() * Transform.GetRotation();
			const FVector Extents = (Local.GetExtent() + Expansion).ComponentMax(FVector::ZeroVector);
			Box = PCGExMath::OBB::FOBB(
				PCGExMath::OBB::FBounds(Position + Rotation.RotateVector(Local.GetCenter()), Extents, -1),
				PCGExMath::OBB::FOrientation(Rotation));

			Box.ForEachCorner([this](const FVector& Corner) { WorldBounds += Corner; });
		}
	};
}

USTRUCT(BlueprintType)
struct FPCGExDynamicMeshFilterConfig
{
	GENERATED_BODY()

	FPCGExDynamicMeshFilterConfig()
	{
	}

	/** Type of containment check to perform. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExMeshCheckType CheckType = EPCGExMeshCheckType::IsInside;

	/** Space the mesh vertices live in. Points are world space; a component space offsets the test by that component. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGCoordinateSpace CoordinateSpace = EPCGCoordinateSpace::World;

	/** Bounds tested against the mesh surface. Ignored by 'Is Inside'. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="CheckType != EPCGExMeshCheckType::IsInside", EditConditionHides))
	EPCGExPointBoundsSource BoundsSource = EPCGExPointBoundsSource::ScaledBounds;

	/** Uniform expansion of the bounds half-extents. Negative values shrink, down to a degenerate box. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="CheckType != EPCGExMeshCheckType::IsInside", EditConditionHides))
	FPCGExInputShorthandSelectorDouble Expansion = FPCGExInputShorthandSelectorDouble(FName("Expansion"), 0.0);

	/** Fast-winding iso threshold for the inside test. 0.5 suits closed meshes; lower is more permissive on open meshes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0, ClampMax=1))
	double WindingIsoThreshold = 0.5;

	/** If enabled, invert the result of the test. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;

	bool NeedsBox() const
	{
		return CheckType != EPCGExMeshCheckType::IsInside;
	}
};

/**
 * Factory for dynamic-mesh-based point filters.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExDynamicMeshFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExDynamicMeshFilterConfig Config;

	TArray<PCGExPointFilter::FCachedDynamicMesh> CachedMeshes;
	TSharedPtr<PCGExOctree::FItemOctree> Octree;
	// Inverse of the mesh coordinate-space offset, resolved in Prepare(); points are brought into mesh space.
	FTransform WorldToMesh = FTransform::Identity;

	virtual bool Init(FPCGExContext* InContext) override;
	virtual bool DomainCheck() override;

	virtual bool SupportsProxyEvaluation() const override;
	virtual bool SupportsCollectionEvaluation() const override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;

	virtual bool WantsPreparation(FPCGExContext* InContext) override
	{
		return true;
	}

	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;

	virtual void BeginDestroy() override;

private:
	bool bExpansionConstant = true;
};

namespace PCGExPointFilter
{
	class FDynamicMeshFilter final : public ISimpleFilter
	{
	public:
		explicit FDynamicMeshFilter(const TObjectPtr<const UPCGExDynamicMeshFilterFactory>& InFactory)
			: ISimpleFilter(InFactory)
			  , TypedFilterFactory(InFactory)
		{
		}

		const TObjectPtr<const UPCGExDynamicMeshFilterFactory> TypedFilterFactory;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;
		virtual bool Test(const PCGExData::FProxyPoint& Point) const override;
		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;

		virtual ~FDynamicMeshFilter() override = default;

		/** Pure evaluation against explicit factory data; shared by the per-point, proxy and collection paths. */
		static bool TestQuery(const FMeshQuery& Query, const PCGExOctree::FItemOctree& InOctree, const TArray<FCachedDynamicMesh>& InMeshes, EPCGExMeshCheckType InCheckType, double InWindingIsoThreshold, bool bInInvert);

	private:
		const TArray<FCachedDynamicMesh>* CachedMeshes = nullptr;
		const PCGExOctree::FItemOctree* Octree = nullptr;
		EPCGExMeshCheckType CheckType = EPCGExMeshCheckType::IsInside;
		EPCGExPointBoundsSource BoundsSource = EPCGExPointBoundsSource::ScaledBounds;
		double WindingIsoThreshold = 0.5;
		bool bInvert = false;
		bool bNeedsBox = false;
		FTransform WorldToMesh = FTransform::Identity;

		TSharedPtr<PCGExDetails::TSettingValue<double>> Expansion;
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/spatial/filter-inclusion-dynamic-mesh"))
class UPCGExDynamicMeshFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(DynamicMeshFilterFactory, "Filter : Inclusion (Dynamic Mesh)", "Creates a filter definition that tests points against dynamic mesh data.", PCGEX_FACTORY_NAME_PRIORITY)
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
#endif

	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo) override;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

public:
	/** Filter Config. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExDynamicMeshFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;

	virtual bool ShowMissingDataPolicy_Internal() const override
	{
		return true;
	}
#endif
};
