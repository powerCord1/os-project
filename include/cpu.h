#include <stdbool.h>
#include <stdint.h>

#define cpu_pause() __asm__ volatile("pause")

// CPUID leaves
#define CPUID_LEAF_VENDOR_ID 0
#define CPUID_LEAF_APIC 1

// Vendor strings from CPUs.
#define CPUID_VENDOR_AMD "AuthenticAMD"
#define CPUID_VENDOR_AMD_OLD "AMDisbetter!"
#define CPUID_VENDOR_INTEL "GenuineIntel"
#define CPUID_VENDOR_VIA "VIA VIA VIA "
#define CPUID_VENDOR_TRANSMETA "GenuineTMx86"
#define CPUID_VENDOR_TRANSMETA_OLD "TransmetaCPU"
#define CPUID_VENDOR_CYRIX "CyrixInstead"
#define CPUID_VENDOR_CENTAUR "CentaurHauls"
#define CPUID_VENDOR_NEXGEN "NexGenDriven"
#define CPUID_VENDOR_UMC "UMC UMC UMC "
#define CPUID_VENDOR_SIS "SiS SiS SiS "
#define CPUID_VENDOR_NSC "Geode by NSC"
#define CPUID_VENDOR_RISE "RiseRiseRise"
#define CPUID_VENDOR_VORTEX "Vortex86 SoC"
#define CPUID_VENDOR_AO486 "MiSTer AO486"
#define CPUID_VENDOR_AO486_OLD "GenuineAO486"
#define CPUID_VENDOR_ZHAOXIN "  Shanghai  "
#define CPUID_VENDOR_HYGON "HygonGenuine"
#define CPUID_VENDOR_ELBRUS "E2K MACHINE "

// Vendor strings from hypervisors.
#define CPUID_VENDOR_QEMU "TCGTCGTCGTCG"
#define CPUID_VENDOR_KVM " KVMKVMKVM  "
#define CPUID_VENDOR_VMWARE "VMwareVMware"
#define CPUID_VENDOR_VIRTUALBOX "VBoxVBoxVBox"
#define CPUID_VENDOR_XEN "XenVMMXenVMM"
#define CPUID_VENDOR_HYPERV "Microsoft Hv"
#define CPUID_VENDOR_PARALLELS " prl hyperv "
#define CPUID_VENDOR_PARALLELS_ALT                                             \
    " lrpepyh vr " // Sometimes Parallels incorrectly encodes "prl hyperv" as
                   // "lrpepyh vr" due to an endianness mismatch.
#define CPUID_VENDOR_BHYVE "bhyve bhyve "
#define CPUID_VENDOR_QNX " QNXQVMBSQG "

// RFLAGS
typedef union {
    uint64_t raw;
    struct {
        uint64_t cf : 1; // Carry flag
        uint64_t reserved1 : 1;
        uint64_t pf : 1; // Parity flag
        uint64_t reserved2 : 1;
        uint64_t af : 1; // Auxiliary carry flag
        uint64_t reserved3 : 1;
        uint64_t zf : 1;   // Zero flag
        uint64_t sf : 1;   // Sign flag
        uint64_t tf : 1;   // Trap flag
        uint64_t if_ : 1;  // Interrupt enable flag
        uint64_t df : 1;   // Direction flag
        uint64_t of : 1;   // Overflow flag
        uint64_t iopl : 2; // I/O privilege level
        uint64_t nt : 1;   // Nested task
        uint64_t reserved4 : 1;
        uint64_t rf : 1;  // Resume flag
        uint64_t vm : 1;  // Virtual 8086 mode
        uint64_t ac : 1;  // Alignment check
        uint64_t vif : 1; // Virtual interrupt flag
        uint64_t vip : 1; // Virtual interrupt pending
        uint64_t id : 1;  // ID flag
        uint64_t reserved5 : 42;
    } bit_list;
} rflags_t;

// Control registers

typedef union {
    uint64_t raw;
    struct {
        uint64_t pe : 1; // Protection mode enable
        uint64_t mp : 1; // Monitor coprocessor
        uint64_t em : 1; // FPU emulation
        uint64_t ts : 1; // Task switched``
        uint64_t et : 1; // Extension type
        uint64_t ne : 1; // Numeric error
        uint64_t reserved1 : 10;
        uint64_t wp : 1; // Write protect
        uint64_t reserved2 : 1;
        uint64_t am : 1; // Alignment mask
        uint64_t reserved3 : 10;
        uint64_t nw : 1; // Not write-through
        uint64_t cd : 1; // Cache disable
        uint64_t pg : 1; // Paging
        uint64_t reserved4 : 32;
    } bit_list;
} cr0_t;

// CR2 contains the linear address that trigerred a page fault
typedef uint64_t cr2_t;

// CR3 contains the physical address of the highest level paging structure
typedef uint64_t cr3_t;

typedef union {
    uint64_t raw;
    struct {
        uint64_t vme : 1;    // Virtual-8086 mode extensions
        uint64_t pvi : 1;    // Protected mode virtual interrupts
        uint64_t tsd : 1;    // Timestamp enabled only in ring 0
        uint64_t de : 1;     // Debugging extensions
        uint64_t pse : 1;    // Page size extension
        uint64_t pae : 1;    // Physical address extension
        uint64_t mce : 1;    // Machine check exception enable
        uint64_t pge : 1;    // Page global enable
        uint64_t pce : 1;    // Performance monitoring counter enable
        uint64_t osfxsr : 1; // OS support for FXSAVE and FXRSTOR
        uint64_t osxmmexcpt
            : 1; // OS support for unmasked SIMD floating point exceptions
        uint64_t umip : 1; // User-mode instruction prevention
        uint64_t la57 : 1; // 5 level paging
        uint64_t vmxe : 1; // Virtual machine extensions enable
        uint64_t smxe : 1; // Safer mode extensions enable
        uint64_t reverved1 : 1;
        uint64_t fsgsbase : 1; // Enables the RDFSBASE, RDGSBASE, WRFSBASE and
                               // WRGSBASE instructions
        uint64_t pcide : 1;    // PCID enable
        uint64_t osxsave : 1;  // XSAVE and processor extended states enable
        uint64_t reverved2 : 1;
        uint64_t smep : 1; // Supervisor mode executions protection enable
        uint64_t smap : 1; // Supervisor mode access protection enable
        uint64_t pke : 1;  // Enable protection keys for user-mode pages
        uint64_t cet : 1;  // Enable control-flow enforcement technology
        uint64_t pks : 1;  // Enable protection keys for supervisor-mode pages
        uint64_t reverved3 : 39;
    } bit_list;
} cr4_t;

void halt();
__attribute__((noreturn)) void halt_cf();
char get_cpu_vendor();
void idle();
bool is_pe_enabled();
void cpu_init();
void enable_mce();
void sse_init();
void tsc_init();
uint64_t get_ts();
void enable_a20();
cr0_t get_cr0();
cr2_t get_cr2();
cr3_t get_cr3();
cr4_t get_cr4();
rflags_t get_rflags();
void set_cr0(cr0_t cr0);
void set_cr2(cr2_t cr2);
void set_cr3(cr3_t cr3);
void set_cr4(cr4_t cr4);
void set_rflags(rflags_t rflags);
void get_vendor_id();
bool is_apic_enabled();