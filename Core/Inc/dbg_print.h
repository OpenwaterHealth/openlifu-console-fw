/*
 * dbg_print.h
 *
 * Debug console output gating.
 *
 * The console board has no serial header brought out, so USART3 debug output
 * is dead weight: it costs flash for the newlib printf machinery and RAM for
 * the DMA log ring in logging.c. Both are compiled out by default.
 *
 * Re-enable with:  cmake --preset <p> -DENABLE_DEBUG_PRINT=ON
 *
 * NOTE: this gates printf() only. snprintf() (used in lifu_config.c) is a
 * separate newlib entry point and is deliberately left alone -- it formats
 * into a caller-supplied buffer and never touches the UART.
 */

#ifndef INC_DBG_PRINT_H_
#define INC_DBG_PRINT_H_

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ENABLE_DEBUG_PRINT
  #define DBG_PRINTF(...)    printf(__VA_ARGS__)
  /* These two resolve against logging.h, which main.c already includes. */
  #define DBG_LOG_INIT()     init_dma_logging()
  #define DBG_LOG_TXCPLT(h)  logging_UART_TxCpltCallback(h)
#else
  #define DBG_PRINTF(...)    ((void)0)
  #define DBG_LOG_INIT()     ((void)0)
  #define DBG_LOG_TXCPLT(h)  ((void)(h))
#endif

#ifdef __cplusplus
}
#endif

#endif /* INC_DBG_PRINT_H_ */
