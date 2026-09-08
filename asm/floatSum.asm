.global floatSum
"float sum_f32(const float* p, std::size_t n)":
xorps xmm0, xmm0; Zero out accumulator reg 
xor rcx, rcx; 

.loop:
addps   xmm0, [rdi + rcx*4]; add 4 elements straight from memory
add rcx, 4; //i+= 4
cmp rcx, rax; round down to a multiple of 4.
jb .loop;

.squashLane:
movhlps xmm1, xmm0; give xmm1 the top 2 lanes
addps xmm0, xmm1; lane0 += lane2, lane1 += lane3



