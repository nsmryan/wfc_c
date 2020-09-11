#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "wfc.h"


#define WFC_ADJACENT_BYTES_NEEDED(num_patterns) ((num_patterns / 8UL) + ((num_patterns % 8) != 0))
#define WFC_PATTERN_BYTES_NEEDED(num_patterns) (WFC_ADJACENT_BYTES_NEEDED(num_patterns) * WFC_NUM_ADJACENT)
#define WFC_INDEX_LENGTH_BYTES(num_patterns) (WFC_PATTERN_BYTES_NEEDED(num_patterns) * num_patterns)

#define WFC_PATTERN_INDEX(num_patterns, pattern) (WFC_PATTERN_BYTES_NEEDED(num_patterns) * pattern)
#define WFC_ADJACENT_INDEX(num_patterns, adjacent) (WFC_ADJACENT_BYTES_NEEDED(num_patterns) * adjacent)


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



WFC_RESULT_ENUM WFC_StateInit(WFC_State *state, uint32_t input_width, uint32_t input_height, const uint8_t *input) {
    WFC_RESULT_ENUM result = WFC_RESULT_OKAY;

    if ((NULL == state) || (NULL == input)) {
        result = WFC_RESULT_ERROR;
    }

    if (WFC_RESULT_OKAY == result) {
        // check that input does not contain values >= 16
        for (uint32_t input_index = 0; input_index < input_width * input_height; input_index++) {
            if ((input[input_index] & (~WFC_CELL_MASK)) != 0) {
                result = WFC_RESULT_ERROR;
                break;
            }
        }
    }

    if (WFC_RESULT_OKAY == result) {
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

    if (WFC_RESULT_OKAY == result) {
        // collect patterns from input into a table
        result = WFC_FindPatterns(state);
    }

    if (WFC_RESULT_OKAY == result) {
        // create the index (table of adjacent patterns for each pattern)
        uint8_t *index = (uint8_t*)malloc(WFC_INDEX_LENGTH_BYTES(state->propagator.num_patterns));

        if (NULL == index) {
            result = WFC_RESULT_ERROR;
        } else {
            state->propagator.index = index;
        }
    }

    if (WFC_RESULT_OKAY == result) {
        // fill the index with the discovered patterns and their adjacency information
        result = WFC_IndexInit(state);
    }

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

WFC_RESULT_ENUM WFC_FindPatterns(WFC_State *state) {
    WFC_RESULT_ENUM result = WFC_RESULT_OKAY; 

    assert(NULL != state);

    for (uint32_t y = 0; y < state->input_height; y++) {
        for (uint32_t x = 0; x < state->input_width; x++) {
            WFC_Pos pos = { x, y };

            WFC_Pattern pattern = {0};
            for (uint32_t offset_index = 0; offset_index < WFC_PATTERN_LEN; offset_index++) {
                WFC_Pos offset = gv_pattern_offsets[offset_index];

                WFC_Pos loc = WFC_OffsetFrom(pos, offset, state->input_width, state->input_height);

                pattern.tile = pattern.tile << WFC_CELL_NUM_BITS;
                pattern.tile |= state->input[loc.x + loc.y * state->input_width];
            }

            // NOTE a hash table or set structure may be faster then this linear search
            bool pattern_found = false;
            for (uint32_t pattern_index = 0; pattern_index < state->propagator.num_patterns; pattern_index++) {
                if (memcmp(&state->propagator.patterns[pattern_index].tile, &pattern.tile, sizeof(WFC_Tile)) == 0) {
                    pattern_found = true;
                    pattern.index = pattern_index;
                    break;
                }
            }

            if (!pattern_found) {
                if (state->propagator.patterns == NULL) {
                    state->propagator.max_patterns = 1;
                    state->propagator.patterns = (WFC_Pattern*)malloc(sizeof(WFC_Pattern));
                    assert(NULL != state->propagator.patterns);
                }

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

    return result;
}

#if defined(WFC_TEST)
void WFC_Test(void) {
    WFC_TestOffsetFrom();
}
#endif

#if defined(WFC_TEST_MAIN)
int main(int argc, char *argv[]) {
    WFC_Test();

    printf("All Tests Passed!\n");

    return 0;
}
#endif

