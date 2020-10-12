#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "log.h"

#include "wfc.h"


// bytes needed to create a bitmap with one bit per pattern
#define WFC_BITMAP_BYTES_NEEDED(num_patterns) ((num_patterns / 8UL) + ((num_patterns % 8) != 0))

// number of bytes needed for one bitmap per adjacency type
#define WFC_PATTERN_BYTES_NEEDED(num_patterns) (WFC_BITMAP_BYTES_NEEDED(num_patterns) * WFC_NUM_ADJACENT)

// length of the index (number of patterns times bitmap length for each pattern)
#define WFC_INDEX_LENGTH_BYTES(num_patterns) (num_patterns * WFC_PATTERN_BYTES_NEEDED(num_patterns))

#define WFC_PATTERN_INDEX(num_patterns, pattern) (WFC_PATTERN_BYTES_NEEDED(num_patterns) * pattern)
#define WFC_ADJACENT_INDEX(num_patterns, adjacent) (WFC_BITMAP_BYTES_NEEDED(num_patterns) * adjacent)


const WFC_Pos gv_adjacent_offsets[WFC_NUM_ADJACENT] =
    { { -1, -1 }
    , { -1, 0 }
    , { -1, 1 }
    , {  0, 1 }
    , {  1, 1 }
    , {  1, 0 }
    , {  1, -1 }
    , {  0, -1 }
    };

const WFC_Pos gv_pattern_offsets[WFC_PATTERN_LEN] =
    { { 0, 0 }
    , { 1, 0 }
    , { 0, 1 }
    , { 1, 1 }
    };


// check if 'tile' overlaps with 'other_tile', if 'other_tile' is offset by 'adjacency'.
static bool WFC_TilesOverlap(WFC_Tile tile, WFC_Tile other_tile, WFC_Pos adjacency);

// helper functions to check tile overlaps
static WFC_Tile WFC_MaskTile(WFC_Tile tile, WFC_Pos adjacency);
static WFC_Tile WFC_ShiftTile(WFC_Tile tile, WFC_Pos adjacency);

// get a pointer to the output array's pattern bitmap for a particular pixel
static uint8_t *WFC_GetOutputBitmap(WFC_State *state, WFC_Pos pos);


