// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExModuleInterface.h"

#include "PCGExLog.h"
#include "CoreMinimal.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Class.h"
#include "UObject/CoreRedirects.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ToolMenus.h"
#endif

TArray<IPCGExModuleInterface*> IPCGExModuleInterface::RegisteredModules;

#if WITH_EDITOR
TWeakPtr<FSlateStyleSet> IPCGExModuleInterface::EditorStyle;
#endif

void IPCGExModuleInterface::StartupModule()
{
	UE_LOG(LogPCGEx, Log, TEXT("IPCGExModuleInterface::StartupModule >> %s"), *GetModuleName());
	RegisteredModules.Add(this);

	if (OldBaseModules.Num() > 0)
	{
		RegisterRedirectors();
	}
}

void IPCGExModuleInterface::ShutdownModule()
{
	RegisteredModules.Remove(this);

#if WITH_EDITOR
	UnregisterMenuExtensions();
#endif
}

void IPCGExModuleInterface::RegisterRedirectors() const
{
	TArray<FCoreRedirect> Redirects;

	const FString ThisModuleName = GetModuleName();

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;

		FString ClassPath = Class->GetPathName();
		if (!ClassPath.StartsWith(FString::Printf(TEXT("/Script/%s."), *ThisModuleName)))
		{
			continue;
		}

		FString ClassName = Class->GetName();

		for (const FString& OldModuleName : OldBaseModules)
		{
			Redirects.Emplace(
				ECoreRedirectFlags::Type_Class,
				*FString::Printf(TEXT("/Script/%s.%s"), *OldModuleName, *ClassName),
				*FString::Printf(TEXT("/Script/%s.%s"), *ThisModuleName, *ClassName));
		}
	}

	if (Redirects.Num() > 0)
	{
		FCoreRedirects::AddRedirectList(Redirects, *ThisModuleName);
		UE_LOG(LogPCGEx, Log, TEXT("%s: Registered %d class redirects"), *ThisModuleName, Redirects.Num());
	}
}

#if WITH_EDITOR
void IPCGExModuleInterface::RegisterToEditor(const TSharedPtr<FSlateStyleSet>& InStyle)
{
}

void IPCGExModuleInterface::RegisterMenuExtensions()
{
}

void IPCGExModuleInterface::UnregisterMenuExtensions()
{
	UToolMenus::UnregisterOwner(this);
}
#endif
