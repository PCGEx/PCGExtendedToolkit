// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/K2Node_PCGExPropertyAccessBase.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "KismetCompiler.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "Details/PCGExK2NodeTypeHelpers.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "K2Node_PCGExPropertyAccessBase"

// Pin names match the library functions' parameter names so ExpandNode can look up the
// spawned UK2Node_CallFunction's pins via FindPinChecked. Display labels are set via
// PinFriendlyName in AllocateDefaultPins.
const FName UK2Node_PCGExPropertyAccessBase::PropertyNamePinName(TEXT("PropertyName"));
const FName UK2Node_PCGExPropertyAccessBase::OutValuePinName(TEXT("OutValue"));
const FName UK2Node_PCGExPropertyAccessBase::NewValuePinName(TEXT("NewValue"));
const FName UK2Node_PCGExPropertyAccessBase::ReadbackPinName(TEXT("Readback"));
const FName UK2Node_PCGExPropertyAccessBase::SuccessPinName(TEXT("Success"));

namespace PCGExK2PropertyAccessBase
{
	// Forward one of our input pins into every intermediate call: live links are connected to each
	// target, otherwise the literal default is copied to each. The Set path needs it because the
	// same target / key / name inputs feed both the write call and the readback call.
	void ForwardInput(const UEdGraphSchema_K2* K2, UEdGraphPin* Source, TArrayView<UEdGraphPin*> Targets)
	{
		if (!Source)
		{
			return;
		}

		for (UEdGraphPin* Target : Targets)
		{
			if (!Target)
			{
				continue;
			}

			if (Source->LinkedTo.Num() > 0)
			{
				for (UEdGraphPin* Linked : Source->LinkedTo)
				{
					K2->TryCreateConnection(Linked, Target);
				}
			}
			else
			{
				Target->DefaultObject = Source->DefaultObject;
				Target->DefaultValue = Source->DefaultValue;
				Target->DefaultTextValue = Source->DefaultTextValue;
			}
		}
	}
}

void UK2Node_PCGExPropertyAccessBase::AllocateDefaultPins()
{
	const bool bSet = IsSetNode();

	if (bSet)
	{
		CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
		CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);
	}

	UEdGraphPin* TargetPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, GetTargetPinClass(), GetTargetPinName());
	TargetPin->PinFriendlyName = LOCTEXT("TargetPinFriendlyName", "Collection");

	CreateKeyPin();

	UEdGraphPin* NamePin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, PropertyNamePinName);
	NamePin->PinFriendlyName = LOCTEXT("PropertyNamePinFriendlyName", "Prop");

	TArray<UEdGraphPin*, TInlineAllocator<2>> Wildcards;
	if (bSet)
	{
		UEdGraphPin* NewValuePin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Wildcard, NewValuePinName);
		NewValuePin->PinFriendlyName = LOCTEXT("NewValuePinFriendlyName", "New Value");
		Wildcards.Add(NewValuePin);

		UEdGraphPin* ReadbackPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Wildcard, ReadbackPinName);
		ReadbackPin->PinFriendlyName = LOCTEXT("ReadbackPinFriendlyName", "Readback");
		Wildcards.Add(ReadbackPin);
	}
	else
	{
		UEdGraphPin* ValuePin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Wildcard, OutValuePinName);
		ValuePin->PinFriendlyName = LOCTEXT("ValuePinFriendlyName", "Value");
		Wildcards.Add(ValuePin);
	}

	// Restore the previously resolved type so the wildcard isn't reset to gray on graph reopen.
	// The engine's pin reconstruction only carries the type across when the pin has live
	// connections; manually-picked types with no wiring would otherwise be lost.
	if (ResolvedPinType.PinCategory != NAME_None &&
		ResolvedPinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard)
	{
		for (UEdGraphPin* Pin : Wildcards)
		{
			Pin->PinType = ResolvedPinType;
		}
	}

	UEdGraphPin* SuccessPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, SuccessPinName);
	SuccessPin->PinFriendlyName = LOCTEXT("SuccessPinFriendlyName", "Success");

	Super::AllocateDefaultPins();
}

FSlateIcon UK2Node_PCGExPropertyAccessBase::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = GetNodeTitleColor();
	static FSlateIcon Icon(FAppStyle::GetAppStyleSetName(), "GraphEditor.Function_16x");
	return Icon;
}

FLinearColor UK2Node_PCGExPropertyAccessBase::GetNodeTitleColor() const
{
	return FLinearColor(0.0f, 0.5f, 0.8f);
}

void UK2Node_PCGExPropertyAccessBase::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

