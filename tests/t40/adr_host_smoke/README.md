# ADR literal chain: host smoke test

Runs the literal OEM ADR chain (init, day/night refresh, 20 event-2 process
runs, deinit) on an x86-64 host against a real tuning bin. Register writes are
counted; unknown call targets fail the run.

The literal code keeps MIPS addresses in `uint32_t`, so everything must live
below 4 GiB: build with `-no-pie`. Heap memory comes from a `MAP_32BIT` arena.

    # in a work dir holding prod.asm/prod.sym of the OEM module and
    # adrh/<fn>.c translated with tools/mips2c_literal.py (symtab mode):
    python3 tools/gen_t40_host_lit.py <work> adrh adrh <oem tx-isp-t40.ko> <objdump> oem_adr.c
    gcc -O0 -no-pie -fno-pie -w -o adrsmoke main.c oem_adr.c
    ./adrsmoke sc830ai-t40.bin

Result for the sc830ai bin on 2026-10-15: init ret 0 (316 writes, Gauss table
computed), day/night refresh ret 0, process x20 without fault, deinit ret 0,
0 bad calls.
