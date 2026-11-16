; 测试专用半主机退出；不是目标固件的一部分。
    PRESERVE8
    THUMB
    AREA |.text|, CODE, READONLY
    EXPORT test_exit
test_exit PROC
    MOV R2,R0
    LDR R1,=0x20026
    PUSH {R1,R2}
    MOV R1,SP
    MOVS R0,#0x20
    BKPT 0xAB
    B .
    ENDP
    ALIGN
    END
