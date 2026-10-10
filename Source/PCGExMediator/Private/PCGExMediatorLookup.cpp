// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorLookup.h"

#include "Helpers/PCGExStreamingHelpers.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"

UObject* PCGExMediator::ResolveObject(const FString& Path)
{
	const FSoftObjectPath SoftPath(FPackageName::ExportTextPathToObjectPath(Path.TrimStartAndEnd()));
	if (SoftPath.IsNull()) { return nullptr; }
	if (UObject* Found = SoftPath.ResolveObject()) { return Found; }
	PCGExHelpers::LoadBlocking_AnyThread(SoftPath);
	return SoftPath.ResolveObject();
}

UField* PCGExMediator::FindTypeByName(UClass* TypeClass, const FString& NameOrPath)
{
	const FString Trimmed = NameOrPath.TrimStartAndEnd();
	if (Trimmed.IsEmpty()) { return nullptr; }
	if (UField* ByPath = Cast<UField>(StaticFindObject(TypeClass, nullptr, *Trimmed))) { return ByPath; }
	if (UField* ByName = Cast<UField>(StaticFindFirstObject(TypeClass, Trimmed, EFindFirstObjectOptions::NativeFirst))) { return ByName; }

	// Reflected names carry no C++ prefix; accept the C++ spelling too.
	const TCHAR Prefix = Trimmed[0];
	if (Trimmed.Len() > 1 && FChar::IsUpper(Trimmed[1]) && (Prefix == TEXT('U') || Prefix == TEXT('A') || Prefix == TEXT('F') || Prefix == TEXT('E')))
	{
		return Cast<UField>(StaticFindFirstObject(TypeClass, Trimmed.Mid(1), EFindFirstObjectOptions::NativeFirst));
	}
	return nullptr;
}
