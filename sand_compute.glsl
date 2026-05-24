#[compute]
#version 450

/*
each gpu workgroup contains 16*16 threads
one thread = one cell of the grid
*/

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

/*
input_grid = current state  --> binding(0)
shader only reads from it
*/
layout(set = 0, binding = 0, rgba32f) uniform readonly image2D input_grid;

/*
output_grid = next statue --> binding(1)
*/
layout(set = 0, binding = 1, rgba32f) uniform writeonly image2D output_grid;

/*
cell types 
*/
const int EMPTY = 0;
const int SAND = 1;

/*
grid size (match with c++ values)
*/
const int WIDTH = 256;
const int HEIGHT = 256;

/*
read one cell 
we only need to read the red channel
empty cell -> black (r = 0.0)
sand cell -> yellow (r = 1.0)
*/
int get_cell(ivec2 p){
    if (p.x < 0 || p.x >= WIDTH || p.y < 0 || p.y >= HEIGHT) {
        return EMPTY;
    }

    vec4 color = imageLoad(input_grid, p);

    if (color.r > 0.5) {
        return SAND;
    }

    return EMPTY;
}

/*
write one cell into output texture
*/
void set_cell(ivec2 p, int cell) {
    if (cell == SAND) {
        imageStore(output_grid, p, vec4(1.0, 0.85, 0.15, 1.0));
    } else {
        imageStore(output_grid, p, vec4(0.0, 0.0, 0.0, 1.0));
    }
}

void main(){
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);

    if (coord.x >= WIDTH || coord.y >= HEIGHT) {
        return;
    }

    /*
    read current cell and vertical neighbors
    */
    int current = get_cell(coord);

    ivec2 above = coord + ivec2(0, -1);
    ivec2 below = coord + ivec2(0, 1);

    int above_cell = get_cell(above);
    int below_cell = get_cell(below);

    /*
    cell keeps current state
    */
    int next = current;

    /*
    simple falling 
    - if current cell empty + cell above contains sand -> current cell contains sand
    - if current cell sand + cell below empty -> current empty
    */

    if (current == EMPTY && above_cell == SAND) {
        next = SAND;
    } else if (current == SAND && below_cell == EMPTY) {
        next = EMPTY;
    }

    /*
    write
    */
    set_cell(coord, next);
}