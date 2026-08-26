// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "UObject/Object.h"

#include "CollisionShape.h"
#include "PCGExOctree.h"
#include "Components/PrimitiveComponent.h"
#include "Core/PCGExPointFilter.h"
#include "Math/PCGExMathBounds.h"

#include "PCGExPrimitiveFilter.generated.h"

class UPrimitiveComponent;
class UPCGPrimitiveData;

namespace PCGExPointFilter
{
	struct FCachedPrimitive
	{
		FBox WorldBounds = FBox(ForceInit);
		TWeakObjectPtr<UPrimitiveComponent> PrimitiveComponent = nullptr;
	};
}

USTRUCT(BlueprintType)
struct FPCGExPrimitiveFilterConfig
{
	GENERATED_BODY()

	FPCGExPrimitiveFilterConfig()
	{
	}

	/** Bounds to use on input points for the overlap collision shape. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExPointBoundsSource BoundsSource = EPCGExPointBoundsSource::ScaledBounds;

	/** If enabled, invert the result of the test. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;
};

/**
 * Factory for primitive-based point filters.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExPrimitiveFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExPrimitiveFilterConfig Config;

	TArray<PCGExPointFilter::FCachedPrimitive> CachedPrimitives;
	TSharedPtr<PCGExOctree::FItemOctree> Octree;

	virtual bool SupportsProxyEvaluation() const override;
	virtual bool SupportsCollectionEvaluation() const override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;

	virtual bool WantsPreparation(FPCGExContext* InContext) override
	{
		return true;
	}

	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;

	virtual void BeginDestroy() override;
};

namespace PCGExPointFilter
{
	class FPrimitiveFilter final : public ISimpleFilter
	{
	public:
		explicit FPrimitiveFilter(const TObjectPtr<const UPCGExPrimitiveFilterFactory>& InFactory)
			: ISimpleFilter(InFactory)
			  , TypedFilterFactory(InFactory)
		{
		}

		const TObjectPtr<const UPCGExPrimitiveFilterFactory> TypedFilterFactory;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;
		virtual bool Test(const PCGExData::FProxyPoint& Point) const override;
		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;

		virtual ~FPrimitiveFilter() override = default;

	private:
		const TArray<FCachedPrimitive>* CachedPrimitives = nullptr;
		const PCGExOctree::FItemOctree* Octree = nullptr;
		EPCGExPointBoundsSource BoundsSource = EPCGExPointBoundsSource::ScaledBounds;
		bool bInvert = false;

		template <typename PointType>
		bool TestPoint(const PointType& Point) const
		{
			const FTransform& Transform = Point.GetTransform();
			// GetLocalBounds already applies scale per BoundsSource -- transform by rotation/translation only.
			const FBox LocalBox = PCGExMath::GetLocalBounds(Point, BoundsSource);
			const FBox WorldBox = LocalBox.TransformBy(Transform.ToMatrixNoScale());

			FCollisionShape CollisionShape;
			CollisionShape.SetBox(FVector3f(LocalBox.GetExtent()));

			const FVector BoxCenter = WorldBox.GetCenter();
			const FQuat Rotation = Transform.GetRotation();

			bool bResult = bInvert;

			// Broad phase uses the rotated box's AABB so elongated rotated boxes aren't missed.
			Octree->FindElementsWithBoundsTest(
				FBoxCenterAndExtent(BoxCenter, WorldBox.GetExtent()),
				[&](const PCGExOctree::FItem& Item)
				{
					if (bResult != bInvert)
					{
						return;
					} // Already matched

					const FCachedPrimitive& Primitive = (*CachedPrimitives)[Item.Index];
					if (!Primitive.PrimitiveComponent.IsValid())
					{
						return;
					}

					if (Primitive.PrimitiveComponent->OverlapComponent(BoxCenter, Rotation, CollisionShape))
					{
						bResult = !bInvert;
					}
				});

			return bResult;
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/spatial/filter-primitive-overlap"))
class UPCGExPrimitiveFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PrimitiveFilterFactory, "Filter : Primitive Overlap", "Creates a filter definition that tests point OBB overlap against primitive component collision.")
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

public:
	/** Filter Config. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExPrimitiveFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;

	virtual bool ShowMissingDataPolicy_Internal() const override
	{
		return true;
	}
#endif
};
