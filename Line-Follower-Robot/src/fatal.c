/*
 * Thay handler mặc định của Zephyr (treo máy, motor giữ nguyên lệnh cuối):
 * cắt motor rồi reset. Có trong mọi bản build.
 */

#include <zephyr/fatal.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>

#include "motor_control.h"

LOG_MODULE_REGISTER(fatal_handler, LOG_LEVEL_ERR);

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	motor_emergency_cut();
	LOG_PANIC();
	LOG_ERR("[SAFETY] fatal error %u; rebooting", reason);
	sys_reboot(SYS_REBOOT_COLD);
	CODE_UNREACHABLE;
}
