#[compute]
#version 450

layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) restrict readonly buffer InputBuffer {
    float data[];
}
input_buffer;

layout(set = 0, binding = 1, std430) restrict writeonly buffer OutputBuffer {
    float data[];
}
output_buffer;

void sum_gpu_aux(uint chunk_size, uint N) {
    uint idx = gl_GlobalInvocationID.x;
    uint start = chunk_size * idx;
    uint end = start + chunk_size;
    if (end > N) {
        end = N;
    }
    float result = 0.f;
    for (uint i = start; i < end; i++) {
        result += input_buffer.data[i];
    }
    output_buffer.data[idx] = result;
}

void main() {
    uint N = 1 << 23; // TODO uniform?
    uint TOTAL_INVOCATIONS = 128 * 1024; // TODO: calculate ?
    uint chunk_size = (N + TOTAL_INVOCATIONS - 1) / TOTAL_INVOCATIONS;
    sum_gpu_aux (chunk_size, N);
}
