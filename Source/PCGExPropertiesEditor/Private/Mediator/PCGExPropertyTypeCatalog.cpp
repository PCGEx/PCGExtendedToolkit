// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExPropertyTypeCatalog.h"

#include "PCGExLog.h"
#include "PCGExProperty.h"
#include "Modules/ModuleManager.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/UObjectIterator.h"

namespace PCGExPropertyTypeCatalog
{
	struct FState
	{
		TArray<FPCGExPropertyTypeInfo> Types;
		bool bBuilt = false;
		FDelegateHandle ModulesChangedHandle;
	};

	FState& State()
	{
		static FState Instance;
		return Instance;
	}

	void Build(TArray<FPCGExPropertyTypeInfo>& OutTypes)
	{
		const UScriptStruct* Base = FPCGExProperty::StaticStruct();

		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			const UScriptStruct* Struct = *It;
			if (Struct == Base || !Struct->IsChildOf(Base)) { continue; }
			if (Struct->HasMetaData(TEXT("Deprecated"))) { continue; }

			FInstancedStruct Instance;
			Instance.InitializeAs(Struct);
			const FPCGExProperty* Prototype = Instance.GetPtr<FPCGExProperty>();
			if (!Prototype) { continue; }

			FPCGExPropertyTypeInfo Info;
			Info.TypeName = Prototype->GetTypeName();
			Info.Struct = Struct;
			Info.OutputType = Prototype->GetOutputType();
			Info.bSupportsOutput = Prototype->SupportsOutput();
			Info.bSupportsSampling = Prototype->SupportsSampling();
			Info.DisplayName = Struct->GetDisplayNameText().ToString();

			if (Info.TypeName.IsNone() || Info.TypeName == FName("Unknown"))
			{
				UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] Property type %s reports no type name; addressable by struct path only."), *Struct->GetPathName());
			}
			else if (const FPCGExPropertyTypeInfo* Clash = OutTypes.FindByPredicate([&Info](const FPCGExPropertyTypeInfo& T) { return T.TypeName == Info.TypeName; }))
			{
				UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] Property types %s and %s share the type name '%s'; the second is addressable by struct path only."),
				       *Clash->Struct->GetPathName(), *Struct->GetPathName(), *Info.TypeName.ToString());
				Info.TypeName = NAME_None;
			}

			OutTypes.Add(MoveTemp(Info));
		}

		OutTypes.Sort([](const FPCGExPropertyTypeInfo& A, const FPCGExPropertyTypeInfo& B)
		{
			return A.TypeName.LexicalLess(B.TypeName);
		});
	}

	void OnModulesChanged(FName, EModuleChangeReason)
	{
		PCGExPropertyCatalog::Invalidate();
	}
}

const TArray<FPCGExPropertyTypeInfo>& PCGExPropertyCatalog::Get()
{
	check(IsInGameThread());
	PCGExPropertyTypeCatalog::FState& S = PCGExPropertyTypeCatalog::State();
	if (!S.bBuilt)
	{
		S.Types.Reset();
		PCGExPropertyTypeCatalog::Build(S.Types);
		S.bBuilt = true;
	}
	return S.Types;
}

const FPCGExPropertyTypeInfo* PCGExPropertyCatalog::Find(const FString& TypeNameOrStructPath)
{
	if (TypeNameOrStructPath.IsEmpty()) { return nullptr; }

	const TArray<FPCGExPropertyTypeInfo>& Types = Get();
	const FName AsName(*TypeNameOrStructPath);
	for (const FPCGExPropertyTypeInfo& Info : Types)
	{
		if (!Info.TypeName.IsNone() && Info.TypeName == AsName) { return &Info; }
	}
	for (const FPCGExPropertyTypeInfo& Info : Types)
	{
		if (Info.Struct->GetPathName() == TypeNameOrStructPath || Info.Struct->GetName() == TypeNameOrStructPath) { return &Info; }
	}
	return nullptr;
}

const FPCGExPropertyTypeInfo* PCGExPropertyCatalog::FindByStruct(const UScriptStruct* Struct)
{
	if (!Struct) { return nullptr; }
	for (const FPCGExPropertyTypeInfo& Info : Get())
	{
		if (Info.Struct == Struct) { return &Info; }
	}
	return nullptr;
}

bool PCGExPropertyCatalog::MakeProperty(const FPCGExPropertyTypeInfo& Info, const FName PropertyName, FInstancedStruct& OutProperty)
{
	if (!Info.Struct) { return false; }
	OutProperty.InitializeAs(Info.Struct);
	FPCGExProperty* Property = OutProperty.GetMutablePtr<FPCGExProperty>();
	if (!Property) { return false; }
	Property->PropertyName = PropertyName;
	return true;
}

FString PCGExPropertyCatalog::ListTypeNames()
{
	TArray<FString> Names;
	for (const FPCGExPropertyTypeInfo& Info : Get())
	{
		if (!Info.TypeName.IsNone()) { Names.Add(Info.TypeName.ToString()); }
	}
	return FString::Join(Names, TEXT(", "));
}

void PCGExPropertyCatalog::Invalidate()
{
	PCGExPropertyTypeCatalog::State().bBuilt = false;
}

void PCGExPropertyCatalog::Startup()
{
	PCGExPropertyTypeCatalog::FState& S = PCGExPropertyTypeCatalog::State();
	if (!S.ModulesChangedHandle.IsValid())
	{
		S.ModulesChangedHandle = FModuleManager::Get().OnModulesChanged().AddStatic(&PCGExPropertyTypeCatalog::OnModulesChanged);
	}
}

void PCGExPropertyCatalog::Shutdown()
{
	PCGExPropertyTypeCatalog::FState& S = PCGExPropertyTypeCatalog::State();
	if (S.ModulesChangedHandle.IsValid())
	{
		FModuleManager::Get().OnModulesChanged().Remove(S.ModulesChangedHandle);
		S.ModulesChangedHandle.Reset();
	}
	S.Types.Reset();
	S.bBuilt = false;
}
