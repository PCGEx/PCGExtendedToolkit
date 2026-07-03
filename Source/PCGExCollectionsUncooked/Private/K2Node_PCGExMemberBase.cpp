// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "K2Node_PCGExMemberBase.h"

#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "KismetCompiler.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExAssetCollectionTypes.h"
#include "Helpers/PCGExCollectionEntryBlueprintLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Styling/AppStyle.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "K2Node_PCGExMemberBase"

// Pin names match the library functions' parameter names so ExpandNode can look up the
// spawned UK2Node_CallFunction's pins via FindPinChecked.
const FName UK2Node_PCGExMemberBase::CollectionPinName(TEXT("Collection"));
const FName UK2Node_PCGExMemberBase::EntryIndexPinName(TEXT("EntryIndex"));
const FName UK2Node_PCGExMemberBase::MemberPathPinName(TEXT("MemberPath"));
const FName UK2Node_PCGExMemberBase::ValuePinNameGet(TEXT("OutValue"));
const FName UK2Node_PCGExMemberBase::ValuePinNameSet(TEXT("NewValue"));
const FName UK2Node_PCGExMemberBase::SuccessPinName(TEXT("Success"));

void UK2Node_PCGExMemberBase::AllocateDefaultPins()
{
	if (IsSetNode())
	{
		CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
		CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);
	}

	UEdGraphPin* CollectionPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UPCGExAssetCollection::StaticClass(), CollectionPinName);
	CollectionPin->PinFriendlyName = LOCTEXT("CollectionPinFriendlyName", "Collection");

	if (IsEntryScoped())
	{
		UEdGraphPin* EntryIndexPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Int, EntryIndexPinName);
		EntryIndexPin->PinFriendlyName = LOCTEXT("EntryIndexPinFriendlyName", "Entry Index");
		EntryIndexPin->DefaultValue = TEXT("0");
	}

	UEdGraphPin* MemberPathPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, MemberPathPinName);
	MemberPathPin->PinFriendlyName = LOCTEXT("MemberPathPinFriendlyName", "Member");

	UEdGraphPin* ValuePin = IsSetNode()
		? CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Wildcard, ValuePinNameSet)
		: CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Wildcard, ValuePinNameGet);
	ValuePin->PinFriendlyName = IsSetNode()
		? LOCTEXT("NewValuePinFriendlyName", "New Value")
		: LOCTEXT("ValuePinFriendlyName", "Value");

	// Restore the picked member's type (including container type -- whole-array members are
	// legitimate) so the node isn't reset to a gray wildcard on graph reopen.
	if (ResolvedPinType.PinCategory != NAME_None &&
		ResolvedPinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard)
	{
		ValuePin->PinType = ResolvedPinType;
	}

	UEdGraphPin* SuccessPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, SuccessPinName);
	SuccessPin->PinFriendlyName = LOCTEXT("SuccessPinFriendlyName", "Success");

	Super::AllocateDefaultPins();
}

FText UK2Node_PCGExMemberBase::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::MenuTitle || TitleType == ENodeTitleType::ListView)
	{
		return GetBaseTitle();
	}

	const UEdGraphPin* MemberPathPin = GetMemberPathPin();
	const FString Path = MemberPathPin ? MemberPathPin->DefaultValue : FString();
	if (Path.IsEmpty())
	{
		return GetBaseTitle();
	}

	return FText::Format(INVTEXT("{0}\n{1}"), GetBaseTitle(), FText::FromString(Path));
}

FText UK2Node_PCGExMemberBase::GetTooltipText() const
{
	return LOCTEXT("NodeTooltip",
	               "Reflected member access on a PCGEx Asset Collection.\n"
	               "Right-click the node and Pick Member to choose a member path -- this also "
	               "types the value pin to the member's exact reflected type.\n"
	               "The member path pin can be wired for data-driven paths; the value pin keeps "
	               "the last picked type and mismatches fail loud at runtime.");
}

FSlateIcon UK2Node_PCGExMemberBase::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = GetNodeTitleColor();
	static FSlateIcon Icon(FAppStyle::GetAppStyleSetName(), "GraphEditor.Function_16x");
	return Icon;
}

FLinearColor UK2Node_PCGExMemberBase::GetNodeTitleColor() const
{
	return FLinearColor(0.0f, 0.5f, 0.8f);
}

FText UK2Node_PCGExMemberBase::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "PCGEx|Collection");
}