WFC_RESULT_ENUM WFC_StateInit(WFC_State *state,
                              uint32_t input_width,
                              uint32_t input_height,
                              const uint8_t *input
                              uint32_t output_width,
                              uint32_t output_height) {
    WFC_RESULT_ENUM result = WFC_RESULT_OKAY;

    if ((NULL == state) || (NULL == input)) {
        result = WFC_RESULT_ERROR;
    }

    if (WFC_RESULT_OKAY == result) {
        log_trace("WFC checking input");
        // check that input does not contain values >= 16
        for (uint32_t input_index = 0; input_index < input_width * input_height; input_index++) {
            if ((input[input_index] & (~WFC_CELL_MASK)) != 0) {
                result = WFC_RESULT_ERROR;
                break;
            }
        }
    }

    if (WFC_RESULT_OKAY == result) {
        memset(state, 0, sizeof(*state));

        state->rng = 7;

        log_trace("WFC initializing state");
        // copy input buffer to ensure we can clean up at the end
        uint32_t input_size_bytes = input_width * input_height;

        // check arithmatic overflow
        assert(input_height == (input_size_bytes / input_width));

        uint8_t *input_copy = (uint8_t*)malloc(input_size_bytes);

        if (NULL == input_copy) {
            result = WFC_RESULT_ERROR;
        } else {
            memcpy(input_copy, input, input_size_bytes);
            state->input = input_copy;
            state->input_width = input_width;
            state->input_height = input_height;
        }
    }

    if (result == WFC_RESULT_OKAY) {
        state->queue.num_items = 0;
        state->queue.max_items = output_width * output_height;
        state->queue.items = (WFC_Pos*)(calloc(1, state->queue.max_items * sizeof(WFC_Pos)));

        if (NULL == state->queue.items) {
            result = WFC_RESULT_ERROR;
        }
    }

    if (WFC_RESULT_OKAY == result) {
        log_trace("WFC finding patterns");
        // collect patterns from input into a table
        result = WFC_FindPatterns(state);
    }

    if (WFC_RESULT_OKAY == result) {
        log_trace("WFC allocating index");
        // create the index (table of adjacent patterns for each pattern)
        uint8_t *index = (uint8_t*)calloc(1, WFC_INDEX_LENGTH_BYTES(state->propagator.num_patterns));

        if (NULL == index) {
            result = WFC_RESULT_ERROR;
        } else {
            state->propagator.index = index;
        }
    }

    if (WFC_RESULT_OKAY == result) {
        log_trace("WFC initializing index");
        // fill the index with the discovered patterns and their adjacency information
        result = WFC_IndexInit(state);
    }

    if (WFC_RESULT_OKAY == result) {
        log_trace("WFC setting up output map");
        state->propagator.bitmap_len =
            (state->propagator.num_patterns / 8) + 
            ((state->propagator.num_patterns % 8) != 0);
        uint32_t num_pixels = state->input_width * state->input_height;
        log_trace("Output bitmap length %d", state->propagator.bitmap_len);

        // allocate a bitmap for each pixel
        state->output = (uint8_t*)calloc(state->propagator.bitmap_len, num_pixels);
        assert(NULL != state->output);

        // initial each bitmap to all 1, indicating that all patterns are valid
        // NOTE this could be done by setting 0xFF in each byte. The current approach at
        // least only sets bits that are actually used.
        for (uint32_t pix_index = 0; pix_index < num_pixels; pix_index++) {
            for (uint32_t pat_index = 0; pat_index < state->propagator.num_patterns; pat_index++) {
                uint32_t bitmap_offset = pix_index * (state->propagator.bitmap_len);
                state->output[bitmap_offset + (pat_index / 8)] |= 1 << (pat_index % 8);
            }
        }
    }

    // TODO we should probably call WFC_StateDestroy on error to clean up
    // any allocated memory from a partially constructed state.

    return result;
}

void WFC_StateDestroy(WFC_State *state) {
    if (NULL != state) {
          if (NULL != state->input) {
              free(state->input);
          }

          if (NULL != state->output) {
              free(state->output);
          }

          if (NULL != state->propagator.patterns) {
              free(state->propagator.patterns);
          }

          if (NULL != state->propagator.index) {
              free(state->propagator.index);
          }

          // clear memory so its pointers are no longer available for use
          memset(state, 0, sizeof(*state));
     }
}

void WFC_PrintTile(WFC_Tile tile) {
    printf("\t\t");
    printf("%1X", (tile & 0xF000) >> 12);
    printf("%1X", (tile & 0x0F00) >> 8);
    printf("\n");
    printf("\t\t");
    printf("%1X", (tile & 0x00F0) >> 4);
    printf("%1X", (tile & 0x000F) >> 0);
    printf("\n");
}

void WFC_PrintState(WFC_State *state) {
    printf("WFC_State: \n");
    printf("\tinput:\n");
    for (uint32_t y = 0; y < state->input_height; y++) {
        printf("\t\t");
        for (uint32_t x = 0; x < state->input_width; x++) {
            printf("%1X", state->input[x + y * state->input_width]);
        }
        printf("\n");
    }

    printf("\tpatterns (%d):\n", state->propagator.num_patterns);
    for (uint32_t pattern_index = 0; pattern_index < state->propagator.num_patterns; pattern_index++) {
        WFC_Pattern pattern = state->propagator.patterns[pattern_index];
        printf("\t\tindex %d (count %d)\n", pattern.index, pattern.count);
        WFC_PrintTile(pattern.tile);
    }
    printf("\n");
}

