
#[compute]
#version 450

/*
Cellular automaton 

--> we treat the grid in small blocks of size 2x2

[a b]|[a b]|[a b]|
[c d]|[c d]|[c d]|
[a b]|[a b]|[a b]|
[c d]|[c d]|[c d]|
-> independent blocks at each step 

And at each step we shift the blocks by one pixel vertically and horizontally


so one invocation = 2x2 blocks
two phases : 
- phase 0 : (0,0), (2,0), (4,0), ...
- phase 1 : (-1, -1), (1, -1), (3, -1), ...
out of grid -> treat as wall
*/

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

/*
still double buffering
*/
layout(set = 0, binding = 0, rgba32f) uniform readonly image2D input_grid;
layout(set = 0, binding = 1, rgba32f) uniform writeonly image2D output_grid;

/*
parameters sent from c++
*/
layout(push_constant, std430) uniform Params {
    int width;
    int height;
    int phase;
    int step;
} params;

/*
cell types
*/

const int EMPTY = 0;
const int SAND = 1;
const int WOOD = 2;

/* 
grid access
*/

bool in_grid(ivec2 p){
    return p.x >= 0 && p.y >= 0 && p.x < params.width && p.y < params.height;
}

/*
outside cells -> walls
*/
int get_cell(ivec2 p){
    if (!in_grid(p)){
        return WOOD;
    }

    vec4 color = imageLoad(input_grid, p);

    if (color.r > 0.8 && color.g > 0.7) {
        return SAND;
    }

    if (color.r > 0.3 && color.g < 0.5) {
        return WOOD;
    }

    return EMPTY;
}

/*
set a cell inside the grid

*/

void set_cell(ivec2 p, int cell) {
    if (!in_grid(p)) {
        return;
    }
    if (cell == SAND) {
        imageStore(output_grid, p, vec4(1.0, 0.85, 0.15, 1.0));
    } else if (cell == WOOD) {
        imageStore(output_grid, p, vec4(0.45, 0.25, 0.10, 1.0));
    } else {
        imageStore(output_grid, p, vec4(0.0, 0.0, 0.0, 1.0));
    }
}

/*
MAIN

we treat one block 2x2
a b
c d 

*/

void main(){
    ivec2 block_id = ivec2(gl_GlobalInvocationID.xy);

    /*
    phase 0:
    origin = block_id * 2 

    phase 1 (we shift):
    origin = block_id * 2 - (1, 1)
    */

    ivec2 origin = block_id * 2 - ivec2(params.phase, params.phase);

    /*
    get positions of 4 points
    */
    ivec2 pos_a = origin + ivec2(0,0);
    int a = get_cell(pos_a);

    ivec2 pos_b = origin + ivec2(1,0);
    int b = get_cell(pos_b);

    ivec2 pos_c = origin + ivec2(0,1);
    int c = get_cell(pos_c);

    ivec2 pos_d = origin + ivec2(1,1);
    int d = get_cell(pos_d);

    /*
    vertical fall
    */

    if (a == SAND && c == EMPTY){
        a = EMPTY;
        c = SAND;
    }
    if (b == SAND && d == EMPTY){
        b = EMPTY;
        d = SAND;
    }

    /*
    diagonal fall
    */

    if (a == SAND && c != EMPTY && d == EMPTY) {
        a = EMPTY;
        d = SAND;
    }
    if (b == SAND && d != EMPTY && c == EMPTY){
        b = EMPTY;
        c = SAND;
    }

    /*
    write output
    */
    set_cell(pos_a, a);
    set_cell(pos_b, b);
    set_cell(pos_c, c);
    set_cell(pos_d, d);
}