void UK2Node_PCGExMemberBase::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	if (Context->bIsDebugging)
	{
		return;
	}

	{
		FToolMenuSection& Section = Menu->AddSection("PCGExPickMember", LOCTEXT("PickMemberSection", "Pick Member"));
		BuildMemberMenu(Section, ResolveRootStruct());
	}

	{
		FToolMenuSection& Section = Menu->AddSection("PCGExCollectionType", LOCTEXT("CollectionTypeSection", "Collection Type"));

		const UK2Node_PCGExMemberBase* ConstThis = this;

		Section.AddMenuEntry(
			NAME_None,
			LOCTEXT("CollectionTypeAutomatic", "Automatic (from Collection pin)"),
			LOCTEXT("CollectionTypeAutomaticTooltip", "Resolve the member list from the Collection pin's connected static type."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([ConstThis]()
			{
				if (UK2Node_PCGExMemberBase* MutableThis = const_cast<UK2Node_PCGExMemberBase*>(ConstThis))
				{
					MutableThis->Modify();
					MutableThis->ExplicitTypeId = NAME_None;
				}
			})));

		TArray<FName> TypeIds;
		PCGExAssetCollection::FTypeRegistry::Get().GetAllTypeIds(TypeIds);
		TypeIds.Sort(FNameLexicalLess());

		for (const FName TypeId : TypeIds)
		{
			const PCGExAssetCollection::FTypeInfo* TypeInfo = PCGExAssetCollection::FTypeRegistry::Get().Find(TypeId);
			if (!TypeInfo)
			{
				continue;
			}

			Section.AddMenuEntry(
				NAME_None,
				TypeInfo->DisplayName,
				FText::FromName(TypeId),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([ConstThis, TypeId]()
				{
					if (UK2Node_PCGExMemberBase* MutableThis = const_cast<UK2Node_PCGExMemberBase*>(ConstThis))
					{
						MutableThis->Modify();
						MutableThis->ExplicitTypeId = TypeId;
					}
				})));
		}
	}
}

const PCGExAssetCollection::FTypeInfo* UK2Node_PCGExMemberBase::ResolveTypeInfo() const
{
	PCGExAssetCollection::FTypeRegistry& Registry = PCGExAssetCollection::FTypeRegistry::Get();

	if (!ExplicitTypeId.IsNone())
	{
		if (const PCGExAssetCollection::FTypeInfo* TypeInfo = Registry.Find(ExplicitTypeId))
		{
			return TypeInfo;
		}
	}

	const UEdGraphPin* CollectionPin = GetCollectionPin();
	const UEdGraphPin* Source = (CollectionPin && CollectionPin->LinkedTo.Num() > 0) ? CollectionPin->LinkedTo[0] : nullptr;
	const UClass* PinClass = Source ? Cast<UClass>(Source->PinType.PinSubCategoryObject.Get()) : nullptr;

	return PinClass ? Registry.FindByClass(PinClass) : nullptr;
}

const UStruct* UK2Node_PCGExMemberBase::ResolveRootStruct() const
{
	const PCGExAssetCollection::FTypeInfo* TypeInfo = ResolveTypeInfo();

	if (IsEntryScoped())
	{
		if (TypeInfo && TypeInfo->EntryStruct)
		{
			return TypeInfo->EntryStruct;
		}
		return FPCGExAssetCollectionEntry::StaticStruct();
	}

	if (TypeInfo && TypeInfo->CollectionClass.IsValid())
	{
		return TypeInfo->CollectionClass.Get();
	}

	// Collection scope without registry info: prefer the connected pin's static class so BP
	// subclass members stay reachable.
	const UEdGraphPin* CollectionPin = GetCollectionPin();
	const UEdGraphPin* Source = (CollectionPin && CollectionPin->LinkedTo.Num() > 0) ? CollectionPin->LinkedTo[0] : nullptr;
	if (const UClass* PinClass = Source ? Cast<UClass>(Source->PinType.PinSubCategoryObject.Get()) : nullptr)
	{
		if (PinClass->IsChildOf(UPCGExAssetCollection::StaticClass()))
		{
			return PinClass;
		}
	}

	return UPCGExAssetCollection::StaticClass();
}