uint8_t *WFC_GetOutputBitmap(WFC_State *state, WFC_Pos pos) {
    uint32_t pixel_index = pos.x + pos.y * state->output_width;
    uint32_t output_index =
         pixel_index * WFC_BITMAP_BYTES_NEEDED(state->propagator.num_patterns);

    return &state->output[output_index];
}

/** Offset a given position by a given offset, wrapping around a grid of a given
 * width and height.
 */
WFC_Pos WFC_OffsetFrom(WFC_Pos pos, WFC_Pos offset, uint32_t width, uint32_t height) {
    WFC_Pos loc = pos;

    loc.x = loc.x + offset.x;
    if (loc.x < 0) {
        loc.x = width + loc.x;
    }
    loc.x %= width;

    loc.y = loc.y + offset.y;
    if (loc.y < 0) {
        loc.y = height + loc.y;
    }
    loc.y %= height;

    return loc;
}

#if defined(WFC_TEST)
bool WFC_PosEqual(WFC_Pos first, WFC_Pos second) {
    return (first.x == second.x) && (first.y == second.y);
}

void WFC_TestOffsetFrom(void) {
    WFC_Pos pos = { .x = 0, .y = 0};
    WFC_Pos answer;

    WFC_Pos offset;
    offset.x = 1;
    offset.y = 1;

    answer = WFC_OffsetFrom(pos, offset, 10, 10);
    assert(WFC_PosEqual((WFC_Pos){ .x = 1, .y = 1 }, answer));

    offset.x = 1;
    offset.y = -1;
    answer = WFC_OffsetFrom(pos, offset, 10, 10);
    assert(WFC_PosEqual((WFC_Pos){ .x = 1, .y = 9 }, answer));
}
#endif

/** Get the WFC_Tile from a given offset. This is a 2x2 pattern
 * encoded into an integer.
 */
WFC_Tile WFC_TileAt(WFC_Pos pos, uint32_t width, uint32_t height, uint8_t *input) {
    assert(NULL != input);

    WFC_Tile tile = 0;

    for (uint32_t offset_index = 0; offset_index < WFC_PATTERN_LEN; offset_index++) {
        WFC_Pos offset = gv_pattern_offsets[offset_index];

        WFC_Pos loc = WFC_OffsetFrom(pos, offset, width, height);

        tile = tile << WFC_CELL_NUM_BITS;

        tile |= input[loc.x + loc.y * width];
    }

    return tile;
}

WFC_RESULT_ENUM WFC_FindPatterns(WFC_State *state) {
    WFC_RESULT_ENUM result = WFC_RESULT_OKAY; 

    assert(NULL != state);

    for (uint32_t y = 0; y < state->input_height; y++) {
        for (uint32_t x = 0; x < state->input_width; x++) {
            WFC_Pos pos = { x, y };

            // get the tile at the current location
            WFC_Pattern pattern = {0};
            pattern.tile = WFC_TileAt(pos, state->input_width, state->input_height, state->input);

            // check if the pattern is already defined
            // NOTE a hash table or set structure may be faster then this linear search
            bool pattern_found = false;
            for (uint32_t pattern_index = 0; pattern_index < state->propagator.num_patterns; pattern_index++) {
                if (memcmp(&state->propagator.patterns[pattern_index].tile, &pattern.tile, sizeof(WFC_Tile)) == 0) {
                    pattern_found = true;
                    pattern.index = pattern_index;
                    break;
                }
            }

            // if not defined, add to the propagator table
            if (!pattern_found) {
                // if we have not yet allocated the patterns table, allocate it now.
                if (state->propagator.patterns == NULL) {
                    state->propagator.max_patterns = 1;
                    state->propagator.patterns = (WFC_Pattern*)calloc(1, sizeof(WFC_Pattern));
                    assert(NULL != state->propagator.patterns);
                }

                // check that we have space for one more pattern. If not, realloc
                if ((state->propagator.num_patterns + 1) == state->propagator.max_patterns) {
                    uint32_t new_size = state->propagator.max_patterns * sizeof(WFC_Pattern) * 2;

                    state->propagator.patterns =
                        realloc(state->propagator.patterns, new_size);
                    assert(NULL != state->propagator.patterns);

                    state->propagator.max_patterns *= 2;
                }

                pattern.count = 1;
                pattern.index = state->propagator.num_patterns;
                state->propagator.patterns[state->propagator.num_patterns] = pattern;
                state->propagator.num_patterns++;
            } else {
                // found another occurrance, so bump the count
                state->propagator.patterns[pattern.index].count++;
            }
        }
    }

    return result;
}

