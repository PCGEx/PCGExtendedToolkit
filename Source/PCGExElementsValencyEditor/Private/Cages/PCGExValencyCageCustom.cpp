// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Cages/PCGExValencyCageCustom.h"

APCGExValencyCageCustom::APCGExValencyCageCustom()
{
	// Root component is created by APCGExValencyCageBase
	// No shape component — containment is fully user-defined via BP overrides
}

#pragma region APCGExValencyCageCustom

FString APCGExValencyCageCustom::GetCageDisplayName() const
{
	if (!CageName.IsEmpty())
	{
		return CageName;
	}

	// Fall through to parent logic for asset counts, mirror status, etc.
	const FString ParentName = Super::GetCageDisplayName();

	// Replace "Cage" prefix with "Custom Cage" for clarity
	if (ParentName.StartsWith(TEXT("Cage")))
	{
		return TEXT("Custom ") + ParentName;
	}

	return ParentName;
}

#pragma endregion
