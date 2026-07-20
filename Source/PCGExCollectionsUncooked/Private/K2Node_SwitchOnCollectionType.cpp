// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "K2Node_SwitchOnCollectionType.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Literal.h"
#include "K2Node_SwitchName.h"
#include "KismetCompiler.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExAssetCollectionTypes.h"
#include "Helpers/PCGExCollectionEntryBlueprintLibrary.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "K2Node_SwitchOnCollectionType"

const FName UK2Node_SwitchOnCollectionType::CollectionPinName(TEXT("Collection"));
const FName UK2Node_SwitchOnCollectionType::DefaultExecPinName(TEXT("Default"));

void UK2Node_SwitchOnCollectionType::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);

	UEdGraphPin* CollectionPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UPCGExAssetCollection::StaticClass(), CollectionPinName);
	CollectionPin->PinFriendlyName = LOCTEXT("CollectionPinFriendlyName", "Collection");

	PCGExAssetCollection::FTypeRegistry& Registry = PCGExAssetCollection::FTypeRegistry::Get();

	TArray<FName> TypeIds;
	GetSortedTypeIds(TypeIds);

	for (const FName TypeId : TypeIds)
	{
		PCGExAssetCollection::FTypeInfo TypeInfo;
		UClass* CollectionClass = Registry.GetInfo(TypeId, TypeInfo) ? TypeInfo.CollectionClass.Get() : nullptr;
		if (!CollectionClass)
		{
			continue;
		}

		UEdGraphPin* ExecOut = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, TypeId);
		ExecOut->PinFriendlyName = TypeInfo.DisplayName;

		UEdGraphPin* TypedOut = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object, CollectionClass, MakeTypedOutPinName(TypeId));
		TypedOut->PinFriendlyName = FText::Format(LOCTEXT("TypedOutFriendlyName", "As {0}"), TypeInfo.DisplayName);
	}

	UEdGraphPin* DefaultExec = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, DefaultExecPinName);
	DefaultExec->PinFriendlyName = LOCTEXT("DefaultExecFriendlyName", "Default");

	Super::AllocateDefaultPins();
}

FText UK2Node_SwitchOnCollectionType::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Switch on Collection Type");
}

FText UK2Node_SwitchOnCollectionType::GetTooltipText() const
{
	return LOCTEXT("NodeTooltip",
	               "Routes execution by the collection's registered type id (exact match).\n"
	               "Each branch provides the collection pre-cast to its registered class.\n"
	               "Unregistered types take the Default branch.");
}

FSlateIcon UK2Node_SwitchOnCollectionType::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = GetNodeTitleColor();
	static FSlateIcon Icon(FAppStyle::GetAppStyleSetName(), "GraphEditor.Switch_16x");
	return Icon;
}

FLinearColor UK2Node_SwitchOnCollectionType::GetNodeTitleColor() const
{
	return FLinearColor(0.0f, 0.5f, 0.8f);
}

FText UK2Node_SwitchOnCollectionType::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "PCGEx|Collection");
}

void UK2Node_SwitchOnCollectionType::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