WFC_RESULT_ENUM WFC_IndexInit(WFC_State *state) {
    WFC_RESULT_ENUM result = WFC_RESULT_OKAY; 

    assert(NULL != state);

    const uint32_t num_patterns = state->propagator.num_patterns;

    //   NOTE could do triangular matrix and mark opposite adjacencies as you go
    for (uint32_t pat_index = 0; pat_index < num_patterns; pat_index++) {
        WFC_Tile tile = state->propagator.patterns[pat_index].tile;

        uint32_t pattern_bitmap_offset = pat_index * WFC_PATTERN_BYTES_NEEDED(num_patterns);

        for (uint8_t adj_index = 0; adj_index < WFC_NUM_ADJACENT; adj_index++) {
            uint32_t bitmap_offset =
                pattern_bitmap_offset + adj_index * WFC_BITMAP_BYTES_NEEDED(num_patterns);

            for (uint32_t other_pat_index = 0; other_pat_index < num_patterns; other_pat_index++) {
                WFC_Tile other_tile = state->propagator.patterns[other_pat_index].tile;

                // if the tiles overlap with the given adjacency, mark the bit
                if (WFC_TilesOverlap(tile, other_tile, gv_adjacent_offsets[adj_index])) {
                    state->propagator.index[bitmap_offset + (other_pat_index / 8)] |=
                        1 << (other_pat_index % 8);
                }
            }
        }
    }

    return result;
}

WFC_Tile WFC_MaskTile(WFC_Tile tile, WFC_Pos adjacency) {
    uint16_t tile_part = tile;

    // TODO(perf) consider something like
    //
    // initiailze to no mask (keep all bits)
    // uint16_t x_mask = 0xFFFF;
    // flip mask if negative, and set to 0 if x == 0
    // x_mask &= (0x0F0F ^ (0xFFFF * (adjacency.x < 0))) * (adjacency.x != 0);
    //
    // uint16_t y_mask = 0xFFFF;
    // y_mask = (0x00FF ^ (0xFFFF * (adjacency.y < 0))) * (adjacency.x != 0);
    // return tile & x_mask & y_mask;
    if (adjacency.x == 1) {
        tile_part &= 0x0F0F;
    } else if (adjacency.x == -1) {
        tile_part &= 0xF0F0;
    }

    if (adjacency.y == 1) {
        tile_part &= 0x00FF;
    } else if (adjacency.y == -1) {
        tile_part &= 0xFF00;
    }

    return tile_part;
}

WFC_Tile WFC_ShiftTile(WFC_Tile tile, WFC_Pos adjacency) {
    uint16_t tile_part = tile;

    if (adjacency.x == 1) {
        tile_part = tile_part << 4;
    } else if (adjacency.x == -1) {
        tile_part = tile_part >> 4;
    }

    if (adjacency.y == 1) {
        tile_part = tile_part << 8;
    } else if (adjacency.y == -1) {
        tile_part = tile_part >> 8;
    }

    return tile_part;
}

