// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExPropertyMediatorBindings.h"

#include "PCGExMediatorRegistry.h"
#include "PCGExPropertySchemaAsset.h"
#include "Elements/PCGExTuple.h"
#include "Mediator/PCGExPropertyMediatorHooks.h"
#include "UObject/UnrealType.h"

void PCGExPropertyMediatorBindings::Register(FPCGExMediatorDomain& Domain)
{
	{
		FPCGExMediatorBinding Binding;
		Binding.HostClass = UPCGExPropertySchemaAsset::StaticClass();
		Binding.Members = {GET_MEMBER_NAME_CHECKED(UPCGExPropertySchemaAsset, Collection)};
		Binding.Summary = TEXT("Property Schema asset.");
		Domain.AddBinding(Binding);
	}
	{
		FPCGExMediatorBinding Binding;
		Binding.HostClass = UPCGExTupleSettings::StaticClass();
		Binding.Members = {GET_MEMBER_NAME_CHECKED(UPCGExTupleSettings, Composition), GET_MEMBER_NAME_CHECKED(UPCGExTupleSettings, Values)};
		// Rows resolve names and types against the composition the document carries, else the live one.
		Binding.WrapImport = [](UObject* Host, const FName Member, FPCGExMediatorDecodedMember Decoded, TFunctionRef<void()> Import)
		{
			const UPCGExTupleSettings* Tuple = Cast<UPCGExTupleSettings>(Host);
			if (Tuple && Member == GET_MEMBER_NAME_CHECKED(UPCGExTupleSettings, Values))
			{
				const FPCGExPropertySchemaCollection* Composition = static_cast<const FPCGExPropertySchemaCollection*>(Decoded(GET_MEMBER_NAME_CHECKED(UPCGExTupleSettings, Composition)));
				PCGExPropertyMediator::FOverridesSchemaScope Scope((Composition ? Composition : &Tuple->Composition)->BuildSchema());
				Import();
				return;
			}
			Import();
		};
		Binding.Summary = TEXT("Tuple node: composition schema and value rows.");
		Domain.AddBinding(Binding);
	}
}
