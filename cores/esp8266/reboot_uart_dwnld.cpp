#include "reboot_uart_dwnld.h"
#include <stdnoreturn.h>
#include <xtensa/corebits.h>
#include <ets_sys.h>
#include <user_interface.h>
#include <spi_flash.h>
#include <esp8266_undocumented.h>

extern int main();

void ICACHE_RAM_ATTR boot_from_something_uart_dwnld(void (**user_start_ptr)())
{
	/* simplified for following condition
	 * (GPI >> 0x10 & 0x07) == 1 and
	 * (GPI >> 0x1D & 0x07) == 6
	 */

	const uint32_t uart_no = 0;
	const uint16_t divlatch = uart_baudrate_detect(uart_no, 0);
	rom_uart_div_modify(uart_no, divlatch);
	UartDwnLdProc((uint8_t*)0x3fffa000, 0x2000, user_start_ptr);
}

[[noreturn]] void ICACHE_RAM_ATTR main_uart_dwnld()
{
	uartAttach();
	Uart_Init(0);
	ets_install_uart_printf(0);

	//SET_PERI_REG_MASK(PERIPHS_DPORT_IRAM_MAPPING, IRAM_UNMAP_40108000 | IRAM_UNMAP_4010C000);
	boot_from_something_uart_dwnld(&user_start_fptr);

	if (user_start_fptr == NULL) {
		if (boot_from_flash() != 0) {
			ets_printf("%s %s \n", "ets_main.c", "181");
			while (true);
		}
	}

	_xtos_set_exception_handler(EXCCAUSE_UNALIGNED, print_fatal_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_ILLEGAL, print_fatal_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_INSTR_ERROR, print_fatal_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_LOAD_STORE_ERROR, print_fatal_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_LOAD_PROHIBITED, print_fatal_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_STORE_PROHIBITED, print_fatal_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_PRIVILEGED, print_fatal_exc_handler);

	if (user_start_fptr) {
		ets_printf("\n\n%p\n", user_start_fptr);
		uint32_t* data = (uint32_t*)0x4010e000;
		for (uint32_t i=0; i<0xD0/4; i++) {
			uint32_t tmp = data[i];
			uint8_t* tmp8 = (uint8_t*)&tmp;
			ets_printf("0x%02x 0x%02x 0x%02x 0x%02x ", tmp8[0], tmp8[1], tmp8[2], tmp8[3]);
		}
		ets_printf("\n\n\n");

		user_start_fptr();
	}

	ets_printf("user code done\n");
	ets_run();
}

static inline uint32_t __rsil_1() {
	uint32_t program_state;
       	asm volatile("rsil %0, 1" : "=r" (program_state));
	return program_state;
}

static inline uint32_t __rsr_prid() {
	uint32_t processor_id;
       	asm volatile("rsr.prid %0" : "=r" (processor_id));
	return processor_id;
}

static inline void __witlb(uint32_t attribute, uint32_t page) {
       	asm volatile("witlb %0, %1" :: "r" (attribute), "r" (page));
}

static inline void __wdtlb(uint32_t attribute, uint32_t page) {
       	asm volatile("wdtlb %0, %1" :: "r" (attribute), "r" (page));
}

static inline void __wsr_intenable(uint32_t interupt_enable) {
       	asm volatile("wsr.intenable %0" :: "r" (interupt_enable));
}

static inline void __wsr_litbase(uint32_t literal_base) {
       	asm volatile("wsr.litbase %0" :: "r" (literal_base));
}

static inline void __wsr_ps(uint32_t program_state) {
       	asm volatile("wsr.ps %0" :: "r" (program_state));
}

static inline void __wsr_vecbase(uint32_t vector_base) {
       	asm volatile("wsr.vecbase %0" :: "r" (vector_base));
}

[[noreturn]] void ICACHE_RAM_ATTR _start_uart_dwnld()
{
	/* Set the program state register
	 * Name				Value	Description
	 * Interupt level disable	0	enable all interrupt levels
	 * Exception mode		0	normal operation
	 * User vector mode		1	user vector mode, exceptions need to switch stacks
	 * Privilege level		0	Set to Ring 0
	 */
	__wsr_ps(0x20);
	asm volatile("rsync");

//	for(uint32_t *p = _sysram_bss; p<_sysram_bss_end; p++) {
//		*p = 0;
//	}

	main_uart_dwnld();

	while (true) {
		/* raise DebugException */
		asm volatile("break 1, 15");
	}
}