void UK2Node_PCGExPropertyAccessBase::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	TArray<UEdGraphPin*> Wildcards;
	GetWildcardPins(Wildcards);
	if (!Pin || !Wildcards.Contains(Pin))
	{
		return;
	}

	if (Pin->LinkedTo.Num() > 0)
	{
		AdoptTypeFromOther(Pin->LinkedTo[0]);
		GetGraph()->NotifyGraphChanged();
		return;
	}

	// Every wildcard fully disconnected -- revert so the node can adapt to the next connection.
	bool bAnyLinked = false;
	bool bAnyTyped = false;
	for (const UEdGraphPin* Wildcard : Wildcards)
	{
		bAnyLinked |= Wildcard->LinkedTo.Num() > 0;
		bAnyTyped |= Wildcard->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard;
	}
	if (!bAnyLinked && bAnyTyped)
	{
		ResetWildcardPins();
		GetGraph()->NotifyGraphChanged();
	}
}

void UK2Node_PCGExPropertyAccessBase::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	if (Context->bIsDebugging || !Context->Pin)
	{
		return;
	}

	TArray<UEdGraphPin*> Wildcards;
	GetWildcardPins(Wildcards);
	if (!Wildcards.Contains(Context->Pin))
	{
		return;
	}

	FToolMenuSection& Section = Menu->AddSection(
		GetClass()->GetFName(),
		IsSetNode() ? LOCTEXT("ChangeValueType", "Change Value Type") : LOCTEXT("ChangeOutputType", "Change Output Type"));

	const UK2Node_PCGExPropertyAccessBase* ConstThis = this;

	for (uint8 i = 0; i < static_cast<uint8>(EPCGMetadataTypes::Count); ++i)
	{
		const EPCGMetadataTypes MetadataType = static_cast<EPCGMetadataTypes>(i);

		FEdGraphPinType PinType;
		if (!PCGExK2NodeTypeHelpers::MakePinTypeForMetadataType(MetadataType, PinType))
		{
			continue;
		}

		Section.AddMenuEntry(
			NAME_None,
			PCGExK2NodeTypeHelpers::GetDisplayNameForMetadataType(MetadataType),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([ConstThis, PinType]()
			{
				if (UK2Node_PCGExPropertyAccessBase* MutableThis = const_cast<UK2Node_PCGExPropertyAccessBase*>(ConstThis))
				{
					MutableThis->SetWildcardPinsType(PinType);
				}
			})));
	}
}

