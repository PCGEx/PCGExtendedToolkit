// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Types/PCGExTypeOpsImpl.h"

#include "Helpers/PCGExMetaHelpers.h"
#include "Types/PCGExTypeOps.h"

namespace PCGExTypeOps
{
	// FTypeOpsRegistry Implementation
	TArray<TUniquePtr<ITypeOpsBase>> FTypeOpsRegistry::TypeOps;
	bool FTypeOpsRegistry::bInitialized = false;

	const ITypeOpsBase* FTypeOpsRegistry::Get(const EPCGMetadataTypes Type)
	{
#define PCGEX_TPL(_TYPE, _NAME, ...) case EPCGMetadataTypes::_NAME: return &TTypeOpsImpl<_TYPE>::GetInstance();
		switch (Type)
		{
		PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
		default: return &FCopyOnlyTypeOps::GetInstance();
		}
#undef PCGEX_TPL
	}

	void FTypeOpsRegistry::Initialize()
	{
		// Using static instances via GetInstance(), no additional initialization needed
		bInitialized = true;
	}

	// FConversionTable Implementation
	FConvertFn FConversionTable::Table[PCGExTypes::TypesAllocations][PCGExTypes::TypesAllocations] = {};
	bool FConversionTable::bInitialized = false;

	namespace
	{
		void NoOpConvert(const void* /*Src*/, void* /*Dst*/) {}

		// Helper to populate a row of the conversion table at the correct enum indices
		template <typename TFrom>
		void PopulateConversionRow(FConvertFn* Row)
		{
			using namespace ConversionFunctions;
#define PCGEX_TPL(_TYPE, _NAME, ...) Row[static_cast<int32>(EPCGMetadataTypes::_NAME)] = GetConvertFunction<TFrom, _TYPE>();
			PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL
		}
	}

	// Populates the 256x256 type conversion dispatch table.
	// Known types get proper conversion functions at their enum value indices.
	// All other slots are initialized to NoOpConvert for safe fallback.
	void FConversionTable::Initialize()
	{
		if (bInitialized) { return; }

		// Fill entire table with no-op fallback
		for (int32 i = 0; i < PCGExTypes::TypesAllocations; ++i)
		{
			for (int32 j = 0; j < PCGExTypes::TypesAllocations; ++j)
			{
				Table[i][j] = &NoOpConvert;
			}
		}

		// Populate known type conversions at correct enum indices
#define PCGEX_TPL(_TYPE, _NAME, ...) PopulateConversionRow<_TYPE>(Table[static_cast<int32>(EPCGMetadataTypes::_NAME)]);
		PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

		bInitialized = true;
	}

	// Module Initialization
	struct FTypeOpsModuleInit
	{
		FTypeOpsModuleInit()
		{
			FConversionTable::Initialize();
			FTypeOpsRegistry::Initialize();
		}
	};

	// Static instance triggers initialization at module load
	static FTypeOpsModuleInit GTypeOpsModuleInit;

	// Explicit Template Instantiations
	// TTypeOpsImpl instantiations
#define PCGEX_TPL(_TYPE, _NAME, ...) \
	template class TTypeOpsImpl<_TYPE>; \
	template PCGEXCORE_API const ITypeOpsBase* FTypeOpsRegistry::Get<_TYPE>();
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

	// FTypeOps<T> instantiations - prevents recompilation in every translation unit
#define PCGEX_TPL(_TYPE, _NAME, ...) template struct FTypeOps<_TYPE>;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL
}
