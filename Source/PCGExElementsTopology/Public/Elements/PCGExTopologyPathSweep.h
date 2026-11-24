// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExBlendingCommon.h"
#include "PCGExTopology.h"
#include "Core/PCGExPathProcessor.h"
#include "Data/PCGDynamicMeshData.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"

#include "PCGExTopologyPathSweep.generated.h"

namespace PCGExBlending
{
	class IBlendOperation;
}

namespace PCGExTopologyPathSweep
{
	const FName SourceProfile = TEXT("Profile");
	const FName SourceSecondaryProfile = TEXT("Secondary Profile");

	/** A profile flattened to XY and normalized by the longest side of its bounds, which then runs 0..1. */
	struct FProfile
	{
		TArray<FVector2D> Positions;
		TArray<double> Alphas; // Normalized distance along the profile as authored; a closed profile has a last entry of 1 for its seam
		TArray<FVector4> Colors;

		FVector2D Center = FVector2D::ZeroVector; // Bounds center, normalized
		FVector2D Origin = FVector2D::ZeroVector; // Origin of the profile data, normalized

		double RawLength = 0; // Length as authored, before normalization
		int32 Num = 0;
		int32 NumRows = 0;   // Quad rows: one more than an open profile when closed
		int32 NumUVRows = 0; // A closed profile's last row wraps onto the first point, which then needs its own UVs
		bool bClosed = false;

		FVector2D GetPositionAt(const double Alpha) const;
	};
}

UENUM()
enum class EPCGExPathSweepMode : uint8
{
	Single = 0 UMETA(DisplayName = "Single", ToolTip="One profile, swept along a single cross axis."),
	Dual   = 1 UMETA(DisplayName = "Dual", ToolTip="Two profiles, one per cross axis."),
};

UENUM()
enum class EPCGExPathSweepAxis : uint8
{
	Y = 0 UMETA(DisplayName = "Y", ToolTip="The profile extends sideways from the path."),
	Z = 1 UMETA(DisplayName = "Z", ToolTip="The profile extends upward from the path."),
};

UENUM()
enum class EPCGExPathSweepUVMode : uint8
{
	Normalized = 0 UMETA(DisplayName = "Normalized", ToolTip="0..1 over the whole length."),
	Distance   = 1 UMETA(DisplayName = "Distance", ToolTip="Distance divided by the tile size: world distance along the path for U, the profile's length as authored for V."),
};

UENUM()
enum class EPCGExPathSweepVLayout : uint8
{
	Continuous = 0 UMETA(DisplayName = "Continuous", ToolTip="The primary profile runs reversed so both profiles meet at the split, as one unfolded strip."),
	Sequential = 1 UMETA(DisplayName = "Sequential", ToolTip="Both profiles run first to last point, each in its own range."),
};

UENUM()
enum class EPCGExPathSweepVSplit : uint8
{
	Midpoint    = 0 UMETA(DisplayName = "Midpoint", ToolTip="The split sits at a fixed V."),
	LengthRatio = 1 UMETA(DisplayName = "Length Ratio", ToolTip="The split follows the ratio between the two profiles' lengths, as authored."),
};

UENUM()
enum class EPCGExPathSweepAnchor : uint8
{
	Reference = 0 UMETA(DisplayName = "Reference", ToolTip="A point along the profile sits on the path."),
	Center    = 1 UMETA(DisplayName = "Center", ToolTip="The center of the profile's bounds sits on the path."),
	Origin    = 2 UMETA(DisplayName = "Origin", ToolTip="The origin of the profile data sits on the path, for profiles drawn around it."),
};

USTRUCT(BlueprintType)
struct FPCGExPathSweepProfileDetails
{
	GENERATED_BODY()

	FPCGExPathSweepProfileDetails() = default;

