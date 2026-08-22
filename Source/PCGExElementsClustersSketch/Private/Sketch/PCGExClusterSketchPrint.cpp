// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExClusterSketchPrint.h"

#include "Sketch/PCGExClusterSketchData.h"

#include "PCGExH.h"
#include "PCGExPropertyWriter.h"
#include "Core/PCGExElement.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Graphs/PCGExGraphCommon.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Sketch/PCGExClusterSketchDecorator.h"
#include "Sketch/PCGExClusterSketchModel.h"
#include "Sketch/PCGExClusterSketchPropertyProvider.h"
#include "Sketch/PCGExClusterSnapProvider.h"

namespace PCGExClusterSketchPrint
{
	/** One enabled config per resolved schema entry, minus the names validation refused for that layer
	 *  (unnamed, colliding after sanitization, or reserved by the cluster compile). */
	void BuildLayerConfigs(const FPCGExSketchDataLayer& InLayer, const FPCGExClusterSketchValidation::FLayerIssues& InIssues, TArray<FPCGExPropertyOutputConfig>& OutConfigs)
	{
		InLayer.BuildOutputConfigs(OutConfigs);
		OutConfigs.RemoveAll([&InIssues](const FPCGExPropertyOutputConfig& Config)
		{
			return InIssues.Rejects(Config.PropertyName);
		});
	}
}

namespace PCGExSketch
{
	TSharedPtr<PCGExGraphs::FGraphBuilder> PrintResolved(
		FPCGExContext* InContext,
		const FPCGExClusterSketchModel& InModel,
		const UPCGExClusterSnapProvider* InSnapProvider,
		const TConstArrayView<TObjectPtr<UPCGExClusterSketchDecorator>> InDecorators,
		const TSharedPtr<PCGExData::FPointIO>& InVtxIO,
		const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager,
		const TSharedPtr<FPCGExClusterSketchPrintContext>& InPrintContext,
		const FPCGExGraphBuilderDetails* InBuilderDetails,
		const bool bQuiet,
		TFunction<void(const TSharedRef<PCGExGraphs::FGraphBuilder>&, bool)> OnCompiled)
	{
		FPCGExLatticeBasis Basis;
		const bool bHasBasis = InSnapProvider ? InSnapProvider->BuildBasis(Basis) : false;

		FPrintRequest Request;
		Request.Model = &InModel;
		Request.Basis = bHasBasis ? &Basis : nullptr;
		Request.BuilderDetails = InBuilderDetails;
		Request.bQuiet = bQuiet;
		Request.OnCompiled = MoveTemp(OnCompiled);
		Request.Decorators.Reserve(InDecorators.Num());
		for (const TObjectPtr<UPCGExClusterSketchDecorator>& Decorator : InDecorators)
		{
			Request.Decorators.Add(Decorator.Get());
		}

		return PrintClusterSketch(InContext, Request, InVtxIO, InTaskManager, InPrintContext);
	}

