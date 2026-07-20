// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "EdGraph/EdGraphPin.h"

#include "K2Node_PCGExMemberBase.generated.h"

class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;
class FProperty;
class UEdGraph;
class UEdGraphPin;
class UGraphNodeContextMenuContext;
class UToolMenu;
struct FToolMenuSection;

namespace PCGExAssetCollection
{
	struct FTypeInfo;
}

/**
 * Shared machinery for the reflected member-access nodes (Get/Set Entry Member,
 * Get/Set Collection Member). Leaves only declare their scope (entry vs collection),
 * direction (get vs set) and backing library function names.
 *
 * Workflow: right-click the node -> "Pick Member" lists the resolved root's reflected
 * members (top-level + one level into struct members); picking one writes the MemberPath
 * pin's default AND stamps the value pin to the member's exact reflected type. The pin
 * type is persisted; re-pick after renaming/retyping members to refresh stale graphs
 * (runtime resolution fails loud on mismatch, it never corrupts).
 *
 * The root struct comes from the collection type registry: inferred from the Collection
 * pin's connected static type, or forced via the right-click "Collection Type" menu.
 * Unregistered/unknown types fall back to the base entry struct / base collection class.
 *
 * Member paths may also be wired dynamically (FName pin); the value pin keeps the last
 * picked type, and the runtime exact-type check guards mismatches.
 */
UCLASS(Abstract)
class PCGEXCOLLECTIONSUNCOOKED_API UK2Node_PCGExMemberBase : public UK2Node
{
	GENERATED_BODY()

public:
	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	// UK2Node
	virtual bool IsNodePure() const override
	{
		return !IsSetNode();
	}

	virtual FText GetMenuCategory() const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

protected:
	/** True for Set nodes (exec pins, value is an input); false for pure Get nodes. */
	virtual bool IsSetNode() const PURE_VIRTUAL(UK2Node_PCGExMemberBase::IsSetNode, return false;);

	/** True when the node targets an entry (Collection + EntryIndex); false for the collection object itself. */
	virtual bool IsEntryScoped() const PURE_VIRTUAL(UK2Node_PCGExMemberBase::IsEntryScoped, return true;);

	/** Backing library function for struct/primitive (wildcard) members. */
	virtual FName GetWildcardFunctionName() const PURE_VIRTUAL(UK2Node_PCGExMemberBase::GetWildcardFunctionName, return NAME_None;);

	/** Backing library function for soft object reference members. */
	virtual FName GetSoftObjectFunctionName() const PURE_VIRTUAL(UK2Node_PCGExMemberBase::GetSoftObjectFunctionName, return NAME_None;);

	/** Backing library function for soft class reference members. */
	virtual FName GetSoftClassFunctionName() const PURE_VIRTUAL(UK2Node_PCGExMemberBase::GetSoftClassFunctionName, return NAME_None;);

	/** Title before the member path is appended, e.g. "Get Entry Member". */
	virtual FText GetBaseTitle() const PURE_VIRTUAL(UK2Node_PCGExMemberBase::GetBaseTitle, return FText::GetEmpty(););

	/**
	 * Persisted resolved pin type for the value pin (including container type -- whole-array
	 * members are legitimate here). Re-stamped in AllocateDefaultPins so picked types survive
	 * save/reopen. Wildcard until a member is picked.
	 */
	UPROPERTY()
	FEdGraphPinType ResolvedPinType;

	/** Forced collection type for member resolution; None = infer from the Collection pin. */
	UPROPERTY()
	FName ExplicitTypeId;

	// Pin accessors
	UEdGraphPin* GetExecInputPin() const;
	UEdGraphPin* GetThenPin() const;
	UEdGraphPin* GetCollectionPin() const;
	UEdGraphPin* GetEntryIndexPin() const;
	UEdGraphPin* GetMemberPathPin() const;
	UEdGraphPin* GetValuePin() const;
	UEdGraphPin* GetSuccessPin() const;

	/** Registry info per ExplicitTypeId or the Collection pin's connected static type.
	 *  Copy-out (registry interior pointers must not escape its lock); returns false when
	 *  neither the explicit type id nor the connected pin's class resolves to a type. */
	bool ResolveTypeInfo(PCGExAssetCollection::FTypeInfo& OutInfo) const;

	/** Reflection root the member menu and edit-time validation walk (never null). */
	const UStruct* ResolveRootStruct() const;

	/** Append the member-picker entries for Root to the given section. */
	void BuildMemberMenu(FToolMenuSection& Section, const UStruct* Root) const;

	/** Apply a picked member: MemberPath pin default + value pin type + persistence. */
	void ApplyMemberPick(const FString& Path, const FEdGraphPinType& PinType);

	/** Stamp the value pin (breaking now-incompatible links) and persist the type. */
	void SetValuePinType(const FEdGraphPinType& InType);

	/** Terminal eligibility: flags, named skiplists, scope rules. Type rules via IsTypeAllowable. */
	bool IsMemberEligible(const FProperty* Property, bool bIsTopLevel) const;

	/** Type-level eligibility (recursive into container inners): no hard object refs, BP-allowable enums/structs. */
	static bool IsTypeAllowable(const FProperty* Property);

	static const FName CollectionPinName;
	static const FName EntryIndexPinName;
	static const FName MemberPathPinName;
	static const FName ValuePinNameGet;
	static const FName ValuePinNameSet;
	static const FName SuccessPinName;
};