	/** Scale of the profile's X, along its cross axis. The profile's longest side is one unit, so equal Depth and
	 *  Width keep its proportions. Negative values mirror it, which also turns its faces around. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorDouble Depth = FPCGExInputShorthandSelectorDouble(FName("Depth"), 100.0, false);

	/** Scale of the profile's Y, along the other cross axis, in the same unit as Depth. Negative values mirror it,
	 *  which also turns its faces around. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorDouble Width = FPCGExInputShorthandSelectorDouble(FName("Width"), 100.0, false);

	/** Which part of the profile sits on the path. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExPathSweepAnchor Anchor = EPCGExPathSweepAnchor::Reference;

	/** Normalized position along the profile that sits on the path: 0 is its first point, 1 its last. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="Anchor == EPCGExPathSweepAnchor::Reference", EditConditionHides))
	FPCGExInputShorthandSelectorDouble01 Reference = FPCGExInputShorthandSelectorDouble01(FName("Reference"), 0.0, false);
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(Keywords = "extrude profile loft ribbon wall", PCGExNodeLibraryDoc="topology/topology-path-sweep"))
class UPCGExTopologyPathSweepSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(TopologyPathSweep, "Topology : Path Sweep", "Sweep one or two profiles along paths and output a dynamic mesh.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::DynamicMesh;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return FLinearColor::White;
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Single sweeps one profile along one cross axis; Dual sweeps one profile per cross axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExPathSweepMode Mode = EPCGExPathSweepMode::Single;

	/** Cross axis the primary profile's depth runs along. In Dual mode the secondary profile takes the other one. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExPathSweepAxis PrimaryAxis = EPCGExPathSweepAxis::Y;

	/** Size and placement of the primary profile, read per path point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Profile - Primary"))
	FPCGExPathSweepProfileDetails Primary;

	/** Size and placement of the secondary profile, read per path point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Profile - Secondary", EditCondition="Mode == EPCGExPathSweepMode::Dual", EditConditionHides))
	FPCGExPathSweepProfileDetails Secondary;

	/** How the cross axes are oriented around the path. Custom reads an up vector per point, which carries point roll. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Orient", meta = (PCG_NotOverridable))
	EPCGExPathNormalDirection Orientation = EPCGExPathNormalDirection::Custom;

	/** Up vector the profiles roll with around the path. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Orient", meta = (PCG_Overridable, EditCondition="Orientation == EPCGExPathNormalDirection::Custom", EditConditionHides))
	FPCGExInputShorthandSelectorDirection UpVector = FPCGExInputShorthandSelectorDirection(FName("$Rotation.Up"), FVector::UpVector, true);

	/** Maximum stretch applied to the profiles at corners to keep their size constant along each segment. 1 disables it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Orient", meta = (PCG_Overridable, ClampMin=1, ClampMax=100))
	double MiterLimit = 4;

	/** UV channel receiving the generated coordinates: U runs along the path, V along the profile. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|UVs", meta = (PCG_NotOverridable, ClampMin=0, ClampMax=7, UIMin=0, UIMax=7))
	int32 UVChannel = 0;

	/** How U is measured along the path. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|UVs", meta = (PCG_NotOverridable))
	EPCGExPathSweepUVMode UMode = EPCGExPathSweepUVMode::Normalized;

	/** How V is measured along the profile. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|UVs", meta = (PCG_NotOverridable))
	EPCGExPathSweepUVMode VMode = EPCGExPathSweepUVMode::Normalized;

	/** Size of one UV tile for the axes measured in distance: world units along the path (X), the profiles' own
	 *  authored units along them (Y). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|UVs", meta = (PCG_Overridable, EditCondition="UMode == EPCGExPathSweepUVMode::Distance || VMode == EPCGExPathSweepUVMode::Distance", EditConditionHides))
	FVector2D TileSize = FVector2D(100, 100);

	/** How the two profiles share V. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|UVs", meta = (PCG_NotOverridable, EditCondition="Mode == EPCGExPathSweepMode::Dual", EditConditionHides))
	EPCGExPathSweepVLayout VLayout = EPCGExPathSweepVLayout::Continuous;

	/** Where V switches from the primary to the secondary profile. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|UVs", meta = (PCG_NotOverridable, EditCondition="Mode == EPCGExPathSweepMode::Dual && VMode == EPCGExPathSweepUVMode::Normalized", EditConditionHides))
	EPCGExPathSweepVSplit VSplit = EPCGExPathSweepVSplit::Midpoint;

	/** V at which the primary profile ends and the secondary one starts. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|UVs", meta = (PCG_Overridable, DisplayName=" └─ Midpoint", EditCondition="Mode == EPCGExPathSweepMode::Dual && VMode == EPCGExPathSweepUVMode::Normalized && VSplit == EPCGExPathSweepVSplit::Midpoint", EditConditionHides, ClampMin=0, ClampMax=1))
	double VMidpoint = 0.5;

	/** Blends the profile point color (B) into the path point color (A) to make the vertex color. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Mesh", meta = (PCG_NotOverridable))
	EPCGExABBlendingType ColorBlending = EPCGExABBlendingType::Multiply;

	/** Color blend weight, only read by Lerp, Weight and the weighted modes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Mesh", meta = (PCG_Overridable, DisplayName=" └─ Weight"))
	FPCGExInputShorthandSelectorDouble ColorBlendWeight = FPCGExInputShorthandSelectorDouble(FName("ColorBlendWeight"), 1.0, false);

	/** Material of the secondary profile, on its own material slot. Uses the topology material when unset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Mesh", meta = (PCG_Overridable, EditCondition="Mode == EPCGExPathSweepMode::Dual", EditConditionHides))
	TSoftObjectPtr<UMaterialInterface> SecondaryMaterial;

	/** Topology settings. Triangulation settings are ignored: the mesh is a regular grid. Faces follow the direction
	 *  the profiles are drawn in; Flip Normals turns them around. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Mesh", meta = (PCG_Overridable))
	FPCGExTopologyDetails Topology;

protected:
	virtual bool IsCacheable() const override
	{
		return false;
	}
};

struct FPCGExTopologyPathSweepContext final : FPCGExPathProcessorContext
{
	friend class FPCGExTopologyPathSweepElement;

	TSharedPtr<PCGExTopologyPathSweep::FProfile> Profile;
	/** The primary profile when the secondary pin is unconnected; null in Single mode. */
	TSharedPtr<PCGExTopologyPathSweep::FProfile> SecondaryProfile;

