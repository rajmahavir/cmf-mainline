SECTIONS { . = 0; .text : { *(.text) } /DISCARD/ : { *(.comment) *(.note*) } }
