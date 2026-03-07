// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExSubSelectionOps.h"
#include "Data/PCGExSubSelectionOpsImpl.h"

namespace PCGExData
{
	//
	// FSubSelectorRegistry implementation
	//

	TArray<TUniquePtr<ISubSelectorOps>> FSubSelectorRegistry::Ops;
	bool FSubSelectorRegistry::bInitialized = false;

	void FSubSelectorRegistry::Initialize()
	{
		if (bInitialized) { return; }

		Ops.SetNum(PCGExTypes::TypesAllocations); // Full enum range (256)

		// Create instances for each supported type
#define PCGEX_TPL(_TYPE, _NAME, ...) Ops[static_cast<int32>(EPCGMetadataTypes::_NAME)] = MakeUnique<TSubSelectorOpsImpl<_TYPE>>();
		PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

		bInitialized = true;
	}

	const ISubSelectorOps* FSubSelectorRegistry::Get(EPCGMetadataTypes Type)
	{
		if (!bInitialized) { Initialize(); }

		const int32 Index = static_cast<int32>(Type);
		if (Index >= 0 && Index < Ops.Num() && Ops[Index].IsValid()) { return Ops[Index].Get(); }

		return nullptr;
	}

	//
	// Explicit template instantiations
	//

#define PCGEX_INSTANTIATE_SUBSELECTOR_OPS(_TYPE, _NAME, ...) \
	template class TSubSelectorOpsImpl<_TYPE>;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_INSTANTIATE_SUBSELECTOR_OPS)
#undef PCGEX_INSTANTIATE_SUBSELECTOR_OPS
}
