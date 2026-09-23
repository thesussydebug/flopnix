/* Records faults and recovers when the affected context allows it. */
#include "os.h"
#include "faultring.inc"
#include "panicnet.h"

const PanicMonitor *panic_monitor;


volatile int fault_armed[THR_MAX];
JmpBuf fault_ctx[THR_MAX];
u32 fault_vec, fault_err, fault_eip;
u32 fault_cr2;
u32 fault_recoveries;
u8 fault_fallback[THR_MAX];

void fault_notice(void)
{
    if (!fault_fallback[thr_self]) return;
    char msg[64];
    kfmt(msg, sizeof msg, "P%u %s - Attempting a fallback.", fault_vec,
         fault_fallback[thr_self] & 1 ? "gdi.kx" : "g3d.kx");
    klog(msg); klog("\n"); ktrace(msg);
    extern void fault_show_banner(const char *msg);
    fault_show_banner(msg);
}

FaultRec fault_hist[FAULT_HIST];

int fault_count(void) { return fault_ring_count(fault_recoveries); }

const FaultRec *fault_get(int i)
{
    int s = fault_ring_slot(i, fault_recoveries);
    return s < 0 ? 0 : &fault_hist[s];
}

static void fault_record(u32 vec, u32 err, u32 eip)
{
    FaultRec *r = &fault_hist[fault_recoveries % FAULT_HIST];
    memset(r,0,sizeof *r);
    r->sequence=fault_recoveries+1;
    r->cr2=fault_cr2;
    r->vec  = vec;
    r->err  = err;
    r->eip  = eip;
    r->tick = ticks;
    fault_snapshot(r);
    char tm[192];
    kfmt(tm, sizeof tm, "RECOVERED P%u eip %x cr2 %x err %x at %s",
         vec, eip, fault_cr2, err, r->location);
    ktrace(tm);
}

void fault_record_hang(const char *who)
{
    FaultRec *r = &fault_hist[fault_recoveries % FAULT_HIST];
    memset(r,0,sizeof *r);
    r->sequence=fault_recoveries+1;
    r->vec  = FAULT_VEC_HANG;
    r->tick = ticks;
    strlcpy(r->owner,who?who:"",sizeof r->owner);
    fault_vec = FAULT_VEC_HANG;
    fault_err = 0;
    fault_eip = 0;
    fault_fallback[thr_self] = 0;
    char tm[64];
    kfmt(tm, sizeof tm, "HANG ended in %s", r->owner);
    ktrace(tm);
    fault_recoveries++;
}

void fault_handle(const u32 *frame)
{
    u32 vec=frame[8],err=frame[9],eip=frame[10];
    if (panic_active || vec==8 || thr_self<0 || thr_self>=THR_MAX)
        emergency_enter(vec,err,eip,vec==14 ? page_fault_addr() : 0,vec==8 ? EM_DOUBLE : EM_REPORT);

    fault_cr2 = (vec == 14) ? page_fault_addr() : 0;

    if (vec == 14) {
        in_irq++;
        int fixed = kext_fix_window(eip, fault_cr2);
        in_irq--;
        if (fixed) return;
    }

    if (ring3_fault(vec, err)) return;
    int ft = thr_self;
    if (fault_armed[ft]) {
        fault_armed[ft] = 0;
        fault_vec = vec;
        fault_err = err;
        fault_eip = eip;
        fault_record(vec, err, eip);
        fault_recoveries++;
        fault_fallback[ft] = (u8)kext_graphics_fault(vec, eip, fault_cr2);
        busy_end();

        in_irq = 0;

        fj_long(&fault_ctx[ft], (int)vec + 1);
    }
    if(panic_monitor)panic_monitor->capture(frame);
    panic(vec, err, eip);
}