bool UK2Node_PCGExMemberBase::IsTypeAllowable(const FProperty* Property)
{
	if (!Property)
	{
		return false;
	}

	// Soft references first: FSoftObjectProperty/FSoftClassProperty derive from
	// FObjectPropertyBase but ARE supported (typed soft accessors).
	if (Property->IsA<FSoftObjectProperty>())
	{
		return true;
	}

	// Hard object-like references and delegates are out of scope for generic member access.
	if (Property->IsA<FObjectPropertyBase>() ||
		Property->IsA<FInterfaceProperty>() ||
		Property->IsA<FDelegateProperty>() ||
		Property->IsA<FMulticastDelegateProperty>() ||
		Property->IsA<FFieldPathProperty>())
	{
		return false;
	}

	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		return UEdGraphSchema_K2::IsAllowableBlueprintVariableType(EnumProperty->GetEnum());
	}

	if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		return !ByteProperty->Enum || UEdGraphSchema_K2::IsAllowableBlueprintVariableType(ByteProperty->Enum);
	}

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		return UEdGraphSchema_K2::IsAllowableBlueprintVariableType(StructProperty->Struct);
	}

	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		return IsTypeAllowable(ArrayProperty->Inner);
	}

	if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		return IsTypeAllowable(SetProperty->ElementProp);
	}

	if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		return IsTypeAllowable(MapProperty->KeyProp) && IsTypeAllowable(MapProperty->ValueProp);
	}

	return true;
}

bool UK2Node_PCGExMemberBase::IsMemberEligible(const FProperty* Property, bool bIsTopLevel) const
{
	if (!Property)
	{
		return false;
	}

	if (Property->HasAnyPropertyFlags(CPF_Deprecated))
	{
		return false;
	}

	// Only authorable surface: EditAnywhere/VisibleAnywhere set CPF_Edit; plain UPROPERTY()
	// internals (PropertyRegistry, schema versions...) drop out here.
	if (!Property->HasAnyPropertyFlags(CPF_Edit))
	{
		return false;
	}

	if (IsSetNode() && Property->HasAnyPropertyFlags(CPF_EditConst))
	{
		return false;
	}

	if (bIsTopLevel)
	{
		const FName MemberName = Property->GetFName();
		if (IsEntryScoped())
		{
			// PropertyOverrides has dedicated typed nodes; generic struct writes would bypass
			// the HeaderId/schema sync invariants.
			if (MemberName == FName(TEXT("PropertyOverrides")))
			{
				return false;
			}
			// Structural / rebuild-owned members are read-only through this node family.
			if (IsSetNode() && (MemberName == FName(TEXT("bIsSubCollection")) || MemberName == FName(TEXT("Staging"))))
			{
				return false;
			}
		}
		else
		{
			// Schema container with sync invariants -- use the collection's own UI/API.
			if (MemberName == FName(TEXT("CollectionProperties")))
			{
				return false;
			}
			// Resizing entries generically would bypass subcollection sanitize and the cache's
			// raw entry pointers.
			if (IsSetNode() && MemberName == FName(TEXT("Entries")))
			{
				return false;
			}
		}
	}

	return true;
}