void UK2Node_PCGExPropertyAccessBase::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	const bool bSet = IsSetNode();

	UEdGraphPin* ValuePin = GetValuePin();
	UEdGraphPin* ReadbackPin = GetReadbackPin();

	auto IsUnresolved = [](const UEdGraphPin* Pin) { return !Pin || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard; };
	if (IsUnresolved(ValuePin) || (bSet && IsUnresolved(ReadbackPin)))
	{
		CompilerContext.MessageLog.Error(
			*(bSet
				  ? LOCTEXT("SetWildcardUnresolved", "@@ has unresolved wildcard pins. Connect either New Value or Readback to a typed pin, or pick a type from either pin's right-click menu.")
				  : LOCTEXT("GetWildcardUnresolved", "@@ has an unresolved wildcard output. Connect the Value pin to a typed input or pick a type from its right-click menu."))
			.ToString(),
			this);
		BreakAllNodeLinks();
		return;
	}

	// Object/Class pins go to dedicated typed library functions: the CustomStructureParam wildcard path
	// would stuff them into an `int32&` slot and corrupt the property's soft-path payload. Struct and
	// primitive types stay on the wildcard path, where the BP compiler's frame marshalling behaves.
	const FName Category = ValuePin->PinType.PinCategory;
	const bool bIsObjectLike =
		Category == UEdGraphSchema_K2::PC_Object ||
		Category == UEdGraphSchema_K2::PC_Interface ||
		Category == UEdGraphSchema_K2::PC_SoftObject;
	const bool bIsClassLike =
		Category == UEdGraphSchema_K2::PC_Class ||
		Category == UEdGraphSchema_K2::PC_SoftClass;
	const EFlavor Flavor = bIsObjectLike ? EFlavor::Object : bIsClassLike ? EFlavor::Class : EFlavor::Wildcard;

	UClass* TargetClass = nullptr;
	if (Flavor != EFlavor::Wildcard)
	{
		TargetClass = Cast<UClass>(ValuePin->PinType.PinSubCategoryObject.Get());
		if (!TargetClass)
		{
			CompilerContext.MessageLog.Error(
				*LOCTEXT("ObjectPinMissingClass", "@@ has an Object/Class pin with no resolved class.").ToString(),
				this);
			BreakAllNodeLinks();
			return;
		}
	}

	const FName TargetPinName = GetTargetPinName();
	const FName KeyPinName = GetKeyPinName();

	// Get call (pure). On Set nodes it is the readback: pure nodes evaluate when their output is consumed,
	// after the Set's exec fired, so it reflects post-write state. Single-wildcard calls only: multi-wildcard
	// CustomStructureParam doesn't reliably construct non-trivially-copyable output buffers.
	UK2Node_CallFunction* GetCall = SpawnLibraryCall(CompilerContext, SourceGraph, /*bSet=*/false, Flavor);
	UEdGraphPin* GetCallTarget = GetCall->FindPinChecked(TargetPinName);
	UEdGraphPin* GetCallKey = KeyPinName.IsNone() ? nullptr : GetCall->FindPinChecked(KeyPinName);
	UEdGraphPin* GetCallName = GetCall->FindPinChecked(PropertyNamePinName);

	// Typed flavors use DeterminesOutputType on the function's return value: set ExpectedClass to
	// TargetClass and trigger the call node's dynamic-output retyping. The wildcard flavor stamps
	// the user's resolved type onto the CustomStructureParam output instead.
	UEdGraphPin* GetCallValue = nullptr;
	UEdGraphPin* GetCallSuccess = nullptr;
	if (Flavor != EFlavor::Wildcard)
	{
		UEdGraphPin* ExpectedClassPin = GetCall->FindPinChecked(TEXT("ExpectedClass"));
		ExpectedClassPin->DefaultObject = TargetClass;
		GetCall->PinDefaultValueChanged(ExpectedClassPin);
		GetCallValue = GetCall->GetReturnValuePin();
		GetCallSuccess = GetCall->FindPinChecked(TEXT("bSuccess"));
	}
	else
	{
		GetCallValue = GetCall->FindPinChecked(OutValuePinName);
		GetCallValue->PinType = bSet ? ReadbackPin->PinType : ValuePin->PinType;
		GetCallSuccess = GetCall->GetReturnValuePin();
	}

	if (!bSet)
	{
		CompilerContext.MovePinLinksToIntermediate(*GetTargetPin(), *GetCallTarget);
		if (GetCallKey)
		{
			CompilerContext.MovePinLinksToIntermediate(*GetKeyPin(), *GetCallKey);
		}
		CompilerContext.MovePinLinksToIntermediate(*GetPropertyNamePin(), *GetCallName);
		CompilerContext.MovePinLinksToIntermediate(*ValuePin, *GetCallValue);
		CompilerContext.MovePinLinksToIntermediate(*GetSuccessPin(), *GetCallSuccess);

		BreakAllNodeLinks();
		return;
	}

	// Set call (impure). Carries the user's exec flow and Success bool.
	UK2Node_CallFunction* SetCall = SpawnLibraryCall(CompilerContext, SourceGraph, /*bSet=*/true, Flavor);
	UEdGraphPin* SetCallTarget = SetCall->FindPinChecked(TargetPinName);
	UEdGraphPin* SetCallKey = KeyPinName.IsNone() ? nullptr : SetCall->FindPinChecked(KeyPinName);
	UEdGraphPin* SetCallName = SetCall->FindPinChecked(PropertyNamePinName);

	// The wildcard flavor needs type stamping; the typed flavors have a concrete UObject*/UClass*
	// pin that accepts the user's connection via the schema's standard upcast.
	UEdGraphPin* SetCallNewValue = nullptr;
	switch (Flavor)
	{
	case EFlavor::Object:
		SetCallNewValue = SetCall->FindPinChecked(TEXT("NewObject"));
		break;
	case EFlavor::Class:
		SetCallNewValue = SetCall->FindPinChecked(TEXT("NewClass"));
		break;
	default:
		SetCallNewValue = SetCall->FindPinChecked(NewValuePinName);
		SetCallNewValue->PinType = ValuePin->PinType;
		break;
	}

	CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *SetCall->GetExecPin());
	CompilerContext.MovePinLinksToIntermediate(*GetThenPin(), *SetCall->GetThenPin());
	CompilerContext.MovePinLinksToIntermediate(*ValuePin, *SetCallNewValue);
	CompilerContext.MovePinLinksToIntermediate(*GetSuccessPin(), *SetCall->GetReturnValuePin());

	const UEdGraphSchema_K2* K2 = GetDefault<UEdGraphSchema_K2>();
	UEdGraphPin* TargetTargets[] = {SetCallTarget, GetCallTarget};
	UEdGraphPin* KeyTargets[] = {SetCallKey, GetCallKey};
	UEdGraphPin* NameTargets[] = {SetCallName, GetCallName};
	PCGExK2PropertyAccessBase::ForwardInput(K2, GetTargetPin(), TargetTargets);
	PCGExK2PropertyAccessBase::ForwardInput(K2, GetKeyPin(), KeyTargets);
	PCGExK2PropertyAccessBase::ForwardInput(K2, GetPropertyNamePin(), NameTargets);

	CompilerContext.MovePinLinksToIntermediate(*ReadbackPin, *GetCallValue);

	BreakAllNodeLinks();
}

