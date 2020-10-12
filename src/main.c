#include <stdint.h>
#include <stdio.h>
#include <assert.h>

#include "wfc.h"



char gv_input[] = 
    "0000"
    "0111"
    "0121"
    "0111";

const uint32_t gv_width = 4;
const uint32_t gv_height = 4;

const uint32_t gv_output_width = 20;
const uint32_t gv_output_height = 20;


int main(int argc, char *argv[]) {
    printf("Hello, wfc\n");

    WFC_RESULT_ENUM result = WFC_RESULT_OKAY;

    for (uint32_t index = 0; index < gv_width * gv_height; index++) {
        gv_input[index] = gv_input[index] - '0';
    }

    WFC_State state = {0};
    result = WFC_StateInit(&state, gv_width, gv_height, (const uint8_t*)gv_input, gv_output_width, gv_output_height);
    assert(WFC_RESULT_OKAY == result);

    WFC_PrintState(&state);

    WFC_StateDestroy(&state);

    printf("Goodbye, wfc\n");

    return 0;
}

