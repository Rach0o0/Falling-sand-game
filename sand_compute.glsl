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
dynamic grid size sent from cpp (GpuBackend::step)
push constant = small block of data given directly to the shader no buffer needed
+ that is not a vector/matrix, just a number, push cte faster for this
  https://docs.godotengine.org/en/stable/tutorials/rendering/compositor.html#compositor-effects
  https://www.khronos.org/opengl/wiki/Layout_Qualifier_(GLSL)
  https://vkguide.dev/docs/chapter-3/push_constants/#:~:text=Push%20constants%20let%20us%20send,in%20the%20command%20buffer%20itself.
*/
layout(push_constant, std430) uniform Params {
    int width;
    int height;
} params;

/*
cell types
*/
const int EMPTY = 0;
const int SAND = 1;
const int WOOD = 2;

const int INVALID = -100000;


/*
check if coordinates are inside the grid
*/
bool in_grid(ivec2 p){
    return p.x >= 0 && p.y >= 0 && p.x < params.width && p.y < params.height;
}

/*
read one cell 
we only need to read the red channel
empty cell -> black (r = 0.0)
sand cell -> yellow (r = 1.0)
*/
int get_cell(ivec2 p){
    if (!in_grid(p)) {
        return EMPTY;
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
write one cell into output texture
*/
void set_cell(ivec2 p, int cell) {
    if (cell == SAND) {
        imageStore(output_grid, p, vec4(1.0, 0.85, 0.15, 1.0));
    } else if (cell == WOOD){
        imageStore(output_grid, p, vec4(0.45, 0.25, 0.10, 1.0));
    }
    else {
        imageStore(output_grid, p, vec4(0.0, 0.0, 0.0, 1.0));
    }
}

/*
compare two positions
*/
bool same_pos(ivec2 a, ivec2 b){
    return a.x == b.x && a.y == b.y;
}

/*
choose left or right
*/
uint hash_uvec2(uvec2 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1013904223u;
    return v.x ^ v.y;
}

bool random_bool(ivec2 p) {
    uint h = hash_uvec2(uvec2(p));
    return (h & 1u) == 0u;
}




/*
find where the sand particle at position s wants to go
*/
ivec2 wanted_target(ivec2 s){
    if (!in_grid(s)){
        return ivec2(INVALID, INVALID);
    }

    if (get_cell(s) != SAND){
        return s;
    }

    ivec2 down = s + ivec2(0, 1);
    ivec2 down_left = s + ivec2(-1, 1);
    ivec2 down_right = s + ivec2(1,1);

    /*
    try vertically
    */
    if (in_grid(down) && get_cell(down) == EMPTY){
        return down;
    }

    /*
    diagonals
    */
    bool can_left = in_grid(down_left) && get_cell(down_left) == EMPTY;
    bool can_right = in_grid(down_right) && get_cell(down_right) == EMPTY;

    /*
    if both possible, pseudo random
    */
    if (can_left && can_right){
        if (random_bool(s)){
            return down_left;
        } else {
            return down_right;
        }
    }

    if (can_left){
        return down_left;
    }

    if (can_right){
        return down_right;
    }

    /*
    stay
    */
    return s;
}

/*
for a target cell t, decide which particle is allowed to enter it
sources : above, above-left, above-right
prevents two grains from entering the same cell
*/
ivec2 accepted_source_for(ivec2 t){
    if (!in_grid(t)){
        return ivec2(INVALID, INVALID);
    }

    /*
    check above
    */
    ivec2 from_above = t + ivec2(0, -1);

    if (in_grid(from_above) && get_cell(from_above) == SAND){
        ivec2 target = wanted_target(from_above);

        if (same_pos(target, t)){
            return from_above;
        }
    }

    /*
    diag
    */
    ivec2 from_left = t + ivec2(-1, -1);
    ivec2 from_right = t + ivec2(1, -1);

    bool left_ok = false;
    bool right_ok = false;

    if (in_grid(from_left) && get_cell(from_left) == SAND){
        ivec2 target = wanted_target(from_left);

        if (same_pos(target, t)){
            left_ok = true;
        }
    }

    if (in_grid(from_right) && get_cell(from_right) == SAND){
        ivec2 target = wanted_target(from_right);

        if (same_pos(target, t)){
            right_ok = true;
        }
    }

    /*
    if both, random
    */

    if (left_ok && right_ok){
        if (random_bool(t)){
            return from_left;
        } else {
            return from_right;
        }
    }

    if (left_ok) {
        return from_left;
    }
    if (right_ok){
        return from_right;
    }

    return ivec2(INVALID, INVALID);
}


void main(){
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);

    if (!in_grid(coord)) {
        return;
    }

    int current = get_cell(coord);
    int next = current;

    /*
    case 1 : current cell empty
    just see if one particle is accepted into it
    */
    if (current == EMPTY){
        ivec2 source = accepted_source_for(coord);

        if (source.x != INVALID) {
            next = SAND;
        } else {
            next = EMPTY;
        }
    }

    /*
    CASE 2 : contains sand 
    becomes empty only if it wants to move somewhere else and the target accepts 
    */
    else if (current == SAND) {
        ivec2 target = wanted_target(coord);

        if (!same_pos(target, coord)){
            ivec2 accepted_source = accepted_source_for(target);

            if (same_pos(accepted_source, coord)){
                next = EMPTY;
            } else {
                next = SAND;
            }
        } else {
            next = SAND;
        }
    }

    else if (current == WOOD) {
        next = WOOD;
    }
    set_cell(coord, next);
}