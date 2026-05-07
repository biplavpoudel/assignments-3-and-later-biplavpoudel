# Kernel Oops Error Analysis

## TRIGGER:
As per the assignment instruction, I ran the following command inside the shell of qemu build:
```bash
echo “hello_world” > /dev/faulty
```
This command should, in theory, write the string **hello world** to the device: `/dev/faulty`.

## OUTPUT:
The output (formatted for clarity) I got in the console of the QEMU build was:
```sh
biplavpoudel

Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000

Mem abort info:
  ESR = 0x0000000096000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault

Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1

user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041bfa000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000

Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 151 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)

pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
sp : ffffffc008dfbd20
x29: ffffffc008dfbd80 x28: ffffff8001b35cc0 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000000000012 x22: 0000000000000012 x21: ffffffc008dfbdc0
x20: 000000557272e990 x19: ffffff8001b60300 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc000787000 x3 : ffffffc008dfbdc0
x2 : 0000000000000012 x1 : 0000000000000000 x0 : 0000000000000000

Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110
 __arm64_sys_write+0x1c/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x2c/0xc0
 el0_svc+0x2c/0x90
 el0t_64_sync_handler+0xf4/0x120
 el0t_64_sync+0x18c/0x190

Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 0000000000000000 ]---
```


## OBJDUMP ANALYSIS

I used the cross-objdump utility to see the ssembly content associated with the faulty.ko file:

```bash
buildroot/output/host/bin/aarch64-linux-objdump -S buildroot/output/target/lib/modules/6.1.44/extra/faulty.ko 
```

