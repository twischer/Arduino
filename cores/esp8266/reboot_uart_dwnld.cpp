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

	if (user_start_fptr) {
		// TODO fails with an exception
		// Fatal exception (0):
		// epc1=0x4010f45c, epc2=0x00000000, epc3=0x00000000, excvaddr=0x00000000, depc=0x00000000
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
	ets_install_uart_printf(0);

	if (boot_from_flash() != 0) {
		// TODO runs int this if condition
		ets_printf("%s %s \n", "ets_main.c", "181");
		while (true);
	}
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