void UK2Node_PCGExMemberBase::BuildMemberMenu(FToolMenuSection& Section, const UStruct* Root) const
{
	if (!Root)
	{
		return;
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	const UK2Node_PCGExMemberBase* ConstThis = this;

	for (TFieldIterator<FProperty> It(Root); It; ++It)
	{
		const FProperty* Property = *It;
		if (!IsMemberEligible(Property, true))
		{
			continue;
		}

		// Struct members get a submenu: optional whole-struct pick + direct leaves. The
		// submenu itself only needs flag/skiplist eligibility -- a non-BP struct (e.g. the
		// engine ISM descriptor) still exposes its leaves even though the whole-struct pick
		// is filtered out.
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			const FString StructPath = Property->GetName();
			Section.AddSubMenu(
				FName(*StructPath),
				Property->GetDisplayNameText(),
				FText::FromString(StructPath),
				FNewToolMenuDelegate::CreateLambda([ConstThis, StructProperty, StructPath](UToolMenu* SubMenu)
				{
					FToolMenuSection& SubSection = SubMenu->AddSection("PCGExMemberLeaves");
					const UEdGraphSchema_K2* SubSchema = GetDefault<UEdGraphSchema_K2>();

					FEdGraphPinType WholeStructType;
					if (UEdGraphSchema_K2::IsAllowableBlueprintVariableType(StructProperty->Struct) &&
						SubSchema->ConvertPropertyToPinType(StructProperty, WholeStructType))
					{
						SubSection.AddMenuEntry(
							NAME_None,
							LOCTEXT("WholeStructPick", "(whole struct)"),
							FText::FromString(StructPath),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([ConstThis, StructPath, WholeStructType]()
							{
								if (UK2Node_PCGExMemberBase* MutableThis = const_cast<UK2Node_PCGExMemberBase*>(ConstThis))
								{
									MutableThis->ApplyMemberPick(StructPath, WholeStructType);
								}
							})));
					}

					for (TFieldIterator<FProperty> LeafIt(StructProperty->Struct); LeafIt; ++LeafIt)
					{
						const FProperty* Leaf = *LeafIt;
						if (!ConstThis->IsMemberEligible(Leaf, false) || !IsTypeAllowable(Leaf))
						{
							continue;
						}

						FEdGraphPinType LeafType;
						if (!SubSchema->ConvertPropertyToPinType(Leaf, LeafType))
						{
							continue;
						}

						const FString LeafPath = StructPath + TEXT(".") + Leaf->GetName();
						SubSection.AddMenuEntry(
							NAME_None,
							Leaf->GetDisplayNameText(),
							FText::FromString(LeafPath),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([ConstThis, LeafPath, LeafType]()
							{
								if (UK2Node_PCGExMemberBase* MutableThis = const_cast<UK2Node_PCGExMemberBase*>(ConstThis))
								{
									MutableThis->ApplyMemberPick(LeafPath, LeafType);
								}
							})));
					}
				}));
			continue;
		}

		if (!IsTypeAllowable(Property))
		{
			continue;
		}

		FEdGraphPinType PinType;
		if (!Schema->ConvertPropertyToPinType(Property, PinType))
		{
			continue;
		}

		const FString Path = Property->GetName();
		Section.AddMenuEntry(
			NAME_None,
			Property->GetDisplayNameText(),
			FText::FromString(Path),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([ConstThis, Path, PinType]()
			{
				if (UK2Node_PCGExMemberBase* MutableThis = const_cast<UK2Node_PCGExMemberBase*>(ConstThis))
				{
					MutableThis->ApplyMemberPick(Path, PinType);
				}
			})));
	}
}

void UK2Node_PCGExMemberBase::ApplyMemberPick(const FString& Path, const FEdGraphPinType& PinType)
{
	Modify();

	if (UEdGraphPin* MemberPathPin = GetMemberPathPin())
	{
		MemberPathPin->Modify();
		MemberPathPin->DefaultValue = Path;
	}

	SetValuePinType(PinType);
}

void UK2Node_PCGExMemberBase::SetValuePinType(const FEdGraphPinType& InType)
{
	UEdGraphPin* ValuePin = GetValuePin();
	if (!ValuePin)
	{
		return;
	}

	// Break existing links that no longer match the picked member's type.
	for (int32 i = ValuePin->LinkedTo.Num() - 1; i >= 0; --i)
	{
		const UEdGraphPin* Linked = ValuePin->LinkedTo[i];
		if (!Linked || Linked->PinType != InType)
		{
			ValuePin->BreakLinkTo(ValuePin->LinkedTo[i]);
		}
	}

	ValuePin->PinType = InType;
	ResolvedPinType = InType;
	GetGraph()->NotifyGraphChanged();

	if (UBlueprint* BP = GetBlueprint())
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	}
}