### DISASSEMBLED OUTPUT:
The resulting output displayed on my console was:
```asm
buildroot/output/target/lib/modules/6.1.44/extra/faulty.ko:     file format elf64-littleaarch64


Disassembly of section .text:

0000000000000000 <faulty_write>:
   0:   d2800001        mov     x1, #0x0                        // #0
   4:   d2800000        mov     x0, #0x0                        // #0
   8:   d503233f        paciasp
   c:   d50323bf        autiasp
  10:   b900003f        str     wzr, [x1]
  14:   d65f03c0        ret
  18:   d503201f        nop
  1c:   d503201f        nop

0000000000000020 <faulty_init>:
  20:   d503233f        paciasp
  24:   a9be7bfd        stp     x29, x30, [sp, #-32]!
  28:   90000004        adrp    x4, 0 <faulty_write>
  2c:   910003fd        mov     x29, sp
  30:   f9000bf3        str     x19, [sp, #16]
  34:   90000013        adrp    x19, 0 <faulty_write>
  38:   b9400260        ldr     w0, [x19]
  3c:   90000003        adrp    x3, 0 <faulty_write>
  40:   91000084        add     x4, x4, #0x0
  44:   91000063        add     x3, x3, #0x0
  48:   52802002        mov     w2, #0x100                      // #256
  4c:   52800001        mov     w1, #0x0                        // #0
  50:   94000000        bl      0 <__register_chrdev>
  54:   37f800a0        tbnz    w0, #31, 68 <faulty_init+0x48>
  58:   b9400261        ldr     w1, [x19]
  5c:   35000041        cbnz    w1, 64 <faulty_init+0x44>
  60:   b9000260        str     w0, [x19]
  64:   52800000        mov     w0, #0x0                        // #0
  68:   f9400bf3        ldr     x19, [sp, #16]
  6c:   a8c27bfd        ldp     x29, x30, [sp], #32
  70:   d50323bf        autiasp
  74:   d65f03c0        ret
  78:   d503201f        nop
  7c:   d503201f        nop

0000000000000080 <cleanup_module>:
  80:   d503233f        paciasp
  84:   90000000        adrp    x0, 0 <faulty_write>
  88:   a9bf7bfd        stp     x29, x30, [sp, #-16]!
  8c:   52802002        mov     w2, #0x100                      // #256
  90:   52800001        mov     w1, #0x0                        // #0
  94:   910003fd        mov     x29, sp
  98:   b9400000        ldr     w0, [x0]
  9c:   90000003        adrp    x3, 0 <faulty_write>
  a0:   91000063        add     x3, x3, #0x0
  a4:   94000000        bl      0 <__unregister_chrdev>
  a8:   a8c17bfd        ldp     x29, x30, [sp], #16
  ac:   d50323bf        autiasp
  b0:   d65f03c0        ret
  b4:   d503201f        nop
  b8:   d503201f        nop
  bc:   d503201f        nop

00000000000000c0 <faulty_read>:
  c0:   d503233f        paciasp
  c4:   d100c3ff        sub     sp, sp, #0x30
  c8:   d5384100        mrs     x0, sp_el0
  cc:   a9017bfd        stp     x29, x30, [sp, #16]
  d0:   910043fd        add     x29, sp, #0x10
  d4:   a90253f3        stp     x19, x20, [sp, #32]
  d8:   aa0103f4        mov     x20, x1
  dc:   aa0203f3        mov     x19, x2
  e0:   f941f801        ldr     x1, [x0, #1008]
  e4:   f90007e1        str     x1, [sp, #8]
  e8:   d2800001        mov     x1, #0x0                        // #0
  ec:   d2800282        mov     x2, #0x14                       // #20
  f0:   52801fe1        mov     w1, #0xff                       // #255
  f4:   910013e0        add     x0, sp, #0x4
  f8:   94000000        bl      0 <memset>
  fc:   d5384101        mrs     x1, sp_el0
 100:   b9402c22        ldr     w2, [x1, #44]
 104:   f100127f        cmp     x19, #0x4
 108:   d2800083        mov     x3, #0x4                        // #4
 10c:   9a839273        csel    x19, x19, x3, ls        // ls = plast
 110:   36a80362        tbz     w2, #21, 17c <faulty_read+0xbc>
 114:   9340de80        sbfx    x0, x20, #0, #56
 118:   8a000280        and     x0, x20, x0
 11c:   d2c01001        mov     x1, #0x8000000000               // #549755813888
 120:   cb130021        sub     x1, x1, x19
 124:   eb01001f        cmp     x0, x1
 128:   aa1303e0        mov     x0, x19
 12c:   540001e9        b.ls    168 <faulty_read+0xa8>  // b.plast
 130:   7100001f        cmp     w0, #0x0
 134:   d5384101        mrs     x1, sp_el0
 138:   93407c00        sxtw    x0, w0
 13c:   9a931000        csel    x0, x0, x19, ne // ne = any
 140:   f94007e3        ldr     x3, [sp, #8]
 144:   f941f822        ldr     x2, [x1, #1008]
 148:   eb020063        subs    x3, x3, x2
 14c:   d2800002        mov     x2, #0x0                        // #0
 150:   54000201        b.ne    190 <faulty_read+0xd0>  // b.any
 154:   a9417bfd        ldp     x29, x30, [sp, #16]
 158:   a94253f3        ldp     x19, x20, [sp, #32]
 15c:   9100c3ff        add     sp, sp, #0x30
 160:   d50323bf        autiasp
 164:   d65f03c0        ret
 168:   9248fa80        and     x0, x20, #0xff7fffffffffffff
 16c:   910013e1        add     x1, sp, #0x4
 170:   aa1303e2        mov     x2, x19
 174:   94000000        bl      0 <__arch_copy_to_user>
 178:   17ffffee        b       130 <faulty_read+0x70>
 17c:   f9400021        ldr     x1, [x1]
 180:   aa1403e0        mov     x0, x20
 184:   7206003f        tst     w1, #0x4000000
 188:   54fffca0        b.eq    11c <faulty_read+0x5c>  // b.none
 18c:   17ffffe2        b       114 <faulty_read+0x54>
 190:   94000000        bl      0 <__stack_chk_fail>

Disassembly of section .plt:

0000000000000000 <.plt>:
        ...

Disassembly of section .text.ftrace_trampoline:

0000000000000000 <.text.ftrace_trampoline>:
```

## ROOT CAUSE OF THE OOPS ERROR:
The crash is caused by a **null pointer dereference** inside the **faulty_write** function of my kernel module: `faulty.ko`.

From `objdump`, the critical part we can look at is:
```bash
0000000000000000 <faulty_write>:
   0:   d2800001        mov     x1, #0x0                        // #0
   4:   d2800000        mov     x0, #0x0                        // #0
  ....
  10:   b900003f        str     wzr, [x1]
  ....
```

Here, `x1` is explicitly set to 0. <br>
And the instruction tries to store the content of 32-bit zero register `wzr` into the memory address stored in `x1`.
This expression equivalently becomes: 
```C
*(uint32_t *)0x0 = 0;
```
A better equivalent code snippet is:
```C
int *ptr = NULL;
*ptr = 0;
```
which causes the kernel **NULL** pointer dereference.