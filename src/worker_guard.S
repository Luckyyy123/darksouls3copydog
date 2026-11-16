.text
.globl CopydogRunPollingGuarded
.def CopydogRunPollingGuarded; .scl 2; .type 32; .endef
.seh_proc CopydogRunPollingGuarded
CopydogRunPollingGuarded:
    subq $40, %rsp
    .seh_stackalloc 40
    .seh_endprologue
.Lpoll_begin:
    call PollingThreadProc
    nop
.Lpoll_end:
    xorl %eax, %eax
    jmp .Lpoll_return
.Lpoll_handler:
    movl $1, %eax
.Lpoll_return:
    addq $40, %rsp
    ret
.seh_handler __C_specific_handler, @except
.seh_handlerdata
    .long 1
    .rva .Lpoll_begin, .Lpoll_end, CopydogWorkerFaultFilter, .Lpoll_handler
.text
.seh_endproc
