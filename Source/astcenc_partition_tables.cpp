// SPDX-License-Identifier: Apache-2.0
// ----------------------------------------------------------------------------
// Copyright 2011-2022 Arm Limited
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy
// of the License at:
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations
// under the License.
// ----------------------------------------------------------------------------

/**
 * @brief Functions for generating partition tables on demand.
 */

#include "astcenc_internal.h"

/**
 * @brief Generate a canonical representation of a partition pattern.
 *
 * The returned value stores two bits per texel, for up to 6x6x6 texels, where the two bits store
 * the remapped texel index. Remapping ensures that we only match on the partition pattern,
 * independent of the partition order generated by the hash.
 *
 * @param      texel_count           The number of texels in the block.
 * @param      partition_of_texel    The partition assignments, in hash order.
 * @param[out] bit_pattern           The output bit pattern representation.
 */
static void generate_canonical_partitioning(
	unsigned int texel_count,
	const uint8_t* partition_of_texel,
	uint64_t bit_pattern[7]
) {
	// Clear the pattern
	for (unsigned int i = 0; i < 7; i++)
	{
		bit_pattern[i] = 0;
	}

	// Store a mapping to reorder the raw partitions so that the the partitions are ordered such
	// that the lowest texel index in partition N is smaller than the lowest texel index in
	// partition N + 1.
	int mapped_index[BLOCK_MAX_PARTITIONS];
	int map_weight_count = 0;

	for (unsigned int i = 0; i < BLOCK_MAX_PARTITIONS; i++)
	{
		mapped_index[i] = -1;
	}

	for (unsigned int i = 0; i < texel_count; i++)
	{
		int index = partition_of_texel[i];
		if (mapped_index[index] < 0)
		{
			mapped_index[index] = map_weight_count++;
		}

		uint64_t xlat_index = mapped_index[index];
		bit_pattern[i >> 5] |= xlat_index << (2 * (i & 0x1F));
	}
}

/**
 * @brief Compare two canonical patterns to see if they are the same.
 *
 * @param part1   The first canonical bit pattern to check.
 * @param part2   The second canonical bit pattern to check.
 *
 * @return @c true if the patterns are the same, @c false otherwise.
 */
static bool compare_canonical_partitionings(
	const uint64_t part1[7],
	const uint64_t part2[7]
) {
	return (part1[0] == part2[0]) && (part1[1] == part2[1]) &&
	       (part1[2] == part2[2]) && (part1[3] == part2[3]) &&
	       (part1[4] == part2[4]) && (part1[5] == part2[5]) &&
	       (part1[6] == part2[6]);
}

/**
 * @brief Hash function used for procedural partition assignment.
 *
 * @param inp The hash seed.
 *
 * @return The hashed value.
 */
static uint32_t hash52(
	uint32_t inp
) {
	inp ^= inp >> 15;

	// (2^4 + 1) * (2^7 + 1) * (2^17 - 1)
	inp *= 0xEEDE0891;
	inp ^= inp >> 5;
	inp += inp << 16;
	inp ^= inp >> 7;
	inp ^= inp >> 3;
	inp ^= inp << 6;
	inp ^= inp >> 17;
	return inp;
}

/**
 * @brief Select texel assignment for a single coordinate.
 *
 * @param seed              The seed - the partition index from the block.
 * @param x                 The texel X coordinate in the block.
 * @param y                 The texel Y coordinate in the block.
 * @param z                 The texel Z coordinate in the block.
 * @param partition_count   The total partition count of this encoding.
 * @param small_block       @c true if the blockhas fewer than 32 texels.
 *
 * @return The assigned partition index for this texel.
 */
