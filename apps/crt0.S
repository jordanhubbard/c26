    .section .cart_header, "a"
    .word 0x54524143              /* "CART" */
    .word 2                       /* ABI version: C26_CART_VERSION */
    .word __cart_load_size
    .word __cart_bss_size
    .word _cart_entry - 0x88000000
    .word 0, 0, 0

    .section .text.entry, "ax"
    .globl _cart_entry
_cart_entry:
    /* a0 = const c26_api_t *; runs on the kernel stack in ABI v1. */
    tail app_main