[[noreturn]] void ICACHE_RAM_ATTR _ResetHandler_uart_dwnld()
{
	/* disable all level 1 interrupts */
	__wsr_intenable(0);
	/* Clear the literal base to use an offset of 0 for
	 * Load 32-bit PC-Relative(L32R) instructions
	 */
	__wsr_litbase(0);
	asm volatile("rsync");

//	/* in case of ESP8266 always 0 because only one processor core exists */
//	uint32_t processor_id = __rsr_prid();
//	if (_ResetVectorDataPtr != NULL && (processor_id & 0xFF) == 0x00)
//		*_ResetVectorDataPtr = 0;

	/* Set interrupt vector base address to system ROM */
	__wsr_vecbase(0x40000000);
	/* Set interrupt level to 1. Therefore disable interrupts of level 1.
	 * Above levels like level 2,... might still be active if available
	 * on ESP8266.
	 */
	__rsil_1();

	/* Set TLB attributes for instruction access for all pages
	 * START	END		Attrib	Description
	 * 0x00000000	0x1FFFFFFFh	0xF	Illegal
	 * 0x20000000	0x5FFFFFFFh	0x1	RWX, Cache Write-Through
	 * 0x60000000h	0xFFFFFFFFh	0x2	RWX, Bypass Cache
	 */
	uint32_t instruction_page = 0;
	uint32_t page_of_this_func = ((uint32_t)&&critical_page) & 0xe0000000;
	for (uint32_t instruction_attr_list = 0x2222211f;; instruction_attr_list >>= 4) {
		/* 0x400000f3 */
		uint32_t attribute = instruction_attr_list & 0x0F;
critical_page:
		if (instruction_page == page_of_this_func) {
			/* 0x400000e0 */
			/* isync has to be executed immediately when changing
			 * attributes of the page currently executing
			 */
			__witlb(attribute, instruction_page);
			asm volatile("isync");
			instruction_page -= 0xe0000000;
			if (instruction_page < 0x10)
				break;
		} else {
			/* 0x400000f9 */
			__witlb(attribute, instruction_page);
			instruction_page -= 0xe0000000;
			if (instruction_page < 0x10) {
				/* for currently not executed pages, calling isync only
				 * once when done with loop is sufficient
				 */
				asm volatile("isync");
				break;
			}
		}
	}

	/* 0x40000105 */
	/* Set TLB attributes for data access for all pages same as for instructions */
	uint32_t data_page = 0;
	uint32_t data_attr_list = 0x2222211f;
	do {
		__wdtlb(data_attr_list & 0x0F, data_page);
		data_page -= 0xe0000000;
		data_attr_list >>= 4;
	} while (data_page >= 0x10);
	asm volatile("dsync");

//	/* Copy system ROM data to system RAM */
//	const rom_store_table_t* iter = _rom_store_table;
//	if (iter != NULL) {
//		uint32_t* dest;
//		const uint32_t* src;
//		do {
//			while (true) {
//				/* 0x40000124 */
//				dest = iter->dest;
//				uint32_t* dest_end = iter->dest_end;
//				src = iter->src;
//				++iter;
//				if (dest >= dest_end)
//					break;
//
//				/* 0x40000130 */
//				do {
//					*dest = *src;
//					++src;
//					++dest;
//				} while (dest < dest_end);
//			}
//			/* 0x40000140 */
//		} while (dest != NULL || src != NULL);
//	}

	/* 0x40000146 */
	asm volatile("isync");
	_start_uart_dwnld();
}

[[noreturn]] void ICACHE_RAM_ATTR system_restart_core_uart_dwnld()
{
	Wait_SPI_Idle(flashchip);

	Cache_Read_Disable();
	CLEAR_PERI_REG_MASK(PERIPHS_DPORT_IRAM_MAPPING, IRAM_UNMAP_40108000 | IRAM_UNMAP_4010C000);

	_ResetHandler_uart_dwnld();
}

[[noreturn]] void system_restart_local_uart_dwnld()
{
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
	CLEAR_PERI_REG_MASK(PERIPHS_I2C_48, 0x2);

	system_restart_core_uart_dwnld();
}

