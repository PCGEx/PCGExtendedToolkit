// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"

/**
 * Custom serialization version for packages containing PCGExCollections data.
 * Append new entries before VersionPlusOne; never reorder or remove existing ones.
 */
struct PCGEXCOLLECTIONS_API FPCGExCollectionsCustomVersion
{
	enum Type
	{
		/** Versions before this custom version system existed. */
		BeforeCustomVersionWasAdded = 0,

		/**
		 * Collection schema migrations (grammar v0 -> v1, fitting v0 -> v1) run only on packages older than this.
		 * The int schema-version UPROPERTYs default to 0 on a fresh collection, so on their own they cannot tell a
		 * new asset from a legacy one. A new migration adds an entry here and gates on it, never on an int default.
		 */
		SchemaMigrationsGatedByCustomVersion,

		/**
		 * Mesh entries and mesh globals carry the static-mesh-only descriptor half (SMExtras) instead of a full SM
		 * descriptor. Older packages fold their SM descriptor into it on load.
		 */
		MeshEntryStaticMeshExtras,

		/** Staging Data mesh selectors forward input data tags. Selectors from older packages load with it off. */
		StagedSelectorForwardsInputDataTags,

		// -- add new versions above this line --
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	/** The GUID for this custom version. Must never change once shipped. */
	static const FGuid GUID;

	FPCGExCollectionsCustomVersion() = delete;
};
