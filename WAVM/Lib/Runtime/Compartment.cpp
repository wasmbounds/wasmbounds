#include <stddef.h>
#include <atomic>
#include <memory>
#include <vector>
#include "RuntimePrivate.h"
#include "WAVM/Inline/Assert.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Inline/Timing.h"
#include "WAVM/Platform/Memory.h"
#include "WAVM/Platform/RWMutex.h"
#include "WAVM/Runtime/Runtime.h"
#include "WAVM/RuntimeABI/RuntimeABI.h"

#ifdef WAVM_HAS_TRACY
#include <Tracy.hpp>
#endif

using namespace WAVM;
using namespace WAVM::Runtime;

Runtime::Compartment::Compartment(std::string&& inDebugName,
								  struct CompartmentRuntimeData* inRuntimeData,
								  U8* inUnalignedRuntimeData)
: GCObject(ObjectKind::compartment, this, std::move(inDebugName))
, runtimeData(inRuntimeData)
, unalignedRuntimeData(inUnalignedRuntimeData)
, tables(0, maxTables - 1)
, memories(0, maxMemories - 1)
// Use UINTPTR_MAX as an invalid ID for globals, exception types, and instances.
, globals(0, UINTPTR_MAX - 1)
, exceptionTypes(0, UINTPTR_MAX - 1)
, instances(0, UINTPTR_MAX - 1)
, contexts(0, maxContexts - 1)
, foreigns(0, UINTPTR_MAX - 1)
{
	runtimeData->compartment = this;
}

Runtime::Compartment::~Compartment()
{
	Platform::RWMutex::ExclusiveLock compartmentLock(mutex);

	WAVM_ASSERT(!memories.size());
	WAVM_ASSERT(!tables.size());
	WAVM_ASSERT(!exceptionTypes.size());
	WAVM_ASSERT(!globals.size());
	WAVM_ASSERT(!instances.size());
	WAVM_ASSERT(!contexts.size());
	WAVM_ASSERT(!foreigns.size());

	Platform::freeAlignedVirtualPages(unalignedRuntimeData,
									  compartmentReservedBytes >> Platform::getBytesPerPageLog2(),
									  compartmentRuntimeDataAlignmentLog2);
	Platform::deregisterVirtualAllocation(offsetof(CompartmentRuntimeData, contexts));
}

static CompartmentRuntimeData* initCompartmentRuntimeData(U8*& outUnalignedRuntimeData)
{
	CompartmentRuntimeData* runtimeData
		= (CompartmentRuntimeData*)Platform::allocateAlignedVirtualPages(
			compartmentReservedBytes >> Platform::getBytesPerPageLog2(),
			compartmentRuntimeDataAlignmentLog2,
			outUnalignedRuntimeData);

	WAVM_ERROR_UNLESS(Platform::commitVirtualPages(
		(U8*)runtimeData,
		offsetof(CompartmentRuntimeData, contexts) >> Platform::getBytesPerPageLog2()));
	Platform::registerVirtualAllocation(offsetof(CompartmentRuntimeData, contexts));

	return runtimeData;
}

Compartment* Runtime::createCompartment(std::string&& debugName)
{
	U8* unalignedRuntimeData = nullptr;
	CompartmentRuntimeData* runtimeData = initCompartmentRuntimeData(unalignedRuntimeData);
	if(!runtimeData) { return nullptr; }

	return new Compartment(std::move(debugName), runtimeData, unalignedRuntimeData);
}

Compartment* Runtime::cloneCompartment(const Compartment* compartment, std::string&& debugName)
{
	Compartment* newCompartment;

	U8* unalignedRuntimeData = nullptr;
	CompartmentRuntimeData* runtimeData = initCompartmentRuntimeData(unalignedRuntimeData);
	if(!runtimeData) { return nullptr; }

	{
#ifdef WAVM_HAS_TRACY
		ZoneNamedN(_zone_nc, "new Compartment", true);
#endif
		newCompartment = new Compartment(std::move(debugName), runtimeData, unalignedRuntimeData);
		;
	}
	Runtime::cloneCompartmentInto(*newCompartment, compartment, std::move(debugName));
	return newCompartment;
}

namespace {
	template<class T = Runtime::GCObject*>
	void clearIndexMap(Runtime::Compartment* ownerCompartment,
					   WAVM::IndexMap<WAVM::Uptr, T>& indexMap)
	{
		Platform::RWMutex::ExclusiveLock lock(ownerCompartment->mutex);
		for(T p : indexMap)
		{
			Runtime::GCObject* gp = p;
			if(gp->compartment == ownerCompartment)
			{
				if(gp->numRootReferences.load() != 0)
				{
					throw std::runtime_error("Removed GCObject still has root references alive");
				}
				delete p;
			}
		}
		indexMap = WAVM::IndexMap<WAVM::Uptr, T>(indexMap.getMinIndex(), indexMap.getMaxIndex());
	}

