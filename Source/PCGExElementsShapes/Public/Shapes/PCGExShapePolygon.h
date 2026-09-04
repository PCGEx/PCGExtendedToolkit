// Copyright 2026 Edward Boucher, Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Core/PCGExShape.h"
#include "Core/PCGExShapeBuilderFactoryProvider.h"
#include "Core/PCGExShapeBuilderOperation.h"
#include "Core/PCGExShapeConfigBase.h"

#include "PCGExShapePolygon.generated.h"

// Not sure if this should be the same class or a different one...
UENUM(BlueprintType)
enum class EPCGExPolygonShapeType : uint8
{
	Convex = 0 UMETA(DisplayName = "Polygon"),
	Star   = 1 UMETA(DisplayName = "Star")
};

UENUM(BlueprintType)
enum class EPCGExPolygonSkeletonConnectionType : uint8
{
	Vertex = 0 UMETA(Description="Connect skeleton to each vertex"),
	Edge   = 1 UMETA(Description="Connect skeleton to each edge"),
	Both   = 2 UMETA(Description="Connect skeleton to both edges and vertices")
};

UENUM(BlueprintType)
enum class EPCGExPolygonFittingMethod : uint8
{
	VertexForward = 0 UMETA(DisplayName="Vertex Forward", Description="Aligns shape so the first vertex faces along the local X axis"),
	EdgeForward   = 1 UMETA(DisplayName="Edge Forward", Description="Aligns shape so the first edge is perpendicular to the local X axis"),
	Custom        = 2 UMETA(DisplayName="Custom", Description="Use a custom angle")
};

USTRUCT(BlueprintType)
struct FPCGExShapePolygonConfig : public FPCGExShapeConfigBase
{
	GENERATED_BODY()
	;

	FPCGExShapePolygonConfig()
		: FPCGExShapeConfigBase()
	{
	}

	// Type of polygon we're creating
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=Settings, meta=(PCG_NotOverridable))
	EPCGExPolygonShapeType PolygonType = EPCGExPolygonShapeType::Convex;

	/** Number of vertices */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorInteger32Abs NumVertices = FPCGExInputShorthandSelectorInteger32Abs(FName("NumVertices"), 5, false);

	/** Whether to add Skeleton */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorBoolean AddSkeleton = FPCGExInputShorthandSelectorBoolean(FName("AddSkeleton"), false, false);

	// Where the skeleton goes if enabled
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=Settings, meta=(DisplayName=" └─ Connection Mode"))
	EPCGExPolygonSkeletonConnectionType SkeletonConnectionMode = EPCGExPolygonSkeletonConnectionType::Vertex;

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType NumVerticesInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector NumVerticesAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	int32 NumVerticesConstant_DEPRECATED = 5;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType AddSkeletonInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector AddSkeletonAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	bool bAddSkeleton_DEPRECATED = false;

#pragma endregion

	// Alignment for the polygon within the bounds of the seed
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=Settings, meta=(PCG_Overridable))
	EPCGExPolygonFittingMethod PolygonOrientation = EPCGExPolygonFittingMethod::VertexForward;

	// Custom alignment
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category=Settings, meta=(PCG_Overridable, EditCondition="PolygonOrientation == EPCGExPolygonFittingMethod::Custom", EditConditionHides))
	float CustomPolygonOrientation = 0.0;

	// Output attributes
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Outputs", meta=(EditCondition=bWriteHullAttribute))
	FName OnHullAttribute = "bIsOnHull";
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Outputs", meta=(InlineEditConditionToggle))
	bool bWriteHullAttribute = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Outputs", meta=(EditCondition=bWriteAngleAttribute))
	FName AngleAttribute = "Angle";
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Outputs", meta=(InlineEditConditionToggle))
	bool bWriteAngleAttribute = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Outputs", meta=(EditCondition=bWriteEdgeIndexAttribute))
	FName EdgeIndexAttribute = "EdgeIndex";
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Outputs", meta=(InlineEditConditionToggle))
	bool bWriteEdgeIndexAttribute = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Outputs", meta=(EditCondition=bWriteEdgeAlphaAttribute))
	FName EdgeAlphaAttribute = "Alpha";
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Settings|Outputs", meta=(InlineEditConditionToggle))
	bool bWriteEdgeAlphaAttribute = false;

	/** If enabled, will flag polygon as being closed if possible. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bIsClosedLoop = true;

#if WITH_EDITOR
	virtual void ApplyDeprecation() override;
#endif
};

namespace PCGExShapes
{
	class FPolygon : public FShape
	{
	public:
		// Computed from seed + config
		double Radius = 1.0;
		double InRadius = 1.0;
		int32 NumVertices = 5;
		int32 PointsPerEdge = 2;
		int32 PointsPerSpoke = 0;
		int32 PointsPerEdgeSpoke = 0;
		double EdgeLength = .2;
		float ScaleAdjustment = 1.0;

		// Taken from config
		bool bHasSkeleton = false;
		bool bConnectSkeletonToVertices = false;
		bool bConnectSkeletonToEdges = false;
		double Orientation = 0;

		const FPCGExShapePolygonConfig* Config = nullptr;

		explicit FPolygon(const PCGExData::FConstPoint& InPointRef)
			: FShape(InPointRef)
		{
		}
	};
}

class FPCGExShapePolygonBuilder : public FPCGExShapeBuilderOperation
{
public:
	FPCGExShapePolygonConfig Config;

	virtual bool PrepareForSeeds(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InSeedDataFacade) override;
	virtual void PrepareShape(const PCGExData::FConstPoint& Seed) override;
	virtual void BuildShape(TSharedPtr<PCGExShapes::FShape> InShape, TSharedPtr<PCGExData::FFacade> InDataFacade, const PCGExData::FScope& Scope, const bool bIsolated = false) override;

protected:
	TSharedPtr<PCGExDetails::TSettingValue<int32>> NumVertices;
	TSharedPtr<PCGExDetails::TSettingValue<bool>> HasSkeleton;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExShapePolygonFactory : public UPCGExShapeBuilderFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExShapePolygonConfig Config;

	virtual TSharedPtr<FPCGExShapeBuilderOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Builder|Params", meta=(PCGExNodeLibraryDoc="shapes/shape-polygon"))
class UPCGExCreateShapePolygonSettings : public UPCGExShapeBuilderFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ShapeBuilderCircle, "Shape : Polygon", "Create points as a regular polygon or star.", FName ("Polygon"))

#endif
	//~End UPCGSettings

	/** Shape properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExShapePolygonConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

protected:
	virtual bool IsCacheable() const override
	{
		return true;
	}
};
