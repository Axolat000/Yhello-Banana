#include "i2chid.h"
#include "terminal.h"
#include "keyboard.h"
#include "sysinfo.h"
#include "kstring.h"
#include "task.h"
#include "timer.h"
#include "rtc.h"
#include "usb.h"
#include "daemon.h"
#include "types.h"
#include "gui.h"
#include "browser.h"
#include "fb.h"
#include "gpu.h"
#include "gfx.h"
#include "idt.h"
#include "serial.h"
#include "kheap.h"
#include "random.h"
#include "../net/net.h"
#include "../usb/usbcore.h"
#include "blockdev.h"
#include "audio.h"
#include "nvme.h"
#include "applesmc.h"
#include "sdhci.h"
#include "acpi.h"
#include "virtio_blk.h"
#include "paging.h"
#include "fpu.h"
#include "tlb.h"
#include "hmem.h"
#include "smp.h"
#include "splash.h"
#include "../shell/shell.h"
#include "font.h"

#define MULTIBOOT2_MAGIC 0x36D76289

/* a boot step, in the log (and on the screen with "verbose") */
#define STEP(what) klog("boot: %s\n", what)

/* The x87 FPU (the browser's JavaScript and httpd's PHP compute with it -
 * the kernel is built without SSE), SSE and, where the processor has it,
 * AVX for apps: kernel/fpu.c (no emulation, no lazy switching, FPU errors
 * reported natively, every exception masked). */
static void fpu_init(void) { fpu_cpu_init(); }

void kernel_main(uint32_t magic, uint32_t mb_info) {
    /* Install exception handlers before anything else: a fault before
     * this point is unrecoverable anyway, but once the IDT is live, any
     * CPU exception paints a readable panic screen instead of silently
     * triple-faulting into a VM reset. */
    idt_init();
    fpu_init();
    tlb_cpu_init();           /* the kernel's pages global, PCIDs where there are (kernel/tlb.c) */

    serial_init();
    klog("Banana OS 0.6 booting\n");

    /* Parse Multiboot2 info first so the console can choose framebuffer mode. */
    if (magic == MULTIBOOT2_MAGIC) {
        fb_init_multiboot2(mb_info);
        gfx_init();
        sysinfo_init_mb2(mb_info);
        sysinfo_log_boot();
    }
    /* After every Multiboot2 consumer above: the heap may reuse that memory. */
    kheap_init(magic == MULTIBOOT2_MAGIC ? mb_info : 0);
    klog("heap: %u KiB free\n", kheap_total_bytes() / 1024u);
    paging_guard_null();      /* NULL pointers fault (64-bit), after every Multiboot2 reader */
    hmem_init(magic == MULTIBOOT2_MAGIC ? mb_info : 0);   /* RAM above 4 GiB, for apps (64-bit) */
    gpu_init();               /* graphics drivers: one may set the screen up (kernel/gpu.h) */
    paging_pat_init();        /* write-combining (64-bit): a framebuffer without a driver gets it */
    gpu_firmware_init();      /* AMD, NVIDIA, Intel Arc / Xe without a driver: named, faster screen */

    terminal_init();
    /* "verbose" (GRUB's "boot messages" entry): no boot screen, every step on
     * the screen - so a computer that stops during the boot shows where */
    int verbose = strstr(sysinfo_get()->cmdline, "verbose") != NULL;
    if (verbose) klog_to_screen(1);
    STEP("timer");
    timer_init();
    __asm__ volatile("sti");  /* timer IRQ from here on */
    if (!verbose) splash_start();   /* the boot screen until the system is ready */
    STEP("clock");
    rtc_init();
    web_init();       /* clock for JavaScript Date / PHP date() */
    STEP("fonts");
    fonts_init();     /* the built-in TrueType fonts (DejaVu) */
    STEP("tasks");
    task_init("banana-sh");   /* boot stack becomes the shell's real thread */
    task_start_sysmon();      /* real background stats-sampling thread */
    gpu_start();              /* (virtual GPUs: what is drawn is pushed to the screen) */
    STEP("processor cores");
    smp_init();               /* the other processor cores: they run app code (64-bit) */
    STEP("usb (legacy handoff)");
    usb_init();       /* xHCI legacy handoff → USB keyboards work via PS/2 */
    STEP("mouse, keyboard");
    mouse_init();     /* enable PS/2 AUX port for USB/PS2 mice */
    i2chid_init();    /* I2C touchpads (laptops since ~2015), when ACPI lists one */
    keyboard_init();  /* drain buffer after USB init */
    random_init();
    STEP("network");
    net_init();       /* NIC probe + netd task; DHCP runs in the background */
    STEP("sound");
    audio_init();     /* HD Audio / AC'97 sound card (PC speaker otherwise) */
    blockdev_init();  /* mounts USB sticks (FAT32) under /mnt once they show up */
    STEP("nvme");
    nvme_init();      /* NVMe SSDs: disks for install/sync, FAT32 partitions in /mnt/nvme */
    virtio_blk_init();  /* virtio disks (QEMU/KVM, Proxmox, clouds): the same, in /mnt/virtio */
    sdhci_init();     /* SD card readers: cards mounted in /mnt/sd as they are put in */
    applesmc_init();  /* Intel Macs: the SMC's temperatures and fans */
    acpi_power_button_init();   /* VirtualBox's ACPI Shutdown, QEMU's system_powerdown, the PC's button */
    STEP("usb");
    usb_stack_init(); /* xHCI/EHCI: USB network adapters, keyboards, mice, sticks */
    STEP("services");
    daemon_init();
    gui_init();
    STEP("shell");

    if (magic != MULTIBOOT2_MAGIC) {
        terminal_write_color(
            "ERROR: Not loaded by a Multiboot2 bootloader!\n",
            VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        while (1) __asm__("hlt");
    }

    shell_run();
}
