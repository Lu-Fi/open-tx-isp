/*
 * Extra module link script (used with -ffunction-sections/-fdata-sections and
 * --gc-sections): after unused sections are collected, fold the per-function /
 * per-object sections back into .text/.data/.bss so the .ko does not carry
 * thousands of section headers and relocation sections.  Special sections
 * (.init.*, .exit.*, .data..percpu, __ksymtab*) are untouched; per-function
 * string sections are folded back into .rodata.str1.N so the linker can
 * merge duplicate strings across functions again.
 */
SECTIONS {
	.rodata.str1.1 : { *(.rodata.str1.1 .rodata.*.str1.1) }
	.rodata.str1.4 : { *(.rodata.str1.4 .rodata.*.str1.4) }
	.rodata : { *(.rodata .rodata.[A-Za-z_0-9]*) }
	.text : { *(.text .text.[A-Za-z_0-9]*) }
	.data : { *(.data .data.[A-Za-z_0-9]*) }
	.bss  : { *(.bss .bss.[A-Za-z_0-9]*) }
}

/* sections only referenced by the module loader must survive --gc-sections */
SECTIONS {
	__ksymtab : { KEEP(*(SORT(___ksymtab+*))) }
	__ksymtab_gpl : { KEEP(*(SORT(___ksymtab_gpl+*))) }
	__kcrctab : { KEEP(*(SORT(___kcrctab+*))) }
	__kcrctab_gpl : { KEEP(*(SORT(___kcrctab_gpl+*))) }
	__ksymtab_strings : { KEEP(*(__ksymtab_strings)) }
	.modinfo : { KEEP(*(.modinfo)) }
	__param : { KEEP(*(__param)) }
	__versions : { KEEP(*(__versions)) }
	.gnu.linkonce.this_module : { KEEP(*(.gnu.linkonce.this_module)) }
	__ex_table : { KEEP(*(__ex_table)) }
	__bug_table : { KEEP(*(__bug_table)) }
	.note.gnu.build-id : { KEEP(*(.note.gnu.build-id)) }
}
