// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExElementsValencyEditor.h"

#include "AssetTypeActions_Base.h"
#include "PCGExAssetTypesMacros.h"
#include "PropertyEditorModule.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "EditorMode/PCGExValencyEditorModeToolkit.h"
#include "EditorMode/PCGExValencyCageConnectorVisualizer.h"
#include "EditorMode/PCGExConstraintVisualizer.h"
#include "EditorMode/Constraints/PCGExConstraintVis_AngularRange.h"
#include "EditorMode/Constraints/PCGExConstraintVis_SurfaceOffset.h"
#include "EditorMode/Constraints/PCGExConstraintVis_VolumeOffset.h"
#include "EditorMode/Constraints/PCGExConstraintVis_HemisphereOffset.h"
#include "EditorMode/Constraints/PCGExConstraintVis_Preset.h"
#include "EditorMode/Constraints/PCGExConstraintVis_Branch.h"
#include "EditorMode/Constraints/PCGExConstraintVis_ContextCondition.h"
#include "EditorMode/Constraints/PCGExConstraintVis_ConicRange.h"
#include "EditorMode/Constraints/PCGExConstraintVis_ArcSurface.h"
#include "EditorMode/Constraints/PCGExConstraintVis_ArcRepeat.h"
#include "EditorMode/Constraints/PCGExConstraintVis_SnapToGrid.h"
#include "EditorMode/Constraints/PCGExConstraintVis_Probability.h"
#include "EditorMode/Constraints/PCGExConstraintVis_ScaleRamp.h"
#include "EditorMode/Constraints/PCGExConstraintVis_AlignToWorld.h"
#include "EditorMode/Constraints/PCGExConstraintVis_Lattice.h"
#include "EditorMode/Constraints/PCGExConstraintVis_Spiral.h"
#include "Growth/Constraints/PCGExConstraint_AngularRange.h"
#include "Growth/Constraints/PCGExConstraint_SurfaceOffset.h"
#include "Growth/Constraints/PCGExConstraint_VolumeOffset.h"
#include "Growth/Constraints/PCGExConstraint_HemisphereOffset.h"
#include "Growth/Constraints/PCGExConstraintPreset.h"
#include "Growth/Constraints/PCGExConstraint_Branch.h"
#include "Growth/Constraints/PCGExConstraint_ContextCondition.h"
#include "Growth/Constraints/PCGExConstraint_ConicRange.h"
#include "Growth/Constraints/PCGExConstraint_ArcSurface.h"
#include "Growth/Constraints/PCGExConstraint_ArcRepeat.h"
#include "Growth/Constraints/PCGExConstraint_SnapToGrid.h"
#include "Growth/Constraints/PCGExConstraint_Probability.h"
#include "Growth/Constraints/PCGExConstraint_ScaleRamp.h"
#include "Growth/Constraints/PCGExConstraint_AlignToWorld.h"
#include "Growth/Constraints/PCGExConstraint_Lattice.h"
#include "Growth/Constraints/PCGExConstraint_Spiral.h"
#include "Components/PCGExValencyCageConnectorComponent.h"
#include "Core/PCGExConnectorPatternAsset.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternActions.h"
#include "Details/PCGExPropertyOutputConfigCustomization.h"
#include "Details/PCGExValencyConnectorCompatibilityCustomization.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternGraphNode.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternConstraintNode.h"
#include "EdGraphUtilities.h"
#include "KismetPins/SGraphPinExec.h"
#include "SGraphNode.h"

namespace
{
	/** Pin factory that renders PatternRoot pins as diamond (exec-style) shapes. */
	struct FPCGExPatternRootPinFactory : public FGraphPanelPinFactory
	{
		virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override
		{
			if (Pin && Pin->PinType.PinCategory == UPCGExConnectorPatternGraphNode::PatternRootPinCategory)
			{
				return SNew(SGraphPinExec, Pin);
			}
			return nullptr;
		}
	};

	/**
	 * Custom SGraphNode for pattern entry nodes.
	 * Places the RootIn pin in the title bar instead of the regular pin list.
	 */
	class SPCGExPatternEntryNode : public SGraphNode
	{
	public:
		SLATE_BEGIN_ARGS(SPCGExPatternEntryNode) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UEdGraphNode* InNode)
		{
			GraphNode = InNode;
			UpdateGraphNode();
		}

		virtual void UpdateGraphNode() override
		{
			SGraphNode::UpdateGraphNode();

			// Inject the stored root pin widget into the title placeholder
			if (RootInPinWidget.IsValid() && RootPinPlaceholder.IsValid())
			{
				RootPinPlaceholder->SetContent(RootInPinWidget.ToSharedRef());
			}
		}