	template<class T = Runtime::GCObject*>
	void clearRemainingIndexMap(Runtime::Compartment* ownerCompartment,
								WAVM::IndexMap<WAVM::Uptr, T>& indexMap,
								const WAVM::HashSet<WAVM::Uptr>& ignoreList,
								std::vector<WAVM::Uptr>& removeList)
	{
		Platform::RWMutex::ExclusiveLock lock(ownerCompartment->mutex);
		removeList.clear();
		for(auto it = indexMap.begin(); it != indexMap.end(); ++it)
		{
			const WAVM::Uptr idx = it.getIndex();
			if(!ignoreList.contains(idx)) { removeList.push_back(idx); }
		}
		for(WAVM::Uptr idx : removeList)
		{
			T p = *indexMap.get(idx);
			Runtime::GCObject* gp = p;
			if(gp->compartment == ownerCompartment)
			{
				if(gp->numRootReferences.load() != 0)
				{
					throw std::runtime_error("Removed GCObject still has root references alive");
				}
				delete p;
			}
			indexMap.removeOrFail(idx);
		}
	}
}

WAVM_API void Runtime::cloneCompartmentInto(Compartment& targetCompartment,
											const Compartment* oldCompartment,
											std::string&& debugName)
{
#ifdef WAVM_HAS_TRACY
	ZoneNamedNS(_zone_root, "Runtime::cloneCompartmentInto", 6, true);
#endif
	Platform::RWMutex::ShareableLock compartmentLock(oldCompartment->mutex);
	Timing::Timer timer;
	WAVM::HashSet<WAVM::Uptr> ignoreIndexList(32);
	std::vector<WAVM::Uptr> removeList;
	removeList.reserve(32);

	// Reset structures
	clearIndexMap(&targetCompartment, targetCompartment.contexts);
	clearIndexMap(&targetCompartment, targetCompartment.foreigns);

	// Clone tables.
	ignoreIndexList.clear();
	for(auto it = oldCompartment->tables.begin(); it != oldCompartment->tables.end(); ++it)
	{
		const WAVM::Uptr idx = it.getIndex();
		ignoreIndexList.addOrFail(idx);
		if(targetCompartment.tables.contains(idx))
		{
			const Table* oldTable = *it;
			Table* newTable = *targetCompartment.tables.get(idx);
			cloneTableInto(newTable, oldTable, &targetCompartment);
		}
		else
		{
			Table* newTable = cloneTable(*it, &targetCompartment);
			WAVM_ASSERT(newTable->id == (*it)->id);
		}
	}
	clearRemainingIndexMap(
		&targetCompartment, targetCompartment.memories, ignoreIndexList, removeList);

	// Clone memories.
	// for(Memory* memory : oldCompartment->memories)
	ignoreIndexList.clear();
	for(auto it = oldCompartment->memories.begin(); it != oldCompartment->memories.end(); ++it)
	{
		const WAVM::Uptr idx = it.getIndex();
		ignoreIndexList.addOrFail(idx);
		if(targetCompartment.memories.contains(idx))
		{
			const Memory* oldMemory = *it;
			Memory* newMemory = *targetCompartment.memories.get(idx);
			cloneMemoryInto(newMemory, oldMemory, &targetCompartment);
		}
		else
		{
			Memory* newMemory = cloneMemory(*it, &targetCompartment);
			WAVM_ASSERT(newMemory->id == (*it)->id);
		}
	}
	clearRemainingIndexMap(
		&targetCompartment, targetCompartment.memories, ignoreIndexList, removeList);

	// Clone globals.
	clearIndexMap(&targetCompartment, targetCompartment.globals);
	{
#ifdef WAVM_HAS_TRACY
		ZoneNamedN(_zone_mcg, "memcpy Globals", true);
#endif
		targetCompartment.globalDataAllocationMask = oldCompartment->globalDataAllocationMask;
		memcpy(targetCompartment.initialContextMutableGlobals,
			   oldCompartment->initialContextMutableGlobals,
			   sizeof(targetCompartment.initialContextMutableGlobals));
	}
	for(Global* global : oldCompartment->globals)
	{
#ifdef WAVM_HAS_TRACY
		ZoneNamedN(_zone_cg, "clone Global", true);
#endif
		Global* newGlobal = cloneGlobal(global, &targetCompartment);
		WAVM_ASSERT(newGlobal->id == global->id);
		WAVM_ASSERT(newGlobal->mutableGlobalIndex == global->mutableGlobalIndex);
	}

	// Clone exception types.
	clearIndexMap(&targetCompartment, targetCompartment.exceptionTypes);
	for(ExceptionType* exceptionType : oldCompartment->exceptionTypes)
	{
#ifdef WAVM_HAS_TRACY
		ZoneNamedN(_zone_cet, "clone ExceptionType", true);
#endif
		ExceptionType* newExceptionType = cloneExceptionType(exceptionType, &targetCompartment);
		WAVM_ASSERT(newExceptionType->id == exceptionType->id);
	}

	// Clone instances.
	clearIndexMap(&targetCompartment, targetCompartment.instances);
	for(Instance* instance : oldCompartment->instances)
	{
#ifdef WAVM_HAS_TRACY
		ZoneNamedN(_zone_ci, "clone Instance", true);
#endif
		Instance* newInstance = cloneInstance(instance, &targetCompartment);
		WAVM_ASSERT(newInstance->id == instance->id);
	}

	Timing::logTimer("Cloned compartment", timer);
}

