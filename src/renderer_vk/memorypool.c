#include "memorypool.h"


void Pool_Clear(memoryPool* pool) {
	pool->firstFree = 1;
	for (int i = 0; i < pool->itemCount + 1; i++) {
		pool->lookupData[i].inUse = 0;
		pool->lookupData[i].nextFreeIndex = i < (pool->itemCount - 1) ? i + 1 : 0;
	}
}

void Pool_ClearUnused(memoryPool *pool){
	uint32_t nextFree = 0;
	for(int i = pool->itemCount - 1; i > 0; i--){
		if(!pool->lookupData[i].inUse){
			pool->lookupData[i].nextFreeIndex = nextFree;
			nextFree = i;
		}
	}
	pool->firstFree = nextFree;
}

void Pool_Init(memoryPool *pool, const uint32_t itemCount, const uint32_t typeSize, uint16_t type){
	pool->poolType = type;
	pool->typeSize = typeSize;
	pool->itemCount = itemCount;
	if(pool->poolData == NULL){
		assert(pool->lookupData == NULL);
		pool->poolData = (uint8_t*)malloc((itemCount + 1) * typeSize);
		pool->lookupData = (memoryPoolItem*)malloc((itemCount + 1) * sizeof(memoryPoolItem));
		//@TODO: ri.fatal error if malloc returned null
		for (int i = 0; i < pool->itemCount + 1; i++) {
			pool->lookupData[i].generation = 0;
		}
	}
	Pool_Clear(pool);
	
}

uint64_t ComposeHandle(uint32_t index, uint16_t generation,uint16_t type){
	return ((uint64_t)index << 32) | ((uint64_t)generation << 16) | (uint64_t)type;
}


DecomposedHandle DecomposeHandle(uint64_t handle){
	DecomposedHandle item;
	item.index = (handle >> 32) & 0xFFFFFFFF;
	item.generation = (handle >> 16) & 0xFFFF;
	item.type = (handle) & 0xFFFF;
	return item;
}

uint32_t RHI_GetIndexFromHandle(uint64_t handle){
	return (uint32_t)((handle >> 32) & 0xFFFFFFFF);
}

uint64_t Pool_Add(memoryPool *pool, void *rawItem){
	uint32_t freeSpot = pool->firstFree;
	
	if(freeSpot == 0){
		ri.Error(ERR_FATAL, "(Pool_Add)Ran out of pool memory (%u items of %u bytes each)\n", pool->itemCount, pool->typeSize);	
	}	
	assert(pool->lookupData[freeSpot].inUse == qfalse);

	uint8_t *poolLocation = pool->poolData + (freeSpot * pool->typeSize);
	memcpy(poolLocation, rawItem, pool->typeSize);
	
	pool->lookupData[freeSpot].inUse = qtrue;
	pool->firstFree = pool->lookupData[freeSpot].nextFreeIndex;
	
	return ComposeHandle(freeSpot, pool->lookupData[freeSpot].generation, pool->poolType);
}

uint64_t Pool_Size(memoryPool *pool){
	int64_t inUseCount = 0;
	for(int i = 0; i < pool->itemCount; i++){
		if(pool->lookupData[i].inUse){
			inUseCount++;
		}
	}
	return inUseCount;
}

static void HandleChecks(DecomposedHandle item, memoryPool* pool, const char* operation){
	

	if(item.type != pool->poolType){
		ri.Error(ERR_FATAL, "(%s)Wrong pool type\n", operation);
	}

	if(item.index > pool->itemCount){
		ri.Error(ERR_FATAL, "(%s)Invalid index into pool\n", operation);
	}

	if(item.index == 0 ){
		ri.Error(ERR_FATAL, "(%s)Pool index zero is reserved\n", operation);
	}

	if(item.generation < pool->lookupData[item.index].generation){
		ri.Error(ERR_FATAL, "(%s)Invalid handle generation (too old)\n", operation);
	}

	if(item.generation > pool->lookupData[item.index].generation){
		ri.Error(ERR_FATAL, "(%s)Invalid handle generation (too new)\n", operation);
	}

	if (pool->lookupData[item.index].inUse == qfalse) {
		ri.Error(ERR_FATAL, "(%s)Invalid handle index (not in use)\n", operation);
	}
}
extern qbool crashing;
void Pool_Remove(memoryPool *pool, uint64_t handle){
	DecomposedHandle item = DecomposeHandle(handle);
	HandleChecks(item, pool, "Pool_Remove");
	if(0 && crashing){
		pool->lookupData[item.index].inUse = 0;
		if(pool->typeSize == 72){
			Sys_DebugPrintf("Index %d\n", (int)item.index);
		}
		
		//pool->lookupData[item.index].generation++;
	
	}else{
		pool->lookupData[item.index].nextFreeIndex = pool->firstFree;
		pool->lookupData[item.index].inUse = 0;
		pool->lookupData[item.index].generation++;
		pool->firstFree = item.index;
	}
}

void* Pool_Get(memoryPool *pool, uint64_t handle){
	DecomposedHandle item = DecomposeHandle(handle);
	HandleChecks(item, pool, "Pool_Get");

	uint32_t itemLocation = item.index * pool->typeSize;
	return pool->poolData+itemLocation;
}

PoolIterator Pool_BeginIteration(void){
	return (PoolIterator) { 0, NULL, 0 };
}

qboolean Pool_Iterate(memoryPool *pool, PoolIterator *it){
	for(it->index++; it->index < pool->itemCount + 1; it->index++){
		assert(it->index >= 1 && it->index <= pool->itemCount);

		if(pool->lookupData[it->index].inUse){
			uint32_t itemLocation = it->index * pool->typeSize;
			it->value = pool->poolData + itemLocation;
			assert((uint8_t*)it->value > pool->poolData);
			assert((uint8_t*)it->value <= pool->poolData + (pool->itemCount * pool->typeSize));
			it->handle = ComposeHandle(it->index, pool->lookupData[it->index].generation, pool->poolType);
			uint8_t* pool_item = (uint8_t*)Pool_Get(pool, it->handle);
			assert(pool_item <= pool->poolData + (pool->itemCount * pool->typeSize) && pool_item  > pool->poolData);
			return qtrue;
		}
	}
	return qfalse;
}