#[compute]
#version 450

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) restrict readonly buffer InputBuffer {
    float data[];
}
input_buffer;

layout(set = 0, binding = 1, std430) restrict writeonly buffer SumsBuffer {
    float data[];
}
sums_buffer;

layout(set = 0, binding = 2, std430) restrict writeonly buffer SquaresBuffer {
    float data[];
}
squares_buffer;

void variance_gpu_aux(uint chunk_size, uint N) {
    uint idx = gl_GlobalInvocationID.x;
    uint start = chunk_size * idx;
    uint end = start + chunk_size;
    if (end > N) {
        end = N;
    }
    float sum = 0.f;
    float squares = 0.f;
    for (uint i = start; i < end; i++) {
        sum += input_buffer.data[i];
        squares += input_buffer.data[i] * input_buffer.data[i];
    }
    sums_buffer.data[idx] = sum;
    squares_buffer.data[idx] = squares;
}

void main() {
    uint N = 1 << 26; // TODO uniform?
    uint TOTAL_INVOCATIONS = 64 * 256; // TODO: calculate ?
    uint chunk_size = (N + TOTAL_INVOCATIONS - 1) / TOTAL_INVOCATIONS;
    variance_gpu_aux (chunk_size, N);
}