static uint8_t select_partition(
	int seed,
	int x,
	int y,
	int z,
	int partition_count,
	bool small_block
) {
	// For small blocks bias the coordinates to get better distribution
	if (small_block)
	{
		x <<= 1;
		y <<= 1;
		z <<= 1;
	}

	seed += (partition_count - 1) * 1024;

	uint32_t rnum = hash52(seed);

	uint8_t seed1 = rnum & 0xF;
	uint8_t seed2 = (rnum >> 4) & 0xF;
	uint8_t seed3 = (rnum >> 8) & 0xF;
	uint8_t seed4 = (rnum >> 12) & 0xF;
	uint8_t seed5 = (rnum >> 16) & 0xF;
	uint8_t seed6 = (rnum >> 20) & 0xF;
	uint8_t seed7 = (rnum >> 24) & 0xF;
	uint8_t seed8 = (rnum >> 28) & 0xF;
	uint8_t seed9 = (rnum >> 18) & 0xF;
	uint8_t seed10 = (rnum >> 22) & 0xF;
	uint8_t seed11 = (rnum >> 26) & 0xF;
	uint8_t seed12 = ((rnum >> 30) | (rnum << 2)) & 0xF;

	// Squaring all the seeds in order to bias their distribution towards lower values.
	seed1 *= seed1;
	seed2 *= seed2;
	seed3 *= seed3;
	seed4 *= seed4;
	seed5 *= seed5;
	seed6 *= seed6;
	seed7 *= seed7;
	seed8 *= seed8;
	seed9 *= seed9;
	seed10 *= seed10;
	seed11 *= seed11;
	seed12 *= seed12;

	int sh1, sh2;
	if (seed & 1)
	{
		sh1 = (seed & 2 ? 4 : 5);
		sh2 = (partition_count == 3 ? 6 : 5);
	}
	else
	{
		sh1 = (partition_count == 3 ? 6 : 5);
		sh2 = (seed & 2 ? 4 : 5);
	}

	int sh3 = (seed & 0x10) ? sh1 : sh2;

	seed1 >>= sh1;
	seed2 >>= sh2;
	seed3 >>= sh1;
	seed4 >>= sh2;
	seed5 >>= sh1;
	seed6 >>= sh2;
	seed7 >>= sh1;
	seed8 >>= sh2;

	seed9 >>= sh3;
	seed10 >>= sh3;
	seed11 >>= sh3;
	seed12 >>= sh3;

	int a = seed1 * x + seed2 * y + seed11 * z + (rnum >> 14);
	int b = seed3 * x + seed4 * y + seed12 * z + (rnum >> 10);
	int c = seed5 * x + seed6 * y + seed9 * z + (rnum >> 6);
	int d = seed7 * x + seed8 * y + seed10 * z + (rnum >> 2);

	// Apply the saw
	a &= 0x3F;
	b &= 0x3F;
	c &= 0x3F;
	d &= 0x3F;

	// Remove some of the components if we are to output < 4 partitions.
	if (partition_count <= 3)
	{
		d = 0;
	}

	if (partition_count <= 2)
	{
		c = 0;
	}

	if (partition_count <= 1)
	{
		b = 0;
	}

	uint8_t partition;
	if (a >= b && a >= c && a >= d)
	{
		partition = 0;
	}
	else if (b >= c && b >= d)
	{
		partition = 1;
	}
	else if (c >= d)
	{
		partition = 2;
	}
	else
	{
		partition = 3;
	}

	return partition;
}

/**
 * @brief Generate a single partition info structure.
 *
 * @param[out] bsd                     The block size information.
 * @param      partition_count         The partition count of this partitioning.
 * @param      partition_index         The partition index / seed of this partitioning.
 * @param      partition_remap_index   The remapped partition index of this partitioning.
 * @param[out] pi                      The partition info structure to populate.
 *
 * @return True if this is a useful partition index, False if we can skip it.
 */
static bool generate_one_partition_info_entry(
	block_size_descriptor& bsd,
	unsigned int partition_count,
	unsigned int partition_index,
	unsigned int partition_remap_index,
	partition_info& pi
) {
	int texels_per_block = bsd.texel_count;
	bool small_block = texels_per_block < 32;

	uint8_t *partition_of_texel = pi.partition_of_texel;

	// Assign texels to partitions
	int texel_idx = 0;
	int counts[BLOCK_MAX_PARTITIONS] { 0 };
	for (unsigned int z = 0; z < bsd.zdim; z++)
	{
		for (unsigned int y = 0; y <  bsd.ydim; y++)
		{
			for (unsigned int x = 0; x <  bsd.xdim; x++)
			{
				uint8_t part = select_partition(partition_index, x, y, z, partition_count, small_block);
				pi.texels_of_partition[part][counts[part]++] = static_cast<uint8_t>(texel_idx++);
				*partition_of_texel++ = part;
			}
		}
	}

	// Fill loop tail so we can overfetch later
	for (unsigned int i = 0; i < partition_count; i++)
	{
		int ptex_count = counts[i];
		int ptex_count_simd = round_up_to_simd_multiple_vla(ptex_count);
		for (int j = ptex_count; j < ptex_count_simd; j++)
		{
			pi.texels_of_partition[i][j] = pi.texels_of_partition[i][ptex_count - 1];
		}
	}

	// Populate the actual procedural partition count
	if (counts[0] == 0)
	{
		pi.partition_count = 0;
	}
	else if (counts[1] == 0)
	{
		pi.partition_count = 1;
	}
	else if (counts[2] == 0)
	{
		pi.partition_count = 2;
	}
	else if (counts[3] == 0)
	{
		pi.partition_count = 3;
	}
	else
	{
		pi.partition_count = 4;
	}

	// Populate the partition index
	pi.partition_index = partition_index;

	// Populate the coverage bitmaps for 2/3/4 partitions
	uint64_t* bitmaps { nullptr };
	uint8_t* valids { nullptr };
	if (partition_count == 2)
	{
		bitmaps = bsd.coverage_bitmaps_2[partition_remap_index];
		valids = bsd.partitioning_valid_2;
	}
	else if (partition_count == 3)
	{
		bitmaps = bsd.coverage_bitmaps_3[partition_remap_index];
		valids = bsd.partitioning_valid_3;
	}
	else if (partition_count == 4)
	{
		bitmaps = bsd.coverage_bitmaps_4[partition_remap_index];
		valids = bsd.partitioning_valid_4;
	}

	for (unsigned int i = 0; i < BLOCK_MAX_PARTITIONS; i++)
	{
		pi.partition_texel_count[i] = static_cast<uint8_t>(counts[i]);
	}

	// Valid partitionings have texels in all of the requested partitions
	bool valid = pi.partition_count == partition_count;

	if (bitmaps)
	{
		// Populate the bitmap validity mask
		valids[partition_remap_index] = valid ? 0 : 255;

		for (unsigned int i = 0; i < partition_count; i++)
		{
			bitmaps[i] = 0ULL;
		}

		unsigned int texels_to_process = astc::min(bsd.texel_count, BLOCK_MAX_KMEANS_TEXELS);
		for (unsigned int i = 0; i < texels_to_process; i++)
		{
			unsigned int idx = bsd.kmeans_texels[i];
			bitmaps[pi.partition_of_texel[idx]] |= 1ULL << i;
		}
	}

	return valid;
}