void UK2Node_SwitchOnCollectionType::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	const UEdGraphSchema_K2* K2 = GetDefault<UEdGraphSchema_K2>();
	PCGExAssetCollection::FTypeRegistry& Registry = PCGExAssetCollection::FTypeRegistry::Get();

	TArray<FName> TypeIds;
	GetSortedTypeIds(TypeIds);

	// Selection value: GetCollectionTypeId(Collection).
	UK2Node_CallFunction* TypeIdCall = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	TypeIdCall->FunctionReference.SetExternalMember(
		GET_FUNCTION_NAME_CHECKED(UPCGExCollectionEntryBlueprintLibrary, GetCollectionTypeId),
		UPCGExCollectionEntryBlueprintLibrary::StaticClass());
	TypeIdCall->AllocateDefaultPins();
	UEdGraphPin* TypeIdCallCollection = TypeIdCall->FindPinChecked(CollectionPinName);

	// Name switch: case exec pins are named exactly PinNames[i]; ctor pre-wires the
	// NotEqual_NameName comparison delegate.
	UK2Node_SwitchName* SwitchNode = CompilerContext.SpawnIntermediateNode<UK2Node_SwitchName>(this, SourceGraph);
	SwitchNode->PinNames = TypeIds;
	SwitchNode->bHasDefaultPin = true;
	SwitchNode->AllocateDefaultPins();

	CompilerContext.MovePinLinksToIntermediate(*GetExecInputPin(), *SwitchNode->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
	K2->TryCreateConnection(TypeIdCall->GetReturnValuePin(), SwitchNode->GetSelectionPin());

	// Collection source fan-out: linked sources feed the type-id call and every used cast;
	// an unlinked literal default goes through a literal node so casts have a real source.
	UEdGraphPin* OurCollection = GetCollectionPin();
	TArray<UEdGraphPin*> CollectionSources;
	if (OurCollection->LinkedTo.Num() > 0)
	{
		CollectionSources = OurCollection->LinkedTo;
		for (UEdGraphPin* Source : CollectionSources)
		{
			K2->TryCreateConnection(Source, TypeIdCallCollection);
		}
	}
	else if (OurCollection->DefaultObject)
	{
		TypeIdCallCollection->DefaultObject = OurCollection->DefaultObject;

		UK2Node_Literal* LiteralNode = CompilerContext.SpawnIntermediateNode<UK2Node_Literal>(this, SourceGraph);
		LiteralNode->SetObjectRef(OurCollection->DefaultObject);
		LiteralNode->AllocateDefaultPins();
		CollectionSources.Add(LiteralNode->GetValuePin());
	}

	for (const FName TypeId : TypeIds)
	{
		// Exec routing for this type's branch.
		if (UEdGraphPin* OurExec = FindPin(TypeId, EGPD_Output))
		{
			CompilerContext.MovePinLinksToIntermediate(*OurExec, *SwitchNode->FindPinChecked(TypeId));
		}

		// Typed output: spawn a pure cast only when the pin is actually consumed. On the
		// matched exec branch the cast always succeeds (exec routing is exact-TypeId).
		UEdGraphPin* OurTyped = FindPin(MakeTypedOutPinName(TypeId), EGPD_Output);
		if (!OurTyped || OurTyped->LinkedTo.Num() == 0)
		{
			continue;
		}

		PCGExAssetCollection::FTypeInfo TypeInfo;
		UClass* TargetClass = Registry.GetInfo(TypeId, TypeInfo) ? TypeInfo.CollectionClass.Get() : nullptr;
		if (!TargetClass)
		{
			CompilerContext.MessageLog.Error(
				*FText::Format(
					LOCTEXT("StaleTypeClass", "@@ references collection type '{0}' whose class is no longer available."),
					FText::FromName(TypeId)).ToString(),
				this);
			continue;
		}

		UK2Node_DynamicCast* CastNode = CompilerContext.SpawnIntermediateNode<UK2Node_DynamicCast>(this, SourceGraph);
		CastNode->SetPurity(true);
		CastNode->TargetType = TargetClass;
		CastNode->AllocateDefaultPins();

		for (UEdGraphPin* Source : CollectionSources)
		{
			K2->TryCreateConnection(Source, CastNode->GetCastSourcePin());
		}

		CompilerContext.MovePinLinksToIntermediate(*OurTyped, *CastNode->GetCastResultPin());
	}

	if (UEdGraphPin* OurDefault = FindPin(DefaultExecPinName, EGPD_Output))
	{
		CompilerContext.MovePinLinksToIntermediate(*OurDefault, *SwitchNode->GetDefaultPin());
	}

	BreakAllNodeLinks();
}

UEdGraphPin* UK2Node_SwitchOnCollectionType::GetExecInputPin() const
{
	return FindPin(UEdGraphSchema_K2::PN_Execute);
}

UEdGraphPin* UK2Node_SwitchOnCollectionType::GetCollectionPin() const
{
	return FindPin(CollectionPinName);
}

void UK2Node_SwitchOnCollectionType::GetSortedTypeIds(TArray<FName>& OutTypeIds)
{
	PCGExAssetCollection::FTypeRegistry& Registry = PCGExAssetCollection::FTypeRegistry::Get();

	TArray<FName> AllIds;
	Registry.GetAllTypeIds(AllIds);

	OutTypeIds.Reset(AllIds.Num());
	for (const FName TypeId : AllIds)
	{
		PCGExAssetCollection::FTypeInfo TypeInfo;
		if (Registry.GetInfo(TypeId, TypeInfo) && TypeInfo.CollectionClass.IsValid())
		{
			OutTypeIds.Add(TypeId);
		}
	}

	// TMap iteration order is unstable; lexical sort keeps pin order deterministic across
	// sessions so reconstruction doesn't shuffle connections.
	OutTypeIds.Sort(FNameLexicalLess());
}

FName UK2Node_SwitchOnCollectionType::MakeTypedOutPinName(FName TypeId)
{
	return FName(*(TypeId.ToString() + TEXT("_As")));
}

#undef LOCTEXT_NAMESPACE
