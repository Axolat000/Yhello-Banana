/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../kernel/types.h"
#define SPINLOCK_H
typedef struct { int v; } spinlock_t;
#define SPINLOCK_INIT {0}
static uintptr_t spin_lock_irq(spinlock_t* l){(void)l;return 0;}
static void spin_unlock_irq(spinlock_t* l,uintptr_t f){(void)l;(void)f;}
static void spin_lock(spinlock_t* l){(void)l;}
static void spin_unlock(spinlock_t* l){(void)l;}
#include "../../kernel/uspace.c"
extern int printf(const char*,...);
extern void exit(int);
static uint32_t charged;
static int permitted=1,checks;
int umem_vm_charge(umem_t* m,uint32_t n){(void)m;if(!permitted)return 0;charged+=n;return 1;}
#define CHECK(x) do{checks++;if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){
    uvma_t areas[]={{0,64u<<20,3}};
    uspace_t us={0};us.vma=areas;us.nvma=1;
    us.vm_pages=100;us.vm_zpages=3;
    umem_t* budget=(umem_t*)(uintptr_t)1;
    CHECK(uvm_limit(NULL,budget,128u<<20,10)<0);
    CHECK(uvm_limit(&us,NULL,128u<<20,10)<0);
    CHECK(uvm_limit(&us,budget,32u<<20,10)<0);
    CHECK(!charged&&!us.vm_budget);
    permitted=0;
    CHECK(uvm_limit(&us,budget,128u<<20,10)<0);
    CHECK(!charged&&!us.vm_budget&&!us.vm_charged);
    permitted=1;
    CHECK(!uvm_limit(&us,budget,128u<<20,10));
    CHECK(charged==103u*4096&&us.vm_charged==charged);
    CHECK(us.vm_budget==budget&&us.vm_limit==(128u<<20)&&us.vma_limit==10);
    CHECK(uvm_limit(&us,budget,128u<<20,10)<0);
    CHECK(charged==103u*4096);
    uspace_t crowded={0};crowded.nvma=11;crowded.vma=areas;
    CHECK(uvm_limit(&crowded,budget,128u<<20,10)<0);
    printf("PASS: %d preloaded-page quota checks\n",checks);
}
