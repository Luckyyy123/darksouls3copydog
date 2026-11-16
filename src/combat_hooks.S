.text
.macro HOOK_ENTER
    pushfq
    lock incl CopydogHookCalls(%rip)
    popfq
.endm
.macro HOOK_LEAVE
    pushfq
    lock decl CopydogHookCalls(%rip)
    popfq
.endm
.globl OnBulletObserve
OnBulletObserve:
    HOOK_ENTER
    # This is an inline JMP entry: native RSP is already 16-byte aligned.
    leaq -0xc0(%rsp),%rsp
    movq %rax,0x20(%rsp)
    movq %rcx,0x28(%rsp)
    movq %rdx,0x30(%rsp)
    movq %r8,0x38(%rsp)
    movq %r9,0x40(%rsp)
    movq %r10,0x48(%rsp)
    movq %r11,0x50(%rsp)
    pushfq
    popq %rax
    movq %rax,0x58(%rsp)
    movdqu %xmm0,0x60(%rsp)
    movdqu %xmm1,0x70(%rsp)
    movdqu %xmm2,0x80(%rsp)
    movdqu %xmm3,0x90(%rsp)
    movdqu %xmm4,0xa0(%rsp)
    movdqu %xmm5,0xb0(%rsp)
    movq %rdi,%rcx
    call CombatObserveBullet
    movdqu 0x60(%rsp),%xmm0
    movdqu 0x70(%rsp),%xmm1
    movdqu 0x80(%rsp),%xmm2
    movdqu 0x90(%rsp),%xmm3
    movdqu 0xa0(%rsp),%xmm4
    movdqu 0xb0(%rsp),%xmm5
    movq 0x58(%rsp),%rax
    pushq %rax
    popfq
    movq 0x20(%rsp),%rax
    movq 0x28(%rsp),%rcx
    movq 0x30(%rsp),%rdx
    movq 0x38(%rsp),%r8
    movq 0x40(%rsp),%r9
    movq 0x48(%rsp),%r10
    movq 0x50(%rsp),%r11
    leaq 0xc0(%rsp),%rsp
    # Replay all three original loads, preserving all other live state.
    movl 0x4d8(%rdi),%edx
    movq 0x4d0(%rdi),%rcx
    movl (%rcx,%rdx,4),%r8d
    HOOK_LEAVE
    jmp *BulletResume(%rip)

.globl OnHitApply
OnHitApply:
    HOOK_ENTER
    # The hit record was already reconstructed. Never bypass that constructor.
    movq 0x1f90(%r14),%rcx
    movq 0x98(%rcx),%rcx
    subq $0x20,%rsp
    call CombatShouldSkipHit
    leaq 0x20(%rsp),%rsp
    testl %eax,%eax
    jne HitSkip
    # Rebuild all four native arguments after the C predicate helper.
    movq 0x1f90(%r14),%rcx
    movb $1,%r9b
    leaq -0x50(%rbp),%r8
    movq %rbx,%rdx
    movq 0x98(%rcx),%rcx
    # The original CALL was not overwritten, retaining its native return site.
    HOOK_LEAVE
    jmp *HitCall(%rip)
HitSkip:
    HOOK_LEAVE
    jmp *HitResume(%rip)

.globl OnRollDirection
OnRollDirection:
    HOOK_ENTER
    # Function entry: align native RSP8 before the helper call, all helper-clobbered state.
    leaq -0xd8(%rsp),%rsp
    movq %rax,0x20(%rsp)
    movq %rcx,0x28(%rsp)
    movq %rdx,0x30(%rsp)
    movq %r8,0x38(%rsp)
    movq %r9,0x40(%rsp)
    movq %r10,0x48(%rsp)
    movq %r11,0x50(%rsp)
    pushfq
    popq %rax
    movq %rax,0x58(%rsp)
    movdqu %xmm0,0x60(%rsp)
    movdqu %xmm1,0x70(%rsp)
    movdqu %xmm2,0x80(%rsp)
    movdqu %xmm3,0x90(%rsp)
    movdqu %xmm4,0xa0(%rsp)
    movdqu %xmm5,0xb0(%rsp)
    # Windows x64 ABI: module in RCX, original float in XMM1, accessor in R8.
    movq %rcx,%r8
    movq %rdi,%rcx
    call CopydogOverrideRollingAngle
    movss %xmm0,0xc0(%rsp)
    movdqu 0x60(%rsp),%xmm0
    movdqu 0x70(%rsp),%xmm1
    movdqu 0x80(%rsp),%xmm2
    movdqu 0x90(%rsp),%xmm3
    movdqu 0xa0(%rsp),%xmm4
    movdqu 0xb0(%rsp),%xmm5
    movq 0x58(%rsp),%rax
    pushq %rax
    popfq
    movq 0x20(%rsp),%rax
    movq 0x28(%rsp),%rcx
    movq 0x30(%rsp),%rdx
    movq 0x38(%rsp),%r8
    movq 0x40(%rsp),%r9
    movq 0x48(%rsp),%r10
    movq 0x50(%rsp),%r11
    movss 0xc0(%rsp),%xmm1
    leaq 0xd8(%rsp),%rsp
    # Replay the exact native setter prologue before returning to its body.
    pushq %rbx
    subq $0x30,%rsp
    movq 0x10(%rcx),%rax
    movaps %xmm6,0x20(%rsp)
    HOOK_LEAVE
    jmp *CdRollResume(%rip)