static void build_partition_table_for_one_partition_count(
	block_size_descriptor& bsd,
	bool can_omit_partitionings,
	unsigned int partition_count_cutoff,
	unsigned int partition_count,
	partition_info* ptab,
	uint64_t* canonical_patterns
) {
	uint8_t* partitioning_valid[3] {
		bsd.partitioning_valid_2,
		bsd.partitioning_valid_3,
		bsd.partitioning_valid_4
	};

	unsigned int next_index = 0;
	bsd.partitioning_count_selected[partition_count - 1] = 0;
	bsd.partitioning_count_all[partition_count - 1] = 0;

	// Skip tables larger than config max partition count if we can omit modes
	if (can_omit_partitionings && (partition_count > partition_count_cutoff))
	{
		return;
	}

	// Iterate through twice
	//   - Pass 0: Keep selected partitionings
	//   - Pass 1: Keep non-selected partitionings (skip if in omit mode)
	unsigned int max_iter = can_omit_partitionings ? 1 : 2;

	// Tracker for things we built in the first iteration
	uint8_t build[BLOCK_MAX_PARTITIONINGS] { 0 };
		for (unsigned int x = 0; x < max_iter; x++)
	{
		for (unsigned int i = 0; i < BLOCK_MAX_PARTITIONINGS; i++)
		{
			// Don't include things we built in the first pass
			if ((x == 1) && build[i])
			{
				continue;
			}

			bool keep_useful = generate_one_partition_info_entry(bsd, partition_count, i, next_index, ptab[next_index]);
			if ((x == 0) && !keep_useful)
			{
				continue;
			}

			generate_canonical_partitioning(bsd.texel_count, ptab[next_index].partition_of_texel, canonical_patterns + next_index * 7);
			bool keep_canonical = true;
			for (unsigned int j = 0; j < next_index; j++)
			{
				bool match = compare_canonical_partitionings(canonical_patterns + 7 * next_index, canonical_patterns + 7 * j);
				if (match)
				{
					keep_canonical = false;
					break;
				}
			}

			if (keep_useful && keep_canonical)
			{
				if (x == 0)
				{
					bsd.partitioning_packed_index[partition_count - 2][i] = next_index;
					bsd.partitioning_count_selected[partition_count - 1]++;
					bsd.partitioning_count_all[partition_count - 1]++;
					build[i] = 1;
					next_index++;
				}
			}
			else
			{
				if (x == 1)
				{
					bsd.partitioning_packed_index[partition_count - 2][i] = next_index;
					bsd.partitioning_count_all[partition_count - 1]++;
					partitioning_valid[partition_count - 2][next_index] = 255;
					next_index++;
				}
			}
		}
	}
}

/* See header for documentation. */
void init_partition_tables(
	block_size_descriptor& bsd,
	bool can_omit_partitionings,
	unsigned int partition_count_cutoff
) {
	partition_info* par_tab2 = bsd.partitionings;
	partition_info* par_tab3 = par_tab2 + BLOCK_MAX_PARTITIONINGS;
	partition_info* par_tab4 = par_tab3 + BLOCK_MAX_PARTITIONINGS;
	partition_info* par_tab1 = par_tab4 + BLOCK_MAX_PARTITIONINGS;

	generate_one_partition_info_entry(bsd, 1, 0, 0, *par_tab1);
	bsd.partitioning_count_selected[0] = 1;
	bsd.partitioning_count_all[0] = 1;

	uint64_t* canonical_patterns = new uint64_t[BLOCK_MAX_PARTITIONINGS * 7];
	build_partition_table_for_one_partition_count(bsd, can_omit_partitionings, partition_count_cutoff, 2, par_tab2, canonical_patterns);
	build_partition_table_for_one_partition_count(bsd, can_omit_partitionings, partition_count_cutoff, 3, par_tab3, canonical_patterns);
	build_partition_table_for_one_partition_count(bsd, can_omit_partitionings, partition_count_cutoff, 4, par_tab4, canonical_patterns);

	delete[] canonical_patterns;
}