Object* Runtime::remapToClonedCompartment(const Object* object, const Compartment* newCompartment)
{
	if(!object) { return nullptr; }
	if(object->kind == ObjectKind::function) { return const_cast<Object*>(object); }

	Platform::RWMutex::ShareableLock compartmentLock(newCompartment->mutex);
	switch(object->kind)
	{
	case ObjectKind::table: return newCompartment->tables[asTable(object)->id];
	case ObjectKind::memory: return newCompartment->memories[asMemory(object)->id];
	case ObjectKind::global: return newCompartment->globals[asGlobal(object)->id];
	case ObjectKind::exceptionType:
		return newCompartment->exceptionTypes[asExceptionType(object)->id];
	case ObjectKind::instance: return newCompartment->instances[asInstance(object)->id];

	case ObjectKind::function:
	case ObjectKind::context:
	case ObjectKind::compartment:
	case ObjectKind::foreign:
	case ObjectKind::invalid:
	default: WAVM_UNREACHABLE();
	};
}

Function* Runtime::remapToClonedCompartment(const Function* function,
											const Compartment* newCompartment)
{
	return const_cast<Function*>(function);
}
Table* Runtime::remapToClonedCompartment(const Table* table, const Compartment* newCompartment)
{
	if(!table) { return nullptr; }
	Platform::RWMutex::ShareableLock compartmentLock(newCompartment->mutex);
	return newCompartment->tables[table->id];
}
Memory* Runtime::remapToClonedCompartment(const Memory* memory, const Compartment* newCompartment)
{
	if(!memory) { return nullptr; }
	Platform::RWMutex::ShareableLock compartmentLock(newCompartment->mutex);
	return newCompartment->memories[memory->id];
}
Global* Runtime::remapToClonedCompartment(const Global* global, const Compartment* newCompartment)
{
	if(!global) { return nullptr; }
	Platform::RWMutex::ShareableLock compartmentLock(newCompartment->mutex);
	return newCompartment->globals[global->id];
}
ExceptionType* Runtime::remapToClonedCompartment(const ExceptionType* exceptionType,
												 const Compartment* newCompartment)
{
	if(!exceptionType) { return nullptr; }
	Platform::RWMutex::ShareableLock compartmentLock(newCompartment->mutex);
	return newCompartment->exceptionTypes[exceptionType->id];
}
Instance* Runtime::remapToClonedCompartment(const Instance* instance,
											const Compartment* newCompartment)
{
	if(!instance) { return nullptr; }
	Platform::RWMutex::ShareableLock compartmentLock(newCompartment->mutex);
	return newCompartment->instances[instance->id];
}
Foreign* Runtime::remapToClonedCompartment(const Foreign* foreign,
										   const Compartment* newCompartment)
{
	if(!foreign) { return nullptr; }
	Platform::RWMutex::ShareableLock compartmentLock(newCompartment->mutex);
	return newCompartment->foreigns[foreign->id];
}

bool Runtime::isInCompartment(const Object* object, const Compartment* compartment)
{
	if(object->kind == ObjectKind::function)
	{
		// The function may be in multiple compartments, but if this compartment maps the function's
		// instanceId to a Instance with the LLVMJIT LoadedModule that contains this
		// function, then the function is in this compartment.
		Function* function = (Function*)object;

		// Treat functions with instanceId=UINTPTR_MAX as if they are in all compartments.
		if(function->instanceId == UINTPTR_MAX) { return true; }

		Platform::RWMutex::ShareableLock compartmentLock(compartment->mutex);
		if(!compartment->instances.contains(function->instanceId)) { return false; }
		Instance* instance = compartment->instances[function->instanceId];
		return instance->jitModule.get() == function->mutableData->jitModule;
	}
	else
	{
		GCObject* gcObject = (GCObject*)object;
		return gcObject->compartment == compartment;
	}
}