void UK2Node_PCGExMemberBase::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	UEdGraphPin* ValuePin = GetValuePin();
	if (!ValuePin || ValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
	{
		CompilerContext.MessageLog.Error(
			*LOCTEXT("WildcardUnresolved",
			         "@@ has no member picked. Right-click the node and use Pick Member to choose one.")
			.ToString(),
			this);
		BreakAllNodeLinks();
		return;
	}

	const FName Category = ValuePin->PinType.PinCategory;
	const bool bIsSoftObject = Category == UEdGraphSchema_K2::PC_SoftObject;
	const bool bIsSoftClass = Category == UEdGraphSchema_K2::PC_SoftClass;
	const bool bIsHardRef =
		Category == UEdGraphSchema_K2::PC_Object ||
		Category == UEdGraphSchema_K2::PC_Class ||
		Category == UEdGraphSchema_K2::PC_Interface;

	if (bIsHardRef)
	{
		CompilerContext.MessageLog.Error(
			*LOCTEXT("HardRefUnsupported",
			         "@@ targets a hard object/class reference member; only soft references and value types are supported.")
			.ToString(),
			this);
		BreakAllNodeLinks();
		return;
	}

	FName FunctionName;
	if (bIsSoftObject)
	{
		FunctionName = GetSoftObjectFunctionName();
	}
	else if (bIsSoftClass)
	{
		FunctionName = GetSoftClassFunctionName();
	}
	else
	{
		FunctionName = GetWildcardFunctionName();
	}

	UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallNode->FunctionReference.SetExternalMember(FunctionName, UPCGExCollectionEntryBlueprintLibrary::StaticClass());
	CallNode->AllocateDefaultPins();

	CompilerContext.MovePinLinksToIntermediate(*GetCollectionPin(), *CallNode->FindPinChecked(CollectionPinName));
	if (IsEntryScoped())
	{
		CompilerContext.MovePinLinksToIntermediate(*GetEntryIndexPin(), *CallNode->FindPinChecked(EntryIndexPinName));
	}
	CompilerContext.MovePinLinksToIntermediate(*GetMemberPathPin(), *CallNode->FindPinChecked(MemberPathPinName));

	if (IsSetNode())
	{
		CompilerContext.MovePinLinksToIntermediate(*GetExecInputPin(), *CallNode->GetExecPin());
		CompilerContext.MovePinLinksToIntermediate(*GetThenPin(), *CallNode->GetThenPin());
	}

	if (bIsSoftObject || bIsSoftClass)
	{
		if (IsSetNode())
		{
			// Soft setters take TSoft(Object|Class)Ptr<UObject>; the schema's soft upcast
			// accepts the member-typed connection.
			CompilerContext.MovePinLinksToIntermediate(*ValuePin, *CallNode->FindPinChecked(ValuePinNameSet));
			CompilerContext.MovePinLinksToIntermediate(*GetSuccessPin(), *CallNode->GetReturnValuePin());
		}
		else
		{
			UClass* TargetClass = Cast<UClass>(ValuePin->PinType.PinSubCategoryObject.Get());
			if (!TargetClass)
			{
				CompilerContext.MessageLog.Error(
					*LOCTEXT("SoftPinMissingClass",
					         "@@ has a soft reference pin with no resolved class. Re-pick the member.")
					.ToString(),
					this);
				BreakAllNodeLinks();
				return;
			}

			// Drive DeterminesOutputType so the call's return pin conforms to the member class.
			UEdGraphPin* ExpectedClassPin = CallNode->FindPinChecked(TEXT("ExpectedClass"));
			ExpectedClassPin->DefaultObject = TargetClass;
			CallNode->PinDefaultValueChanged(ExpectedClassPin);

			CompilerContext.MovePinLinksToIntermediate(*ValuePin, *CallNode->GetReturnValuePin());
			CompilerContext.MovePinLinksToIntermediate(*GetSuccessPin(), *CallNode->FindPinChecked(TEXT("bSuccess")));
		}

		BreakAllNodeLinks();
		return;
	}

	// Wildcard path: stamp the picked member's type onto the call's CustomStructureParam pin
	// before moving connections, so the BP compiler marshals the exact concrete type.
	UEdGraphPin* CallValuePin = CallNode->FindPinChecked(IsSetNode() ? ValuePinNameSet : ValuePinNameGet);
	CallValuePin->PinType = ValuePin->PinType;

	CompilerContext.MovePinLinksToIntermediate(*ValuePin, *CallValuePin);
	CompilerContext.MovePinLinksToIntermediate(*GetSuccessPin(), *CallNode->GetReturnValuePin());

	BreakAllNodeLinks();
}

UEdGraphPin* UK2Node_PCGExMemberBase::GetExecInputPin() const
{
	return FindPin(UEdGraphSchema_K2::PN_Execute);
}

UEdGraphPin* UK2Node_PCGExMemberBase::GetThenPin() const
{
	return FindPin(UEdGraphSchema_K2::PN_Then);
}

UEdGraphPin* UK2Node_PCGExMemberBase::GetCollectionPin() const
{
	return FindPin(CollectionPinName);
}

UEdGraphPin* UK2Node_PCGExMemberBase::GetEntryIndexPin() const
{
	return FindPin(EntryIndexPinName);
}

UEdGraphPin* UK2Node_PCGExMemberBase::GetMemberPathPin() const
{
	return FindPin(MemberPathPinName);
}

UEdGraphPin* UK2Node_PCGExMemberBase::GetValuePin() const
{
	return FindPin(IsSetNode() ? ValuePinNameSet : ValuePinNameGet);
}

UEdGraphPin* UK2Node_PCGExMemberBase::GetSuccessPin() const
{
	return FindPin(SuccessPinName);
}

#undef LOCTEXT_NAMESPACE
