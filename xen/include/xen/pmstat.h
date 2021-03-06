#ifndef __XEN_PMSTAT_H_
#define __XEN_PMSTAT_H_

#include <xen/types.h>
#include <public/platform.h> /* for struct xen_processor_power */
#include <public/sysctl.h>   /* for struct pm_cx_stat */

int set_px_pminfo(uint32_t cpu, struct xen_processor_performance *perf);
#ifdef CONFIG_ACPI /* or CONFIG_X86 ? */
long set_cx_pminfo(uint32_t cpu, struct xen_processor_power *power);
uint32_t pmstat_get_cx_nr(uint32_t cpuid);
int pmstat_get_cx_stat(uint32_t cpuid, struct pm_cx_stat *stat);
int pmstat_reset_cx_stat(uint32_t cpuid);
#else
static inline long set_cx_pminfo(uint32_t cpu, struct xen_processor_power *power) { return 0; }
static inline uint32_t pmstat_get_cx_nr(uint32_t cpuid) { return 0; }
static inline int pmstat_get_cx_stat(uint32_t cpuid, struct pm_cx_stat *stat) { return 0; }
static inline int pmstat_reset_cx_stat(uint32_t cpuid) { return 0; }
#endif

int do_get_pm_info(struct xen_sysctl_get_pmstat *op);
int do_pm_op(struct xen_sysctl_pm_op *op);

#endif /* __XEN_PMSTAT_H_ */
