// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExClusterSketchStyle.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

UPCGExClusterSketchStyleSettings::UPCGExClusterSketchStyleSettings()
{
	// TSoftObjectPtr's FSoftObjectPath constructor is EXPLICIT and there is no assignment from a path,
	// so the typed pointers are built once here and copied. Nothing loads: these stay soft until a
	// sketch actually draws.

	// Ships with the plugin. Reads per-instance custom data 0..2 as colour, which is what lets ONE
	// material serve every sketch and every state.
	const TSoftObjectPtr<UMaterialInterface> Material(FSoftObjectPath(TEXT("/PCGExtendedToolkit/Data/Materials/M_ClusterSketch.M_ClusterSketch")));

	// Engine content, always present.
	const TSoftObjectPtr<UStaticMesh> VertexMesh(FSoftObjectPath(TEXT("/Engine/InteractiveToolsFramework/Meshes/GizmoBoxHandle.GizmoBoxHandle")));
	const TSoftObjectPtr<UStaticMesh> EdgeMesh(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));

	// ControlRig plugin content -- EnabledByDefault, so present in a stock project; one that disables it
	// drops to immediate-mode drawing for these two rather than failing.
	const TSoftObjectPtr<UStaticMesh> PhantomMesh(FSoftObjectPath(TEXT("/ControlRig/Controls/ControlRig_Box_3mm.ControlRig_Box_3mm")));

	PreviewVertex.Mesh = VertexMesh;
	PreviewVertex.Material = Material;
	PreviewEdge.Mesh = EdgeMesh;
	PreviewEdge.Material = Material;

	EditVertexIdle.Mesh = VertexMesh;
	EditVertexIdle.Material = Material;
	EditVertexPhantom.Mesh = PhantomMesh;
	EditVertexPhantom.Material = Material;
	EditEdge.Mesh = EdgeMesh;
	EditEdge.Material = Material;

	HoverOutlineMesh = PhantomMesh;
	HoverOutlineMaterial = Material;
}

const UPCGExClusterSketchStyleSettings* UPCGExClusterSketchStyleSettings::Get()
{
	return GetDefault<UPCGExClusterSketchStyleSettings>();
}

#if WITH_EDITOR
UPCGExClusterSketchStyleSettings::FOnSketchStyleChanged& UPCGExClusterSketchStyleSettings::OnChanged()
{
	static FOnSketchStyleChanged Delegate;
	return Delegate;
}

void UPCGExClusterSketchStyleSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnChanged().Broadcast();
}
#endif

namespace PCGExSketchStyle
{
	// Degenerate-axis floor: a flat mesh (a plane used as a billboard) has a zero extent on one
	// axis, and dividing by it would explode the instance.
	constexpr double MinExtent = 1.e-4;

	/** Component index of an axis, and whether it points backwards along it. */
	void ResolveAxis(const EPCGExAxis InAxis, int32& OutIndex, double& OutSign)
	{
		switch (InAxis)
		{
		case EPCGExAxis::Forward:
			OutIndex = 0;
			OutSign = 1.0;
			break;
		case EPCGExAxis::Backward:
			OutIndex = 0;
			OutSign = -1.0;
			break;
		case EPCGExAxis::Right:
			OutIndex = 1;
			OutSign = 1.0;
			break;
		case EPCGExAxis::Left:
			OutIndex = 1;
			OutSign = -1.0;
			break;
		case EPCGExAxis::Up:
			OutIndex = 2;
			OutSign = 1.0;
			break;
		case EPCGExAxis::Down:
			OutIndex = 2;
			OutSign = -1.0;
			break;
		default:
			OutIndex = 0;
			OutSign = 1.0;
			break;
		}
	}

	FTransform MakePointInstanceTransform(const FBoxSphereBounds& InMeshBounds, const FVector& InLocation, const double InRadius)
	{
		const FVector Extent = InMeshBounds.BoxExtent;
		const double Largest = FMath::Max3(Extent.X, Extent.Y, Extent.Z);
		const double Scale = InRadius / FMath::Max(Largest, MinExtent);

		// Translation cancels the mesh's own bounds offset, so an off-centre mesh still marks the
		// vertex rather than sitting beside it.
		return FTransform(FQuat::Identity, InLocation - InMeshBounds.Origin * Scale, FVector(Scale));
	}

	FTransform MakeSegmentInstanceTransform(const FBoxSphereBounds& InMeshBounds, const EPCGExAxis InLengthAxis, const FVector& InStart, const FVector& InEnd, const double InRadius)
	{
		const FVector Delta = InEnd - InStart;
		const double Length = Delta.Size();
		const FVector Dir = Length > MinExtent ? Delta / Length : FVector::XAxisVector;

		int32 AxisIndex = 0;
		double AxisSign = 1.0;
		ResolveAxis(InLengthAxis, AxisIndex, AxisSign);

		const FVector Extent = InMeshBounds.BoxExtent;
		// Full span along the length axis, so a mesh authored 0..1 and one authored 0..100 both stretch
		// to exactly the segment length.
		const double AxisSpan = FMath::Max(Extent[AxisIndex] * 2.0, MinExtent);

		FVector Scale;
		for (int32 i = 0; i < 3; ++i)
		{
			Scale[i] = i == AxisIndex ? Length / AxisSpan : InRadius / FMath::Max(Extent[i], MinExtent);
		}
		Scale[AxisIndex] *= AxisSign;

		// Rotate the chosen mesh axis onto the segment direction.
		FQuat Rotation;
		switch (AxisIndex)
		{
		case 1:
			Rotation = FRotationMatrix::MakeFromY(Dir).ToQuat();
			break;
		case 2:
			Rotation = FRotationMatrix::MakeFromZ(Dir).ToQuat();
			break;
		default:
			Rotation = FRotationMatrix::MakeFromX(Dir).ToQuat();
			break;
		}

		// Bounds centre lands on the midpoint, so where the mesh sits in its own space is irrelevant.
		const FVector Midpoint = (InStart + InEnd) * 0.5;
		return FTransform(Rotation, Midpoint - Rotation.RotateVector(InMeshBounds.Origin * Scale), Scale);
	}
}