	/** Shared by every processor; null when a color is copied as-is. */
	TSharedPtr<PCGExBlending::IBlendOperation> ColorBlend;
	bool bColorBlendWeighted = false;
	bool bProfileColorOnly = false;

	// Settings enums, resolved once in Boot
	bool bPrimaryOnY = true;
	bool bUDistance = false;
	bool bVDistance = false;
	bool bVContinuous = true;
	double VSplit = 0.5;

	virtual void RegisterAssetDependencies() override;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExTopologyPathSweepElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(TopologyPathSweep)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExTopologyPathSweep
{
	/** One swept profile: a grid of path points x profile points, starting at VertexOffset. */
	struct FSheet
	{
		TSharedPtr<FProfile> Profile;

		TSharedPtr<PCGExDetails::TSettingValue<double>> Depth;
		TSharedPtr<PCGExDetails::TSettingValue<double>> Width;

		// Null when the anchor is the same for every path point, held by FixedAnchor
		TSharedPtr<PCGExDetails::TSettingValue<double>> Reference;
		FVector2D FixedAnchor = FVector2D::ZeroVector;

		int32 VertexOffset = 0;
		int32 MaterialIndex = 0;

		bool bDepthOnY = true;
		bool bFlipWinding = false;

		// V = VStart + VScale * Alpha in both V modes: a row keeps its V along the whole path
		double VStart = 0;
		double VScale = 1;
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExTopologyPathSweepContext, UPCGExTopologyPathSweepSettings>
	{
	protected:
		TObjectPtr<UDynamicMesh> InternalMesh;
		UPCGDynamicMeshData* InternalMeshData = nullptr;
		FPCGExTopologyUVDetails UVDetails;
		FTransform MeshTransform = FTransform::Identity;
		int32 UVChannel = 0; // Channel of the generated UVs; UVDetails never holds an attribute channel on it

		int32 NumPoints = 0;
		bool bClosedLoop = false;
		double MiterLimit = 1;
		FVector WorldUp = FVector::UpVector;

		TSharedPtr<PCGExPaths::FPath> Path;
		TArray<FVector> EdgeDirs; // Per edge; zero-length edges borrow a neighbor's direction

		TSharedPtr<PCGExPaths::TPathEdgeExtra<FVector>> SideNormal;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> UpGetter;
		TSharedPtr<PCGExDetails::TSettingValue<double>> ColorBlendWeight;

		TArray<FSheet> Sheets;

		// Per vertex: filled in parallel, then consumed by the mesh assembly
		TArray<FVector> Positions;
		TArray<FVector4f> Colors;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;
		virtual void Output() override;

	protected:
		bool InitSheet(FSheet& Sheet, const FPCGExPathSweepProfileDetails& Details, const TSharedPtr<FProfile>& InProfile);
		void BuildMesh();
	};
}