	TSharedPtr<PCGExGraphs::FGraphBuilder> PrintClusterSketch(
		FPCGExContext* InContext,
		const FPrintRequest& InRequest,
		const TSharedPtr<PCGExData::FPointIO>& InVtxIO,
		const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager,
		const TSharedPtr<FPCGExClusterSketchPrintContext>& InPrintContext)
	{
		check(InRequest.Model)
		check(InRequest.BuilderDetails)
		check(InVtxIO)
		check(InTaskManager)
		check(InPrintContext)

		const FPCGExClusterSketchModel& Model = *InRequest.Model;
		const int32 NumVtx = Model.NumVertices();

		if (NumVtx < 2)
		{
			if (!InRequest.bQuiet)
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("The sketch holds fewer than 2 vertices -- nothing to print."));
			}
			return nullptr;
		}

		// --- Validate + build the print edge list (authored order, first occurrence wins) ---
		// LOAD-BEARING, not cosmetic: FGraph::InsertEdges has no bounds check and its self-loop guard is
		// editor-only, so anything invalid must be dropped HERE.
		FPCGExClusterSketchValidation Validation;
		Model.Validate(Validation);

		TArray<uint64> PrintEdges;
		TArray<int32> ParentToModelEdge;
		PrintEdges.Reserve(Model.NumEdges());
		ParentToModelEdge.Reserve(Model.NumEdges());
		{
			TSet<uint64> Seen;
			Seen.Reserve(Model.NumEdges());
			for (int32 e = 0; e < Model.NumEdges(); ++e)
			{
				const FPCGExClusterSketchEdge& E = Model.Edges[e];
				if (E.A < 0 || E.B < 0 || E.A >= NumVtx || E.B >= NumVtx || E.A == E.B)
				{
					continue;
				}
				bool bAlreadySeen = false;
				Seen.Add(PCGEx::H64U(static_cast<uint32>(E.A), static_cast<uint32>(E.B)), &bAlreadySeen);
				if (bAlreadySeen)
				{
					continue;
				}
				PrintEdges.Add(PCGEx::H64U(static_cast<uint32>(E.A), static_cast<uint32>(E.B)));
				ParentToModelEdge.Add(e);
			}
		}

		if (!InRequest.bQuiet)
		{
			if (Validation.HasEdgeIssues())
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(
					           FTEXT("Sketch edges dropped at print: {0} invalid, {1} self-loop(s), {2} duplicate(s)."),
					           FText::AsNumber(Validation.InvalidEdges), FText::AsNumber(Validation.SelfLoops), FText::AsNumber(Validation.DuplicateEdges)));
			}
			if (Validation.IsolatedVertices > 0)
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(
					           FTEXT("{0} isolated sketch vertex/vertices will not be printed (clusters cannot represent them)."),
					           FText::AsNumber(Validation.IsolatedVertices)));
			}
			if (Validation.HasLayerIssues())
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("The sketch's authored data has issues: schema entries skipped (unnamed, duplicated, or reserved), items pointing at a missing record, or records sharing an id."));
			}
		}

		if (PrintEdges.IsEmpty())
		{
			if (!InRequest.bQuiet)
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("The sketch holds no valid edges -- nothing to print."));
			}
			return nullptr;
		}

		// --- Basis + bound-vertex fallback ---
		const bool bHasBasis = InRequest.Basis && InRequest.Basis->bValid;
		if (!bHasBasis && !InRequest.bQuiet)
		{
			for (const FPCGExClusterSketchVertex& V : Model.Vertices)
			{
				if (V.bLatticeBound)
				{
					PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("The sketch has lattice-bound vertices but no usable snap basis -- their cached locations are used as-is."));
					break;
				}
			}
		}

		// --- Allocate + write native properties (BEFORE the builder ctor: it checks and caches them) ---
		(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(InVtxIO->GetOut(), NumVtx, EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::Seed);

		UPCGBasePointData* OutData = InVtxIO->GetOut();
		TPCGValueRange<FTransform> OutTransforms = OutData->GetTransformValueRange();
		TPCGValueRange<int32> OutSeeds = OutData->GetSeedValueRange();
		// Collocation is judged on RESOLVED output locations -- the only definition that also catches a
		// rank-collapsed basis projecting distinct coords onto one spot.
		// Collocation and seeds are judged on FINAL locations, so a placement that collapses distinct
		// vertices onto one spot is caught too.
		const bool bPlaced = !InRequest.LocalToWorld.Equals(FTransform::Identity);
		TSet<FVector> SeenLocations;
		SeenLocations.Reserve(NumVtx);
		int32 CollocatedCount = 0;
		for (int32 i = 0; i < NumVtx; ++i)
		{
			const FPCGExClusterSketchVertex& V = Model.Vertices[i];
			// A bound vertex's location ALWAYS derives from its coord -- a stale cached transform
			// location can never reach output. Rotation/scale stay authored either way.
			const FVector Local = (V.bLatticeBound && bHasBasis) ? InRequest.Basis->CoordToWorld(V.LatticeCoord) : V.Transform.GetLocation();
			OutTransforms[i] = bPlaced
				? FTransform(V.Transform.GetRotation(), Local, V.Transform.GetScale3D()) * InRequest.LocalToWorld
				: FTransform(V.Transform.GetRotation(), Local, V.Transform.GetScale3D());
			const FVector Location = OutTransforms[i].GetLocation();
			// Seeds ride the compile reorder as a native property, so they can be written up front.
			OutSeeds[i] = PCGExRandomHelpers::ComputeSpatialSeed(Location);

			bool bAlreadySeen = false;
			SeenLocations.Add(QuantizedLocationKey(Location), &bAlreadySeen);
			if (bAlreadySeen)
			{
				++CollocatedCount;
			}
		}
		if (CollocatedCount > 0 && !InRequest.bQuiet)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(
				           FTEXT("{0} sketch vertex/vertices print at an already-occupied location (duplicate coords, overlapping positions, or a collapsed snap basis) -- clusters cannot carry collocated vertices; use Merge Collocated Vertices on the sketch."),
				           FText::AsNumber(CollocatedCount)));
		}

		if (!InRequest.bQuiet)
		{
			const FPCGExLatticeBasis* WarnBasis = bHasBasis ? InRequest.Basis : nullptr;
			int32 OverlappingEdges = 0;
			for (int32 e = 0; e < Model.NumEdges(); ++e)
			{
				if (Model.FindVertexOnEdgeInterior(e, WarnBasis) != INDEX_NONE)
				{
					++OverlappingEdges;
				}
			}
			if (OverlappingEdges > 0)
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(
					           FTEXT("{0} sketch edge(s) pass through a vertex (collinear A-C over B) -- degenerate for a cluster; use Split Overlapping Edges on the sketch."),
					           FText::AsNumber(OverlappingEdges)));
			}

			TArray<FPCGExClusterSketchCrossing> Crossings;
			Model.FindEdgeCrossings(Crossings, WarnBasis);
			if (!Crossings.IsEmpty())
			{
				PCGE_LOG_C(Warning, GraphAndLog, InContext, FText::Format(
					           FTEXT("{0} sketch edge crossing(s) with no vertex at the intersection -- materialize them in the sketch editor, or use Split Overlapping Edges."),
					           FText::AsNumber(Crossings.Num())));
			}
		}

		// --- Print context ---
		InPrintContext->Model = InRequest.Model;
		InPrintContext->bHasBasis = bHasBasis;
		if (bHasBasis)
		{
			InPrintContext->Basis = *InRequest.Basis;
		}
		InPrintContext->VtxFacade = MakeShared<PCGExData::FFacade>(InVtxIO.ToSharedRef());
		InPrintContext->ParentToModelEdge = MoveTemp(ParentToModelEdge);

		TArray<const UPCGExClusterSketchDecorator*> EnabledDecorators;
		EnabledDecorators.Reserve(InRequest.Decorators.Num());
		for (const UPCGExClusterSketchDecorator* Decorator : InRequest.Decorators)
		{
			if (Decorator && Decorator->bEnabled)
			{
				EnabledDecorators.Add(Decorator);
			}
		}

		const FPCGExSketchData& SketchData = Model.Data;

		// Built before the compile starts; the edge hook and every decorator only ever read them.
		InPrintContext->VertexDataProvider = MakeShared<FPCGExSketchLayerPropertyProvider>(SketchData.VertexLayer, TConstArrayView<FPCGExClusterSketchVertex>(Model.Vertices));
		InPrintContext->EdgeDataProvider = MakeShared<FPCGExSketchLayerPropertyProvider>(SketchData.EdgeLayer, TConstArrayView<FPCGExClusterSketchEdge>(Model.Edges));

		// --- Vertex-domain writes: authored layer, then decorators, then ONE synchronous commit ---
		// Point index == model vertex index here. Committing before the compile is MANDATORY: committed
		// values ride the reorder via MetadataEntry, an uncommitted buffer would flush positionally onto
		// the reordered points.
		const TSharedRef<PCGExData::FFacade> VtxFacade = InPrintContext->VtxFacade.ToSharedRef();

		// Resolved once: Resolve walks the whole import tree and allocates, and @Data is one value per output.
		TArray<FPCGExPropertyResolved> SketchProperties;
		{
			SketchData.SketchProperties.Resolve(SketchProperties);
			SketchProperties.RemoveAll([&Validation](const FPCGExPropertyResolved& Entry)
			{
				return Validation.SketchLayerIssues.Rejects(Entry.Source->Name);
			});
			PCGExProperties::WriteResolvedToDataDomain(VtxFacade->GetOut(), SketchProperties);

			TArray<FPCGExPropertyOutputConfig> VertexConfigs;
			PCGExClusterSketchPrint::BuildLayerConfigs(SketchData.VertexLayer, Validation.VertexLayerIssues, VertexConfigs);

			// This phase is serial, so one writer for the whole pass is legal here -- unlike the edge one.
			FPCGExPropertyWriter Writer;
			if (Writer.Initialize(InPrintContext->VertexDataProvider.Get(), VtxFacade, VertexConfigs))
			{
				for (int32 i = 0; i < NumVtx; ++i)
				{
					Writer.WriteProperties(i, i);
				}
			}
		}

		for (const UPCGExClusterSketchDecorator* Decorator : EnabledDecorators)
		{
			Decorator->DecorateVertices(*InPrintContext);
		}

		InPrintContext->VtxFacade->WriteSynchronous();

		// --- Build + compile the cluster ---
		const TSharedPtr<PCGExGraphs::FGraphBuilder> GraphBuilder = MakeShared<PCGExGraphs::FGraphBuilder>(VtxFacade, InRequest.BuilderDetails);
		GraphBuilder->bInheritNodeData = false;
		// Skips the Morton sort; output order is component-grouped, NEVER authored order -- all mapping
		// goes through VtxIndexMap below, never through order.
		GraphBuilder->bNodesPreSorted = true;
		// The TArray overload preserves insertion order, so parent edge index == print edge order
		// (ParentToModelEdge's key space). The TSet overload would be hash order -- never use it here.
		GraphBuilder->Graph->InsertEdges(PrintEdges, -1);

		// Required: OnPreCompile never fires without a user context.
		GraphBuilder->OnCreateContext = []() -> TSharedPtr<PCGExGraphs::FSubGraphUserContext>
		{
			return MakeShared<PCGExGraphs::FSubGraphUserContext>();
		};

		// Edge-domain writes, per subgraph: authored layer then decorators. Fires after FlattenedEdges is
		// built, before the EData write; buffers created here are flushed by the subgraph itself.
		const TSharedPtr<FPCGExClusterSketchPrintContext> PrintContext = InPrintContext;
		const bool bMirrorSketchProperties = InRequest.bWriteSketchPropertiesToEdges;
		TArray<FPCGExPropertyOutputConfig> EdgeConfigs;
		{
			PCGExClusterSketchPrint::BuildLayerConfigs(SketchData.EdgeLayer, Validation.EdgeLayerIssues, EdgeConfigs);
		}

		GraphBuilder->OnPreCompile = [PrintContext, EnabledDecorators, EdgeConfigs, bMirrorSketchProperties, SketchProperties](PCGExGraphs::FSubGraphUserContext&, const PCGExGraphs::FSubGraphPreCompileData& Data)
		{
			const TSharedRef<PCGExData::FFacade> EdgesFacade = Data.EdgesDataFacade.ToSharedRef();

			if (bMirrorSketchProperties)
			{
				PCGExProperties::WriteResolvedToDataDomain(EdgesFacade->GetOut(), SketchProperties);
			}

			// The writer MUST be a local: this hook runs concurrently across subgraphs, and a property's
			// output buffer binds to exactly one facade -- a hoisted writer would write every subgraph's
			// edges into the first one's output.
			FPCGExPropertyWriter Writer;
			if (Writer.Initialize(PrintContext->EdgeDataProvider.Get(), EdgesFacade, EdgeConfigs))
			{
				const int32 Num = Data.FlattenedEdges.Num();
				for (int32 i = 0; i < Num; ++i)
				{
					// Writer index = output edge point; EdgeKeys[i].Index = parent graph edge = print edge order.
					Writer.WriteProperties(i, PrintContext->ParentToModelEdge[Data.EdgeKeys[i].Index]);
				}
			}

			for (const UPCGExClusterSketchDecorator* Decorator : EnabledDecorators)
			{
				Decorator->DecorateEdges(*PrintContext, Data);
			}
		};

		TFunction<void(const TSharedRef<PCGExGraphs::FGraphBuilder>&, bool)> UserCallback = InRequest.OnCompiled;
		GraphBuilder->OnCompilationEndCallback = [PrintContext, UserCallback](const TSharedRef<PCGExGraphs::FGraphBuilder>& InBuilder, const bool bSuccess)
		{
			if (bSuccess)
			{
				// PointIndex is reassigned to the output index during compile; PRUNED nodes keep a stale
				// one, so the map is gated on bValid.
				const TArray<PCGExGraphs::FNode>& Nodes = InBuilder->Graph->Nodes;
				TArray<int32>& Map = PrintContext->VtxIndexMap;
				Map.SetNumUninitialized(Nodes.Num());
				for (int32 i = 0; i < Nodes.Num(); ++i)
				{
					Map[i] = Nodes[i].bValid ? Nodes[i].PointIndex : INDEX_NONE;
				}
			}
			if (UserCallback)
			{
				UserCallback(InBuilder, bSuccess);
			}
		};

		// bWriteNodeFacade=true: the builder commits every vtx buffer (incl. its own PCGEx/VData) before
		// the end callback fires.
		GraphBuilder->CompileAsync(InTaskManager, true);
		return GraphBuilder;
	}
}
