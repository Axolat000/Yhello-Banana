// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic ACPI laptop + UCSI PPM. Does not emulate physical PD electronics.
// QEMU: -object memory-backend-file,id=ucsimem,size=4096,mem-path=...,share=on
//       -device ivshmem-plain,memdev=ucsimem,addr=0x4
DefinitionBlock ("", "SSDT", 2, "BANANA", "PWRTEST", 1)
{
    External (\_SB.PCI0, DeviceObj)
    Scope (\_SB)
    {
        Device (BATX)
        {
            Name (_HID, EisaId ("PNP0C0A"))
            Name (_UID, 99)
            Method (_STA, 0) { Return (0x1F) }
            Method (_BIF, 0) {
                Return (Package () {0,50000,40000,1,12000,4000,2000,1,1,
                                    "TestBattery","123456","LiIon","BananaTest"})
            }
            Method (_BST, 0) { Return (Package () {1,10000,20000,12000}) }
        }
        Device (ACPX)
        {
            Name (_HID, "ACPI0003")
            Name (_UID, 99)
            Name (_PSR, One)
        }
        Device (LIDX)
        {
            Name (_HID, EisaId ("PNP0C0D"))
            Method (_LID, 0) { Return (One) }
        }
        Device (PNLX)
        {
            Name (_HID, "BAN0001")
            Name (BVAL, 60)
            Method (_BCL, 0) { Return (Package () {80,40,0,20,40,60,80,100}) }
            Method (_BCM, 1) { Store (Arg0, BVAL) }
            Method (_BQC, 0) { Return (BVAL) }
        }
    }
    Scope (\_SB.PCI0)
    {
        Device (USHW)
        {
            Name (_ADR, 0x00040000)
            OperationRegion (PCFG, PCI_Config, Zero, 0x100)
            Field (PCFG, DWordAcc, NoLock, Preserve) {
                Offset (0x18), BAR2, 32, BAR3, 32
            }
            Device (USCX) {
            Name (_HID, "USBC000")
            Name (CRSB, ResourceTemplate () {
                Memory32Fixed (ReadWrite, 0, 0x1000)
            })
            Name (QRSB, ResourceTemplate () {
                QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                             NonCacheable, ReadWrite, 0, 0, 0xFFF, 0, 0x1000)
            })
            Method (BASE, 0) {
                If (BAR3) { Return (Or (ShiftLeft (BAR3,32),And (BAR2,0xFFFFFFF0))) }
                Return (And (BAR2,0xFFFFFFF0))
            }
            Method (_CRS, 0, Serialized) {
                If (BAR3) {
                    CreateQWordField (QRSB,14,QMIN)
                    CreateQWordField (QRSB,22,QMAX)
                    Store (BASE (),QMIN)
                    Store (Add (BASE (),0xFFF),QMAX)
                    Return (QRSB)
                }
                CreateDWordField (CRSB, 4, ADDR)
                Store (And (BAR2, 0xFFFFFFF0), ADDR)
                Return (CRSB)
            }
            Name (SOUR, Zero)
            OperationRegion (MBOX, SystemMemory, BASE (), 0x1000)
            Field (MBOX, AnyAcc, NoLock, Preserve) {
                VERS,16, Offset (4), CCI0,32, CTRL,32, CTHI,32,
                MSG0,32, MSG1,32, MSG2,32, MSG3,32
            }
            Method (_INI, 0) { Store (0x0120,VERS) }
            Method (_DSM, 4, Serialized) {
                If (LEqual (Arg2, Zero)) { Return (Buffer () {7}) }
                If (LEqual (Arg2, 2)) { Return (Zero) }
                If (LNotEqual (Arg2, One)) { Return (Zero) }
                Store (And (CTRL,255),Local0)
                Store (Zero,MSG0)
                Store (Zero,MSG1)
                Store (Zero,MSG2)
                Store (Zero,MSG3)
                If (LEqual (Local0,1)) { Store (0x08000000,CCI0) }
                ElseIf (LEqual (Local0,2)) { Store (0x04000000,CCI0) }
                ElseIf (LEqual (Local0,4)) { Store (0x20000000,CCI0) }
                ElseIf (LEqual (Local0,5)) { Store (0x80000000,CCI0) }
                ElseIf (LEqual (Local0,6)) {
                    Store (6,MSG0)
                    Store (0x1001,MSG1)
                    Store (0x01200000,MSG2)
                    Store (0x01200300,MSG3)
                    Store (0x80001000,CCI0)
                }
                ElseIf (LEqual (Local0,7)) {
                    Store (0x3364,MSG0)
                    Store (0x80000200,CCI0)
                }
                ElseIf (LEqual (Local0,18)) {
                    Store (0x200B4000,MSG0)
                    If (SOUR) { Or (MSG0,0x00100000,MSG0) }
                    Store (0x2004B12C,MSG1)
                    Store (One,MSG2)
                    Store (0x80000900,CCI0)
                }
                ElseIf (LEqual (Local0,16)) {
                    If (LEqual (And (ShiftRight (CTRL,24),255),Zero)) {
                        Store (0x0001912C,MSG0)
                        Store (0x0006412C,MSG1)
                        Store (0x80000800,CCI0)
                    } Else { Store (0x80000000,CCI0) }
                }
                ElseIf (LEqual (Local0,11)) {
                    Store (LEqual (And (ShiftRight (CTRL,23),3),One),SOUR)
                    Store (0x80000000,CCI0)
                }
                Else { Store (0x82000000,CCI0) }
                Return (Zero)
            }
        }
    }
    }
}
