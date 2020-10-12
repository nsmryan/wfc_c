#ifndef __WFC_H__
#define __WFC_H__

#include <stdint.h>
#include <assert.h>


#define WFC_TILE_NUM_CELLS 4
#define WFC_N 2
#define WFC_PATTERN_LEN (WFC_N * WFC_N)
#define WFC_CELL_NUM_BITS 4
#define WFC_CELL_MASK ((1 << WFC_CELL_NUM_BITS) - 1)


typedef uint8_t WFC_Value;

typedef enum WFC_RESULT_ENUM {
    WFC_RESULT_OKAY,
    WFC_RESULT_FINISHED,
    WFC_RESULT_RESTART,
    WFC_RESULT_CONTINUE,
    WFC_RESULT_ERROR,
} WFC_RESULT_ENUM;

typedef enum WFC_ADJACENT_ENUM {
    WFC_ADJACENT_UPLEFT = 0,
    WFC_ADJACENT_UP,
    WFC_ADJACENT_UPRIGHT,
    WFC_ADJACENT_RIGHT,
    WFC_ADJACENT_DOWNRIGHT,
    WFC_ADJACENT_DOWN,
    WFC_ADJACENT_DOWNLEFT,
    WFC_ADJACENT_LEFT,
    WFC_NUM_ADJACENT,
} WFC_ADJACENT_ENUM;

typedef struct WFC_Pos {
    int32_t x;
    int32_t y;
} WFC_Pos;

typedef uint16_t WFC_Tile;
static_assert((sizeof(WFC_Tile) * 8) == (WFC_PATTERN_LEN * WFC_TILE_NUM_CELLS));

typedef struct WFC_Pattern {
    uint32_t index; /* index into the propagator's pattern array */
	uint32_t count; /* number of times the pattern occurred in the input image */
    WFC_Tile tile; /* 2x2 tile pattern */
} WFC_Pattern;

typedef struct WFC_Propagator {
    uint32_t max_patterns;
    uint32_t num_patterns;
    WFC_Pattern *patterns;

    uint32_t bitmap_len;
    uint8_t *index; /* Patterns x Adjacency x Pattern where the last dimension is a bitmap */
} WFC_Propagator;

// NOTE used more like a stack than a queue
typedef struct WFC_Queue {
    WFC_Pos *items;
    uint32_t num_items;
    uint32_t max_items;
} WFC_Queue;

typedef struct WFC_State {
    WFC_Propagator propagator;
    uint32_t step_num;

    uint32_t rng;

    // TODO split some of these into structures
    uint32_t input_width;
    uint32_t input_height;
    uint8_t *input;

    uint32_t output_width;
    uint32_t output_height;
    uint8_t *output; /* Array of bitmaps indicating which tiles are valid for each output image pixel */

    WFC_Queue queue;
} WFC_State;

WFC_RESULT_ENUM WFC_StateInit(WFC_State *state,
                              uint32_t input_width,
                              uint32_t input_height,
                              const uint8_t *input,
                              uint32_t output_width,
                              uint32_t output_height,
                              uint8_t *output);
void WFC_StateDestroy(WFC_State *state);

WFC_RESULT_ENUM WFC_FindPatterns(WFC_State *state);
WFC_RESULT_ENUM WFC_IndexInit(WFC_State *state);
WFC_Pos WFC_OffsetFrom(WFC_Pos pos, WFC_Pos offset, uint32_t width, uint32_t height);

void WFC_PrintState(WFC_State *state);

WFC_RESULT_ENUM WFC_Step(WFC_State *state);

/*


// Create an output image of 4bit colors by copying the state->output bitmaps
// into the output image.
// TODO either average the tile color values, or return an error if there is
// more then one possible color for a tile.
WFC_RESULT_ENUM WFC_Output(WFC_State *state, uint8_t *output);
*/

#if defined(WFC_TEST)
void WFC_Test(void);
#endif

#endif