		virtual TSharedRef<SWidget> CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle) override
		{
			// Title layout: [RootIn diamond] [title text]
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 2, 0)
				[
					SAssignNew(RootPinPlaceholder, SBox)
				]
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				[
					SGraphNode::CreateTitleWidget(NodeTitle)
				];
		}

		virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override
		{
			const UEdGraphPin* PinObj = PinToAdd->GetPinObj();

			if (PinObj && PinObj->PinName == TEXT("RootIn"))
			{
				// Intercept: store for injection into title bar, don't add to LeftNodeBox
				PinToAdd->SetOwner(SharedThis(this));
				PinToAdd->SetShowLabel(false);
				RootInPinWidget = PinToAdd;
				InputPins.Add(PinToAdd);
				return;
			}

			SGraphNode::AddPin(PinToAdd);
		}

	private:
		TSharedPtr<SBox> RootPinPlaceholder;
		TSharedPtr<SGraphPin> RootInPinWidget;
	};

	/** Node factory that uses SPCGExPatternEntryNode for entry nodes (not constraint nodes). */
	struct FPCGExPatternEntryNodeFactory : public FGraphPanelNodeFactory
	{
		virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* InNode) const override
		{
			if (Cast<UPCGExConnectorPatternConstraintNode>(InNode)) { return nullptr; }
			if (Cast<UPCGExConnectorPatternGraphNode>(InNode))
			{
				return SNew(SPCGExPatternEntryNode, InNode);
			}
			return nullptr;
		}
	};
}

void FPCGExElementsValencyEditorModule::StartupModule()
{
	IPCGExEditorModuleInterface::StartupModule();

	// Register editor mode command bindings
	FValencyEditorCommands::Register();

	// Register connector component visualizer
	if (GUnrealEd)
	{
		GUnrealEd->RegisterComponentVisualizer(
			UPCGExValencyCageConnectorComponent::StaticClass()->GetFName(),
			MakeShareable(new FPCGExValencyCageConnectorVisualizer()));
	}

	// Register constraint visualizers
	{
		FConstraintVisualizerRegistry& Registry = FConstraintVisualizerRegistry::Get();
		Registry.Register<FPCGExConstraint_AngularRange, FAngularRangeVisualizer>();
		Registry.Register<FPCGExConstraint_SurfaceOffset, FSurfaceOffsetVisualizer>();
		Registry.Register<FPCGExConstraint_VolumeOffset, FVolumeOffsetVisualizer>();
		Registry.Register<FPCGExConstraint_HemisphereOffset, FHemisphereOffsetVisualizer>();
		Registry.Register<FPCGExConstraint_Preset, FPresetVisualizer>();
		Registry.Register<FPCGExConstraint_Branch, FBranchVisualizer>();
		Registry.Register<FPCGExConstraint_ContextCondition, FContextConditionVisualizer>();
		Registry.Register<FPCGExConstraint_ConicRange, FConicRangeVisualizer>();
		Registry.Register<FPCGExConstraint_ArcSurface, FArcSurfaceVisualizer>();
		Registry.Register<FPCGExConstraint_ArcRepeat, FArcRepeatVisualizer>();
		Registry.Register<FPCGExConstraint_SnapToGrid, FSnapToGridVisualizer>();
		Registry.Register<FPCGExConstraint_Probability, FProbabilityVisualizer>();
		Registry.Register<FPCGExConstraint_ScaleRamp, FScaleRampVisualizer>();
		Registry.Register<FPCGExConstraint_AlignToWorld, FAlignToWorldVisualizer>();
		Registry.Register<FPCGExConstraint_Lattice, FLatticeVisualizer>();
		Registry.Register<FPCGExConstraint_Spiral, FSpiralVisualizer>();
	}

	// Asset type actions — custom editor with graph view
	FAssetToolsModule::GetModule().Get().RegisterAssetTypeActions(MakeShared<FPCGExConnectorPatternActions>());

	// Visual factories for connector pattern graph
	PatternRootPinFactory = MakeShared<FPCGExPatternRootPinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(PatternRootPinFactory);

	PatternEntryNodeFactory = MakeShared<FPCGExPatternEntryNodeFactory>();
	FEdGraphUtilities::RegisterVisualNodeFactory(PatternEntryNodeFactory);

	// Property customizations
	PCGEX_REGISTER_CUSTO_START
	PCGEX_REGISTER_CUSTO("PCGExValencyPropertyOutputConfig", FPCGExPropertyOutputConfigCustomization)
	PCGEX_REGISTER_CUSTO("PCGExValencyConnectorEntry", FPCGExValencyConnectorEntryCustomization)
}

void FPCGExElementsValencyEditorModule::ShutdownModule()
{
	// Unregister visual factories
	if (PatternEntryNodeFactory)
	{
		FEdGraphUtilities::UnregisterVisualNodeFactory(PatternEntryNodeFactory);
		PatternEntryNodeFactory.Reset();
	}
	if (PatternRootPinFactory)
	{
		FEdGraphUtilities::UnregisterVisualPinFactory(PatternRootPinFactory);
		PatternRootPinFactory.Reset();
	}

	// Unregister connector component visualizer
	if (GUnrealEd)
	{
		GUnrealEd->UnregisterComponentVisualizer(
			UPCGExValencyCageConnectorComponent::StaticClass()->GetFName());
	}

	// Unregister editor mode command bindings
	FValencyEditorCommands::Unregister();

	IPCGExEditorModuleInterface::ShutdownModule();
}

PCGEX_IMPLEMENT_MODULE(FPCGExElementsValencyEditorModule, PCGExElementsValencyEditor)
