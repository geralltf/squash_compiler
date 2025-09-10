section .text
global main
tan: 
mov	rax, 	2
push	rax
mov	rax, 	1
pop	rbx
add	rax,	rbx
push	rax
mov	rax, 	3
push	rax
mov	rax, 	2
pop	rbx
add	rax,	rbx
push	rax
mov	rax, 	5
push	rax
mov	rax, 	8
pop	rbx
add	rax,	rbx
push	rax
mov	rax, 	6
push	rax
mov	rax, 	9
pop	rbx
mul	rax,	rbx
push	rax
mov	rax, 	14
pop	rbx
add	rax,	rbx
push	rax
mov	rax, 	7
pop	rbx
add	rax,	rbx
pop	rbx
mul	rax,	rbx
pop	rbx
mul	rax,	rbx
pop	rbx
sub	rax,	rbx
mov	rax, 	[fg]
ret
cos: 
mov	rax, 	12
push	rax
mov	rax, 	8
pop	rbx
mul	rax,	rbx
push	rax
mov	rax, 	32
push	rax
mov	rax, 	3
push	rax
mov	rax, 	6
pop	rbx
mul	rax,	rbx
push	rax
mov	rax, 	4
push	rax
mov	rax, 	8
pop	rbx
mul	rax,	rbx
push	rax
mov	rax, 	2
push	rax
mov	rax, 	9
pop	rbx
mul	rax,	rbx
pop	rbx
mul	rax,	rbx
pop	rbx
add	rax,	rbx
pop	rbx
add	rax,	rbx
pop	rbx
add	rax,	rbx
ret
sin: 
mov	rax, 	2
push	rax
mov	rax, 	3
pop	rbx
add	rax,	rbx
ret
simple_hello: 
mov	rax, 	2
push	rax
mov	rax, 	12
push	rax
mov	rax, 	15
pop	rbx
add	rax,	rbx
pop	rbx
mul	rax,	rbx
ret
simple_hi: 
mov	rax, 	3
push	rax
mov	rax, 	16
push	rax
mov	rax, 	14
pop	rbx
mul	rax,	rbx
pop	rbx
sub	rax,	rbx
ret
main: 
mov	rax, 	2
push	rax
mov	rax, 	1
pop	rbx
add	rax,	rbx
push	rax
mov	rax, 	3
push	rax
mov	rax, 	2
pop	rbx
add	rax,	rbx
push	rax
mov	rax, 	5
push	rax
mov	rax, 	8
pop	rbx
add	rax,	rbx
push	rax
mov	rax, 	6
push	rax
mov	rax, 	9
pop	rbx
mul	rax,	rbx
push	rax
mov	rax, 	14
pop	rbx
add	rax,	rbx
push	rax
mov	rax, 	7
pop	rbx
add	rax,	rbx
pop	rbx
mul	rax,	rbx
pop	rbx
mul	rax,	rbx
pop	rbx
sub	rax,	rbx
mov	rax, 	[ff]
mov	rax, 	6
push	rax
mov	rax, 	9
pop	rbx
mul	rax,	rbx
push	rax
mov	rax, 	14
pop	rbx
add	rax,	rbx
push	rax
mov	rax, 	7
push	rax
mov	rax, 	[fg]
pop	rbx
add	rax,	rbx
pop	rbx
add	rax,	rbx
mov	rax, 	[hit2]
mov	rax, 	5
push	rax
mov	rax, 	8
pop	rbx
add	rax,	rbx
push	rax
mov	rax, 	6
push	rax
mov	rax, 	9
pop	rbx
mul	rax,	rbx
push	rax
mov	rax, 	14
pop	rbx
add	rax,	rbx
push	rax
mov	rax, 	7
push	rax
mov	rax, 	[fg]
pop	rbx
add	rax,	rbx
pop	rbx
add	rax,	rbx
pop	rbx
mul	rax,	rbx
mov	rax, 	[hit5]
mov	rax, 	13
push	rax
mov	rax, 	68
pop	rbx
mul	rax,	rbx
push	rax
mov	rax, 	7
pop	rbx
add	rax,	rbx
mov	rax, 	[hit6]
mov	rax, 	10
push	rax
mov	rax, 	[fg]
push	rax
mov	rax, 	5
pop	rbx
mul	rax,	rbx
pop	rbx
add	rax,	rbx
mov	rax, 	[g]
mov	rax, 	6
push	rax
mov	rax, 	5
pop	rbx
mul	rax,	rbx
mov	rax, 	[a]
mov	rax, 	2
push	rax
mov	rax, 	3
pop	rbx
mul	rax,	rbx
mov	rax, 	[b]
mov	rax, 	3
push	rax
mov	rax, 	5
pop	rbx
mul	rax,	rbx
mov	rax, 	[c]
mov	rax, 	4
push	rax
mov	rax, 	3
push	rax
mov	rax, 	1
pop	rbx
add	rax,	rbx
pop	rbx
mul	rax,	rbx
mov	rax, 	[e]
mov	rax, 	3
push	rax
mov	rax, 	8
push	rax
mov	rax, 	4
push	rax
mov	rax, 	[fg]
pop	rbx
add	rax,	rbx
pop	rbx
add	rax,	rbx
pop	rbx
mul	rax,	rbx
mov	rax, 	[d]
mov	rax, 	10
push	rax
mov	rax, 	[fg]
push	rax
mov	rax, 	5
pop	rbx
mul	rax,	rbx
pop	rbx
add	rax,	rbx
mov	rax, 	[fg]
mov	rax, 	[fg]
push	rax
mov	rax, 	[fg]
push	rax
mov	rax, 	[fg]
push	rax
mov	rax, 	[fg]
push	rax
mov	rax, 	[fg]
pop	rbx
add	rax,	rbx
pop	rbx
add	rax,	rbx
pop	rbx
mul	rax,	rbx
pop	rbx
mul	rax,	rbx
ret
