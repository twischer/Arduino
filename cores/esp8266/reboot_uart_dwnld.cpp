#include "reboot_uart_dwnld.h"
#include <stdnoreturn.h>
#include <xtensa/corebits.h>
#include <ets_sys.h>
#include <user_interface.h>
#include <spi_flash.h>
#include <esp8266_undocumented.h>

void ICACHE_RAM_ATTR boot_from_something_uart_dwnld(void (**user_start_ptr)())
{
	/* simplified for following condition
	 * (GPI >> 0x10 & 0x07) == 1 and
	 * (GPI >> 0x1D & 0x07) == 6
	 */

	const uint32_t uart_no = 0;
	const uint16_t divlatch = uart_baudrate_detect(uart_no, 0);
	uart_div_modify(uart_no, divlatch);
	UartDwnLdProc((uint8_t*)0x3fffa000, 0x2000, user_start_ptr);
}

[[noreturn]] void ICACHE_RAM_ATTR main_uart_dwnld()
{
	// TODO may be it is part of _start()
	// at least ets_intr_lock() is called in system_restart_local()
	// Therefore unlock it here again to enable UART Rx IRQ
	ets_intr_unlock();

	uartAttach();
	Uart_Init(0);
	ets_install_uart_printf(0);

	boot_from_something_uart_dwnld(&user_start_fptr);

	// 0x4010e004 in case of esptool.py flasher stub
	ets_printf("\n\n user_start_fptr *%p = %p\n", &user_start_fptr, user_start_fptr);
	if (user_start_fptr == NULL) {
		if (boot_from_flash() != 0) {
			ets_printf("%s %s \n", "ets_main.c", "181");
			while (true);
		}
	}

	_xtos_set_exception_handler(EXCCAUSE_UNALIGNED, window_spill_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_ILLEGAL, print_fatal_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_INSTR_ERROR, print_fatal_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_LOAD_STORE_ERROR, print_fatal_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_LOAD_PROHIBITED, print_fatal_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_STORE_PROHIBITED, print_fatal_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_PRIVILEGED, print_fatal_exc_handler);

	/* Moved from system_restart_core_uart_dwnld(). Not sure if it is required */
	//Cache_Read_Disable();
	//CLEAR_PERI_REG_MASK(PERIPHS_DPORT_24, 0x18);

	// 0x4010f498 in case of esptool.py --no-stub ...
	ets_printf("\n\n user_start_fptr *%p = %p\n", &user_start_fptr, user_start_fptr);
	if (user_start_fptr) {
		// TODO fails with an exception
		// Fatal exception (0):
		// epc1=0x4010f498, epc2=0x00000000, epc3=0x00000000, excvaddr=0x00000000, depc=0x00000000
		// May be execution flag is not set for memory region
		user_start_fptr();
	}

	ets_printf("user code done\n");
	ets_run();
}

[[noreturn]] void ICACHE_RAM_ATTR system_restart_core_uart_dwnld()
{
	Wait_SPI_Idle(flashchip);

	// TODO exception when calling uart_div_modify()
	//Cache_Read_Disable();
	//CLEAR_PERI_REG_MASK(PERIPHS_DPORT_24, 0x18);
	main_uart_dwnld();
}

[[noreturn]] void system_restart_local_uart_dwnld()
{
	// Fails with
	// Exception (0):
	// epc1=0x4010f498 epc2=0x00000000 epc3=0x00000000 excvaddr=0x00000000 depc=0x00000000
	// >>>stack>>>
	// ctx: cont
	// sp: 3ffef6e0 end: 3ffef8d0 offset: 0190
	// 3ffef870:  4022973e 042c1d80 3ffee734 40203bcc
	// 3ffef880:  40202b61 000003e8 3ffef914 40202b78
	// 3ffef890:  3fffdad0 3ffee714 3ffee734 40202b51
	// 3ffef8a0:  00000000 000f000f 00000000 feefeffe
	// 3ffef8b0:  feefeffe 3ffee714 3ffef900 40204778
	// 3ffef8c0:  feefeffe feefeffe 3ffe85cc 401001e1
	// <<<stack<<<
	user_start_fptr = (void(*)())0x4010f498;
	user_start_fptr();
	while (true);

	if (system_func1(0x4) == -1) {
		clockgate_watchdog(0);
		SET_PERI_REG_MASK(PERIPHS_DPORT_18, 0xffff00ff);
		pm_open_rf();
	}

	struct rst_info rst_info;
	system_rtc_mem_read(0, &rst_info, sizeof(rst_info));
	if (rst_info.reason != REASON_SOFT_WDT_RST &&
		rst_info.reason != REASON_EXCEPTION_RST) {
		ets_memset(&rst_info, 0, sizeof(rst_info));
		WRITE_PERI_REG(RTC_STORE0, REASON_SOFT_RESTART);
		rst_info.reason = REASON_SOFT_RESTART;
        	system_rtc_mem_write(0, &rst_info, sizeof(rst_info));
	}

	user_uart_wait_tx_fifo_empty(0, 0x7a120);
	user_uart_wait_tx_fifo_empty(1, 0x7a120);
	ets_intr_lock();
	SET_PERI_REG_MASK(PERIPHS_DPORT_18, 0x7500);
	CLEAR_PERI_REG_MASK(PERIPHS_DPORT_18, 0x7500);
	SET_PERI_REG_MASK(PERIPHS_I2C_48, 0x2);
	SET_PERI_REG_MASK(PERIPHS_I2C_48, 0x2);

	system_restart_core_uart_dwnld();
}