bool WFC_TilesOverlap(WFC_Tile tile, WFC_Tile other_tile, WFC_Pos adjacency) {
    uint16_t tile_part = WFC_ShiftTile(WFC_MaskTile(tile, adjacency), adjacency);
    uint16_t other_tile_part = WFC_MaskTile(other_tile, (WFC_Pos){-adjacency.x, -adjacency.y});
    //log_trace("%04X tile", tile_part);
    //log_trace("%04X other", other_tile_part);

    return tile_part == other_tile_part;
}

#if defined(WFC_TEST)
void WFC_TestTileOverlap(void) {
    assert(WFC_TilesOverlap(0x0001, 0x1000, (WFC_Pos){1, 1}));
    assert(WFC_TilesOverlap(0x1234, 0x4321, (WFC_Pos){1, 1}));

    assert(WFC_TilesOverlap(0x1234, 0x2040, (WFC_Pos){1, 0}));
    assert(WFC_TilesOverlap(0x1234, 0x2948, (WFC_Pos){1, 0}));

    assert(WFC_TilesOverlap(0x1234, 0x3400, (WFC_Pos){0, 1}));

    assert(WFC_TilesOverlap(0x1234, 0x0001, (WFC_Pos){-1, -1}));

    assert(WFC_TilesOverlap(0x1234, 0x0103, (WFC_Pos){-1, 0}));

    assert(WFC_TilesOverlap(0x1234, 0x0012, (WFC_Pos){0, -1}));
}
#endif

uint32_t WFC_Entropy(State *state, uint32_t x, uint32_t y) {
    uint32_t entropy = 0;

    uint8_t *output_bitmap = WFC_GetOutputBitmap(state, (WFC_Pos){x, y});

    for (uint32_t pat_index = 0; pat_index < state->propagator.num_patterns; pat_index++) {
        if ((output_bitmap[pat_index / 8] & (1 << (pat_index % 8))) != 0) {
            entropy += state->propagator.patterns[pat_index].count;
        }
    }

    return entropy;
}

WFC_RESULT_ENUM WFC_LowestEntropy(WFC_State *state, WFC_Pos *pos, uint32_t *entropy) {
    assert(NULL != state);
    assert(NULL != pos);
    assert(NULL != entropy);

    uint32_t min_entropy_count = 0;
    *entropy = 0xFFFFFFFF;

    uint32_t bitmap_len = WFC_BITMAP_BYTES_NEEDED(state->propagator.num_patterns);
    for (uint32_t y = 0; y < state->output_height; y++) {
        for (uint32_t x = 0; x < state->output_width; x++) {

            uint32_t current_entropy = WFC_Entropy(state, x, y);

            if (current_entropy == 0) {
                return WFC_RESULT_RESTART;
            }

            if (current_entropy < *entropy) {
                *pos = (WFC_Pos){x, y};
                min_entropy_count = 0;
                *entropy = current_entropy;
            } else if (current_entropy == *entropy) {
                min_entropy_count++;

                // accept with probability 1 / min_entropy_count
                float prob = ((1.0 / (float)0xFFFFFFFF)) * WFC_GenRandom(state);

                if (prob < (1.0 / ((float)min_entropy_count))) {
                    *entropy = current_entropy;
                    *pos = (WFC_Pos){x, y};
                }
            }
            // otherwise ignore
        }
    }

    if (min_entropy == 1) {
        return WFC_RESULT_OKAY;
    }

    return WFC_RESULT_CONTINUE;
}

