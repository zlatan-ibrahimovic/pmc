////////////////////////////////////////////////////////////////////////////////////////////////////
#define MEMORY_BANK_COUNT (64) // Adjust to your architecture
#define LOG2_MEMORY_BANK_COUNT (5) // Set to log2(MEMORY_BANK_COUNT)
#define ELIMINATE_CONFLICTS (0) // Enable for slow address calculation, but zero bank conflicts
////////////////////////////////////////////////////////////////////////////////////////////////////
#if (ELIMINATE_CONFLICTS)
#define MEMORY_BANK_OFFSET(index) ((index) >> LOG2_MEMORY_BANK_COUNT + (index) >> (2*LOG2_MEMORY_BANK_COUNT))
#else
#define MEMORY_BANK_OFFSET(index) ((index) >> LOG2_MEMORY_BANK_COUNT)
#endif
////////////////////////////////////////////////////////////////////////////////////////////////////
uint4 GetAddressMapping(int index) {
const uint local_id = get_local_id(0);
const uint group_id = get_global_id(0) / get_local_size(0);
const uint group_size = get_local_size(0);
uint2 global_index;
global_index.x = index + local_id;
global_index.y = global_index.x + group_size;
uint2 local_index;
local_index.x = local_id;
local_index.y = local_id + group_size;
return (uint4)(global_index.x, global_index.y, local_index.x, local_index.y);
}
void LoadLocalFromGlobal(
__local int *shared_data,
__global const int *input_data,
const uint4 address_pair,
const uint n
) {
const uint global_index_a = address_pair.x;
const uint global_index_b = address_pair.y;
const uint local_index_a = address_pair.z;
const uint local_index_b = address_pair.w;
const uint bank_offset_a = MEMORY_BANK_OFFSET(local_index_a);
const uint bank_offset_b = MEMORY_BANK_OFFSET(local_index_b);
shared_data[local_index_a + bank_offset_a] = input_data[global_index_a];
shared_data[local_index_b + bank_offset_b] = input_data[global_index_b];
}
void LoadLocalFromGlobalNonPowerOfTwo(
__local int *shared_data,
__global const int *input_data,
const uint4 address_pair,
const uint n
) {
const uint global_index_a = address_pair.x;
const uint global_index_b = address_pair.y;
const uint local_index_a = address_pair.z;
const uint local_index_b = address_pair.w;
const uint bank_offset_a = MEMORY_BANK_OFFSET(local_index_a);
const uint bank_offset_b = MEMORY_BANK_OFFSET(local_index_b);
shared_data[local_index_a + bank_offset_a] = input_data[global_index_a];
shared_data[local_index_b + bank_offset_b] = (local_index_b < n) ? input_data[global_index_b] : 0;
barrier(CLK_LOCAL_MEM_FENCE);
}
void StoreLocalToGlobal(
__global int* output_data,
__local const int* shared_data,
const uint4 address_pair,
const uint n
) {
barrier(CLK_LOCAL_MEM_FENCE);
const uint global_index_a = address_pair.x;
const uint global_index_b = address_pair.y;
const uint local_index_a = address_pair.z;
const uint local_index_b = address_pair.w;
const uint bank_offset_a = MEMORY_BANK_OFFSET(local_index_a);
const uint bank_offset_b = MEMORY_BANK_OFFSET(local_index_b);
output_data[global_index_a] = shared_data[local_index_a + bank_offset_a];
output_data[global_index_b] = shared_data[local_index_b + bank_offset_b];
}
void StoreLocalToGlobalNonPowerOfTwo(
__global int* output_data,
__local const int* shared_data,
const uint4 address_pair,
const uint n
) {
barrier(CLK_LOCAL_MEM_FENCE);
const uint global_index_a = address_pair.x;
const uint global_index_b = address_pair.y;
const uint local_index_a = address_pair.z;
const uint local_index_b = address_pair.w;
const uint bank_offset_a = MEMORY_BANK_OFFSET(local_index_a);
const uint bank_offset_b = MEMORY_BANK_OFFSET(local_index_b);
output_data[global_index_a] = shared_data[local_index_a + bank_offset_a];
if(local_index_b < n) output_data[global_index_b] = shared_data[local_index_b + bank_offset_b];
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void ClearLastElement(__local int* shared_data, int group_index) {
const uint local_id = get_local_id(0);
const uint group_id = get_global_id(0) / get_local_size(0);
const uint group_size = get_local_size(0);
if (local_id == 0) {
int index = (group_size << 1) - 1;
index += MEMORY_BANK_OFFSET(index);
shared_data[index] = 0;
}
}
void ClearLastElementStoreSum(__local int* shared_data, __global int *partial_sums, int group_index) {
const uint group_id = get_global_id(0) / get_local_size(0);
const uint group_size = get_local_size(0);
const uint local_id = get_local_id(0);
if (local_id == 0) {
int index = (group_size << 1) - 1;
index += MEMORY_BANK_OFFSET(index);
partial_sums[group_index] = shared_data[index];
shared_data[index] = 0;
}
}
////////////////////////////////////////////////////////////////////////////////////////////////////
uint BuildPartialSum(__local int *shared_data) {
const uint local_id = get_local_id(0);
const uint group_size = get_local_size(0);
const uint two = 2;
uint stride = 1;
for (uint j = group_size; j > 0; j >>= 1) {
barrier(CLK_LOCAL_MEM_FENCE);
if (local_id < j) {
int i = mul24(mul24(two, stride), local_id);
uint local_index_a = i + stride - 1;
uint local_index_b = local_index_a + stride;
local_index_a += MEMORY_BANK_OFFSET(local_index_a);
local_index_b += MEMORY_BANK_OFFSET(local_index_b);
shared_data[local_index_b] += shared_data[local_index_a];
}
stride *= two;
}
return stride;
}
void ScanRootToLeaves(__local int *shared_data, uint stride) {
const uint local_id = get_local_id(0);
const uint group_id = get_global_id(0) / get_local_size(0);
const uint group_size = get_local_size(0);
const uint two = 2;
for (uint j = 1; j <= group_size; j *= two) {
stride >>= 1;
barrier(CLK_LOCAL_MEM_FENCE);
if (local_id < j) {
int i = mul24(mul24(two, stride), local_id);
uint local_index_a = i + stride - 1;
uint local_index_b = local_index_a + stride;
local_index_a += MEMORY_BANK_OFFSET(local_index_a);
local_index_b += MEMORY_BANK_OFFSET(local_index_b);
int t = shared_data[local_index_a];
shared_data[local_index_a] = shared_data[local_index_b];
shared_data[local_index_b] += t;
}
}
}
void PreScanGroup(__local int *shared_data, int group_index) {
const uint group_id = get_global_id(0) / get_local_size(0);
int stride = BuildPartialSum(shared_data);
ClearLastElement(shared_data, (group_index == 0) ? group_id : group_index);
ScanRootToLeaves(shared_data, stride);
}
void PreScanGroupStoreSum(__global int *partial_sums, __local int *shared_data, int group_index) {
const uint group_id = get_global_id(0) / get_local_size(0);
int stride = BuildPartialSum(shared_data);
ClearLastElementStoreSum(shared_data, partial_sums, (group_index == 0) ? group_id : group_index);
ScanRootToLeaves(shared_data, stride);
}

__kernel void prescan(__global const int *A,
		      __global int *B,
		      __local int* shared_data,
		      const uint group_index,
		      const uint base_index,
		      const uint count)
{
	const uint group_id = get_global_id(0) / get_local_size(0);	
	const uint group_size = get_local_size(0);			

	uint local_index = (base_index == 0) ? mul24(group_id, (group_size << 1)) : base_index;
	uint4 address_pair = GetAddressMapping(local_index);

	LoadLocalFromGlobal(shared_data, A, address_pair, count);
	PreScanGroup(shared_data, group_index);
	StoreLocalToGlobal(B, shared_data, address_pair, count);


}

__kernel void PreScanStoreSumKernel(
__global int *output_data,
__global const int *input_data,
__global int *partial_sums,
__local int* shared_data,
const uint group_index,
const uint base_index,
const uint n
) {
const uint group_id = get_global_id(0) / get_local_size(0);
const uint group_size = get_local_size(0);
uint local_index = (base_index == 0) ? mul24(group_id, (group_size << 1)) : base_index;
uint4 address_pair = GetAddressMapping(local_index);
LoadLocalFromGlobal(shared_data, input_data, address_pair, n);
PreScanGroupStoreSum(partial_sums, shared_data, group_index);
StoreLocalToGlobal(output_data, shared_data, address_pair, n);
}
__kernel void PreScanStoreSumNonPowerOfTwoKernel(
__global int *output_data,
__global const int *input_data,
__global int *partial_sums,
__local int* shared_data,
const uint group_index,
const uint base_index,
const uint n
) {
const uint local_id = get_local_id(0);
const uint group_id = get_global_id(0) / get_local_size(0);
const uint group_size = get_local_size(0);
uint local_index = (base_index == 0) ? mul24(group_id, (group_size << 1)) : base_index;
uint4 address_pair = GetAddressMapping(local_index);
LoadLocalFromGlobalNonPowerOfTwo(shared_data, input_data, address_pair, n);
PreScanGroupStoreSum(partial_sums, shared_data, group_index);
StoreLocalToGlobalNonPowerOfTwo(output_data, shared_data, address_pair, n);
}
__kernel void PreScanNonPowerOfTwoKernel(
__global int *output_data,
__global const int *input_data,
__local int* shared_data,
const uint group_index,
const uint base_index,
const uint n
) {
const uint local_id = get_local_id(0);
const uint group_id = get_global_id(0) / get_local_size(0);
const uint group_size = get_local_size(0);
uint local_index = (base_index == 0) ? mul24(group_id, (group_size << 1)) : base_index;
uint4 address_pair = GetAddressMapping(local_index);
LoadLocalFromGlobalNonPowerOfTwo(shared_data, input_data, address_pair, n);
PreScanGroup(shared_data, group_index);
StoreLocalToGlobalNonPowerOfTwo(output_data, shared_data, address_pair, n);
}
////////////////////////////////////////////////////////////////////////////////////////////////////
__kernel void UniformAddKernel(
__global int *output_data,
__global int *input_data,
__local int *shared_data,
const uint group_offset,
const uint base_index,
const uint n
) {
const uint local_id = get_local_id(0);
const uint group_id = get_global_id(0) / get_local_size(0);
const uint group_size = get_local_size(0);
if (local_id == 0) shared_data[0] = input_data[group_id + group_offset];
barrier(CLK_LOCAL_MEM_FENCE);
uint address = mul24(group_id, (group_size << 1)) + base_index + local_id;
output_data[address] += shared_data[0];
if( (local_id + group_size) < n) output_data[address + group_size] += shared_data[0];
}
