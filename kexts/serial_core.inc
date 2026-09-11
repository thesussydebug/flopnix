/* Calculates UART settings and buffer positions. */
#define UART_RBR 0
#define UART_THR 0
#define UART_DLL 0
#define UART_IER 1
#define UART_DLM 1
#define UART_FCR 2
#define UART_LCR 3
#define UART_MCR 4
#define UART_LSR 5

#define LSR_DR   0x01
#define LSR_OE   0x02
#define LSR_PE   0x04
#define LSR_FE   0x08
#define LSR_THRE 0x20

#define LCR_DLAB 0x80
#define LCR_8N1  0x03

static int uart_divisor(u32 baud)
{
    if (baud == 0 || baud > 115200) return 0;
    if (115200u % baud) return 0;
    return (int)(115200u / baud);
}

static int uart_lsr_error(u8 lsr)
{
    return (lsr & (LSR_OE | LSR_PE | LSR_FE)) != 0;
}