WFC_RESULT_ENUM WFC_Observe(WFC_State *state, WFC_Pos *pos) {
    assert(NULL != state);
    assert(NULL != pos);

    uint32_t entropy = 0;

    WFC_RESULT_ENUM result;
    result = WFC_LowestEntropy(state, pos, &entropy);

    if (result == WFC_RESULT_CONTINUE) {
        uint32_t n = WFC_GenRandom(state) % entropy;
        bool chosen_pattern = false;

        uint8_t *output_bitmap = WFC_GetOutputBitmap(state, *pos);

        for (uint32_t pat_index = 0; pat_index < state->propagator.num_patterns; pat_index++) {
            // skip patterns that are not available for selection
            if (output_bitmap[(pat_index / 8)] & (1 << (pat_index % 8)) == 0) {
                continue
            }

            if (chosen_pattern) {
                // clear all remaining patterns once we have chosen one
                output_bitmap[(pat_index / 8)] &= ~(1 << (pat_index % 8));
            } else {
                uint32_t pat_count = state->propagator.patterns[pat_index].count;

                if (entropy < count) {
                    // we found our chosen pattern
                    chosen_pattern = true;
                    // we leave the pattern bit set here to select it
                    break;
                } else {
                    // this is not our chosen pattern so clear it an remove its count
                    output_bitmap[(pat_index / 8)] &= ~(1 << (pat_index % 8));
                    entropy -= count;
                }
            }
        }
        // check that we did actually choose a pattern
        assert(chosen_pattern);
    }

    return result;
}

WFC_RESULT_ENUM WFC_Propagate(WFC_State *state, WFC_Pos start_pos) {
    assert(NULL != state);

    state->queue.items[0] = start_pos;
    state->queue.num_items = 1;

    while (state->queue.num_items > 0) {
        // pop off an item
        state->queue.num_items--;
        WFC_Pos cur_pos = state->queue.items[state->queue.num_items];

        uint8_t *output_bitmap = WFC_GetOutputBitmap(state, cur_pos);

        for (uint32_t adj_index = 0; adj_index < WFC_NUM_ADJACENT; adj_index++) {
            WFC_Pos adjacency = gv_adjacent_offsets[adj_index];
            WFC_Pos other_pos = WFC_OffsetFrom(pos, adjacency, state->output_width, state->output_height);

            uint8_t *other_output_bitmap = WFC_GetOutputBitmap(state, other_pos);

            for (uint32_t pat_index = 0; pat_index < state->propagator.num_patterns; pat_index++) {
                // if the pattern is not currently valid, skip it
                // TODO wrap pattern stuff in functions to avoid duplication like this
                if (state->output[output_index + (pat_index / 8)] & (1 << (pat_index % 8)) == 0) {
                    continue
                }

                WFC_Tile tile = state->propagator.patterns[pat_index].tile;

                for (uint32_t other_pat_index = 0; other_pat_index < state->propagator.num_patterns; other_pat_index++) {
                    // TODO what about if this pattern is 0 already?

                    WFC_Tile other_tile = state->propagator.patterns[other_pat_index].tile;

                    if (!WFC_TilesOverlap(tile, other_tile, adjacency)) {
                        // TODO the tiles do not overlap, so remove other_pat_index from the other cell and
                        // mark it as needing queueing. Then, after the loop, queue if needed
                        // NOTE this is wrong- we need the output index of the other cell, not the current cell
                        // this is a good argument for abstraction to avoid lots of duplicate logic
                        //state->output[output_index + (pat_index / 8)] &= ~(1 << (pat_index % 8));
                    }
                }
            }
        }
    }
}

WFC_RESULT_ENUM WFC_Step(WFC_State *state) {
    assert(NULL != state);

    WFC_Pos pos;

    WFC_RESULT_ENUM result;
    result = WFC_Observe(state, &pos);

    if (result == WFC_RESULT_CONTINUE) {
        WFC_Propagate(state, pos);
    }

    return result;
}

uint32_t WFC_GenRandom(WFC_State *state) {
    state->rng = random(state->rng);

    return state->rng;
}

static uint32_t random(uint32_t seed)
{
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

#if defined(WFC_TEST)
void WFC_Test(void) {
    WFC_TestOffsetFrom();
    WFC_TestTileOverlap();
}
#endif

#if defined(WFC_TEST_MAIN)
int main(int argc, char *argv[]) {
    WFC_Test();

    printf("All Tests Passed!\n");

    return 0;
}
#endif

