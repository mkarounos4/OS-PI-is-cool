#include "uart.h"
#include <stdarg.h>

/* 
 * printf() implementation with support for the following
 * %c, %d, %s, %u, %x, and %X
*/
void printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
	if (*fmt == '%') {
	    fmt++;
	    switch (*fmt) {
	    case 'c':
		uart_putc((char)va_arg(args, int));
		break;
	    case 'd':
		uart_putint(va_arg(args, int));
	 	break;
	    case 's':
		uart_puts(va_arg(args, const char *));
	    	break;
	    case 'u':
		uart_putuint(va_arg(args, unsigned int));
	    	break;
	    case 'x':
	    case 'X':
		uart_puthex(va_arg(args, unsigned long));
	    	break;
	    case '%':
		uart_putc('%');
		break;
	    default:
		uart_putc('%');
		uart_putc(*fmt);
		break;
	    }
	} else {
	    uart_putc(*fmt);
	}

	fmt++;
    }

    va_end(args);
}