UK2Node_CallFunction* UK2Node_PCGExPropertyAccessBase::SpawnLibraryCall(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, const bool bSet, const EFlavor Flavor)
{
	UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallNode->FunctionReference.SetExternalMember(GetLibraryFunctionName(bSet, Flavor), GetLibraryClass());
	CallNode->AllocateDefaultPins();
	return CallNode;
}

UEdGraphPin* UK2Node_PCGExPropertyAccessBase::GetTargetPin() const
{
	return FindPin(GetTargetPinName());
}

UEdGraphPin* UK2Node_PCGExPropertyAccessBase::GetKeyPin() const
{
	const FName KeyPinName = GetKeyPinName();
	return KeyPinName.IsNone() ? nullptr : FindPin(KeyPinName);
}

UEdGraphPin* UK2Node_PCGExPropertyAccessBase::GetPropertyNamePin() const
{
	return FindPin(PropertyNamePinName);
}

UEdGraphPin* UK2Node_PCGExPropertyAccessBase::GetValuePin() const
{
	return FindPin(IsSetNode() ? NewValuePinName : OutValuePinName);
}

UEdGraphPin* UK2Node_PCGExPropertyAccessBase::GetReadbackPin() const
{
	return IsSetNode() ? FindPin(ReadbackPinName) : nullptr;
}

UEdGraphPin* UK2Node_PCGExPropertyAccessBase::GetSuccessPin() const
{
	return FindPin(SuccessPinName);
}

void UK2Node_PCGExPropertyAccessBase::GetWildcardPins(TArray<UEdGraphPin*>& OutPins) const
{
	OutPins.Reset();
	if (UEdGraphPin* ValuePin = GetValuePin())
	{
		OutPins.Add(ValuePin);
	}
	if (UEdGraphPin* ReadbackPin = GetReadbackPin())
	{
		OutPins.Add(ReadbackPin);
	}
}

void UK2Node_PCGExPropertyAccessBase::ResetWildcardPins()
{
	TArray<UEdGraphPin*> Wildcards;
	GetWildcardPins(Wildcards);
	for (UEdGraphPin* Pin : Wildcards)
	{
		Pin->PinType = FEdGraphPinType();
		Pin->PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
		Pin->BreakAllPinLinks();
	}

	ResolvedPinType = FEdGraphPinType();
	ResolvedPinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
}

void UK2Node_PCGExPropertyAccessBase::AdoptTypeFromOther(const UEdGraphPin* OtherPin)
{
	if (!OtherPin)
	{
		return;
	}

	FEdGraphPinType InType = OtherPin->PinType;
	InType.ContainerType = EPinContainerType::None;

	TArray<UEdGraphPin*> Wildcards;
	GetWildcardPins(Wildcards);
	for (UEdGraphPin* Pin : Wildcards)
	{
		Pin->PinType = InType;
	}
	ResolvedPinType = InType;
}

void UK2Node_PCGExPropertyAccessBase::SetWildcardPinsType(const FEdGraphPinType& InType)
{
	TArray<UEdGraphPin*> Wildcards;
	GetWildcardPins(Wildcards);
	for (UEdGraphPin* Pin : Wildcards)
	{
		// Break links that no longer match the new type.
		for (int32 i = Pin->LinkedTo.Num() - 1; i >= 0; --i)
		{
			const UEdGraphPin* Linked = Pin->LinkedTo[i];
			if (!Linked || Linked->PinType != InType)
			{
				Pin->BreakLinkTo(Pin->LinkedTo[i]);
			}
		}
		Pin->PinType = InType;
	}
	ResolvedPinType = InType;

	GetGraph()->NotifyGraphChanged();

	if (UBlueprint* BP = GetBlueprint())
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	}
}

#undef LOCTEXT_NAMESPACE
