
bin/kernel:     file format elf64-littleriscv


Disassembly of section .text:

ffffffffc0200000 <kern_entry>:
    .globl kern_entry
kern_entry:
    # a0: hartid
    # a1: dtb physical address
    # save hartid and dtb address
    la t0, boot_hartid
ffffffffc0200000:	00006297          	auipc	t0,0x6
ffffffffc0200004:	00028293          	mv	t0,t0
    sd a0, 0(t0)
ffffffffc0200008:	00a2b023          	sd	a0,0(t0) # ffffffffc0206000 <boot_hartid>
    la t0, boot_dtb
ffffffffc020000c:	00006297          	auipc	t0,0x6
ffffffffc0200010:	ffc28293          	addi	t0,t0,-4 # ffffffffc0206008 <boot_dtb>
    sd a1, 0(t0)
ffffffffc0200014:	00b2b023          	sd	a1,0(t0)

    # t0 := 三级页表的虚拟地址
    lui     t0, %hi(boot_page_table_sv39)
ffffffffc0200018:	c02052b7          	lui	t0,0xc0205
    # t1 := 0xffffffff40000000 即虚实映射偏移量
    li      t1, 0xffffffffc0000000 - 0x80000000
ffffffffc020001c:	ffd0031b          	addiw	t1,zero,-3
ffffffffc0200020:	037a                	slli	t1,t1,0x1e
    # t0 减去虚实映射偏移量 0xffffffff40000000，变为三级页表的物理地址
    sub     t0, t0, t1
ffffffffc0200022:	406282b3          	sub	t0,t0,t1
    # t0 >>= 12，变为三级页表的物理页号
    srli    t0, t0, 12
ffffffffc0200026:	00c2d293          	srli	t0,t0,0xc

    # t1 := 8 << 60，设置 satp 的 MODE 字段为 Sv39
    li      t1, 8 << 60
ffffffffc020002a:	fff0031b          	addiw	t1,zero,-1
ffffffffc020002e:	137e                	slli	t1,t1,0x3f
    # 将刚才计算出的预设三级页表物理页号附加到 satp 中
    or      t0, t0, t1
ffffffffc0200030:	0062e2b3          	or	t0,t0,t1
    # 将算出的 t0(即新的MODE|页表基址物理页号) 覆盖到 satp 中
    csrw    satp, t0
ffffffffc0200034:	18029073          	csrw	satp,t0
    # 使用 sfence.vma 指令刷新 TLB
    sfence.vma
ffffffffc0200038:	12000073          	sfence.vma
    # 从此，我们给内核搭建出了一个完美的虚拟内存空间！
    #nop # 可能映射的位置有些bug。。插入一个nop
    
    # 我们在虚拟内存空间中：随意将 sp 设置为虚拟地址！
    lui sp, %hi(bootstacktop)
ffffffffc020003c:	c0205137          	lui	sp,0xc0205

    # 我们在虚拟内存空间中：随意跳转到虚拟地址！
    # 跳转到 kern_init
    lui t0, %hi(kern_init)
ffffffffc0200040:	c02002b7          	lui	t0,0xc0200
    addi t0, t0, %lo(kern_init)
ffffffffc0200044:	0d828293          	addi	t0,t0,216 # ffffffffc02000d8 <kern_init>
    jr t0
ffffffffc0200048:	8282                	jr	t0

ffffffffc020004a <print_kerninfo>:
/* *
 * print_kerninfo - print the information about kernel, including the location
 * of kernel entry, the start addresses of data and text segements, the start
 * address of free memory and how many memory that kernel has used.
 * */
void print_kerninfo(void) {
ffffffffc020004a:	1141                	addi	sp,sp,-16
    extern char etext[], edata[], end[];
    cprintf("Special kernel symbols:\n");
ffffffffc020004c:	00001517          	auipc	a0,0x1
ffffffffc0200050:	7dc50513          	addi	a0,a0,2012 # ffffffffc0201828 <etext+0x4>
void print_kerninfo(void) {
ffffffffc0200054:	e406                	sd	ra,8(sp)
    cprintf("Special kernel symbols:\n");
ffffffffc0200056:	0fa000ef          	jal	ra,ffffffffc0200150 <cprintf>
    cprintf("  entry  0x%016lx (virtual)\n", (uintptr_t)kern_init);
ffffffffc020005a:	00000597          	auipc	a1,0x0
ffffffffc020005e:	07e58593          	addi	a1,a1,126 # ffffffffc02000d8 <kern_init>
ffffffffc0200062:	00001517          	auipc	a0,0x1
ffffffffc0200066:	7e650513          	addi	a0,a0,2022 # ffffffffc0201848 <etext+0x24>
ffffffffc020006a:	0e6000ef          	jal	ra,ffffffffc0200150 <cprintf>
    cprintf("  etext  0x%016lx (virtual)\n", etext);
ffffffffc020006e:	00001597          	auipc	a1,0x1
ffffffffc0200072:	7b658593          	addi	a1,a1,1974 # ffffffffc0201824 <etext>
ffffffffc0200076:	00001517          	auipc	a0,0x1
ffffffffc020007a:	7f250513          	addi	a0,a0,2034 # ffffffffc0201868 <etext+0x44>
ffffffffc020007e:	0d2000ef          	jal	ra,ffffffffc0200150 <cprintf>
    cprintf("  edata  0x%016lx (virtual)\n", edata);
ffffffffc0200082:	00006597          	auipc	a1,0x6
ffffffffc0200086:	f9658593          	addi	a1,a1,-106 # ffffffffc0206018 <free_area>
ffffffffc020008a:	00001517          	auipc	a0,0x1
ffffffffc020008e:	7fe50513          	addi	a0,a0,2046 # ffffffffc0201888 <etext+0x64>
ffffffffc0200092:	0be000ef          	jal	ra,ffffffffc0200150 <cprintf>
    cprintf("  end    0x%016lx (virtual)\n", end);
ffffffffc0200096:	00006597          	auipc	a1,0x6
ffffffffc020009a:	1c258593          	addi	a1,a1,450 # ffffffffc0206258 <end>
ffffffffc020009e:	00002517          	auipc	a0,0x2
ffffffffc02000a2:	80a50513          	addi	a0,a0,-2038 # ffffffffc02018a8 <etext+0x84>
ffffffffc02000a6:	0aa000ef          	jal	ra,ffffffffc0200150 <cprintf>
    cprintf("Kernel executable memory footprint: %dKB\n",
            (end - (char*)kern_init + 1023) / 1024);
ffffffffc02000aa:	00006597          	auipc	a1,0x6
ffffffffc02000ae:	5ad58593          	addi	a1,a1,1453 # ffffffffc0206657 <end+0x3ff>
ffffffffc02000b2:	00000797          	auipc	a5,0x0
ffffffffc02000b6:	02678793          	addi	a5,a5,38 # ffffffffc02000d8 <kern_init>
ffffffffc02000ba:	40f587b3          	sub	a5,a1,a5
    cprintf("Kernel executable memory footprint: %dKB\n",
ffffffffc02000be:	43f7d593          	srai	a1,a5,0x3f
}
ffffffffc02000c2:	60a2                	ld	ra,8(sp)
    cprintf("Kernel executable memory footprint: %dKB\n",
ffffffffc02000c4:	3ff5f593          	andi	a1,a1,1023
ffffffffc02000c8:	95be                	add	a1,a1,a5
ffffffffc02000ca:	85a9                	srai	a1,a1,0xa
ffffffffc02000cc:	00001517          	auipc	a0,0x1
ffffffffc02000d0:	7fc50513          	addi	a0,a0,2044 # ffffffffc02018c8 <etext+0xa4>
}
ffffffffc02000d4:	0141                	addi	sp,sp,16
    cprintf("Kernel executable memory footprint: %dKB\n",
ffffffffc02000d6:	a8ad                	j	ffffffffc0200150 <cprintf>

ffffffffc02000d8 <kern_init>:

int kern_init(void) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);
ffffffffc02000d8:	00006517          	auipc	a0,0x6
ffffffffc02000dc:	f4050513          	addi	a0,a0,-192 # ffffffffc0206018 <free_area>
ffffffffc02000e0:	00006617          	auipc	a2,0x6
ffffffffc02000e4:	17860613          	addi	a2,a2,376 # ffffffffc0206258 <end>
int kern_init(void) {
ffffffffc02000e8:	1141                	addi	sp,sp,-16
    memset(edata, 0, end - edata);
ffffffffc02000ea:	8e09                	sub	a2,a2,a0
ffffffffc02000ec:	4581                	li	a1,0
int kern_init(void) {
ffffffffc02000ee:	e406                	sd	ra,8(sp)
    memset(edata, 0, end - edata);
ffffffffc02000f0:	722010ef          	jal	ra,ffffffffc0201812 <memset>
    dtb_init();
ffffffffc02000f4:	130000ef          	jal	ra,ffffffffc0200224 <dtb_init>
    cons_init();  // init the console
ffffffffc02000f8:	122000ef          	jal	ra,ffffffffc020021a <cons_init>
    const char *message = "(THU.CST) os is loading ...\0";
    //cprintf("%s\n\n", message);
    cputs(message);
ffffffffc02000fc:	00001517          	auipc	a0,0x1
ffffffffc0200100:	7fc50513          	addi	a0,a0,2044 # ffffffffc02018f8 <etext+0xd4>
ffffffffc0200104:	082000ef          	jal	ra,ffffffffc0200186 <cputs>

    print_kerninfo();
ffffffffc0200108:	f43ff0ef          	jal	ra,ffffffffc020004a <print_kerninfo>

    // grade_backtrace();
    pmm_init();  // init physical memory management
ffffffffc020010c:	67b000ef          	jal	ra,ffffffffc0200f86 <pmm_init>

    slub_init();  // init slub object allocator (lab2 challenge)
ffffffffc0200110:	22a010ef          	jal	ra,ffffffffc020133a <slub_init>
#ifdef ucore_test
    slub_check();  // run slub behavior tests (lab2 challenge, test build only)
#endif

    /* do nothing */
    while (1)
ffffffffc0200114:	a001                	j	ffffffffc0200114 <kern_init+0x3c>

ffffffffc0200116 <cputch>:
/* *
 * cputch - writes a single character @c to stdout, and it will
 * increace the value of counter pointed by @cnt.
 * */
static void
cputch(int c, int *cnt) {
ffffffffc0200116:	1141                	addi	sp,sp,-16
ffffffffc0200118:	e022                	sd	s0,0(sp)
ffffffffc020011a:	e406                	sd	ra,8(sp)
ffffffffc020011c:	842e                	mv	s0,a1
    cons_putc(c);
ffffffffc020011e:	0fe000ef          	jal	ra,ffffffffc020021c <cons_putc>
    (*cnt) ++;
ffffffffc0200122:	401c                	lw	a5,0(s0)
}
ffffffffc0200124:	60a2                	ld	ra,8(sp)
    (*cnt) ++;
ffffffffc0200126:	2785                	addiw	a5,a5,1
ffffffffc0200128:	c01c                	sw	a5,0(s0)
}
ffffffffc020012a:	6402                	ld	s0,0(sp)
ffffffffc020012c:	0141                	addi	sp,sp,16
ffffffffc020012e:	8082                	ret

ffffffffc0200130 <vcprintf>:
 *
 * Call this function if you are already dealing with a va_list.
 * Or you probably want cprintf() instead.
 * */
int
vcprintf(const char *fmt, va_list ap) {
ffffffffc0200130:	1101                	addi	sp,sp,-32
ffffffffc0200132:	862a                	mv	a2,a0
ffffffffc0200134:	86ae                	mv	a3,a1
    int cnt = 0;
    vprintfmt((void*)cputch, &cnt, fmt, ap);
ffffffffc0200136:	00000517          	auipc	a0,0x0
ffffffffc020013a:	fe050513          	addi	a0,a0,-32 # ffffffffc0200116 <cputch>
ffffffffc020013e:	006c                	addi	a1,sp,12
vcprintf(const char *fmt, va_list ap) {
ffffffffc0200140:	ec06                	sd	ra,24(sp)
    int cnt = 0;
ffffffffc0200142:	c602                	sw	zero,12(sp)
    vprintfmt((void*)cputch, &cnt, fmt, ap);
ffffffffc0200144:	2b8010ef          	jal	ra,ffffffffc02013fc <vprintfmt>
    return cnt;
}
ffffffffc0200148:	60e2                	ld	ra,24(sp)
ffffffffc020014a:	4532                	lw	a0,12(sp)
ffffffffc020014c:	6105                	addi	sp,sp,32
ffffffffc020014e:	8082                	ret

ffffffffc0200150 <cprintf>:
 *
 * The return value is the number of characters which would be
 * written to stdout.
 * */
int
cprintf(const char *fmt, ...) {
ffffffffc0200150:	711d                	addi	sp,sp,-96
    va_list ap;
    int cnt;
    va_start(ap, fmt);
ffffffffc0200152:	02810313          	addi	t1,sp,40 # ffffffffc0205028 <boot_page_table_sv39+0x28>
cprintf(const char *fmt, ...) {
ffffffffc0200156:	8e2a                	mv	t3,a0
ffffffffc0200158:	f42e                	sd	a1,40(sp)
ffffffffc020015a:	f832                	sd	a2,48(sp)
ffffffffc020015c:	fc36                	sd	a3,56(sp)
    vprintfmt((void*)cputch, &cnt, fmt, ap);
ffffffffc020015e:	00000517          	auipc	a0,0x0
ffffffffc0200162:	fb850513          	addi	a0,a0,-72 # ffffffffc0200116 <cputch>
ffffffffc0200166:	004c                	addi	a1,sp,4
ffffffffc0200168:	869a                	mv	a3,t1
ffffffffc020016a:	8672                	mv	a2,t3
cprintf(const char *fmt, ...) {
ffffffffc020016c:	ec06                	sd	ra,24(sp)
ffffffffc020016e:	e0ba                	sd	a4,64(sp)
ffffffffc0200170:	e4be                	sd	a5,72(sp)
ffffffffc0200172:	e8c2                	sd	a6,80(sp)
ffffffffc0200174:	ecc6                	sd	a7,88(sp)
    va_start(ap, fmt);
ffffffffc0200176:	e41a                	sd	t1,8(sp)
    int cnt = 0;
ffffffffc0200178:	c202                	sw	zero,4(sp)
    vprintfmt((void*)cputch, &cnt, fmt, ap);
ffffffffc020017a:	282010ef          	jal	ra,ffffffffc02013fc <vprintfmt>
    cnt = vcprintf(fmt, ap);
    va_end(ap);
    return cnt;
}
ffffffffc020017e:	60e2                	ld	ra,24(sp)
ffffffffc0200180:	4512                	lw	a0,4(sp)
ffffffffc0200182:	6125                	addi	sp,sp,96
ffffffffc0200184:	8082                	ret

ffffffffc0200186 <cputs>:
/* *
 * cputs- writes the string pointed by @str to stdout and
 * appends a newline character.
 * */
int
cputs(const char *str) {
ffffffffc0200186:	1101                	addi	sp,sp,-32
ffffffffc0200188:	e822                	sd	s0,16(sp)
ffffffffc020018a:	ec06                	sd	ra,24(sp)
ffffffffc020018c:	e426                	sd	s1,8(sp)
ffffffffc020018e:	842a                	mv	s0,a0
    int cnt = 0;
    char c;
    while ((c = *str ++) != '\0') {
ffffffffc0200190:	00054503          	lbu	a0,0(a0)
ffffffffc0200194:	c51d                	beqz	a0,ffffffffc02001c2 <cputs+0x3c>
ffffffffc0200196:	0405                	addi	s0,s0,1
ffffffffc0200198:	4485                	li	s1,1
ffffffffc020019a:	9c81                	subw	s1,s1,s0
    cons_putc(c);
ffffffffc020019c:	080000ef          	jal	ra,ffffffffc020021c <cons_putc>
    while ((c = *str ++) != '\0') {
ffffffffc02001a0:	00044503          	lbu	a0,0(s0)
ffffffffc02001a4:	008487bb          	addw	a5,s1,s0
ffffffffc02001a8:	0405                	addi	s0,s0,1
ffffffffc02001aa:	f96d                	bnez	a0,ffffffffc020019c <cputs+0x16>
    (*cnt) ++;
ffffffffc02001ac:	0017841b          	addiw	s0,a5,1
    cons_putc(c);
ffffffffc02001b0:	4529                	li	a0,10
ffffffffc02001b2:	06a000ef          	jal	ra,ffffffffc020021c <cons_putc>
        cputch(c, &cnt);
    }
    cputch('\n', &cnt);
    return cnt;
}
ffffffffc02001b6:	60e2                	ld	ra,24(sp)
ffffffffc02001b8:	8522                	mv	a0,s0
ffffffffc02001ba:	6442                	ld	s0,16(sp)
ffffffffc02001bc:	64a2                	ld	s1,8(sp)
ffffffffc02001be:	6105                	addi	sp,sp,32
ffffffffc02001c0:	8082                	ret
    while ((c = *str ++) != '\0') {
ffffffffc02001c2:	4405                	li	s0,1
ffffffffc02001c4:	b7f5                	j	ffffffffc02001b0 <cputs+0x2a>

ffffffffc02001c6 <__panic>:
 * __panic - __panic is called on unresolvable fatal errors. it prints
 * "panic: 'message'", and then enters the kernel monitor.
 * */
void
__panic(const char *file, int line, const char *fmt, ...) {
    if (is_panic) {
ffffffffc02001c6:	00006317          	auipc	t1,0x6
ffffffffc02001ca:	04a30313          	addi	t1,t1,74 # ffffffffc0206210 <is_panic>
ffffffffc02001ce:	00032e03          	lw	t3,0(t1)
__panic(const char *file, int line, const char *fmt, ...) {
ffffffffc02001d2:	715d                	addi	sp,sp,-80
ffffffffc02001d4:	ec06                	sd	ra,24(sp)
ffffffffc02001d6:	e822                	sd	s0,16(sp)
ffffffffc02001d8:	f436                	sd	a3,40(sp)
ffffffffc02001da:	f83a                	sd	a4,48(sp)
ffffffffc02001dc:	fc3e                	sd	a5,56(sp)
ffffffffc02001de:	e0c2                	sd	a6,64(sp)
ffffffffc02001e0:	e4c6                	sd	a7,72(sp)
    if (is_panic) {
ffffffffc02001e2:	000e0363          	beqz	t3,ffffffffc02001e8 <__panic+0x22>
    vcprintf(fmt, ap);
    cprintf("\n");
    va_end(ap);

panic_dead:
    while (1) {
ffffffffc02001e6:	a001                	j	ffffffffc02001e6 <__panic+0x20>
    is_panic = 1;
ffffffffc02001e8:	4785                	li	a5,1
ffffffffc02001ea:	00f32023          	sw	a5,0(t1)
    va_start(ap, fmt);
ffffffffc02001ee:	8432                	mv	s0,a2
ffffffffc02001f0:	103c                	addi	a5,sp,40
    cprintf("kernel panic at %s:%d:\n    ", file, line);
ffffffffc02001f2:	862e                	mv	a2,a1
ffffffffc02001f4:	85aa                	mv	a1,a0
ffffffffc02001f6:	00001517          	auipc	a0,0x1
ffffffffc02001fa:	72250513          	addi	a0,a0,1826 # ffffffffc0201918 <etext+0xf4>
    va_start(ap, fmt);
ffffffffc02001fe:	e43e                	sd	a5,8(sp)
    cprintf("kernel panic at %s:%d:\n    ", file, line);
ffffffffc0200200:	f51ff0ef          	jal	ra,ffffffffc0200150 <cprintf>
    vcprintf(fmt, ap);
ffffffffc0200204:	65a2                	ld	a1,8(sp)
ffffffffc0200206:	8522                	mv	a0,s0
ffffffffc0200208:	f29ff0ef          	jal	ra,ffffffffc0200130 <vcprintf>
    cprintf("\n");
ffffffffc020020c:	00001517          	auipc	a0,0x1
ffffffffc0200210:	6e450513          	addi	a0,a0,1764 # ffffffffc02018f0 <etext+0xcc>
ffffffffc0200214:	f3dff0ef          	jal	ra,ffffffffc0200150 <cprintf>
ffffffffc0200218:	b7f9                	j	ffffffffc02001e6 <__panic+0x20>

ffffffffc020021a <cons_init>:

/* serial_intr - try to feed input characters from serial port */
void serial_intr(void) {}

/* cons_init - initializes the console devices */
void cons_init(void) {}
ffffffffc020021a:	8082                	ret

ffffffffc020021c <cons_putc>:

/* cons_putc - print a single character @c to console devices */
void cons_putc(int c) { sbi_console_putchar((unsigned char)c); }
ffffffffc020021c:	0ff57513          	andi	a0,a0,255
ffffffffc0200220:	55e0106f          	j	ffffffffc020177e <sbi_console_putchar>

ffffffffc0200224 <dtb_init>:

// 保存解析出的系统物理内存信息
static uint64_t memory_base = 0;
static uint64_t memory_size = 0;

void dtb_init(void) {
ffffffffc0200224:	7119                	addi	sp,sp,-128
    cprintf("DTB Init\n");
ffffffffc0200226:	00001517          	auipc	a0,0x1
ffffffffc020022a:	71250513          	addi	a0,a0,1810 # ffffffffc0201938 <etext+0x114>
void dtb_init(void) {
ffffffffc020022e:	fc86                	sd	ra,120(sp)
ffffffffc0200230:	f8a2                	sd	s0,112(sp)
ffffffffc0200232:	e8d2                	sd	s4,80(sp)
ffffffffc0200234:	f4a6                	sd	s1,104(sp)
ffffffffc0200236:	f0ca                	sd	s2,96(sp)
ffffffffc0200238:	ecce                	sd	s3,88(sp)
ffffffffc020023a:	e4d6                	sd	s5,72(sp)
ffffffffc020023c:	e0da                	sd	s6,64(sp)
ffffffffc020023e:	fc5e                	sd	s7,56(sp)
ffffffffc0200240:	f862                	sd	s8,48(sp)
ffffffffc0200242:	f466                	sd	s9,40(sp)
ffffffffc0200244:	f06a                	sd	s10,32(sp)
ffffffffc0200246:	ec6e                	sd	s11,24(sp)
    cprintf("DTB Init\n");
ffffffffc0200248:	f09ff0ef          	jal	ra,ffffffffc0200150 <cprintf>
    cprintf("HartID: %ld\n", boot_hartid);
ffffffffc020024c:	00006597          	auipc	a1,0x6
ffffffffc0200250:	db45b583          	ld	a1,-588(a1) # ffffffffc0206000 <boot_hartid>
ffffffffc0200254:	00001517          	auipc	a0,0x1
ffffffffc0200258:	6f450513          	addi	a0,a0,1780 # ffffffffc0201948 <etext+0x124>
ffffffffc020025c:	ef5ff0ef          	jal	ra,ffffffffc0200150 <cprintf>
    cprintf("DTB Address: 0x%lx\n", boot_dtb);
ffffffffc0200260:	00006417          	auipc	s0,0x6
ffffffffc0200264:	da840413          	addi	s0,s0,-600 # ffffffffc0206008 <boot_dtb>
ffffffffc0200268:	600c                	ld	a1,0(s0)
ffffffffc020026a:	00001517          	auipc	a0,0x1
ffffffffc020026e:	6ee50513          	addi	a0,a0,1774 # ffffffffc0201958 <etext+0x134>
ffffffffc0200272:	edfff0ef          	jal	ra,ffffffffc0200150 <cprintf>
    
    if (boot_dtb == 0) {
ffffffffc0200276:	00043a03          	ld	s4,0(s0)
        cprintf("Error: DTB address is null\n");
ffffffffc020027a:	00001517          	auipc	a0,0x1
ffffffffc020027e:	6f650513          	addi	a0,a0,1782 # ffffffffc0201970 <etext+0x14c>
    if (boot_dtb == 0) {
ffffffffc0200282:	120a0463          	beqz	s4,ffffffffc02003aa <dtb_init+0x186>
        return;
    }
    
    // 转换为虚拟地址
    uintptr_t dtb_vaddr = boot_dtb + PHYSICAL_MEMORY_OFFSET;
ffffffffc0200286:	57f5                	li	a5,-3
ffffffffc0200288:	07fa                	slli	a5,a5,0x1e
ffffffffc020028a:	00fa0733          	add	a4,s4,a5
    const struct fdt_header *header = (const struct fdt_header *)dtb_vaddr;
    
    // 验证DTB
    uint32_t magic = fdt32_to_cpu(header->magic);
ffffffffc020028e:	431c                	lw	a5,0(a4)
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc0200290:	00ff0637          	lui	a2,0xff0
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc0200294:	6b41                	lui	s6,0x10
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc0200296:	0087d59b          	srliw	a1,a5,0x8
ffffffffc020029a:	0187969b          	slliw	a3,a5,0x18
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc020029e:	0187d51b          	srliw	a0,a5,0x18
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc02002a2:	0105959b          	slliw	a1,a1,0x10
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc02002a6:	0107d79b          	srliw	a5,a5,0x10
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc02002aa:	8df1                	and	a1,a1,a2
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc02002ac:	8ec9                	or	a3,a3,a0
ffffffffc02002ae:	0087979b          	slliw	a5,a5,0x8
ffffffffc02002b2:	1b7d                	addi	s6,s6,-1
ffffffffc02002b4:	0167f7b3          	and	a5,a5,s6
ffffffffc02002b8:	8dd5                	or	a1,a1,a3
ffffffffc02002ba:	8ddd                	or	a1,a1,a5
    if (magic != 0xd00dfeed) {
ffffffffc02002bc:	d00e07b7          	lui	a5,0xd00e0
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc02002c0:	2581                	sext.w	a1,a1
    if (magic != 0xd00dfeed) {
ffffffffc02002c2:	eed78793          	addi	a5,a5,-275 # ffffffffd00dfeed <end+0xfed9c95>
ffffffffc02002c6:	10f59163          	bne	a1,a5,ffffffffc02003c8 <dtb_init+0x1a4>
        return;
    }
    
    // 提取内存信息
    uint64_t mem_base, mem_size;
    if (extract_memory_info(dtb_vaddr, header, &mem_base, &mem_size) == 0) {
ffffffffc02002ca:	471c                	lw	a5,8(a4)
ffffffffc02002cc:	4754                	lw	a3,12(a4)
    int in_memory_node = 0;
ffffffffc02002ce:	4c81                	li	s9,0
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc02002d0:	0087d59b          	srliw	a1,a5,0x8
ffffffffc02002d4:	0086d51b          	srliw	a0,a3,0x8
ffffffffc02002d8:	0186941b          	slliw	s0,a3,0x18
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc02002dc:	0186d89b          	srliw	a7,a3,0x18
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc02002e0:	01879a1b          	slliw	s4,a5,0x18
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc02002e4:	0187d81b          	srliw	a6,a5,0x18
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc02002e8:	0105151b          	slliw	a0,a0,0x10
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc02002ec:	0106d69b          	srliw	a3,a3,0x10
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc02002f0:	0105959b          	slliw	a1,a1,0x10
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc02002f4:	0107d79b          	srliw	a5,a5,0x10
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc02002f8:	8d71                	and	a0,a0,a2
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc02002fa:	01146433          	or	s0,s0,a7
ffffffffc02002fe:	0086969b          	slliw	a3,a3,0x8
ffffffffc0200302:	010a6a33          	or	s4,s4,a6
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc0200306:	8e6d                	and	a2,a2,a1
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc0200308:	0087979b          	slliw	a5,a5,0x8
ffffffffc020030c:	8c49                	or	s0,s0,a0
ffffffffc020030e:	0166f6b3          	and	a3,a3,s6
ffffffffc0200312:	00ca6a33          	or	s4,s4,a2
ffffffffc0200316:	0167f7b3          	and	a5,a5,s6
ffffffffc020031a:	8c55                	or	s0,s0,a3
ffffffffc020031c:	00fa6a33          	or	s4,s4,a5
    const char *strings_base = (const char *)(dtb_vaddr + strings_offset);
ffffffffc0200320:	1402                	slli	s0,s0,0x20
    const uint32_t *struct_ptr = (const uint32_t *)(dtb_vaddr + struct_offset);
ffffffffc0200322:	1a02                	slli	s4,s4,0x20
    const char *strings_base = (const char *)(dtb_vaddr + strings_offset);
ffffffffc0200324:	9001                	srli	s0,s0,0x20
    const uint32_t *struct_ptr = (const uint32_t *)(dtb_vaddr + struct_offset);
ffffffffc0200326:	020a5a13          	srli	s4,s4,0x20
    const char *strings_base = (const char *)(dtb_vaddr + strings_offset);
ffffffffc020032a:	943a                	add	s0,s0,a4
    const uint32_t *struct_ptr = (const uint32_t *)(dtb_vaddr + struct_offset);
ffffffffc020032c:	9a3a                	add	s4,s4,a4
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc020032e:	00ff0c37          	lui	s8,0xff0
        switch (token) {
ffffffffc0200332:	4b8d                	li	s7,3
                if (in_memory_node && strcmp(prop_name, "reg") == 0 && prop_len >= 16) {
ffffffffc0200334:	00001917          	auipc	s2,0x1
ffffffffc0200338:	68c90913          	addi	s2,s2,1676 # ffffffffc02019c0 <etext+0x19c>
ffffffffc020033c:	49bd                	li	s3,15
        switch (token) {
ffffffffc020033e:	4d91                	li	s11,4
ffffffffc0200340:	4d05                	li	s10,1
                if (strncmp(name, "memory", 6) == 0) {
ffffffffc0200342:	00001497          	auipc	s1,0x1
ffffffffc0200346:	67648493          	addi	s1,s1,1654 # ffffffffc02019b8 <etext+0x194>
        uint32_t token = fdt32_to_cpu(*struct_ptr++);
ffffffffc020034a:	000a2703          	lw	a4,0(s4)
ffffffffc020034e:	004a0a93          	addi	s5,s4,4
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc0200352:	0087569b          	srliw	a3,a4,0x8
ffffffffc0200356:	0187179b          	slliw	a5,a4,0x18
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc020035a:	0187561b          	srliw	a2,a4,0x18
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc020035e:	0106969b          	slliw	a3,a3,0x10
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc0200362:	0107571b          	srliw	a4,a4,0x10
ffffffffc0200366:	8fd1                	or	a5,a5,a2
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc0200368:	0186f6b3          	and	a3,a3,s8
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc020036c:	0087171b          	slliw	a4,a4,0x8
ffffffffc0200370:	8fd5                	or	a5,a5,a3
ffffffffc0200372:	00eb7733          	and	a4,s6,a4
ffffffffc0200376:	8fd9                	or	a5,a5,a4
ffffffffc0200378:	2781                	sext.w	a5,a5
        switch (token) {
ffffffffc020037a:	09778c63          	beq	a5,s7,ffffffffc0200412 <dtb_init+0x1ee>
ffffffffc020037e:	00fbea63          	bltu	s7,a5,ffffffffc0200392 <dtb_init+0x16e>
ffffffffc0200382:	07a78663          	beq	a5,s10,ffffffffc02003ee <dtb_init+0x1ca>
ffffffffc0200386:	4709                	li	a4,2
ffffffffc0200388:	00e79763          	bne	a5,a4,ffffffffc0200396 <dtb_init+0x172>
ffffffffc020038c:	4c81                	li	s9,0
ffffffffc020038e:	8a56                	mv	s4,s5
ffffffffc0200390:	bf6d                	j	ffffffffc020034a <dtb_init+0x126>
ffffffffc0200392:	ffb78ee3          	beq	a5,s11,ffffffffc020038e <dtb_init+0x16a>
        cprintf("  End:  0x%016lx\n", mem_base + mem_size - 1);
        // 保存到全局变量，供 PMM 查询
        memory_base = mem_base;
        memory_size = mem_size;
    } else {
        cprintf("Warning: Could not extract memory info from DTB\n");
ffffffffc0200396:	00001517          	auipc	a0,0x1
ffffffffc020039a:	6a250513          	addi	a0,a0,1698 # ffffffffc0201a38 <etext+0x214>
ffffffffc020039e:	db3ff0ef          	jal	ra,ffffffffc0200150 <cprintf>
    }
    cprintf("DTB init completed\n");
ffffffffc02003a2:	00001517          	auipc	a0,0x1
ffffffffc02003a6:	6ce50513          	addi	a0,a0,1742 # ffffffffc0201a70 <etext+0x24c>
}
ffffffffc02003aa:	7446                	ld	s0,112(sp)
ffffffffc02003ac:	70e6                	ld	ra,120(sp)
ffffffffc02003ae:	74a6                	ld	s1,104(sp)
ffffffffc02003b0:	7906                	ld	s2,96(sp)
ffffffffc02003b2:	69e6                	ld	s3,88(sp)
ffffffffc02003b4:	6a46                	ld	s4,80(sp)
ffffffffc02003b6:	6aa6                	ld	s5,72(sp)
ffffffffc02003b8:	6b06                	ld	s6,64(sp)
ffffffffc02003ba:	7be2                	ld	s7,56(sp)
ffffffffc02003bc:	7c42                	ld	s8,48(sp)
ffffffffc02003be:	7ca2                	ld	s9,40(sp)
ffffffffc02003c0:	7d02                	ld	s10,32(sp)
ffffffffc02003c2:	6de2                	ld	s11,24(sp)
ffffffffc02003c4:	6109                	addi	sp,sp,128
    cprintf("DTB init completed\n");
ffffffffc02003c6:	b369                	j	ffffffffc0200150 <cprintf>
}
ffffffffc02003c8:	7446                	ld	s0,112(sp)
ffffffffc02003ca:	70e6                	ld	ra,120(sp)
ffffffffc02003cc:	74a6                	ld	s1,104(sp)
ffffffffc02003ce:	7906                	ld	s2,96(sp)
ffffffffc02003d0:	69e6                	ld	s3,88(sp)
ffffffffc02003d2:	6a46                	ld	s4,80(sp)
ffffffffc02003d4:	6aa6                	ld	s5,72(sp)
ffffffffc02003d6:	6b06                	ld	s6,64(sp)
ffffffffc02003d8:	7be2                	ld	s7,56(sp)
ffffffffc02003da:	7c42                	ld	s8,48(sp)
ffffffffc02003dc:	7ca2                	ld	s9,40(sp)
ffffffffc02003de:	7d02                	ld	s10,32(sp)
ffffffffc02003e0:	6de2                	ld	s11,24(sp)
        cprintf("Error: Invalid DTB magic number: 0x%x\n", magic);
ffffffffc02003e2:	00001517          	auipc	a0,0x1
ffffffffc02003e6:	5ae50513          	addi	a0,a0,1454 # ffffffffc0201990 <etext+0x16c>
}
ffffffffc02003ea:	6109                	addi	sp,sp,128
        cprintf("Error: Invalid DTB magic number: 0x%x\n", magic);
ffffffffc02003ec:	b395                	j	ffffffffc0200150 <cprintf>
                int name_len = strlen(name);
ffffffffc02003ee:	8556                	mv	a0,s5
ffffffffc02003f0:	3a8010ef          	jal	ra,ffffffffc0201798 <strlen>
ffffffffc02003f4:	8a2a                	mv	s4,a0
                if (strncmp(name, "memory", 6) == 0) {
ffffffffc02003f6:	4619                	li	a2,6
ffffffffc02003f8:	85a6                	mv	a1,s1
ffffffffc02003fa:	8556                	mv	a0,s5
                int name_len = strlen(name);
ffffffffc02003fc:	2a01                	sext.w	s4,s4
                if (strncmp(name, "memory", 6) == 0) {
ffffffffc02003fe:	3ee010ef          	jal	ra,ffffffffc02017ec <strncmp>
ffffffffc0200402:	e111                	bnez	a0,ffffffffc0200406 <dtb_init+0x1e2>
                    in_memory_node = 1;
ffffffffc0200404:	4c85                	li	s9,1
                struct_ptr = (const uint32_t *)(((uintptr_t)struct_ptr + name_len + 4) & ~3);
ffffffffc0200406:	0a91                	addi	s5,s5,4
ffffffffc0200408:	9ad2                	add	s5,s5,s4
ffffffffc020040a:	ffcafa93          	andi	s5,s5,-4
        switch (token) {
ffffffffc020040e:	8a56                	mv	s4,s5
ffffffffc0200410:	bf2d                	j	ffffffffc020034a <dtb_init+0x126>
                uint32_t prop_len = fdt32_to_cpu(*struct_ptr++);
ffffffffc0200412:	004a2783          	lw	a5,4(s4)
                uint32_t prop_nameoff = fdt32_to_cpu(*struct_ptr++);
ffffffffc0200416:	00ca0693          	addi	a3,s4,12
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc020041a:	0087d71b          	srliw	a4,a5,0x8
ffffffffc020041e:	01879a9b          	slliw	s5,a5,0x18
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc0200422:	0187d61b          	srliw	a2,a5,0x18
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc0200426:	0107171b          	slliw	a4,a4,0x10
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc020042a:	0107d79b          	srliw	a5,a5,0x10
ffffffffc020042e:	00caeab3          	or	s5,s5,a2
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc0200432:	01877733          	and	a4,a4,s8
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc0200436:	0087979b          	slliw	a5,a5,0x8
ffffffffc020043a:	00eaeab3          	or	s5,s5,a4
ffffffffc020043e:	00fb77b3          	and	a5,s6,a5
ffffffffc0200442:	00faeab3          	or	s5,s5,a5
ffffffffc0200446:	2a81                	sext.w	s5,s5
                if (in_memory_node && strcmp(prop_name, "reg") == 0 && prop_len >= 16) {
ffffffffc0200448:	000c9c63          	bnez	s9,ffffffffc0200460 <dtb_init+0x23c>
                struct_ptr = (const uint32_t *)(((uintptr_t)struct_ptr + prop_len + 3) & ~3);
ffffffffc020044c:	1a82                	slli	s5,s5,0x20
ffffffffc020044e:	00368793          	addi	a5,a3,3
ffffffffc0200452:	020ada93          	srli	s5,s5,0x20
ffffffffc0200456:	9abe                	add	s5,s5,a5
ffffffffc0200458:	ffcafa93          	andi	s5,s5,-4
        switch (token) {
ffffffffc020045c:	8a56                	mv	s4,s5
ffffffffc020045e:	b5f5                	j	ffffffffc020034a <dtb_init+0x126>
                uint32_t prop_nameoff = fdt32_to_cpu(*struct_ptr++);
ffffffffc0200460:	008a2783          	lw	a5,8(s4)
                if (in_memory_node && strcmp(prop_name, "reg") == 0 && prop_len >= 16) {
ffffffffc0200464:	85ca                	mv	a1,s2
ffffffffc0200466:	e436                	sd	a3,8(sp)
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc0200468:	0087d51b          	srliw	a0,a5,0x8
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc020046c:	0187d61b          	srliw	a2,a5,0x18
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc0200470:	0187971b          	slliw	a4,a5,0x18
ffffffffc0200474:	0105151b          	slliw	a0,a0,0x10
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc0200478:	0107d79b          	srliw	a5,a5,0x10
ffffffffc020047c:	8f51                	or	a4,a4,a2
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc020047e:	01857533          	and	a0,a0,s8
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc0200482:	0087979b          	slliw	a5,a5,0x8
ffffffffc0200486:	8d59                	or	a0,a0,a4
ffffffffc0200488:	00fb77b3          	and	a5,s6,a5
ffffffffc020048c:	8d5d                	or	a0,a0,a5
                const char *prop_name = strings_base + prop_nameoff;
ffffffffc020048e:	1502                	slli	a0,a0,0x20
ffffffffc0200490:	9101                	srli	a0,a0,0x20
                if (in_memory_node && strcmp(prop_name, "reg") == 0 && prop_len >= 16) {
ffffffffc0200492:	9522                	add	a0,a0,s0
ffffffffc0200494:	33a010ef          	jal	ra,ffffffffc02017ce <strcmp>
ffffffffc0200498:	66a2                	ld	a3,8(sp)
ffffffffc020049a:	f94d                	bnez	a0,ffffffffc020044c <dtb_init+0x228>
ffffffffc020049c:	fb59f8e3          	bgeu	s3,s5,ffffffffc020044c <dtb_init+0x228>
                    *mem_base = fdt64_to_cpu(reg_data[0]);
ffffffffc02004a0:	00ca3783          	ld	a5,12(s4)
                    *mem_size = fdt64_to_cpu(reg_data[1]);
ffffffffc02004a4:	014a3703          	ld	a4,20(s4)
        cprintf("Physical Memory from DTB:\n");
ffffffffc02004a8:	00001517          	auipc	a0,0x1
ffffffffc02004ac:	52050513          	addi	a0,a0,1312 # ffffffffc02019c8 <etext+0x1a4>
           fdt32_to_cpu(x >> 32);
ffffffffc02004b0:	4207d613          	srai	a2,a5,0x20
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc02004b4:	0087d31b          	srliw	t1,a5,0x8
           fdt32_to_cpu(x >> 32);
ffffffffc02004b8:	42075593          	srai	a1,a4,0x20
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc02004bc:	0187de1b          	srliw	t3,a5,0x18
ffffffffc02004c0:	0186581b          	srliw	a6,a2,0x18
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc02004c4:	0187941b          	slliw	s0,a5,0x18
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc02004c8:	0107d89b          	srliw	a7,a5,0x10
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc02004cc:	0187d693          	srli	a3,a5,0x18
ffffffffc02004d0:	01861f1b          	slliw	t5,a2,0x18
ffffffffc02004d4:	0087579b          	srliw	a5,a4,0x8
ffffffffc02004d8:	0103131b          	slliw	t1,t1,0x10
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc02004dc:	0106561b          	srliw	a2,a2,0x10
ffffffffc02004e0:	010f6f33          	or	t5,t5,a6
ffffffffc02004e4:	0187529b          	srliw	t0,a4,0x18
ffffffffc02004e8:	0185df9b          	srliw	t6,a1,0x18
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc02004ec:	01837333          	and	t1,t1,s8
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc02004f0:	01c46433          	or	s0,s0,t3
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc02004f4:	0186f6b3          	and	a3,a3,s8
ffffffffc02004f8:	01859e1b          	slliw	t3,a1,0x18
ffffffffc02004fc:	01871e9b          	slliw	t4,a4,0x18
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc0200500:	0107581b          	srliw	a6,a4,0x10
ffffffffc0200504:	0086161b          	slliw	a2,a2,0x8
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc0200508:	8361                	srli	a4,a4,0x18
ffffffffc020050a:	0107979b          	slliw	a5,a5,0x10
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc020050e:	0105d59b          	srliw	a1,a1,0x10
ffffffffc0200512:	01e6e6b3          	or	a3,a3,t5
ffffffffc0200516:	00cb7633          	and	a2,s6,a2
ffffffffc020051a:	0088181b          	slliw	a6,a6,0x8
ffffffffc020051e:	0085959b          	slliw	a1,a1,0x8
ffffffffc0200522:	00646433          	or	s0,s0,t1
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc0200526:	0187f7b3          	and	a5,a5,s8
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc020052a:	01fe6333          	or	t1,t3,t6
    return ((x & 0xff) << 24) | (((x >> 8) & 0xff) << 16) | 
ffffffffc020052e:	01877c33          	and	s8,a4,s8
           (((x >> 16) & 0xff) << 8) | ((x >> 24) & 0xff);
ffffffffc0200532:	0088989b          	slliw	a7,a7,0x8
ffffffffc0200536:	011b78b3          	and	a7,s6,a7
ffffffffc020053a:	005eeeb3          	or	t4,t4,t0
ffffffffc020053e:	00c6e733          	or	a4,a3,a2
ffffffffc0200542:	006c6c33          	or	s8,s8,t1
ffffffffc0200546:	010b76b3          	and	a3,s6,a6
ffffffffc020054a:	00bb7b33          	and	s6,s6,a1
ffffffffc020054e:	01d7e7b3          	or	a5,a5,t4
ffffffffc0200552:	016c6b33          	or	s6,s8,s6
ffffffffc0200556:	01146433          	or	s0,s0,a7
ffffffffc020055a:	8fd5                	or	a5,a5,a3
           fdt32_to_cpu(x >> 32);
ffffffffc020055c:	1702                	slli	a4,a4,0x20
ffffffffc020055e:	1b02                	slli	s6,s6,0x20
    return ((uint64_t)fdt32_to_cpu(x & 0xffffffff) << 32) | 
ffffffffc0200560:	1782                	slli	a5,a5,0x20
           fdt32_to_cpu(x >> 32);
ffffffffc0200562:	9301                	srli	a4,a4,0x20
    return ((uint64_t)fdt32_to_cpu(x & 0xffffffff) << 32) | 
ffffffffc0200564:	1402                	slli	s0,s0,0x20
           fdt32_to_cpu(x >> 32);
ffffffffc0200566:	020b5b13          	srli	s6,s6,0x20
    return ((uint64_t)fdt32_to_cpu(x & 0xffffffff) << 32) | 
ffffffffc020056a:	0167eb33          	or	s6,a5,s6
ffffffffc020056e:	8c59                	or	s0,s0,a4
        cprintf("Physical Memory from DTB:\n");
ffffffffc0200570:	be1ff0ef          	jal	ra,ffffffffc0200150 <cprintf>
        cprintf("  Base: 0x%016lx\n", mem_base);
ffffffffc0200574:	85a2                	mv	a1,s0
ffffffffc0200576:	00001517          	auipc	a0,0x1
ffffffffc020057a:	47250513          	addi	a0,a0,1138 # ffffffffc02019e8 <etext+0x1c4>
ffffffffc020057e:	bd3ff0ef          	jal	ra,ffffffffc0200150 <cprintf>
        cprintf("  Size: 0x%016lx (%ld MB)\n", mem_size, mem_size / (1024 * 1024));
ffffffffc0200582:	014b5613          	srli	a2,s6,0x14
ffffffffc0200586:	85da                	mv	a1,s6
ffffffffc0200588:	00001517          	auipc	a0,0x1
ffffffffc020058c:	47850513          	addi	a0,a0,1144 # ffffffffc0201a00 <etext+0x1dc>
ffffffffc0200590:	bc1ff0ef          	jal	ra,ffffffffc0200150 <cprintf>
        cprintf("  End:  0x%016lx\n", mem_base + mem_size - 1);
ffffffffc0200594:	008b05b3          	add	a1,s6,s0
ffffffffc0200598:	15fd                	addi	a1,a1,-1
ffffffffc020059a:	00001517          	auipc	a0,0x1
ffffffffc020059e:	48650513          	addi	a0,a0,1158 # ffffffffc0201a20 <etext+0x1fc>
ffffffffc02005a2:	bafff0ef          	jal	ra,ffffffffc0200150 <cprintf>
    cprintf("DTB init completed\n");
ffffffffc02005a6:	00001517          	auipc	a0,0x1
ffffffffc02005aa:	4ca50513          	addi	a0,a0,1226 # ffffffffc0201a70 <etext+0x24c>
        memory_base = mem_base;
ffffffffc02005ae:	00006797          	auipc	a5,0x6
ffffffffc02005b2:	c687b523          	sd	s0,-918(a5) # ffffffffc0206218 <memory_base>
        memory_size = mem_size;
ffffffffc02005b6:	00006797          	auipc	a5,0x6
ffffffffc02005ba:	c767b523          	sd	s6,-918(a5) # ffffffffc0206220 <memory_size>
    cprintf("DTB init completed\n");
ffffffffc02005be:	b3f5                	j	ffffffffc02003aa <dtb_init+0x186>

ffffffffc02005c0 <get_memory_base>:

uint64_t get_memory_base(void) {
    return memory_base;
}
ffffffffc02005c0:	00006517          	auipc	a0,0x6
ffffffffc02005c4:	c5853503          	ld	a0,-936(a0) # ffffffffc0206218 <memory_base>
ffffffffc02005c8:	8082                	ret

ffffffffc02005ca <get_memory_size>:

uint64_t get_memory_size(void) {
    return memory_size;
ffffffffc02005ca:	00006517          	auipc	a0,0x6
ffffffffc02005ce:	c5653503          	ld	a0,-938(a0) # ffffffffc0206220 <memory_size>
ffffffffc02005d2:	8082                	ret

ffffffffc02005d4 <best_fit_insert_block>:
 * list_next - get the next entry
 * @listelm:    the list head
 **/
static inline list_entry_t *
list_next(list_entry_t *listelm) {
    return listelm->next;
ffffffffc02005d4:	00006697          	auipc	a3,0x6
ffffffffc02005d8:	a4468693          	addi	a3,a3,-1468 # ffffffffc0206018 <free_area>
ffffffffc02005dc:	669c                	ld	a5,8(a3)
 * The caller accounts for the newly freed pages; merging does not add pages. */
static void
best_fit_insert_block(struct Page *base)
{
    list_entry_t *le = list_next(&free_list); /* 从链表头开始查找插入位置 */
    while (le != &free_list && le2page(le, page_link) < base)
ffffffffc02005de:	00d78b63          	beq	a5,a3,ffffffffc02005f4 <best_fit_insert_block+0x20>
ffffffffc02005e2:	fe878713          	addi	a4,a5,-24
ffffffffc02005e6:	0aa77363          	bgeu	a4,a0,ffffffffc020068c <best_fit_insert_block+0xb8>
ffffffffc02005ea:	679c                	ld	a5,8(a5)
ffffffffc02005ec:	fe878713          	addi	a4,a5,-24
ffffffffc02005f0:	fed79be3          	bne	a5,a3,ffffffffc02005e6 <best_fit_insert_block+0x12>
    __list_add(elm, listelm->prev, listelm);
ffffffffc02005f4:	6290                	ld	a2,0(a3)
    { /* 跳过所有地址比 base 小的空闲块 */
        le = list_next(le);
    }
    list_add_before(le, &base->page_link); /* 插到 le 之前：保持链表按物理地址升序 */
ffffffffc02005f6:	01850793          	addi	a5,a0,24
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 * */
static inline void
__list_add(list_entry_t *elm, list_entry_t *prev, list_entry_t *next) {
    prev->next = next->prev = elm;
ffffffffc02005fa:	e29c                	sd	a5,0(a3)
ffffffffc02005fc:	e61c                	sd	a5,8(a2)
    elm->next = next;
ffffffffc02005fe:	f114                	sd	a3,32(a0)
    elm->prev = prev;
ffffffffc0200600:	ed10                	sd	a2,24(a0)

    le = list_prev(&base->page_link); /* 取新块的前驱，尝试向前合并 */
    if (le != &free_list)
ffffffffc0200602:	00006797          	auipc	a5,0x6
ffffffffc0200606:	a1678793          	addi	a5,a5,-1514 # ffffffffc0206018 <free_area>
ffffffffc020060a:	02d60f63          	beq	a2,a3,ffffffffc0200648 <best_fit_insert_block+0x74>
    { /* 前驱存在（不是链表头） */
        struct Page *prev = le2page(le, page_link);
        if (prev + prev->property == base)
ffffffffc020060e:	ff862883          	lw	a7,-8(a2) # fefff8 <kern_entry-0xffffffffbf210008>
        struct Page *prev = le2page(le, page_link);
ffffffffc0200612:	fe860813          	addi	a6,a2,-24
        if (prev + prev->property == base)
ffffffffc0200616:	02089593          	slli	a1,a7,0x20
ffffffffc020061a:	9181                	srli	a1,a1,0x20
ffffffffc020061c:	00259713          	slli	a4,a1,0x2
ffffffffc0200620:	972e                	add	a4,a4,a1
ffffffffc0200622:	070e                	slli	a4,a4,0x3
ffffffffc0200624:	9742                	add	a4,a4,a6
ffffffffc0200626:	04e50563          	beq	a0,a4,ffffffffc0200670 <best_fit_insert_block+0x9c>
            base = prev;                      /* base 指向合并后的首页，便于再与后继比较 */
        }
    }

    le = list_next(&base->page_link); /* 取合并后新块的后继，尝试向后合并 */
    if (le != &free_list)
ffffffffc020062a:	00d78f63          	beq	a5,a3,ffffffffc0200648 <best_fit_insert_block+0x74>
ffffffffc020062e:	fe878713          	addi	a4,a5,-24
    { /* 后继存在 */
        struct Page *next = le2page(le, page_link);
        if (base + base->property == next)
ffffffffc0200632:	490c                	lw	a1,16(a0)
ffffffffc0200634:	02059613          	slli	a2,a1,0x20
ffffffffc0200638:	9201                	srli	a2,a2,0x20
ffffffffc020063a:	00261693          	slli	a3,a2,0x2
ffffffffc020063e:	96b2                	add	a3,a3,a2
ffffffffc0200640:	068e                	slli	a3,a3,0x3
ffffffffc0200642:	96aa                	add	a3,a3,a0
ffffffffc0200644:	00d70363          	beq	a4,a3,ffffffffc020064a <best_fit_insert_block+0x76>
            ClearPageProperty(next);          /* next 不再是块首页：清除块标记 */
            next->property = 0;               /* 并清空其 property */
            list_del(&next->page_link);       /* 把 next 从链表摘除 */
        }
    }
}
ffffffffc0200648:	8082                	ret
            base->property += next->property; /* 把 next 并入 base，页数累加 */
ffffffffc020064a:	ff87a683          	lw	a3,-8(a5)
            ClearPageProperty(next);          /* next 不再是块首页：清除块标记 */
ffffffffc020064e:	ff07b703          	ld	a4,-16(a5)
    __list_del(listelm->prev, listelm->next);
ffffffffc0200652:	0007b803          	ld	a6,0(a5)
ffffffffc0200656:	6790                	ld	a2,8(a5)
            base->property += next->property; /* 把 next 并入 base，页数累加 */
ffffffffc0200658:	9db5                	addw	a1,a1,a3
ffffffffc020065a:	c90c                	sw	a1,16(a0)
            ClearPageProperty(next);          /* next 不再是块首页：清除块标记 */
ffffffffc020065c:	9b75                	andi	a4,a4,-3
ffffffffc020065e:	fee7b823          	sd	a4,-16(a5)
            next->property = 0;               /* 并清空其 property */
ffffffffc0200662:	fe07ac23          	sw	zero,-8(a5)
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 * */
static inline void
__list_del(list_entry_t *prev, list_entry_t *next) {
    prev->next = next;
ffffffffc0200666:	00c83423          	sd	a2,8(a6)
    next->prev = prev;
ffffffffc020066a:	01063023          	sd	a6,0(a2)
}
ffffffffc020066e:	8082                	ret
            prev->property += base->property; /* 前驱吞并 base，页数累加 */
ffffffffc0200670:	490c                	lw	a1,16(a0)
            ClearPageProperty(base);          /* base 不再是块首页：清除块标记 */
ffffffffc0200672:	6518                	ld	a4,8(a0)
            prev->property += base->property; /* 前驱吞并 base，页数累加 */
ffffffffc0200674:	011588bb          	addw	a7,a1,a7
ffffffffc0200678:	ff162c23          	sw	a7,-8(a2)
            ClearPageProperty(base);          /* base 不再是块首页：清除块标记 */
ffffffffc020067c:	9b75                	andi	a4,a4,-3
ffffffffc020067e:	e518                	sd	a4,8(a0)
            base->property = 0;               /* 并清空其 property */
ffffffffc0200680:	00052823          	sw	zero,16(a0)
    prev->next = next;
ffffffffc0200684:	e61c                	sd	a5,8(a2)
    next->prev = prev;
ffffffffc0200686:	e390                	sd	a2,0(a5)
            base = prev;                      /* base 指向合并后的首页，便于再与后继比较 */
ffffffffc0200688:	8542                	mv	a0,a6
ffffffffc020068a:	b745                	j	ffffffffc020062a <best_fit_insert_block+0x56>
    __list_add(elm, listelm->prev, listelm);
ffffffffc020068c:	6390                	ld	a2,0(a5)
    list_add_before(le, &base->page_link); /* 插到 le 之前：保持链表按物理地址升序 */
ffffffffc020068e:	01850593          	addi	a1,a0,24
    prev->next = next->prev = elm;
ffffffffc0200692:	e38c                	sd	a1,0(a5)
ffffffffc0200694:	e60c                	sd	a1,8(a2)
    elm->next = next;
ffffffffc0200696:	f11c                	sd	a5,32(a0)
    elm->prev = prev;
ffffffffc0200698:	ed10                	sd	a2,24(a0)
    if (le != &free_list)
ffffffffc020069a:	f6d61ae3          	bne	a2,a3,ffffffffc020060e <best_fit_insert_block+0x3a>
ffffffffc020069e:	bf51                	j	ffffffffc0200632 <best_fit_insert_block+0x5e>

ffffffffc02006a0 <best_fit_init>:
    elm->prev = elm->next = elm;
ffffffffc02006a0:	00006797          	auipc	a5,0x6
ffffffffc02006a4:	97878793          	addi	a5,a5,-1672 # ffffffffc0206018 <free_area>
ffffffffc02006a8:	e79c                	sd	a5,8(a5)
ffffffffc02006aa:	e39c                	sd	a5,0(a5)
 */
static void
best_fit_init(void)
{
    list_init(&free_list); /* 让链表头自指，形成一个空的双向循环链表 */
    nr_free = 0;           /* 空闲页计数清零 */
ffffffffc02006ac:	0007a823          	sw	zero,16(a5)
}
ffffffffc02006b0:	8082                	ret

ffffffffc02006b2 <best_fit_nr_free_pages>:
 */
static size_t
best_fit_nr_free_pages(void)
{
    return nr_free; /* 直接返回维护好的空闲页计数 */
}
ffffffffc02006b2:	00006517          	auipc	a0,0x6
ffffffffc02006b6:	97656503          	lwu	a0,-1674(a0) # ffffffffc0206028 <free_area+0x10>
ffffffffc02006ba:	8082                	ret

ffffffffc02006bc <best_fit_alloc_pages>:
    assert(n > 0); /* 请求页数必须为正 */
ffffffffc02006bc:	c155                	beqz	a0,ffffffffc0200760 <best_fit_alloc_pages+0xa4>
    if (n > nr_free)
ffffffffc02006be:	00006597          	auipc	a1,0x6
ffffffffc02006c2:	95a58593          	addi	a1,a1,-1702 # ffffffffc0206018 <free_area>
ffffffffc02006c6:	0105a883          	lw	a7,16(a1)
ffffffffc02006ca:	862a                	mv	a2,a0
ffffffffc02006cc:	02089793          	slli	a5,a7,0x20
ffffffffc02006d0:	9381                	srli	a5,a5,0x20
ffffffffc02006d2:	08a7e563          	bltu	a5,a0,ffffffffc020075c <best_fit_alloc_pages+0xa0>
    return listelm->next;
ffffffffc02006d6:	659c                	ld	a5,8(a1)
    struct Page *best = NULL; /* 记录当前最优（最贴合）的候选块 */
ffffffffc02006d8:	4501                	li	a0,0
    while ((le = list_next(le)) != &free_list)
ffffffffc02006da:	08b78063          	beq	a5,a1,ffffffffc020075a <best_fit_alloc_pages+0x9e>
        if (p->property >= n &&
ffffffffc02006de:	ff87a703          	lw	a4,-8(a5)
ffffffffc02006e2:	02071693          	slli	a3,a4,0x20
ffffffffc02006e6:	9281                	srli	a3,a3,0x20
ffffffffc02006e8:	00c6eb63          	bltu	a3,a2,ffffffffc02006fe <best_fit_alloc_pages+0x42>
ffffffffc02006ec:	c509                	beqz	a0,ffffffffc02006f6 <best_fit_alloc_pages+0x3a>
            (best == NULL || p->property < best->property))
ffffffffc02006ee:	01052803          	lw	a6,16(a0)
ffffffffc02006f2:	01077663          	bgeu	a4,a6,ffffffffc02006fe <best_fit_alloc_pages+0x42>
        struct Page *p = le2page(le, page_link);
ffffffffc02006f6:	fe878513          	addi	a0,a5,-24
            if (p->property == n)
ffffffffc02006fa:	00c68663          	beq	a3,a2,ffffffffc0200706 <best_fit_alloc_pages+0x4a>
ffffffffc02006fe:	679c                	ld	a5,8(a5)
    while ((le = list_next(le)) != &free_list)
ffffffffc0200700:	fcb79fe3          	bne	a5,a1,ffffffffc02006de <best_fit_alloc_pages+0x22>
    if (best == NULL)
ffffffffc0200704:	c939                	beqz	a0,ffffffffc020075a <best_fit_alloc_pages+0x9e>
    if (best->property > n)
ffffffffc0200706:	01052803          	lw	a6,16(a0)
    return listelm->prev;
ffffffffc020070a:	6d18                	ld	a4,24(a0)
    __list_del(listelm->prev, listelm->next);
ffffffffc020070c:	7114                	ld	a3,32(a0)
ffffffffc020070e:	02081793          	slli	a5,a6,0x20
ffffffffc0200712:	9381                	srli	a5,a5,0x20
    prev->next = next;
ffffffffc0200714:	e714                	sd	a3,8(a4)
    next->prev = prev;
ffffffffc0200716:	e298                	sd	a4,0(a3)
        rest->property = best->property - n;    /* 剩余页数 */
ffffffffc0200718:	0006031b          	sext.w	t1,a2
    if (best->property > n)
ffffffffc020071c:	02f67563          	bgeu	a2,a5,ffffffffc0200746 <best_fit_alloc_pages+0x8a>
        struct Page *rest = best + n;           /* 剩余部分的首页 */
ffffffffc0200720:	00261793          	slli	a5,a2,0x2
ffffffffc0200724:	97b2                	add	a5,a5,a2
ffffffffc0200726:	078e                	slli	a5,a5,0x3
ffffffffc0200728:	97aa                	add	a5,a5,a0
        SetPageProperty(rest);                  /* 标记 rest 为新的空闲块首页 */
ffffffffc020072a:	6790                	ld	a2,8(a5)
        rest->property = best->property - n;    /* 剩余页数 */
ffffffffc020072c:	4068083b          	subw	a6,a6,t1
ffffffffc0200730:	0107a823          	sw	a6,16(a5)
        SetPageProperty(rest);                  /* 标记 rest 为新的空闲块首页 */
ffffffffc0200734:	00266613          	ori	a2,a2,2
ffffffffc0200738:	e790                	sd	a2,8(a5)
        list_add_after(prev, &rest->page_link); /* 插回原 best 位置（prev 之后），维持地址序 */
ffffffffc020073a:	01878613          	addi	a2,a5,24
    prev->next = next->prev = elm;
ffffffffc020073e:	e290                	sd	a2,0(a3)
ffffffffc0200740:	e710                	sd	a2,8(a4)
    elm->next = next;
ffffffffc0200742:	f394                	sd	a3,32(a5)
    elm->prev = prev;
ffffffffc0200744:	ef98                	sd	a4,24(a5)
    ClearPageProperty(best); /* 分配出去的首页不再是空闲块首页 */
ffffffffc0200746:	651c                	ld	a5,8(a0)
    nr_free -= n;            /* 扣除已分配页数，更新空闲计数 */
ffffffffc0200748:	406888bb          	subw	a7,a7,t1
ffffffffc020074c:	0115a823          	sw	a7,16(a1)
    ClearPageProperty(best); /* 分配出去的首页不再是空闲块首页 */
ffffffffc0200750:	9bf5                	andi	a5,a5,-3
ffffffffc0200752:	e51c                	sd	a5,8(a0)
    best->property = 0;      /* 并清空其块大小 */
ffffffffc0200754:	00052823          	sw	zero,16(a0)
    return best;
ffffffffc0200758:	8082                	ret
}
ffffffffc020075a:	8082                	ret
        return NULL;
ffffffffc020075c:	4501                	li	a0,0
ffffffffc020075e:	8082                	ret
{
ffffffffc0200760:	1141                	addi	sp,sp,-16
    assert(n > 0); /* 请求页数必须为正 */
ffffffffc0200762:	00001697          	auipc	a3,0x1
ffffffffc0200766:	32668693          	addi	a3,a3,806 # ffffffffc0201a88 <etext+0x264>
ffffffffc020076a:	00001617          	auipc	a2,0x1
ffffffffc020076e:	32660613          	addi	a2,a2,806 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200772:	09d00593          	li	a1,157
ffffffffc0200776:	00001517          	auipc	a0,0x1
ffffffffc020077a:	33250513          	addi	a0,a0,818 # ffffffffc0201aa8 <etext+0x284>
{
ffffffffc020077e:	e406                	sd	ra,8(sp)
    assert(n > 0); /* 请求页数必须为正 */
ffffffffc0200780:	a47ff0ef          	jal	ra,ffffffffc02001c6 <__panic>

ffffffffc0200784 <best_fit_check>:
    free_page(p2);
}

static void
best_fit_check(void)
{
ffffffffc0200784:	715d                	addi	sp,sp,-80
ffffffffc0200786:	e0a2                	sd	s0,64(sp)
    return listelm->next;
ffffffffc0200788:	00006417          	auipc	s0,0x6
ffffffffc020078c:	89040413          	addi	s0,s0,-1904 # ffffffffc0206018 <free_area>
ffffffffc0200790:	641c                	ld	a5,8(s0)
ffffffffc0200792:	e486                	sd	ra,72(sp)
ffffffffc0200794:	fc26                	sd	s1,56(sp)
ffffffffc0200796:	f84a                	sd	s2,48(sp)
ffffffffc0200798:	f44e                	sd	s3,40(sp)
ffffffffc020079a:	f052                	sd	s4,32(sp)
ffffffffc020079c:	ec56                	sd	s5,24(sp)
ffffffffc020079e:	e85a                	sd	s6,16(sp)
ffffffffc02007a0:	e45e                	sd	s7,8(sp)
ffffffffc02007a2:	e062                	sd	s8,0(sp)
    int score = 0, sumscore = 6;
    int count = 0, total = 0;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list)
ffffffffc02007a4:	26878963          	beq	a5,s0,ffffffffc0200a16 <best_fit_check+0x292>
    int count = 0, total = 0;
ffffffffc02007a8:	4481                	li	s1,0
ffffffffc02007aa:	4901                	li	s2,0
    {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
ffffffffc02007ac:	ff07b703          	ld	a4,-16(a5)
ffffffffc02007b0:	8b09                	andi	a4,a4,2
ffffffffc02007b2:	26070663          	beqz	a4,ffffffffc0200a1e <best_fit_check+0x29a>
        count++, total += p->property;
ffffffffc02007b6:	ff87a703          	lw	a4,-8(a5)
ffffffffc02007ba:	679c                	ld	a5,8(a5)
ffffffffc02007bc:	2905                	addiw	s2,s2,1
ffffffffc02007be:	9cb9                	addw	s1,s1,a4
    while ((le = list_next(le)) != &free_list)
ffffffffc02007c0:	fe8796e3          	bne	a5,s0,ffffffffc02007ac <best_fit_check+0x28>
    }
    assert(total == nr_free_pages());
ffffffffc02007c4:	89a6                	mv	s3,s1
ffffffffc02007c6:	7b4000ef          	jal	ra,ffffffffc0200f7a <nr_free_pages>
ffffffffc02007ca:	33351a63          	bne	a0,s3,ffffffffc0200afe <best_fit_check+0x37a>
    assert((p0 = alloc_page()) != NULL);
ffffffffc02007ce:	4505                	li	a0,1
ffffffffc02007d0:	792000ef          	jal	ra,ffffffffc0200f62 <alloc_pages>
ffffffffc02007d4:	8a2a                	mv	s4,a0
ffffffffc02007d6:	36050463          	beqz	a0,ffffffffc0200b3e <best_fit_check+0x3ba>
    assert((p1 = alloc_page()) != NULL);
ffffffffc02007da:	4505                	li	a0,1
ffffffffc02007dc:	786000ef          	jal	ra,ffffffffc0200f62 <alloc_pages>
ffffffffc02007e0:	89aa                	mv	s3,a0
ffffffffc02007e2:	32050e63          	beqz	a0,ffffffffc0200b1e <best_fit_check+0x39a>
    assert((p2 = alloc_page()) != NULL);
ffffffffc02007e6:	4505                	li	a0,1
ffffffffc02007e8:	77a000ef          	jal	ra,ffffffffc0200f62 <alloc_pages>
ffffffffc02007ec:	8aaa                	mv	s5,a0
ffffffffc02007ee:	2c050863          	beqz	a0,ffffffffc0200abe <best_fit_check+0x33a>
    assert(p0 != p1 && p0 != p2 && p1 != p2);
ffffffffc02007f2:	253a0663          	beq	s4,s3,ffffffffc0200a3e <best_fit_check+0x2ba>
ffffffffc02007f6:	24aa0463          	beq	s4,a0,ffffffffc0200a3e <best_fit_check+0x2ba>
ffffffffc02007fa:	24a98263          	beq	s3,a0,ffffffffc0200a3e <best_fit_check+0x2ba>
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);
ffffffffc02007fe:	000a2783          	lw	a5,0(s4)
ffffffffc0200802:	24079e63          	bnez	a5,ffffffffc0200a5e <best_fit_check+0x2da>
ffffffffc0200806:	0009a783          	lw	a5,0(s3)
ffffffffc020080a:	24079a63          	bnez	a5,ffffffffc0200a5e <best_fit_check+0x2da>
ffffffffc020080e:	411c                	lw	a5,0(a0)
ffffffffc0200810:	24079763          	bnez	a5,ffffffffc0200a5e <best_fit_check+0x2da>
extern struct Page *pages;
extern size_t npage;
extern const size_t nbase;
extern uint64_t va_pa_offset;

static inline ppn_t page2ppn(struct Page *page) { return page - pages + nbase; }
ffffffffc0200814:	00006797          	auipc	a5,0x6
ffffffffc0200818:	a1c7b783          	ld	a5,-1508(a5) # ffffffffc0206230 <pages>
ffffffffc020081c:	40fa0733          	sub	a4,s4,a5
ffffffffc0200820:	870d                	srai	a4,a4,0x3
ffffffffc0200822:	00002597          	auipc	a1,0x2
ffffffffc0200826:	cd65b583          	ld	a1,-810(a1) # ffffffffc02024f8 <error_string+0x38>
ffffffffc020082a:	02b70733          	mul	a4,a4,a1
ffffffffc020082e:	00002617          	auipc	a2,0x2
ffffffffc0200832:	cda63603          	ld	a2,-806(a2) # ffffffffc0202508 <nbase>
    assert(page2pa(p0) < npage * PGSIZE);
ffffffffc0200836:	00006697          	auipc	a3,0x6
ffffffffc020083a:	9f26b683          	ld	a3,-1550(a3) # ffffffffc0206228 <npage>
ffffffffc020083e:	06b2                	slli	a3,a3,0xc
ffffffffc0200840:	9732                	add	a4,a4,a2

static inline uintptr_t page2pa(struct Page *page) {
    return page2ppn(page) << PGSHIFT;
ffffffffc0200842:	0732                	slli	a4,a4,0xc
ffffffffc0200844:	22d77d63          	bgeu	a4,a3,ffffffffc0200a7e <best_fit_check+0x2fa>
static inline ppn_t page2ppn(struct Page *page) { return page - pages + nbase; }
ffffffffc0200848:	40f98733          	sub	a4,s3,a5
ffffffffc020084c:	870d                	srai	a4,a4,0x3
ffffffffc020084e:	02b70733          	mul	a4,a4,a1
ffffffffc0200852:	9732                	add	a4,a4,a2
    return page2ppn(page) << PGSHIFT;
ffffffffc0200854:	0732                	slli	a4,a4,0xc
    assert(page2pa(p1) < npage * PGSIZE);
ffffffffc0200856:	3ed77463          	bgeu	a4,a3,ffffffffc0200c3e <best_fit_check+0x4ba>
static inline ppn_t page2ppn(struct Page *page) { return page - pages + nbase; }
ffffffffc020085a:	40f507b3          	sub	a5,a0,a5
ffffffffc020085e:	878d                	srai	a5,a5,0x3
ffffffffc0200860:	02b787b3          	mul	a5,a5,a1
ffffffffc0200864:	97b2                	add	a5,a5,a2
    return page2ppn(page) << PGSHIFT;
ffffffffc0200866:	07b2                	slli	a5,a5,0xc
    assert(page2pa(p2) < npage * PGSIZE);
ffffffffc0200868:	3ad7fb63          	bgeu	a5,a3,ffffffffc0200c1e <best_fit_check+0x49a>
    assert(alloc_page() == NULL);
ffffffffc020086c:	4505                	li	a0,1
    list_entry_t free_list_store = free_list;
ffffffffc020086e:	00043c03          	ld	s8,0(s0)
ffffffffc0200872:	00843b83          	ld	s7,8(s0)
    unsigned int nr_free_store = nr_free;
ffffffffc0200876:	01042b03          	lw	s6,16(s0)
    elm->prev = elm->next = elm;
ffffffffc020087a:	e400                	sd	s0,8(s0)
ffffffffc020087c:	e000                	sd	s0,0(s0)
    nr_free = 0;
ffffffffc020087e:	00005797          	auipc	a5,0x5
ffffffffc0200882:	7a07a523          	sw	zero,1962(a5) # ffffffffc0206028 <free_area+0x10>
    assert(alloc_page() == NULL);
ffffffffc0200886:	6dc000ef          	jal	ra,ffffffffc0200f62 <alloc_pages>
ffffffffc020088a:	36051a63          	bnez	a0,ffffffffc0200bfe <best_fit_check+0x47a>
    free_page(p0);
ffffffffc020088e:	4585                	li	a1,1
ffffffffc0200890:	8552                	mv	a0,s4
ffffffffc0200892:	6dc000ef          	jal	ra,ffffffffc0200f6e <free_pages>
    free_page(p1);
ffffffffc0200896:	4585                	li	a1,1
ffffffffc0200898:	854e                	mv	a0,s3
ffffffffc020089a:	6d4000ef          	jal	ra,ffffffffc0200f6e <free_pages>
    free_page(p2);
ffffffffc020089e:	4585                	li	a1,1
ffffffffc02008a0:	8556                	mv	a0,s5
ffffffffc02008a2:	6cc000ef          	jal	ra,ffffffffc0200f6e <free_pages>
    assert(nr_free == 3);
ffffffffc02008a6:	4818                	lw	a4,16(s0)
ffffffffc02008a8:	478d                	li	a5,3
ffffffffc02008aa:	32f71a63          	bne	a4,a5,ffffffffc0200bde <best_fit_check+0x45a>
    assert((p0 = alloc_page()) != NULL);
ffffffffc02008ae:	4505                	li	a0,1
ffffffffc02008b0:	6b2000ef          	jal	ra,ffffffffc0200f62 <alloc_pages>
ffffffffc02008b4:	89aa                	mv	s3,a0
ffffffffc02008b6:	30050463          	beqz	a0,ffffffffc0200bbe <best_fit_check+0x43a>
    assert((p1 = alloc_page()) != NULL);
ffffffffc02008ba:	4505                	li	a0,1
ffffffffc02008bc:	6a6000ef          	jal	ra,ffffffffc0200f62 <alloc_pages>
ffffffffc02008c0:	8aaa                	mv	s5,a0
ffffffffc02008c2:	2c050e63          	beqz	a0,ffffffffc0200b9e <best_fit_check+0x41a>
    assert((p2 = alloc_page()) != NULL);
ffffffffc02008c6:	4505                	li	a0,1
ffffffffc02008c8:	69a000ef          	jal	ra,ffffffffc0200f62 <alloc_pages>
ffffffffc02008cc:	8a2a                	mv	s4,a0
ffffffffc02008ce:	2a050863          	beqz	a0,ffffffffc0200b7e <best_fit_check+0x3fa>
    assert(alloc_page() == NULL);
ffffffffc02008d2:	4505                	li	a0,1
ffffffffc02008d4:	68e000ef          	jal	ra,ffffffffc0200f62 <alloc_pages>
ffffffffc02008d8:	28051363          	bnez	a0,ffffffffc0200b5e <best_fit_check+0x3da>
    free_page(p0);
ffffffffc02008dc:	4585                	li	a1,1
ffffffffc02008de:	854e                	mv	a0,s3
ffffffffc02008e0:	68e000ef          	jal	ra,ffffffffc0200f6e <free_pages>
    assert(!list_empty(&free_list));
ffffffffc02008e4:	641c                	ld	a5,8(s0)
ffffffffc02008e6:	1a878c63          	beq	a5,s0,ffffffffc0200a9e <best_fit_check+0x31a>
    assert((p = alloc_page()) == p0);
ffffffffc02008ea:	4505                	li	a0,1
ffffffffc02008ec:	676000ef          	jal	ra,ffffffffc0200f62 <alloc_pages>
ffffffffc02008f0:	52a99763          	bne	s3,a0,ffffffffc0200e1e <best_fit_check+0x69a>
    assert(alloc_page() == NULL);
ffffffffc02008f4:	4505                	li	a0,1
ffffffffc02008f6:	66c000ef          	jal	ra,ffffffffc0200f62 <alloc_pages>
ffffffffc02008fa:	50051263          	bnez	a0,ffffffffc0200dfe <best_fit_check+0x67a>
    assert(nr_free == 0);
ffffffffc02008fe:	481c                	lw	a5,16(s0)
ffffffffc0200900:	4c079f63          	bnez	a5,ffffffffc0200dde <best_fit_check+0x65a>
    free_page(p);
ffffffffc0200904:	854e                	mv	a0,s3
ffffffffc0200906:	4585                	li	a1,1
    free_list = free_list_store;
ffffffffc0200908:	01843023          	sd	s8,0(s0)
ffffffffc020090c:	01743423          	sd	s7,8(s0)
    nr_free = nr_free_store;
ffffffffc0200910:	01642823          	sw	s6,16(s0)
    free_page(p);
ffffffffc0200914:	65a000ef          	jal	ra,ffffffffc0200f6e <free_pages>
    free_page(p1);
ffffffffc0200918:	4585                	li	a1,1
ffffffffc020091a:	8556                	mv	a0,s5
ffffffffc020091c:	652000ef          	jal	ra,ffffffffc0200f6e <free_pages>
    free_page(p2);
ffffffffc0200920:	4585                	li	a1,1
ffffffffc0200922:	8552                	mv	a0,s4
ffffffffc0200924:	64a000ef          	jal	ra,ffffffffc0200f6e <free_pages>

#ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
#endif
    struct Page *p0 = alloc_pages(5), *p1, *p2;
ffffffffc0200928:	4515                	li	a0,5
ffffffffc020092a:	638000ef          	jal	ra,ffffffffc0200f62 <alloc_pages>
ffffffffc020092e:	89aa                	mv	s3,a0
    assert(p0 != NULL);
ffffffffc0200930:	48050763          	beqz	a0,ffffffffc0200dbe <best_fit_check+0x63a>
    assert(!PageProperty(p0));
ffffffffc0200934:	651c                	ld	a5,8(a0)
ffffffffc0200936:	8b89                	andi	a5,a5,2
ffffffffc0200938:	46079363          	bnez	a5,ffffffffc0200d9e <best_fit_check+0x61a>
    cprintf("grading: %d / %d points\n", score, sumscore);
#endif
    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));
    assert(alloc_page() == NULL);
ffffffffc020093c:	4505                	li	a0,1
    list_entry_t free_list_store = free_list;
ffffffffc020093e:	00043b03          	ld	s6,0(s0)
ffffffffc0200942:	00843a83          	ld	s5,8(s0)
ffffffffc0200946:	e000                	sd	s0,0(s0)
ffffffffc0200948:	e400                	sd	s0,8(s0)
    assert(alloc_page() == NULL);
ffffffffc020094a:	618000ef          	jal	ra,ffffffffc0200f62 <alloc_pages>
ffffffffc020094e:	42051863          	bnez	a0,ffffffffc0200d7e <best_fit_check+0x5fa>
#endif
    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    // * - - * -
    free_pages(p0 + 1, 2);
ffffffffc0200952:	4589                	li	a1,2
ffffffffc0200954:	02898513          	addi	a0,s3,40
    unsigned int nr_free_store = nr_free;
ffffffffc0200958:	01042b83          	lw	s7,16(s0)
    free_pages(p0 + 4, 1);
ffffffffc020095c:	0a098c13          	addi	s8,s3,160
    nr_free = 0;
ffffffffc0200960:	00005797          	auipc	a5,0x5
ffffffffc0200964:	6c07a423          	sw	zero,1736(a5) # ffffffffc0206028 <free_area+0x10>
    free_pages(p0 + 1, 2);
ffffffffc0200968:	606000ef          	jal	ra,ffffffffc0200f6e <free_pages>
    free_pages(p0 + 4, 1);
ffffffffc020096c:	8562                	mv	a0,s8
ffffffffc020096e:	4585                	li	a1,1
ffffffffc0200970:	5fe000ef          	jal	ra,ffffffffc0200f6e <free_pages>
    assert(alloc_pages(4) == NULL);
ffffffffc0200974:	4511                	li	a0,4
ffffffffc0200976:	5ec000ef          	jal	ra,ffffffffc0200f62 <alloc_pages>
ffffffffc020097a:	3e051263          	bnez	a0,ffffffffc0200d5e <best_fit_check+0x5da>
    assert(PageProperty(p0 + 1) && p0[1].property == 2);
ffffffffc020097e:	0309b783          	ld	a5,48(s3)
ffffffffc0200982:	8b89                	andi	a5,a5,2
ffffffffc0200984:	3a078d63          	beqz	a5,ffffffffc0200d3e <best_fit_check+0x5ba>
ffffffffc0200988:	0389a703          	lw	a4,56(s3)
ffffffffc020098c:	4789                	li	a5,2
ffffffffc020098e:	3af71863          	bne	a4,a5,ffffffffc0200d3e <best_fit_check+0x5ba>
    // * - - * *
    assert((p1 = alloc_pages(1)) != NULL);
ffffffffc0200992:	4505                	li	a0,1
ffffffffc0200994:	5ce000ef          	jal	ra,ffffffffc0200f62 <alloc_pages>
ffffffffc0200998:	8a2a                	mv	s4,a0
ffffffffc020099a:	38050263          	beqz	a0,ffffffffc0200d1e <best_fit_check+0x59a>
    assert(alloc_pages(2) != NULL);
ffffffffc020099e:	4509                	li	a0,2
ffffffffc02009a0:	5c2000ef          	jal	ra,ffffffffc0200f62 <alloc_pages>
ffffffffc02009a4:	34050d63          	beqz	a0,ffffffffc0200cfe <best_fit_check+0x57a>
    assert(p0 + 4 == p1);
ffffffffc02009a8:	334c1b63          	bne	s8,s4,ffffffffc0200cde <best_fit_check+0x55a>
#ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
#endif
    p2 = p0 + 1;
    free_pages(p0, 5);
ffffffffc02009ac:	854e                	mv	a0,s3
ffffffffc02009ae:	4595                	li	a1,5
ffffffffc02009b0:	5be000ef          	jal	ra,ffffffffc0200f6e <free_pages>
    assert((p0 = alloc_pages(5)) != NULL);
ffffffffc02009b4:	4515                	li	a0,5
ffffffffc02009b6:	5ac000ef          	jal	ra,ffffffffc0200f62 <alloc_pages>
ffffffffc02009ba:	89aa                	mv	s3,a0
ffffffffc02009bc:	30050163          	beqz	a0,ffffffffc0200cbe <best_fit_check+0x53a>
    assert(alloc_page() == NULL);
ffffffffc02009c0:	4505                	li	a0,1
ffffffffc02009c2:	5a0000ef          	jal	ra,ffffffffc0200f62 <alloc_pages>
ffffffffc02009c6:	2c051c63          	bnez	a0,ffffffffc0200c9e <best_fit_check+0x51a>

#ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
#endif
    assert(nr_free == 0);
ffffffffc02009ca:	481c                	lw	a5,16(s0)
ffffffffc02009cc:	2a079963          	bnez	a5,ffffffffc0200c7e <best_fit_check+0x4fa>
    nr_free = nr_free_store;

    free_list = free_list_store;
    free_pages(p0, 5);
ffffffffc02009d0:	4595                	li	a1,5
ffffffffc02009d2:	854e                	mv	a0,s3
    nr_free = nr_free_store;
ffffffffc02009d4:	01742823          	sw	s7,16(s0)
    free_list = free_list_store;
ffffffffc02009d8:	01643023          	sd	s6,0(s0)
ffffffffc02009dc:	01543423          	sd	s5,8(s0)
    free_pages(p0, 5);
ffffffffc02009e0:	58e000ef          	jal	ra,ffffffffc0200f6e <free_pages>
    return listelm->next;
ffffffffc02009e4:	641c                	ld	a5,8(s0)

    le = &free_list;
    while ((le = list_next(le)) != &free_list)
ffffffffc02009e6:	00878963          	beq	a5,s0,ffffffffc02009f8 <best_fit_check+0x274>
    {
        struct Page *p = le2page(le, page_link);
        count--, total -= p->property;
ffffffffc02009ea:	ff87a703          	lw	a4,-8(a5)
ffffffffc02009ee:	679c                	ld	a5,8(a5)
ffffffffc02009f0:	397d                	addiw	s2,s2,-1
ffffffffc02009f2:	9c99                	subw	s1,s1,a4
    while ((le = list_next(le)) != &free_list)
ffffffffc02009f4:	fe879be3          	bne	a5,s0,ffffffffc02009ea <best_fit_check+0x266>
    }
    assert(count == 0);
ffffffffc02009f8:	26091363          	bnez	s2,ffffffffc0200c5e <best_fit_check+0x4da>
    assert(total == 0);
ffffffffc02009fc:	e0ed                	bnez	s1,ffffffffc0200ade <best_fit_check+0x35a>
#ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
#endif
}
ffffffffc02009fe:	60a6                	ld	ra,72(sp)
ffffffffc0200a00:	6406                	ld	s0,64(sp)
ffffffffc0200a02:	74e2                	ld	s1,56(sp)
ffffffffc0200a04:	7942                	ld	s2,48(sp)
ffffffffc0200a06:	79a2                	ld	s3,40(sp)
ffffffffc0200a08:	7a02                	ld	s4,32(sp)
ffffffffc0200a0a:	6ae2                	ld	s5,24(sp)
ffffffffc0200a0c:	6b42                	ld	s6,16(sp)
ffffffffc0200a0e:	6ba2                	ld	s7,8(sp)
ffffffffc0200a10:	6c02                	ld	s8,0(sp)
ffffffffc0200a12:	6161                	addi	sp,sp,80
ffffffffc0200a14:	8082                	ret
    while ((le = list_next(le)) != &free_list)
ffffffffc0200a16:	4981                	li	s3,0
    int count = 0, total = 0;
ffffffffc0200a18:	4481                	li	s1,0
ffffffffc0200a1a:	4901                	li	s2,0
ffffffffc0200a1c:	b36d                	j	ffffffffc02007c6 <best_fit_check+0x42>
        assert(PageProperty(p));
ffffffffc0200a1e:	00001697          	auipc	a3,0x1
ffffffffc0200a22:	0a268693          	addi	a3,a3,162 # ffffffffc0201ac0 <etext+0x29c>
ffffffffc0200a26:	00001617          	auipc	a2,0x1
ffffffffc0200a2a:	06a60613          	addi	a2,a2,106 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200a2e:	14a00593          	li	a1,330
ffffffffc0200a32:	00001517          	auipc	a0,0x1
ffffffffc0200a36:	07650513          	addi	a0,a0,118 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200a3a:	f8cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(p0 != p1 && p0 != p2 && p1 != p2);
ffffffffc0200a3e:	00001697          	auipc	a3,0x1
ffffffffc0200a42:	11268693          	addi	a3,a3,274 # ffffffffc0201b50 <etext+0x32c>
ffffffffc0200a46:	00001617          	auipc	a2,0x1
ffffffffc0200a4a:	04a60613          	addi	a2,a2,74 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200a4e:	11600593          	li	a1,278
ffffffffc0200a52:	00001517          	auipc	a0,0x1
ffffffffc0200a56:	05650513          	addi	a0,a0,86 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200a5a:	f6cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);
ffffffffc0200a5e:	00001697          	auipc	a3,0x1
ffffffffc0200a62:	11a68693          	addi	a3,a3,282 # ffffffffc0201b78 <etext+0x354>
ffffffffc0200a66:	00001617          	auipc	a2,0x1
ffffffffc0200a6a:	02a60613          	addi	a2,a2,42 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200a6e:	11700593          	li	a1,279
ffffffffc0200a72:	00001517          	auipc	a0,0x1
ffffffffc0200a76:	03650513          	addi	a0,a0,54 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200a7a:	f4cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(page2pa(p0) < npage * PGSIZE);
ffffffffc0200a7e:	00001697          	auipc	a3,0x1
ffffffffc0200a82:	13a68693          	addi	a3,a3,314 # ffffffffc0201bb8 <etext+0x394>
ffffffffc0200a86:	00001617          	auipc	a2,0x1
ffffffffc0200a8a:	00a60613          	addi	a2,a2,10 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200a8e:	11900593          	li	a1,281
ffffffffc0200a92:	00001517          	auipc	a0,0x1
ffffffffc0200a96:	01650513          	addi	a0,a0,22 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200a9a:	f2cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(!list_empty(&free_list));
ffffffffc0200a9e:	00001697          	auipc	a3,0x1
ffffffffc0200aa2:	1a268693          	addi	a3,a3,418 # ffffffffc0201c40 <etext+0x41c>
ffffffffc0200aa6:	00001617          	auipc	a2,0x1
ffffffffc0200aaa:	fea60613          	addi	a2,a2,-22 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200aae:	13200593          	li	a1,306
ffffffffc0200ab2:	00001517          	auipc	a0,0x1
ffffffffc0200ab6:	ff650513          	addi	a0,a0,-10 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200aba:	f0cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert((p2 = alloc_page()) != NULL);
ffffffffc0200abe:	00001697          	auipc	a3,0x1
ffffffffc0200ac2:	07268693          	addi	a3,a3,114 # ffffffffc0201b30 <etext+0x30c>
ffffffffc0200ac6:	00001617          	auipc	a2,0x1
ffffffffc0200aca:	fca60613          	addi	a2,a2,-54 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200ace:	11400593          	li	a1,276
ffffffffc0200ad2:	00001517          	auipc	a0,0x1
ffffffffc0200ad6:	fd650513          	addi	a0,a0,-42 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200ada:	eecff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(total == 0);
ffffffffc0200ade:	00001697          	auipc	a3,0x1
ffffffffc0200ae2:	29268693          	addi	a3,a3,658 # ffffffffc0201d70 <etext+0x54c>
ffffffffc0200ae6:	00001617          	auipc	a2,0x1
ffffffffc0200aea:	faa60613          	addi	a2,a2,-86 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200aee:	18d00593          	li	a1,397
ffffffffc0200af2:	00001517          	auipc	a0,0x1
ffffffffc0200af6:	fb650513          	addi	a0,a0,-74 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200afa:	eccff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(total == nr_free_pages());
ffffffffc0200afe:	00001697          	auipc	a3,0x1
ffffffffc0200b02:	fd268693          	addi	a3,a3,-46 # ffffffffc0201ad0 <etext+0x2ac>
ffffffffc0200b06:	00001617          	auipc	a2,0x1
ffffffffc0200b0a:	f8a60613          	addi	a2,a2,-118 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200b0e:	14d00593          	li	a1,333
ffffffffc0200b12:	00001517          	auipc	a0,0x1
ffffffffc0200b16:	f9650513          	addi	a0,a0,-106 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200b1a:	eacff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert((p1 = alloc_page()) != NULL);
ffffffffc0200b1e:	00001697          	auipc	a3,0x1
ffffffffc0200b22:	ff268693          	addi	a3,a3,-14 # ffffffffc0201b10 <etext+0x2ec>
ffffffffc0200b26:	00001617          	auipc	a2,0x1
ffffffffc0200b2a:	f6a60613          	addi	a2,a2,-150 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200b2e:	11300593          	li	a1,275
ffffffffc0200b32:	00001517          	auipc	a0,0x1
ffffffffc0200b36:	f7650513          	addi	a0,a0,-138 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200b3a:	e8cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert((p0 = alloc_page()) != NULL);
ffffffffc0200b3e:	00001697          	auipc	a3,0x1
ffffffffc0200b42:	fb268693          	addi	a3,a3,-78 # ffffffffc0201af0 <etext+0x2cc>
ffffffffc0200b46:	00001617          	auipc	a2,0x1
ffffffffc0200b4a:	f4a60613          	addi	a2,a2,-182 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200b4e:	11200593          	li	a1,274
ffffffffc0200b52:	00001517          	auipc	a0,0x1
ffffffffc0200b56:	f5650513          	addi	a0,a0,-170 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200b5a:	e6cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(alloc_page() == NULL);
ffffffffc0200b5e:	00001697          	auipc	a3,0x1
ffffffffc0200b62:	0ba68693          	addi	a3,a3,186 # ffffffffc0201c18 <etext+0x3f4>
ffffffffc0200b66:	00001617          	auipc	a2,0x1
ffffffffc0200b6a:	f2a60613          	addi	a2,a2,-214 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200b6e:	12f00593          	li	a1,303
ffffffffc0200b72:	00001517          	auipc	a0,0x1
ffffffffc0200b76:	f3650513          	addi	a0,a0,-202 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200b7a:	e4cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert((p2 = alloc_page()) != NULL);
ffffffffc0200b7e:	00001697          	auipc	a3,0x1
ffffffffc0200b82:	fb268693          	addi	a3,a3,-78 # ffffffffc0201b30 <etext+0x30c>
ffffffffc0200b86:	00001617          	auipc	a2,0x1
ffffffffc0200b8a:	f0a60613          	addi	a2,a2,-246 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200b8e:	12d00593          	li	a1,301
ffffffffc0200b92:	00001517          	auipc	a0,0x1
ffffffffc0200b96:	f1650513          	addi	a0,a0,-234 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200b9a:	e2cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert((p1 = alloc_page()) != NULL);
ffffffffc0200b9e:	00001697          	auipc	a3,0x1
ffffffffc0200ba2:	f7268693          	addi	a3,a3,-142 # ffffffffc0201b10 <etext+0x2ec>
ffffffffc0200ba6:	00001617          	auipc	a2,0x1
ffffffffc0200baa:	eea60613          	addi	a2,a2,-278 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200bae:	12c00593          	li	a1,300
ffffffffc0200bb2:	00001517          	auipc	a0,0x1
ffffffffc0200bb6:	ef650513          	addi	a0,a0,-266 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200bba:	e0cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert((p0 = alloc_page()) != NULL);
ffffffffc0200bbe:	00001697          	auipc	a3,0x1
ffffffffc0200bc2:	f3268693          	addi	a3,a3,-206 # ffffffffc0201af0 <etext+0x2cc>
ffffffffc0200bc6:	00001617          	auipc	a2,0x1
ffffffffc0200bca:	eca60613          	addi	a2,a2,-310 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200bce:	12b00593          	li	a1,299
ffffffffc0200bd2:	00001517          	auipc	a0,0x1
ffffffffc0200bd6:	ed650513          	addi	a0,a0,-298 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200bda:	decff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(nr_free == 3);
ffffffffc0200bde:	00001697          	auipc	a3,0x1
ffffffffc0200be2:	05268693          	addi	a3,a3,82 # ffffffffc0201c30 <etext+0x40c>
ffffffffc0200be6:	00001617          	auipc	a2,0x1
ffffffffc0200bea:	eaa60613          	addi	a2,a2,-342 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200bee:	12900593          	li	a1,297
ffffffffc0200bf2:	00001517          	auipc	a0,0x1
ffffffffc0200bf6:	eb650513          	addi	a0,a0,-330 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200bfa:	dccff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(alloc_page() == NULL);
ffffffffc0200bfe:	00001697          	auipc	a3,0x1
ffffffffc0200c02:	01a68693          	addi	a3,a3,26 # ffffffffc0201c18 <etext+0x3f4>
ffffffffc0200c06:	00001617          	auipc	a2,0x1
ffffffffc0200c0a:	e8a60613          	addi	a2,a2,-374 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200c0e:	12400593          	li	a1,292
ffffffffc0200c12:	00001517          	auipc	a0,0x1
ffffffffc0200c16:	e9650513          	addi	a0,a0,-362 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200c1a:	dacff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(page2pa(p2) < npage * PGSIZE);
ffffffffc0200c1e:	00001697          	auipc	a3,0x1
ffffffffc0200c22:	fda68693          	addi	a3,a3,-38 # ffffffffc0201bf8 <etext+0x3d4>
ffffffffc0200c26:	00001617          	auipc	a2,0x1
ffffffffc0200c2a:	e6a60613          	addi	a2,a2,-406 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200c2e:	11b00593          	li	a1,283
ffffffffc0200c32:	00001517          	auipc	a0,0x1
ffffffffc0200c36:	e7650513          	addi	a0,a0,-394 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200c3a:	d8cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(page2pa(p1) < npage * PGSIZE);
ffffffffc0200c3e:	00001697          	auipc	a3,0x1
ffffffffc0200c42:	f9a68693          	addi	a3,a3,-102 # ffffffffc0201bd8 <etext+0x3b4>
ffffffffc0200c46:	00001617          	auipc	a2,0x1
ffffffffc0200c4a:	e4a60613          	addi	a2,a2,-438 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200c4e:	11a00593          	li	a1,282
ffffffffc0200c52:	00001517          	auipc	a0,0x1
ffffffffc0200c56:	e5650513          	addi	a0,a0,-426 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200c5a:	d6cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(count == 0);
ffffffffc0200c5e:	00001697          	auipc	a3,0x1
ffffffffc0200c62:	10268693          	addi	a3,a3,258 # ffffffffc0201d60 <etext+0x53c>
ffffffffc0200c66:	00001617          	auipc	a2,0x1
ffffffffc0200c6a:	e2a60613          	addi	a2,a2,-470 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200c6e:	18c00593          	li	a1,396
ffffffffc0200c72:	00001517          	auipc	a0,0x1
ffffffffc0200c76:	e3650513          	addi	a0,a0,-458 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200c7a:	d4cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(nr_free == 0);
ffffffffc0200c7e:	00001697          	auipc	a3,0x1
ffffffffc0200c82:	ffa68693          	addi	a3,a3,-6 # ffffffffc0201c78 <etext+0x454>
ffffffffc0200c86:	00001617          	auipc	a2,0x1
ffffffffc0200c8a:	e0a60613          	addi	a2,a2,-502 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200c8e:	18000593          	li	a1,384
ffffffffc0200c92:	00001517          	auipc	a0,0x1
ffffffffc0200c96:	e1650513          	addi	a0,a0,-490 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200c9a:	d2cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(alloc_page() == NULL);
ffffffffc0200c9e:	00001697          	auipc	a3,0x1
ffffffffc0200ca2:	f7a68693          	addi	a3,a3,-134 # ffffffffc0201c18 <etext+0x3f4>
ffffffffc0200ca6:	00001617          	auipc	a2,0x1
ffffffffc0200caa:	dea60613          	addi	a2,a2,-534 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200cae:	17a00593          	li	a1,378
ffffffffc0200cb2:	00001517          	auipc	a0,0x1
ffffffffc0200cb6:	df650513          	addi	a0,a0,-522 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200cba:	d0cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert((p0 = alloc_pages(5)) != NULL);
ffffffffc0200cbe:	00001697          	auipc	a3,0x1
ffffffffc0200cc2:	08268693          	addi	a3,a3,130 # ffffffffc0201d40 <etext+0x51c>
ffffffffc0200cc6:	00001617          	auipc	a2,0x1
ffffffffc0200cca:	dca60613          	addi	a2,a2,-566 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200cce:	17900593          	li	a1,377
ffffffffc0200cd2:	00001517          	auipc	a0,0x1
ffffffffc0200cd6:	dd650513          	addi	a0,a0,-554 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200cda:	cecff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(p0 + 4 == p1);
ffffffffc0200cde:	00001697          	auipc	a3,0x1
ffffffffc0200ce2:	05268693          	addi	a3,a3,82 # ffffffffc0201d30 <etext+0x50c>
ffffffffc0200ce6:	00001617          	auipc	a2,0x1
ffffffffc0200cea:	daa60613          	addi	a2,a2,-598 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200cee:	17100593          	li	a1,369
ffffffffc0200cf2:	00001517          	auipc	a0,0x1
ffffffffc0200cf6:	db650513          	addi	a0,a0,-586 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200cfa:	cccff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(alloc_pages(2) != NULL);
ffffffffc0200cfe:	00001697          	auipc	a3,0x1
ffffffffc0200d02:	01a68693          	addi	a3,a3,26 # ffffffffc0201d18 <etext+0x4f4>
ffffffffc0200d06:	00001617          	auipc	a2,0x1
ffffffffc0200d0a:	d8a60613          	addi	a2,a2,-630 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200d0e:	17000593          	li	a1,368
ffffffffc0200d12:	00001517          	auipc	a0,0x1
ffffffffc0200d16:	d9650513          	addi	a0,a0,-618 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200d1a:	cacff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert((p1 = alloc_pages(1)) != NULL);
ffffffffc0200d1e:	00001697          	auipc	a3,0x1
ffffffffc0200d22:	fda68693          	addi	a3,a3,-38 # ffffffffc0201cf8 <etext+0x4d4>
ffffffffc0200d26:	00001617          	auipc	a2,0x1
ffffffffc0200d2a:	d6a60613          	addi	a2,a2,-662 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200d2e:	16f00593          	li	a1,367
ffffffffc0200d32:	00001517          	auipc	a0,0x1
ffffffffc0200d36:	d7650513          	addi	a0,a0,-650 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200d3a:	c8cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(PageProperty(p0 + 1) && p0[1].property == 2);
ffffffffc0200d3e:	00001697          	auipc	a3,0x1
ffffffffc0200d42:	f8a68693          	addi	a3,a3,-118 # ffffffffc0201cc8 <etext+0x4a4>
ffffffffc0200d46:	00001617          	auipc	a2,0x1
ffffffffc0200d4a:	d4a60613          	addi	a2,a2,-694 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200d4e:	16d00593          	li	a1,365
ffffffffc0200d52:	00001517          	auipc	a0,0x1
ffffffffc0200d56:	d5650513          	addi	a0,a0,-682 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200d5a:	c6cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(alloc_pages(4) == NULL);
ffffffffc0200d5e:	00001697          	auipc	a3,0x1
ffffffffc0200d62:	f5268693          	addi	a3,a3,-174 # ffffffffc0201cb0 <etext+0x48c>
ffffffffc0200d66:	00001617          	auipc	a2,0x1
ffffffffc0200d6a:	d2a60613          	addi	a2,a2,-726 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200d6e:	16c00593          	li	a1,364
ffffffffc0200d72:	00001517          	auipc	a0,0x1
ffffffffc0200d76:	d3650513          	addi	a0,a0,-714 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200d7a:	c4cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(alloc_page() == NULL);
ffffffffc0200d7e:	00001697          	auipc	a3,0x1
ffffffffc0200d82:	e9a68693          	addi	a3,a3,-358 # ffffffffc0201c18 <etext+0x3f4>
ffffffffc0200d86:	00001617          	auipc	a2,0x1
ffffffffc0200d8a:	d0a60613          	addi	a2,a2,-758 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200d8e:	16000593          	li	a1,352
ffffffffc0200d92:	00001517          	auipc	a0,0x1
ffffffffc0200d96:	d1650513          	addi	a0,a0,-746 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200d9a:	c2cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(!PageProperty(p0));
ffffffffc0200d9e:	00001697          	auipc	a3,0x1
ffffffffc0200da2:	efa68693          	addi	a3,a3,-262 # ffffffffc0201c98 <etext+0x474>
ffffffffc0200da6:	00001617          	auipc	a2,0x1
ffffffffc0200daa:	cea60613          	addi	a2,a2,-790 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200dae:	15700593          	li	a1,343
ffffffffc0200db2:	00001517          	auipc	a0,0x1
ffffffffc0200db6:	cf650513          	addi	a0,a0,-778 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200dba:	c0cff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(p0 != NULL);
ffffffffc0200dbe:	00001697          	auipc	a3,0x1
ffffffffc0200dc2:	eca68693          	addi	a3,a3,-310 # ffffffffc0201c88 <etext+0x464>
ffffffffc0200dc6:	00001617          	auipc	a2,0x1
ffffffffc0200dca:	cca60613          	addi	a2,a2,-822 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200dce:	15600593          	li	a1,342
ffffffffc0200dd2:	00001517          	auipc	a0,0x1
ffffffffc0200dd6:	cd650513          	addi	a0,a0,-810 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200dda:	becff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(nr_free == 0);
ffffffffc0200dde:	00001697          	auipc	a3,0x1
ffffffffc0200de2:	e9a68693          	addi	a3,a3,-358 # ffffffffc0201c78 <etext+0x454>
ffffffffc0200de6:	00001617          	auipc	a2,0x1
ffffffffc0200dea:	caa60613          	addi	a2,a2,-854 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200dee:	13800593          	li	a1,312
ffffffffc0200df2:	00001517          	auipc	a0,0x1
ffffffffc0200df6:	cb650513          	addi	a0,a0,-842 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200dfa:	bccff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(alloc_page() == NULL);
ffffffffc0200dfe:	00001697          	auipc	a3,0x1
ffffffffc0200e02:	e1a68693          	addi	a3,a3,-486 # ffffffffc0201c18 <etext+0x3f4>
ffffffffc0200e06:	00001617          	auipc	a2,0x1
ffffffffc0200e0a:	c8a60613          	addi	a2,a2,-886 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200e0e:	13600593          	li	a1,310
ffffffffc0200e12:	00001517          	auipc	a0,0x1
ffffffffc0200e16:	c9650513          	addi	a0,a0,-874 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200e1a:	bacff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert((p = alloc_page()) == p0);
ffffffffc0200e1e:	00001697          	auipc	a3,0x1
ffffffffc0200e22:	e3a68693          	addi	a3,a3,-454 # ffffffffc0201c58 <etext+0x434>
ffffffffc0200e26:	00001617          	auipc	a2,0x1
ffffffffc0200e2a:	c6a60613          	addi	a2,a2,-918 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200e2e:	13500593          	li	a1,309
ffffffffc0200e32:	00001517          	auipc	a0,0x1
ffffffffc0200e36:	c7650513          	addi	a0,a0,-906 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200e3a:	b8cff0ef          	jal	ra,ffffffffc02001c6 <__panic>

ffffffffc0200e3e <best_fit_free_pages>:
{
ffffffffc0200e3e:	1141                	addi	sp,sp,-16
ffffffffc0200e40:	e406                	sd	ra,8(sp)
    assert(n > 0); /* 释放页数必须为正 */
ffffffffc0200e42:	c5bd                	beqz	a1,ffffffffc0200eb0 <best_fit_free_pages+0x72>
    for (struct Page *p = base; p != base + n; p++)
ffffffffc0200e44:	00259693          	slli	a3,a1,0x2
ffffffffc0200e48:	96ae                	add	a3,a3,a1
ffffffffc0200e4a:	068e                	slli	a3,a3,0x3
ffffffffc0200e4c:	96aa                	add	a3,a3,a0
ffffffffc0200e4e:	87aa                	mv	a5,a0
ffffffffc0200e50:	00a68f63          	beq	a3,a0,ffffffffc0200e6e <best_fit_free_pages+0x30>
        assert(!PageReserved(p) && !PageProperty(p)); /* 不能释放 reserved 页或已空闲的块首页 */
ffffffffc0200e54:	6798                	ld	a4,8(a5)
ffffffffc0200e56:	8b0d                	andi	a4,a4,3
ffffffffc0200e58:	ef05                	bnez	a4,ffffffffc0200e90 <best_fit_free_pages+0x52>
        p->flags = 0;                                 /* 清空标志位 */
ffffffffc0200e5a:	0007b423          	sd	zero,8(a5)
        p->property = 0;                              /* 块大小先清零，稍后只设置到首页 */
ffffffffc0200e5e:	0007a823          	sw	zero,16(a5)



static inline int page_ref(struct Page *page) { return page->ref; }

static inline void set_page_ref(struct Page *page, int val) { page->ref = val; }
ffffffffc0200e62:	0007a023          	sw	zero,0(a5)
    for (struct Page *p = base; p != base + n; p++)
ffffffffc0200e66:	02878793          	addi	a5,a5,40
ffffffffc0200e6a:	fef695e3          	bne	a3,a5,ffffffffc0200e54 <best_fit_free_pages+0x16>
    SetPageProperty(base);       /* 标记 base 为“空闲块首页” */
ffffffffc0200e6e:	6518                	ld	a4,8(a0)
    nr_free += n;                /* 空闲页总数增加 n */
ffffffffc0200e70:	00005697          	auipc	a3,0x5
ffffffffc0200e74:	1a868693          	addi	a3,a3,424 # ffffffffc0206018 <free_area>
ffffffffc0200e78:	4a9c                	lw	a5,16(a3)
    base->property = n;          /* 该空闲块共 n 页，记在首页 */
ffffffffc0200e7a:	2581                	sext.w	a1,a1
    SetPageProperty(base);       /* 标记 base 为“空闲块首页” */
ffffffffc0200e7c:	00276713          	ori	a4,a4,2
    base->property = n;          /* 该空闲块共 n 页，记在首页 */
ffffffffc0200e80:	c90c                	sw	a1,16(a0)
}
ffffffffc0200e82:	60a2                	ld	ra,8(sp)
    SetPageProperty(base);       /* 标记 base 为“空闲块首页” */
ffffffffc0200e84:	e518                	sd	a4,8(a0)
    nr_free += n;                /* 空闲页总数增加 n */
ffffffffc0200e86:	9dbd                	addw	a1,a1,a5
ffffffffc0200e88:	ca8c                	sw	a1,16(a3)
}
ffffffffc0200e8a:	0141                	addi	sp,sp,16
    best_fit_insert_block(base); /* 插入链表并与左右相邻块合并 */
ffffffffc0200e8c:	f48ff06f          	j	ffffffffc02005d4 <best_fit_insert_block>
        assert(!PageReserved(p) && !PageProperty(p)); /* 不能释放 reserved 页或已空闲的块首页 */
ffffffffc0200e90:	00001697          	auipc	a3,0x1
ffffffffc0200e94:	ef068693          	addi	a3,a3,-272 # ffffffffc0201d80 <etext+0x55c>
ffffffffc0200e98:	00001617          	auipc	a2,0x1
ffffffffc0200e9c:	bf860613          	addi	a2,a2,-1032 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200ea0:	0db00593          	li	a1,219
ffffffffc0200ea4:	00001517          	auipc	a0,0x1
ffffffffc0200ea8:	c0450513          	addi	a0,a0,-1020 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200eac:	b1aff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(n > 0); /* 释放页数必须为正 */
ffffffffc0200eb0:	00001697          	auipc	a3,0x1
ffffffffc0200eb4:	bd868693          	addi	a3,a3,-1064 # ffffffffc0201a88 <etext+0x264>
ffffffffc0200eb8:	00001617          	auipc	a2,0x1
ffffffffc0200ebc:	bd860613          	addi	a2,a2,-1064 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200ec0:	0d800593          	li	a1,216
ffffffffc0200ec4:	00001517          	auipc	a0,0x1
ffffffffc0200ec8:	be450513          	addi	a0,a0,-1052 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200ecc:	afaff0ef          	jal	ra,ffffffffc02001c6 <__panic>

ffffffffc0200ed0 <best_fit_init_memmap>:
{
ffffffffc0200ed0:	1141                	addi	sp,sp,-16
ffffffffc0200ed2:	e406                	sd	ra,8(sp)
    assert(n > 0); /* 至少要把一个页纳入管理 */
ffffffffc0200ed4:	c5bd                	beqz	a1,ffffffffc0200f42 <best_fit_init_memmap+0x72>
    for (struct Page *p = base; p != base + n; p++)
ffffffffc0200ed6:	00259693          	slli	a3,a1,0x2
ffffffffc0200eda:	96ae                	add	a3,a3,a1
ffffffffc0200edc:	068e                	slli	a3,a3,0x3
ffffffffc0200ede:	96aa                	add	a3,a3,a0
ffffffffc0200ee0:	87aa                	mv	a5,a0
ffffffffc0200ee2:	00a68f63          	beq	a3,a0,ffffffffc0200f00 <best_fit_init_memmap+0x30>
        assert(PageReserved(p)); /* 约定：待管理的页此前必须是 reserved 状态 */
ffffffffc0200ee6:	6798                	ld	a4,8(a5)
ffffffffc0200ee8:	8b05                	andi	a4,a4,1
ffffffffc0200eea:	cf05                	beqz	a4,ffffffffc0200f22 <best_fit_init_memmap+0x52>
        p->flags = 0;            /* 清空所有标志位（含 reserved / property） */
ffffffffc0200eec:	0007b423          	sd	zero,8(a5)
        p->property = 0;         /* 块大小先清零，稍后只设置到首页 */
ffffffffc0200ef0:	0007a823          	sw	zero,16(a5)
ffffffffc0200ef4:	0007a023          	sw	zero,0(a5)
    for (struct Page *p = base; p != base + n; p++)
ffffffffc0200ef8:	02878793          	addi	a5,a5,40
ffffffffc0200efc:	fef695e3          	bne	a3,a5,ffffffffc0200ee6 <best_fit_init_memmap+0x16>
    SetPageProperty(base);       /* 标记 base 为“空闲块首页” */
ffffffffc0200f00:	6518                	ld	a4,8(a0)
    nr_free += n;                /* 空闲页总数增加 n */
ffffffffc0200f02:	00005697          	auipc	a3,0x5
ffffffffc0200f06:	11668693          	addi	a3,a3,278 # ffffffffc0206018 <free_area>
ffffffffc0200f0a:	4a9c                	lw	a5,16(a3)
    base->property = n;          /* 整块共 n 页，把块大小记在首页 */
ffffffffc0200f0c:	2581                	sext.w	a1,a1
    SetPageProperty(base);       /* 标记 base 为“空闲块首页” */
ffffffffc0200f0e:	00276713          	ori	a4,a4,2
    base->property = n;          /* 整块共 n 页，把块大小记在首页 */
ffffffffc0200f12:	c90c                	sw	a1,16(a0)
}
ffffffffc0200f14:	60a2                	ld	ra,8(sp)
    SetPageProperty(base);       /* 标记 base 为“空闲块首页” */
ffffffffc0200f16:	e518                	sd	a4,8(a0)
    nr_free += n;                /* 空闲页总数增加 n */
ffffffffc0200f18:	9dbd                	addw	a1,a1,a5
ffffffffc0200f1a:	ca8c                	sw	a1,16(a3)
}
ffffffffc0200f1c:	0141                	addi	sp,sp,16
    best_fit_insert_block(base); /* 按地址序插入链表并与相邻块合并 */
ffffffffc0200f1e:	eb6ff06f          	j	ffffffffc02005d4 <best_fit_insert_block>
        assert(PageReserved(p)); /* 约定：待管理的页此前必须是 reserved 状态 */
ffffffffc0200f22:	00001697          	auipc	a3,0x1
ffffffffc0200f26:	e8668693          	addi	a3,a3,-378 # ffffffffc0201da8 <etext+0x584>
ffffffffc0200f2a:	00001617          	auipc	a2,0x1
ffffffffc0200f2e:	b6660613          	addi	a2,a2,-1178 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200f32:	07c00593          	li	a1,124
ffffffffc0200f36:	00001517          	auipc	a0,0x1
ffffffffc0200f3a:	b7250513          	addi	a0,a0,-1166 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200f3e:	a88ff0ef          	jal	ra,ffffffffc02001c6 <__panic>
    assert(n > 0); /* 至少要把一个页纳入管理 */
ffffffffc0200f42:	00001697          	auipc	a3,0x1
ffffffffc0200f46:	b4668693          	addi	a3,a3,-1210 # ffffffffc0201a88 <etext+0x264>
ffffffffc0200f4a:	00001617          	auipc	a2,0x1
ffffffffc0200f4e:	b4660613          	addi	a2,a2,-1210 # ffffffffc0201a90 <etext+0x26c>
ffffffffc0200f52:	07900593          	li	a1,121
ffffffffc0200f56:	00001517          	auipc	a0,0x1
ffffffffc0200f5a:	b5250513          	addi	a0,a0,-1198 # ffffffffc0201aa8 <etext+0x284>
ffffffffc0200f5e:	a68ff0ef          	jal	ra,ffffffffc02001c6 <__panic>

ffffffffc0200f62 <alloc_pages>:
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
ffffffffc0200f62:	00005797          	auipc	a5,0x5
ffffffffc0200f66:	2d67b783          	ld	a5,726(a5) # ffffffffc0206238 <pmm_manager>
ffffffffc0200f6a:	6f9c                	ld	a5,24(a5)
ffffffffc0200f6c:	8782                	jr	a5

ffffffffc0200f6e <free_pages>:
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
ffffffffc0200f6e:	00005797          	auipc	a5,0x5
ffffffffc0200f72:	2ca7b783          	ld	a5,714(a5) # ffffffffc0206238 <pmm_manager>
ffffffffc0200f76:	739c                	ld	a5,32(a5)
ffffffffc0200f78:	8782                	jr	a5

ffffffffc0200f7a <nr_free_pages>:
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
ffffffffc0200f7a:	00005797          	auipc	a5,0x5
ffffffffc0200f7e:	2be7b783          	ld	a5,702(a5) # ffffffffc0206238 <pmm_manager>
ffffffffc0200f82:	779c                	ld	a5,40(a5)
ffffffffc0200f84:	8782                	jr	a5

ffffffffc0200f86 <pmm_init>:
    pmm_manager = &best_fit_pmm_manager;       /* 默认（Best-Fit 实验）：注册最佳匹配管理器 */
ffffffffc0200f86:	00001797          	auipc	a5,0x1
ffffffffc0200f8a:	e4a78793          	addi	a5,a5,-438 # ffffffffc0201dd0 <best_fit_pmm_manager>
    cprintf("memory management: %s\n", pmm_manager->name);
ffffffffc0200f8e:	638c                	ld	a1,0(a5)
        init_memmap(&pages[PPN(free_start_pa) - nbase], nr_free_pages_cnt);
    }
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
ffffffffc0200f90:	7119                	addi	sp,sp,-128
ffffffffc0200f92:	f0ca                	sd	s2,96(sp)
    cprintf("memory management: %s\n", pmm_manager->name);
ffffffffc0200f94:	00001517          	auipc	a0,0x1
ffffffffc0200f98:	e7450513          	addi	a0,a0,-396 # ffffffffc0201e08 <best_fit_pmm_manager+0x38>
    pmm_manager = &best_fit_pmm_manager;       /* 默认（Best-Fit 实验）：注册最佳匹配管理器 */
ffffffffc0200f9c:	00005917          	auipc	s2,0x5
ffffffffc0200fa0:	29c90913          	addi	s2,s2,668 # ffffffffc0206238 <pmm_manager>
void pmm_init(void) {
ffffffffc0200fa4:	fc86                	sd	ra,120(sp)
ffffffffc0200fa6:	e4d6                	sd	s5,72(sp)
ffffffffc0200fa8:	e0da                	sd	s6,64(sp)
    pmm_manager = &best_fit_pmm_manager;       /* 默认（Best-Fit 实验）：注册最佳匹配管理器 */
ffffffffc0200faa:	00f93023          	sd	a5,0(s2)
void pmm_init(void) {
ffffffffc0200fae:	f8a2                	sd	s0,112(sp)
ffffffffc0200fb0:	f4a6                	sd	s1,104(sp)
ffffffffc0200fb2:	ecce                	sd	s3,88(sp)
ffffffffc0200fb4:	e8d2                	sd	s4,80(sp)
ffffffffc0200fb6:	fc5e                	sd	s7,56(sp)
ffffffffc0200fb8:	f862                	sd	s8,48(sp)
ffffffffc0200fba:	f466                	sd	s9,40(sp)
ffffffffc0200fbc:	f06a                	sd	s10,32(sp)
ffffffffc0200fbe:	ec6e                	sd	s11,24(sp)
    cprintf("memory management: %s\n", pmm_manager->name);
ffffffffc0200fc0:	990ff0ef          	jal	ra,ffffffffc0200150 <cprintf>
    pmm_manager->init();
ffffffffc0200fc4:	00093783          	ld	a5,0(s2)
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;
ffffffffc0200fc8:	00005a97          	auipc	s5,0x5
ffffffffc0200fcc:	288a8a93          	addi	s5,s5,648 # ffffffffc0206250 <va_pa_offset>
    pmm_manager->init();
ffffffffc0200fd0:	679c                	ld	a5,8(a5)
ffffffffc0200fd2:	9782                	jalr	a5
    uint64_t mem_base = get_memory_base();
ffffffffc0200fd4:	decff0ef          	jal	ra,ffffffffc02005c0 <get_memory_base>
ffffffffc0200fd8:	8b2a                	mv	s6,a0
    uint64_t mem_size = get_memory_size();
ffffffffc0200fda:	df0ff0ef          	jal	ra,ffffffffc02005ca <get_memory_size>
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;
ffffffffc0200fde:	57f5                	li	a5,-3
ffffffffc0200fe0:	07fa                	slli	a5,a5,0x1e
ffffffffc0200fe2:	00fab023          	sd	a5,0(s5)
    if (mem_base == 0 || mem_size == 0) {
ffffffffc0200fe6:	240b0d63          	beqz	s6,ffffffffc0201240 <pmm_init+0x2ba>
ffffffffc0200fea:	24050b63          	beqz	a0,ffffffffc0201240 <pmm_init+0x2ba>
    if (mem_base < DRAM_BASE) {
ffffffffc0200fee:	800007b7          	lui	a5,0x80000
ffffffffc0200ff2:	fff7c793          	not	a5,a5
ffffffffc0200ff6:	2967fd63          	bgeu	a5,s6,ffffffffc0201290 <pmm_init+0x30a>
    if (mem_size > ~(uint64_t)0 - mem_base) {
ffffffffc0200ffa:	fffb4793          	not	a5,s6
ffffffffc0200ffe:	32a7e063          	bltu	a5,a0,ffffffffc020131e <pmm_init+0x398>
    if (mem_base > KERNEL_BEGIN_PADDR) {
ffffffffc0201002:	40100713          	li	a4,1025
ffffffffc0201006:	0756                	slli	a4,a4,0x15
    probe_end = mem_base + mem_size;
ffffffffc0201008:	00ab0c33          	add	s8,s6,a0
    if (mem_base > KERNEL_BEGIN_PADDR) {
ffffffffc020100c:	2b676163          	bltu	a4,s6,ffffffffc02012ae <pmm_init+0x328>
    mem_end = ROUNDDOWN(mem_end, PGSIZE);
ffffffffc0201010:	47c5                	li	a5,17
ffffffffc0201012:	07ee                	slli	a5,a5,0x1b
ffffffffc0201014:	8de2                	mv	s11,s8
ffffffffc0201016:	1787ef63          	bltu	a5,s8,ffffffffc0201194 <pmm_init+0x20e>
ffffffffc020101a:	76fd                	lui	a3,0xfffff
ffffffffc020101c:	00ddfdb3          	and	s11,s11,a3
    if (mem_end <= mem_base || mem_end <= DRAM_BASE) {
ffffffffc0201020:	2fbb7163          	bgeu	s6,s11,ffffffffc0201302 <pmm_init+0x37c>
    npage = PPN(mem_end);
ffffffffc0201024:	00cdd793          	srli	a5,s11,0xc
ffffffffc0201028:	00005a17          	auipc	s4,0x5
ffffffffc020102c:	200a0a13          	addi	s4,s4,512 # ffffffffc0206228 <npage>
ffffffffc0201030:	00fa3023          	sd	a5,0(s4)
    if (npage <= nbase) {
ffffffffc0201034:	00080d37          	lui	s10,0x80
ffffffffc0201038:	2ba78563          	beq	a5,s10,ffffffffc02012e2 <pmm_init+0x35c>
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);
ffffffffc020103c:	00006417          	auipc	s0,0x6
ffffffffc0201040:	21b40413          	addi	s0,s0,539 # ffffffffc0207257 <end+0xfff>
ffffffffc0201044:	8c75                	and	s0,s0,a3
ffffffffc0201046:	00005c97          	auipc	s9,0x5
ffffffffc020104a:	1eac8c93          	addi	s9,s9,490 # ffffffffc0206230 <pages>
ffffffffc020104e:	008cb023          	sd	s0,0(s9)
    pages_pa = PADDR((uintptr_t)pages);
ffffffffc0201052:	c02006b7          	lui	a3,0xc0200
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);
ffffffffc0201056:	00005497          	auipc	s1,0x5
ffffffffc020105a:	20248493          	addi	s1,s1,514 # ffffffffc0206258 <end>
    pages_pa = PADDR((uintptr_t)pages);
ffffffffc020105e:	26d46563          	bltu	s0,a3,ffffffffc02012c8 <pmm_init+0x342>
ffffffffc0201062:	470d                	li	a4,3
ffffffffc0201064:	077a                	slli	a4,a4,0x1e
ffffffffc0201066:	00e409b3          	add	s3,s0,a4
    kernel_end_pa = PADDR((uintptr_t)end);
ffffffffc020106a:	1ed4e963          	bltu	s1,a3,ffffffffc020125c <pmm_init+0x2d6>
    nr_descs = npage - nbase;
ffffffffc020106e:	41a78d33          	sub	s10,a5,s10
    desc_size = nr_descs * sizeof(struct Page);
ffffffffc0201072:	002d1b93          	slli	s7,s10,0x2
ffffffffc0201076:	9bea                	add	s7,s7,s10
ffffffffc0201078:	0b8e                	slli	s7,s7,0x3
    if (desc_size > mem_end || pages_pa > mem_end - desc_size) {
ffffffffc020107a:	417d86b3          	sub	a3,s11,s7
    kernel_end_pa = PADDR((uintptr_t)end);
ffffffffc020107e:	94ba                	add	s1,s1,a4
    if (desc_size > mem_end || pages_pa > mem_end - desc_size) {
ffffffffc0201080:	1b36e163          	bltu	a3,s3,ffffffffc0201222 <pmm_init+0x29c>
    memset(pages, 0, desc_size);
ffffffffc0201084:	865e                	mv	a2,s7
ffffffffc0201086:	4581                	li	a1,0
ffffffffc0201088:	8522                	mv	a0,s0
ffffffffc020108a:	e43e                	sd	a5,8(sp)
ffffffffc020108c:	786000ef          	jal	ra,ffffffffc0201812 <memset>
    for (i = 0; i < nr_descs; i++) {
ffffffffc0201090:	67a2                	ld	a5,8(sp)
        SetPageReserved(&pages[i]);
ffffffffc0201092:	000cb803          	ld	a6,0(s9)
ffffffffc0201096:	00279693          	slli	a3,a5,0x2
ffffffffc020109a:	96be                	add	a3,a3,a5
ffffffffc020109c:	fec007b7          	lui	a5,0xfec00
ffffffffc02010a0:	07a1                	addi	a5,a5,8
ffffffffc02010a2:	97c2                	add	a5,a5,a6
ffffffffc02010a4:	068e                	slli	a3,a3,0x3
ffffffffc02010a6:	0821                	addi	a6,a6,8
ffffffffc02010a8:	96be                	add	a3,a3,a5
ffffffffc02010aa:	00083883          	ld	a7,0(a6)
    for (i = 0; i < nr_descs; i++) {
ffffffffc02010ae:	02880813          	addi	a6,a6,40
        SetPageReserved(&pages[i]);
ffffffffc02010b2:	0018e893          	ori	a7,a7,1
ffffffffc02010b6:	fd183c23          	sd	a7,-40(a6)
    for (i = 0; i < nr_descs; i++) {
ffffffffc02010ba:	ff0698e3          	bne	a3,a6,ffffffffc02010aa <pmm_init+0x124>
    free_start_pa = (uintptr_t)ROUNDUP(pages_pa + desc_size, PGSIZE);
ffffffffc02010be:	00001697          	auipc	a3,0x1
ffffffffc02010c2:	4426b683          	ld	a3,1090(a3) # ffffffffc0202500 <error_string+0x40>
ffffffffc02010c6:	9436                	add	s0,s0,a3
ffffffffc02010c8:	945e                	add	s0,s0,s7
ffffffffc02010ca:	777d                	lui	a4,0xfffff
ffffffffc02010cc:	8c79                	and	s0,s0,a4
    cprintf("physical memory detected: [0x%016lx, 0x%016lx)\n",
ffffffffc02010ce:	8662                	mv	a2,s8
ffffffffc02010d0:	85da                	mv	a1,s6
    if (free_start_pa < mem_end) {
ffffffffc02010d2:	0db46363          	bltu	s0,s11,ffffffffc0201198 <pmm_init+0x212>
    cprintf("physical memory detected: [0x%016lx, 0x%016lx)\n",
ffffffffc02010d6:	00001517          	auipc	a0,0x1
ffffffffc02010da:	f4250513          	addi	a0,a0,-190 # ffffffffc0202018 <best_fit_pmm_manager+0x248>
ffffffffc02010de:	872ff0ef          	jal	ra,ffffffffc0200150 <cprintf>
    cprintf("physical memory managed:  [0x%016lx, 0x%016lx), npage: %lu\n",
ffffffffc02010e2:	000a3683          	ld	a3,0(s4)
ffffffffc02010e6:	85da                	mv	a1,s6
ffffffffc02010e8:	866e                	mv	a2,s11
ffffffffc02010ea:	00001517          	auipc	a0,0x1
ffffffffc02010ee:	f5e50513          	addi	a0,a0,-162 # ffffffffc0202048 <best_fit_pmm_manager+0x278>
ffffffffc02010f2:	85eff0ef          	jal	ra,ffffffffc0200150 <cprintf>
    cprintf("reserved: [0x%016lx, 0x%016lx) (firmware+kernel+Page array)\n",
ffffffffc02010f6:	4585                	li	a1,1
ffffffffc02010f8:	8622                	mv	a2,s0
ffffffffc02010fa:	05fe                	slli	a1,a1,0x1f
ffffffffc02010fc:	00001517          	auipc	a0,0x1
ffffffffc0201100:	f8c50513          	addi	a0,a0,-116 # ffffffffc0202088 <best_fit_pmm_manager+0x2b8>
ffffffffc0201104:	84cff0ef          	jal	ra,ffffffffc0200150 <cprintf>
    cprintf("free:     [0x%016lx, 0x%016lx), %lu pages\n",
ffffffffc0201108:	85a2                	mv	a1,s0
ffffffffc020110a:	866e                	mv	a2,s11
ffffffffc020110c:	4681                	li	a3,0
ffffffffc020110e:	00001517          	auipc	a0,0x1
ffffffffc0201112:	fba50513          	addi	a0,a0,-70 # ffffffffc02020c8 <best_fit_pmm_manager+0x2f8>
ffffffffc0201116:	83aff0ef          	jal	ra,ffffffffc0200150 <cprintf>
    cprintf("pages: 0x%016lx (physical 0x%016lx), %lu descriptors, kernel end 0x%016lx\n",
ffffffffc020111a:	000cb583          	ld	a1,0(s9)
ffffffffc020111e:	8726                	mv	a4,s1
ffffffffc0201120:	86ea                	mv	a3,s10
ffffffffc0201122:	864e                	mv	a2,s3
ffffffffc0201124:	00001517          	auipc	a0,0x1
ffffffffc0201128:	fd450513          	addi	a0,a0,-44 # ffffffffc02020f8 <best_fit_pmm_manager+0x328>
ffffffffc020112c:	824ff0ef          	jal	ra,ffffffffc0200150 <cprintf>
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
ffffffffc0201130:	00093783          	ld	a5,0(s2)
ffffffffc0201134:	7b9c                	ld	a5,48(a5)
ffffffffc0201136:	9782                	jalr	a5
    cprintf("check_alloc_page() succeeded!\n");
ffffffffc0201138:	00001517          	auipc	a0,0x1
ffffffffc020113c:	01050513          	addi	a0,a0,16 # ffffffffc0202148 <best_fit_pmm_manager+0x378>
ffffffffc0201140:	810ff0ef          	jal	ra,ffffffffc0200150 <cprintf>
    satp_virtual = (pte_t*)boot_page_table_sv39;
ffffffffc0201144:	00004597          	auipc	a1,0x4
ffffffffc0201148:	ebc58593          	addi	a1,a1,-324 # ffffffffc0205000 <boot_page_table_sv39>
ffffffffc020114c:	00005797          	auipc	a5,0x5
ffffffffc0201150:	0eb7be23          	sd	a1,252(a5) # ffffffffc0206248 <satp_virtual>
    satp_physical = PADDR(satp_virtual);
ffffffffc0201154:	c02007b7          	lui	a5,0xc0200
ffffffffc0201158:	10f5ef63          	bltu	a1,a5,ffffffffc0201276 <pmm_init+0x2f0>
ffffffffc020115c:	000ab603          	ld	a2,0(s5)
}
ffffffffc0201160:	7446                	ld	s0,112(sp)
ffffffffc0201162:	70e6                	ld	ra,120(sp)
ffffffffc0201164:	74a6                	ld	s1,104(sp)
ffffffffc0201166:	7906                	ld	s2,96(sp)
ffffffffc0201168:	69e6                	ld	s3,88(sp)
ffffffffc020116a:	6a46                	ld	s4,80(sp)
ffffffffc020116c:	6aa6                	ld	s5,72(sp)
ffffffffc020116e:	6b06                	ld	s6,64(sp)
ffffffffc0201170:	7be2                	ld	s7,56(sp)
ffffffffc0201172:	7c42                	ld	s8,48(sp)
ffffffffc0201174:	7ca2                	ld	s9,40(sp)
ffffffffc0201176:	7d02                	ld	s10,32(sp)
ffffffffc0201178:	6de2                	ld	s11,24(sp)
    satp_physical = PADDR(satp_virtual);
ffffffffc020117a:	40c58633          	sub	a2,a1,a2
ffffffffc020117e:	00005797          	auipc	a5,0x5
ffffffffc0201182:	0cc7b123          	sd	a2,194(a5) # ffffffffc0206240 <satp_physical>
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
ffffffffc0201186:	00001517          	auipc	a0,0x1
ffffffffc020118a:	fe250513          	addi	a0,a0,-30 # ffffffffc0202168 <best_fit_pmm_manager+0x398>
}
ffffffffc020118e:	6109                	addi	sp,sp,128
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
ffffffffc0201190:	fc1fe06f          	j	ffffffffc0200150 <cprintf>
    mem_end = ROUNDDOWN(mem_end, PGSIZE);
ffffffffc0201194:	8dbe                	mv	s11,a5
ffffffffc0201196:	b551                	j	ffffffffc020101a <pmm_init+0x94>
    cprintf("physical memory detected: [0x%016lx, 0x%016lx)\n",
ffffffffc0201198:	00001517          	auipc	a0,0x1
ffffffffc020119c:	e8050513          	addi	a0,a0,-384 # ffffffffc0202018 <best_fit_pmm_manager+0x248>
ffffffffc02011a0:	fb1fe0ef          	jal	ra,ffffffffc0200150 <cprintf>
    cprintf("physical memory managed:  [0x%016lx, 0x%016lx), npage: %lu\n",
ffffffffc02011a4:	000a3683          	ld	a3,0(s4)
ffffffffc02011a8:	85da                	mv	a1,s6
ffffffffc02011aa:	866e                	mv	a2,s11
ffffffffc02011ac:	00001517          	auipc	a0,0x1
ffffffffc02011b0:	e9c50513          	addi	a0,a0,-356 # ffffffffc0202048 <best_fit_pmm_manager+0x278>
ffffffffc02011b4:	f9dfe0ef          	jal	ra,ffffffffc0200150 <cprintf>
    cprintf("reserved: [0x%016lx, 0x%016lx) (firmware+kernel+Page array)\n",
ffffffffc02011b8:	4585                	li	a1,1
ffffffffc02011ba:	8622                	mv	a2,s0
ffffffffc02011bc:	05fe                	slli	a1,a1,0x1f
        nr_free_pages_cnt = (mem_end - free_start_pa) / PGSIZE;
ffffffffc02011be:	408d8bb3          	sub	s7,s11,s0
    cprintf("reserved: [0x%016lx, 0x%016lx) (firmware+kernel+Page array)\n",
ffffffffc02011c2:	00001517          	auipc	a0,0x1
ffffffffc02011c6:	ec650513          	addi	a0,a0,-314 # ffffffffc0202088 <best_fit_pmm_manager+0x2b8>
ffffffffc02011ca:	f87fe0ef          	jal	ra,ffffffffc0200150 <cprintf>
        nr_free_pages_cnt = (mem_end - free_start_pa) / PGSIZE;
ffffffffc02011ce:	00cbdb93          	srli	s7,s7,0xc
    cprintf("free:     [0x%016lx, 0x%016lx), %lu pages\n",
ffffffffc02011d2:	86de                	mv	a3,s7
ffffffffc02011d4:	866e                	mv	a2,s11
ffffffffc02011d6:	85a2                	mv	a1,s0
ffffffffc02011d8:	00001517          	auipc	a0,0x1
ffffffffc02011dc:	ef050513          	addi	a0,a0,-272 # ffffffffc02020c8 <best_fit_pmm_manager+0x2f8>
ffffffffc02011e0:	f71fe0ef          	jal	ra,ffffffffc0200150 <cprintf>
    cprintf("pages: 0x%016lx (physical 0x%016lx), %lu descriptors, kernel end 0x%016lx\n",
ffffffffc02011e4:	000cb583          	ld	a1,0(s9)
ffffffffc02011e8:	8726                	mv	a4,s1
ffffffffc02011ea:	86ea                	mv	a3,s10
ffffffffc02011ec:	864e                	mv	a2,s3
ffffffffc02011ee:	00001517          	auipc	a0,0x1
ffffffffc02011f2:	f0a50513          	addi	a0,a0,-246 # ffffffffc02020f8 <best_fit_pmm_manager+0x328>
ffffffffc02011f6:	f5bfe0ef          	jal	ra,ffffffffc0200150 <cprintf>
    if (nr_free_pages_cnt > 0) {
ffffffffc02011fa:	f20b8be3          	beqz	s7,ffffffffc0201130 <pmm_init+0x1aa>
    pmm_manager->init_memmap(base, n);
ffffffffc02011fe:	00093703          	ld	a4,0(s2)
        init_memmap(&pages[PPN(free_start_pa) - nbase], nr_free_pages_cnt);
ffffffffc0201202:	00c45693          	srli	a3,s0,0xc
ffffffffc0201206:	00a45793          	srli	a5,s0,0xa
ffffffffc020120a:	000cb503          	ld	a0,0(s9)
ffffffffc020120e:	97b6                	add	a5,a5,a3
    pmm_manager->init_memmap(base, n);
ffffffffc0201210:	6b18                	ld	a4,16(a4)
        init_memmap(&pages[PPN(free_start_pa) - nbase], nr_free_pages_cnt);
ffffffffc0201212:	fec006b7          	lui	a3,0xfec00
ffffffffc0201216:	078e                	slli	a5,a5,0x3
ffffffffc0201218:	97b6                	add	a5,a5,a3
    pmm_manager->init_memmap(base, n);
ffffffffc020121a:	85de                	mv	a1,s7
ffffffffc020121c:	953e                	add	a0,a0,a5
ffffffffc020121e:	9702                	jalr	a4
}
ffffffffc0201220:	bf01                	j	ffffffffc0201130 <pmm_init+0x1aa>
        panic("page_init: page descriptors [0x%016lx, +0x%lx) do not fit below 0x%016lx",
ffffffffc0201222:	87ee                	mv	a5,s11
ffffffffc0201224:	875e                	mv	a4,s7
ffffffffc0201226:	86ce                	mv	a3,s3
ffffffffc0201228:	00001617          	auipc	a2,0x1
ffffffffc020122c:	da060613          	addi	a2,a2,-608 # ffffffffc0201fc8 <best_fit_pmm_manager+0x1f8>
ffffffffc0201230:	0b700593          	li	a1,183
ffffffffc0201234:	00001517          	auipc	a0,0x1
ffffffffc0201238:	c2c50513          	addi	a0,a0,-980 # ffffffffc0201e60 <best_fit_pmm_manager+0x90>
ffffffffc020123c:	f8bfe0ef          	jal	ra,ffffffffc02001c6 <__panic>
        panic("page_init: invalid memory info (base=0x%016lx, size=0x%016lx)",
ffffffffc0201240:	872a                	mv	a4,a0
ffffffffc0201242:	86da                	mv	a3,s6
ffffffffc0201244:	00001617          	auipc	a2,0x1
ffffffffc0201248:	bdc60613          	addi	a2,a2,-1060 # ffffffffc0201e20 <best_fit_pmm_manager+0x50>
ffffffffc020124c:	07e00593          	li	a1,126
ffffffffc0201250:	00001517          	auipc	a0,0x1
ffffffffc0201254:	c1050513          	addi	a0,a0,-1008 # ffffffffc0201e60 <best_fit_pmm_manager+0x90>
ffffffffc0201258:	f6ffe0ef          	jal	ra,ffffffffc02001c6 <__panic>
    kernel_end_pa = PADDR((uintptr_t)end);
ffffffffc020125c:	86a6                	mv	a3,s1
ffffffffc020125e:	00001617          	auipc	a2,0x1
ffffffffc0201262:	d4260613          	addi	a2,a2,-702 # ffffffffc0201fa0 <best_fit_pmm_manager+0x1d0>
ffffffffc0201266:	0ab00593          	li	a1,171
ffffffffc020126a:	00001517          	auipc	a0,0x1
ffffffffc020126e:	bf650513          	addi	a0,a0,-1034 # ffffffffc0201e60 <best_fit_pmm_manager+0x90>
ffffffffc0201272:	f55fe0ef          	jal	ra,ffffffffc02001c6 <__panic>
    satp_physical = PADDR(satp_virtual);
ffffffffc0201276:	86ae                	mv	a3,a1
ffffffffc0201278:	00001617          	auipc	a2,0x1
ffffffffc020127c:	d2860613          	addi	a2,a2,-728 # ffffffffc0201fa0 <best_fit_pmm_manager+0x1d0>
ffffffffc0201280:	0f400593          	li	a1,244
ffffffffc0201284:	00001517          	auipc	a0,0x1
ffffffffc0201288:	bdc50513          	addi	a0,a0,-1060 # ffffffffc0201e60 <best_fit_pmm_manager+0x90>
ffffffffc020128c:	f3bfe0ef          	jal	ra,ffffffffc02001c6 <__panic>
        panic("page_init: memory base 0x%016lx is below DRAM_BASE 0x%016lx",
ffffffffc0201290:	4705                	li	a4,1
ffffffffc0201292:	077e                	slli	a4,a4,0x1f
ffffffffc0201294:	86da                	mv	a3,s6
ffffffffc0201296:	00001617          	auipc	a2,0x1
ffffffffc020129a:	bda60613          	addi	a2,a2,-1062 # ffffffffc0201e70 <best_fit_pmm_manager+0xa0>
ffffffffc020129e:	08200593          	li	a1,130
ffffffffc02012a2:	00001517          	auipc	a0,0x1
ffffffffc02012a6:	bbe50513          	addi	a0,a0,-1090 # ffffffffc0201e60 <best_fit_pmm_manager+0x90>
ffffffffc02012aa:	f1dfe0ef          	jal	ra,ffffffffc02001c6 <__panic>
        panic("page_init: memory base 0x%016lx is above kernel image 0x%016lx",
ffffffffc02012ae:	86da                	mv	a3,s6
ffffffffc02012b0:	00001617          	auipc	a2,0x1
ffffffffc02012b4:	c4060613          	addi	a2,a2,-960 # ffffffffc0201ef0 <best_fit_pmm_manager+0x120>
ffffffffc02012b8:	08f00593          	li	a1,143
ffffffffc02012bc:	00001517          	auipc	a0,0x1
ffffffffc02012c0:	ba450513          	addi	a0,a0,-1116 # ffffffffc0201e60 <best_fit_pmm_manager+0x90>
ffffffffc02012c4:	f03fe0ef          	jal	ra,ffffffffc02001c6 <__panic>
    pages_pa = PADDR((uintptr_t)pages);
ffffffffc02012c8:	86a2                	mv	a3,s0
ffffffffc02012ca:	00001617          	auipc	a2,0x1
ffffffffc02012ce:	cd660613          	addi	a2,a2,-810 # ffffffffc0201fa0 <best_fit_pmm_manager+0x1d0>
ffffffffc02012d2:	0aa00593          	li	a1,170
ffffffffc02012d6:	00001517          	auipc	a0,0x1
ffffffffc02012da:	b8a50513          	addi	a0,a0,-1142 # ffffffffc0201e60 <best_fit_pmm_manager+0x90>
ffffffffc02012de:	ee9fe0ef          	jal	ra,ffffffffc02001c6 <__panic>
        panic("page_init: npage %lu is not above nbase %lu",
ffffffffc02012e2:	00080737          	lui	a4,0x80
ffffffffc02012e6:	000806b7          	lui	a3,0x80
ffffffffc02012ea:	00001617          	auipc	a2,0x1
ffffffffc02012ee:	c8660613          	addi	a2,a2,-890 # ffffffffc0201f70 <best_fit_pmm_manager+0x1a0>
ffffffffc02012f2:	0a400593          	li	a1,164
ffffffffc02012f6:	00001517          	auipc	a0,0x1
ffffffffc02012fa:	b6a50513          	addi	a0,a0,-1174 # ffffffffc0201e60 <best_fit_pmm_manager+0x90>
ffffffffc02012fe:	ec9fe0ef          	jal	ra,ffffffffc02001c6 <__panic>
        panic("page_init: no usable physical memory in [0x%016lx, 0x%016lx)",
ffffffffc0201302:	8762                	mv	a4,s8
ffffffffc0201304:	86da                	mv	a3,s6
ffffffffc0201306:	00001617          	auipc	a2,0x1
ffffffffc020130a:	c2a60613          	addi	a2,a2,-982 # ffffffffc0201f30 <best_fit_pmm_manager+0x160>
ffffffffc020130e:	09c00593          	li	a1,156
ffffffffc0201312:	00001517          	auipc	a0,0x1
ffffffffc0201316:	b4e50513          	addi	a0,a0,-1202 # ffffffffc0201e60 <best_fit_pmm_manager+0x90>
ffffffffc020131a:	eadfe0ef          	jal	ra,ffffffffc02001c6 <__panic>
        panic("page_init: memory range overflow (base=0x%016lx, size=0x%016lx)",
ffffffffc020131e:	872a                	mv	a4,a0
ffffffffc0201320:	86da                	mv	a3,s6
ffffffffc0201322:	00001617          	auipc	a2,0x1
ffffffffc0201326:	b8e60613          	addi	a2,a2,-1138 # ffffffffc0201eb0 <best_fit_pmm_manager+0xe0>
ffffffffc020132a:	08700593          	li	a1,135
ffffffffc020132e:	00001517          	auipc	a0,0x1
ffffffffc0201332:	b3250513          	addi	a0,a0,-1230 # ffffffffc0201e60 <best_fit_pmm_manager+0x90>
ffffffffc0201336:	e91fe0ef          	jal	ra,ffffffffc02001c6 <__panic>

ffffffffc020133a <slub_init>:
static void slub_free_large(struct slub_slab *slab) {
    struct Page *page = pa2page(PADDR((uintptr_t)slab));
    free_pages(page, slab->obj_count);   /* obj_count 存的是页数 */
}

void slub_init(void) {
ffffffffc020133a:	1141                	addi	sp,sp,-16
ffffffffc020133c:	e406                	sd	ra,8(sp)
ffffffffc020133e:	00005797          	auipc	a5,0x5
ffffffffc0201342:	cfa78793          	addi	a5,a5,-774 # ffffffffc0206038 <slub_caches+0x8>
ffffffffc0201346:	00001717          	auipc	a4,0x1
ffffffffc020134a:	ed270713          	addi	a4,a4,-302 # ffffffffc0202218 <slub_sizes+0x8>
ffffffffc020134e:	00005617          	auipc	a2,0x5
ffffffffc0201352:	eca60613          	addi	a2,a2,-310 # ffffffffc0206218 <memory_base>
ffffffffc0201356:	46c1                	li	a3,16
ffffffffc0201358:	a019                	j	ffffffffc020135e <slub_init+0x24>
    size_t i;
    for (i = 0; i < SLUB_NR_CACHES; i++) {
        slub_caches[i].obj_size = slub_sizes[i];
ffffffffc020135a:	6314                	ld	a3,0(a4)
ffffffffc020135c:	0721                	addi	a4,a4,8
ffffffffc020135e:	fed7bc23          	sd	a3,-8(a5)
    elm->prev = elm->next = elm;
ffffffffc0201362:	e79c                	sd	a5,8(a5)
ffffffffc0201364:	e39c                	sd	a5,0(a5)
    for (i = 0; i < SLUB_NR_CACHES; i++) {
ffffffffc0201366:	07e1                	addi	a5,a5,24
ffffffffc0201368:	fec799e3          	bne	a5,a2,ffffffffc020135a <slub_init+0x20>
            "large-object threshold %lu, free-page baseline %lu\n",
            (unsigned long)SLUB_NR_CACHES,
            (unsigned long)slub_sizes[SLUB_NR_CACHES - 1],
            (unsigned long)slub_header_size(),
            (unsigned long)SLUB_LARGE_MAX,
            (unsigned long)nr_free_pages());
ffffffffc020136c:	c0fff0ef          	jal	ra,ffffffffc0200f7a <nr_free_pages>
}
ffffffffc0201370:	60a2                	ld	ra,8(sp)
            (unsigned long)nr_free_pages());
ffffffffc0201372:	87aa                	mv	a5,a0
    cprintf("slub: %lu size classes (16..%lu), header %lu bytes, "
ffffffffc0201374:	40000713          	li	a4,1024
ffffffffc0201378:	04000693          	li	a3,64
ffffffffc020137c:	40000613          	li	a2,1024
ffffffffc0201380:	45d1                	li	a1,20
ffffffffc0201382:	00001517          	auipc	a0,0x1
ffffffffc0201386:	e2650513          	addi	a0,a0,-474 # ffffffffc02021a8 <best_fit_pmm_manager+0x3d8>
}
ffffffffc020138a:	0141                	addi	sp,sp,16
    cprintf("slub: %lu size classes (16..%lu), header %lu bytes, "
ffffffffc020138c:	dc5fe06f          	j	ffffffffc0200150 <cprintf>

ffffffffc0201390 <printnum>:
 * */
static void
printnum(void (*putch)(int, void*), void *putdat,
        unsigned long long num, unsigned base, int width, int padc) {
    unsigned long long result = num;
    unsigned mod = do_div(result, base);
ffffffffc0201390:	02069813          	slli	a6,a3,0x20
        unsigned long long num, unsigned base, int width, int padc) {
ffffffffc0201394:	7179                	addi	sp,sp,-48
    unsigned mod = do_div(result, base);
ffffffffc0201396:	02085813          	srli	a6,a6,0x20
        unsigned long long num, unsigned base, int width, int padc) {
ffffffffc020139a:	e052                	sd	s4,0(sp)
    unsigned mod = do_div(result, base);
ffffffffc020139c:	03067a33          	remu	s4,a2,a6
        unsigned long long num, unsigned base, int width, int padc) {
ffffffffc02013a0:	f022                	sd	s0,32(sp)
ffffffffc02013a2:	ec26                	sd	s1,24(sp)
ffffffffc02013a4:	e84a                	sd	s2,16(sp)
ffffffffc02013a6:	f406                	sd	ra,40(sp)
ffffffffc02013a8:	e44e                	sd	s3,8(sp)
ffffffffc02013aa:	84aa                	mv	s1,a0
ffffffffc02013ac:	892e                	mv	s2,a1
    // first recursively print all preceding (more significant) digits
    if (num >= base) {
        printnum(putch, putdat, result, base, width - 1, padc);
    } else {
        // print any needed pad characters before first digit
        while (-- width > 0)
ffffffffc02013ae:	fff7041b          	addiw	s0,a4,-1
    unsigned mod = do_div(result, base);
ffffffffc02013b2:	2a01                	sext.w	s4,s4
    if (num >= base) {
ffffffffc02013b4:	03067e63          	bgeu	a2,a6,ffffffffc02013f0 <printnum+0x60>
ffffffffc02013b8:	89be                	mv	s3,a5
        while (-- width > 0)
ffffffffc02013ba:	00805763          	blez	s0,ffffffffc02013c8 <printnum+0x38>
ffffffffc02013be:	347d                	addiw	s0,s0,-1
            putch(padc, putdat);
ffffffffc02013c0:	85ca                	mv	a1,s2
ffffffffc02013c2:	854e                	mv	a0,s3
ffffffffc02013c4:	9482                	jalr	s1
        while (-- width > 0)
ffffffffc02013c6:	fc65                	bnez	s0,ffffffffc02013be <printnum+0x2e>
    }
    // then print this (the least significant) digit
    putch("0123456789abcdef"[mod], putdat);
ffffffffc02013c8:	1a02                	slli	s4,s4,0x20
ffffffffc02013ca:	00001797          	auipc	a5,0x1
ffffffffc02013ce:	ee678793          	addi	a5,a5,-282 # ffffffffc02022b0 <slub_sizes+0xa0>
ffffffffc02013d2:	020a5a13          	srli	s4,s4,0x20
ffffffffc02013d6:	9a3e                	add	s4,s4,a5
}
ffffffffc02013d8:	7402                	ld	s0,32(sp)
    putch("0123456789abcdef"[mod], putdat);
ffffffffc02013da:	000a4503          	lbu	a0,0(s4)
}
ffffffffc02013de:	70a2                	ld	ra,40(sp)
ffffffffc02013e0:	69a2                	ld	s3,8(sp)
ffffffffc02013e2:	6a02                	ld	s4,0(sp)
    putch("0123456789abcdef"[mod], putdat);
ffffffffc02013e4:	85ca                	mv	a1,s2
ffffffffc02013e6:	87a6                	mv	a5,s1
}
ffffffffc02013e8:	6942                	ld	s2,16(sp)
ffffffffc02013ea:	64e2                	ld	s1,24(sp)
ffffffffc02013ec:	6145                	addi	sp,sp,48
    putch("0123456789abcdef"[mod], putdat);
ffffffffc02013ee:	8782                	jr	a5
        printnum(putch, putdat, result, base, width - 1, padc);
ffffffffc02013f0:	03065633          	divu	a2,a2,a6
ffffffffc02013f4:	8722                	mv	a4,s0
ffffffffc02013f6:	f9bff0ef          	jal	ra,ffffffffc0201390 <printnum>
ffffffffc02013fa:	b7f9                	j	ffffffffc02013c8 <printnum+0x38>

ffffffffc02013fc <vprintfmt>:
 *
 * Call this function if you are already dealing with a va_list.
 * Or you probably want printfmt() instead.
 * */
void
vprintfmt(void (*putch)(int, void*), void *putdat, const char *fmt, va_list ap) {
ffffffffc02013fc:	7119                	addi	sp,sp,-128
ffffffffc02013fe:	f4a6                	sd	s1,104(sp)
ffffffffc0201400:	f0ca                	sd	s2,96(sp)
ffffffffc0201402:	ecce                	sd	s3,88(sp)
ffffffffc0201404:	e8d2                	sd	s4,80(sp)
ffffffffc0201406:	e4d6                	sd	s5,72(sp)
ffffffffc0201408:	e0da                	sd	s6,64(sp)
ffffffffc020140a:	fc5e                	sd	s7,56(sp)
ffffffffc020140c:	f06a                	sd	s10,32(sp)
ffffffffc020140e:	fc86                	sd	ra,120(sp)
ffffffffc0201410:	f8a2                	sd	s0,112(sp)
ffffffffc0201412:	f862                	sd	s8,48(sp)
ffffffffc0201414:	f466                	sd	s9,40(sp)
ffffffffc0201416:	ec6e                	sd	s11,24(sp)
ffffffffc0201418:	892a                	mv	s2,a0
ffffffffc020141a:	84ae                	mv	s1,a1
ffffffffc020141c:	8d32                	mv	s10,a2
ffffffffc020141e:	8a36                	mv	s4,a3
    register int ch, err;
    unsigned long long num;
    int base, width, precision, lflag, altflag;

    while (1) {
        while ((ch = *(unsigned char *)fmt ++) != '%') {
ffffffffc0201420:	02500993          	li	s3,37
            putch(ch, putdat);
        }

        // Process a %-escape sequence
        char padc = ' ';
        width = precision = -1;
ffffffffc0201424:	5b7d                	li	s6,-1
ffffffffc0201426:	00001a97          	auipc	s5,0x1
ffffffffc020142a:	ebea8a93          	addi	s5,s5,-322 # ffffffffc02022e4 <slub_sizes+0xd4>
        case 'e':
            err = va_arg(ap, int);
            if (err < 0) {
                err = -err;
            }
            if (err > MAXERROR || (p = error_string[err]) == NULL) {
ffffffffc020142e:	00001b97          	auipc	s7,0x1
ffffffffc0201432:	092b8b93          	addi	s7,s7,146 # ffffffffc02024c0 <error_string>
        while ((ch = *(unsigned char *)fmt ++) != '%') {
ffffffffc0201436:	000d4503          	lbu	a0,0(s10) # 80000 <kern_entry-0xffffffffc0180000>
ffffffffc020143a:	001d0413          	addi	s0,s10,1
ffffffffc020143e:	01350a63          	beq	a0,s3,ffffffffc0201452 <vprintfmt+0x56>
            if (ch == '\0') {
ffffffffc0201442:	c121                	beqz	a0,ffffffffc0201482 <vprintfmt+0x86>
            putch(ch, putdat);
ffffffffc0201444:	85a6                	mv	a1,s1
        while ((ch = *(unsigned char *)fmt ++) != '%') {
ffffffffc0201446:	0405                	addi	s0,s0,1
            putch(ch, putdat);
ffffffffc0201448:	9902                	jalr	s2
        while ((ch = *(unsigned char *)fmt ++) != '%') {
ffffffffc020144a:	fff44503          	lbu	a0,-1(s0)
ffffffffc020144e:	ff351ae3          	bne	a0,s3,ffffffffc0201442 <vprintfmt+0x46>
        switch (ch = *(unsigned char *)fmt ++) {
ffffffffc0201452:	00044603          	lbu	a2,0(s0)
        char padc = ' ';
ffffffffc0201456:	02000793          	li	a5,32
        lflag = altflag = 0;
ffffffffc020145a:	4c81                	li	s9,0
ffffffffc020145c:	4881                	li	a7,0
        width = precision = -1;
ffffffffc020145e:	5c7d                	li	s8,-1
ffffffffc0201460:	5dfd                	li	s11,-1
ffffffffc0201462:	05500513          	li	a0,85
                if (ch < '0' || ch > '9') {
ffffffffc0201466:	4825                	li	a6,9
        switch (ch = *(unsigned char *)fmt ++) {
ffffffffc0201468:	fdd6059b          	addiw	a1,a2,-35
ffffffffc020146c:	0ff5f593          	andi	a1,a1,255
ffffffffc0201470:	00140d13          	addi	s10,s0,1
ffffffffc0201474:	04b56263          	bltu	a0,a1,ffffffffc02014b8 <vprintfmt+0xbc>
ffffffffc0201478:	058a                	slli	a1,a1,0x2
ffffffffc020147a:	95d6                	add	a1,a1,s5
ffffffffc020147c:	4194                	lw	a3,0(a1)
ffffffffc020147e:	96d6                	add	a3,a3,s5
ffffffffc0201480:	8682                	jr	a3
            for (fmt --; fmt[-1] != '%'; fmt --)
                /* do nothing */;
            break;
        }
    }
}
ffffffffc0201482:	70e6                	ld	ra,120(sp)
ffffffffc0201484:	7446                	ld	s0,112(sp)
ffffffffc0201486:	74a6                	ld	s1,104(sp)
ffffffffc0201488:	7906                	ld	s2,96(sp)
ffffffffc020148a:	69e6                	ld	s3,88(sp)
ffffffffc020148c:	6a46                	ld	s4,80(sp)
ffffffffc020148e:	6aa6                	ld	s5,72(sp)
ffffffffc0201490:	6b06                	ld	s6,64(sp)
ffffffffc0201492:	7be2                	ld	s7,56(sp)
ffffffffc0201494:	7c42                	ld	s8,48(sp)
ffffffffc0201496:	7ca2                	ld	s9,40(sp)
ffffffffc0201498:	7d02                	ld	s10,32(sp)
ffffffffc020149a:	6de2                	ld	s11,24(sp)
ffffffffc020149c:	6109                	addi	sp,sp,128
ffffffffc020149e:	8082                	ret
            padc = '0';
ffffffffc02014a0:	87b2                	mv	a5,a2
            goto reswitch;
ffffffffc02014a2:	00144603          	lbu	a2,1(s0)
        switch (ch = *(unsigned char *)fmt ++) {
ffffffffc02014a6:	846a                	mv	s0,s10
ffffffffc02014a8:	00140d13          	addi	s10,s0,1
ffffffffc02014ac:	fdd6059b          	addiw	a1,a2,-35
ffffffffc02014b0:	0ff5f593          	andi	a1,a1,255
ffffffffc02014b4:	fcb572e3          	bgeu	a0,a1,ffffffffc0201478 <vprintfmt+0x7c>
            putch('%', putdat);
ffffffffc02014b8:	85a6                	mv	a1,s1
ffffffffc02014ba:	02500513          	li	a0,37
ffffffffc02014be:	9902                	jalr	s2
            for (fmt --; fmt[-1] != '%'; fmt --)
ffffffffc02014c0:	fff44783          	lbu	a5,-1(s0)
ffffffffc02014c4:	8d22                	mv	s10,s0
ffffffffc02014c6:	f73788e3          	beq	a5,s3,ffffffffc0201436 <vprintfmt+0x3a>
ffffffffc02014ca:	ffed4783          	lbu	a5,-2(s10)
ffffffffc02014ce:	1d7d                	addi	s10,s10,-1
ffffffffc02014d0:	ff379de3          	bne	a5,s3,ffffffffc02014ca <vprintfmt+0xce>
ffffffffc02014d4:	b78d                	j	ffffffffc0201436 <vprintfmt+0x3a>
                precision = precision * 10 + ch - '0';
ffffffffc02014d6:	fd060c1b          	addiw	s8,a2,-48
                ch = *fmt;
ffffffffc02014da:	00144603          	lbu	a2,1(s0)
        switch (ch = *(unsigned char *)fmt ++) {
ffffffffc02014de:	846a                	mv	s0,s10
                if (ch < '0' || ch > '9') {
ffffffffc02014e0:	fd06069b          	addiw	a3,a2,-48
                ch = *fmt;
ffffffffc02014e4:	0006059b          	sext.w	a1,a2
                if (ch < '0' || ch > '9') {
ffffffffc02014e8:	02d86463          	bltu	a6,a3,ffffffffc0201510 <vprintfmt+0x114>
                ch = *fmt;
ffffffffc02014ec:	00144603          	lbu	a2,1(s0)
                precision = precision * 10 + ch - '0';
ffffffffc02014f0:	002c169b          	slliw	a3,s8,0x2
ffffffffc02014f4:	0186873b          	addw	a4,a3,s8
ffffffffc02014f8:	0017171b          	slliw	a4,a4,0x1
ffffffffc02014fc:	9f2d                	addw	a4,a4,a1
                if (ch < '0' || ch > '9') {
ffffffffc02014fe:	fd06069b          	addiw	a3,a2,-48
            for (precision = 0; ; ++ fmt) {
ffffffffc0201502:	0405                	addi	s0,s0,1
                precision = precision * 10 + ch - '0';
ffffffffc0201504:	fd070c1b          	addiw	s8,a4,-48
                ch = *fmt;
ffffffffc0201508:	0006059b          	sext.w	a1,a2
                if (ch < '0' || ch > '9') {
ffffffffc020150c:	fed870e3          	bgeu	a6,a3,ffffffffc02014ec <vprintfmt+0xf0>
            if (width < 0)
ffffffffc0201510:	f40ddce3          	bgez	s11,ffffffffc0201468 <vprintfmt+0x6c>
                width = precision, precision = -1;
ffffffffc0201514:	8de2                	mv	s11,s8
ffffffffc0201516:	5c7d                	li	s8,-1
ffffffffc0201518:	bf81                	j	ffffffffc0201468 <vprintfmt+0x6c>
            if (width < 0)
ffffffffc020151a:	fffdc693          	not	a3,s11
ffffffffc020151e:	96fd                	srai	a3,a3,0x3f
ffffffffc0201520:	00ddfdb3          	and	s11,s11,a3
        switch (ch = *(unsigned char *)fmt ++) {
ffffffffc0201524:	00144603          	lbu	a2,1(s0)
ffffffffc0201528:	2d81                	sext.w	s11,s11
ffffffffc020152a:	846a                	mv	s0,s10
            goto reswitch;
ffffffffc020152c:	bf35                	j	ffffffffc0201468 <vprintfmt+0x6c>
            precision = va_arg(ap, int);
ffffffffc020152e:	000a2c03          	lw	s8,0(s4)
        switch (ch = *(unsigned char *)fmt ++) {
ffffffffc0201532:	00144603          	lbu	a2,1(s0)
            precision = va_arg(ap, int);
ffffffffc0201536:	0a21                	addi	s4,s4,8
        switch (ch = *(unsigned char *)fmt ++) {
ffffffffc0201538:	846a                	mv	s0,s10
            goto process_precision;
ffffffffc020153a:	bfd9                	j	ffffffffc0201510 <vprintfmt+0x114>
    if (lflag >= 2) {
ffffffffc020153c:	4705                	li	a4,1
            precision = va_arg(ap, int);
ffffffffc020153e:	008a0593          	addi	a1,s4,8
    if (lflag >= 2) {
ffffffffc0201542:	01174463          	blt	a4,a7,ffffffffc020154a <vprintfmt+0x14e>
    else if (lflag) {
ffffffffc0201546:	1a088e63          	beqz	a7,ffffffffc0201702 <vprintfmt+0x306>
        return va_arg(*ap, unsigned long);
ffffffffc020154a:	000a3603          	ld	a2,0(s4)
ffffffffc020154e:	46c1                	li	a3,16
ffffffffc0201550:	8a2e                	mv	s4,a1
            printnum(putch, putdat, num, base, width, padc);
ffffffffc0201552:	2781                	sext.w	a5,a5
ffffffffc0201554:	876e                	mv	a4,s11
ffffffffc0201556:	85a6                	mv	a1,s1
ffffffffc0201558:	854a                	mv	a0,s2
ffffffffc020155a:	e37ff0ef          	jal	ra,ffffffffc0201390 <printnum>
            break;
ffffffffc020155e:	bde1                	j	ffffffffc0201436 <vprintfmt+0x3a>
            putch(va_arg(ap, int), putdat);
ffffffffc0201560:	000a2503          	lw	a0,0(s4)
ffffffffc0201564:	85a6                	mv	a1,s1
ffffffffc0201566:	0a21                	addi	s4,s4,8
ffffffffc0201568:	9902                	jalr	s2
            break;
ffffffffc020156a:	b5f1                	j	ffffffffc0201436 <vprintfmt+0x3a>
    if (lflag >= 2) {
ffffffffc020156c:	4705                	li	a4,1
            precision = va_arg(ap, int);
ffffffffc020156e:	008a0593          	addi	a1,s4,8
    if (lflag >= 2) {
ffffffffc0201572:	01174463          	blt	a4,a7,ffffffffc020157a <vprintfmt+0x17e>
    else if (lflag) {
ffffffffc0201576:	18088163          	beqz	a7,ffffffffc02016f8 <vprintfmt+0x2fc>
        return va_arg(*ap, unsigned long);
ffffffffc020157a:	000a3603          	ld	a2,0(s4)
ffffffffc020157e:	46a9                	li	a3,10
ffffffffc0201580:	8a2e                	mv	s4,a1
ffffffffc0201582:	bfc1                	j	ffffffffc0201552 <vprintfmt+0x156>
        switch (ch = *(unsigned char *)fmt ++) {
ffffffffc0201584:	00144603          	lbu	a2,1(s0)
            altflag = 1;
ffffffffc0201588:	4c85                	li	s9,1
        switch (ch = *(unsigned char *)fmt ++) {
ffffffffc020158a:	846a                	mv	s0,s10
            goto reswitch;
ffffffffc020158c:	bdf1                	j	ffffffffc0201468 <vprintfmt+0x6c>
            putch(ch, putdat);
ffffffffc020158e:	85a6                	mv	a1,s1
ffffffffc0201590:	02500513          	li	a0,37
ffffffffc0201594:	9902                	jalr	s2
            break;
ffffffffc0201596:	b545                	j	ffffffffc0201436 <vprintfmt+0x3a>
        switch (ch = *(unsigned char *)fmt ++) {
ffffffffc0201598:	00144603          	lbu	a2,1(s0)
            lflag ++;
ffffffffc020159c:	2885                	addiw	a7,a7,1
        switch (ch = *(unsigned char *)fmt ++) {
ffffffffc020159e:	846a                	mv	s0,s10
            goto reswitch;
ffffffffc02015a0:	b5e1                	j	ffffffffc0201468 <vprintfmt+0x6c>
    if (lflag >= 2) {
ffffffffc02015a2:	4705                	li	a4,1
            precision = va_arg(ap, int);
ffffffffc02015a4:	008a0593          	addi	a1,s4,8
    if (lflag >= 2) {
ffffffffc02015a8:	01174463          	blt	a4,a7,ffffffffc02015b0 <vprintfmt+0x1b4>
    else if (lflag) {
ffffffffc02015ac:	14088163          	beqz	a7,ffffffffc02016ee <vprintfmt+0x2f2>
        return va_arg(*ap, unsigned long);
ffffffffc02015b0:	000a3603          	ld	a2,0(s4)
ffffffffc02015b4:	46a1                	li	a3,8
ffffffffc02015b6:	8a2e                	mv	s4,a1
ffffffffc02015b8:	bf69                	j	ffffffffc0201552 <vprintfmt+0x156>
            putch('0', putdat);
ffffffffc02015ba:	03000513          	li	a0,48
ffffffffc02015be:	85a6                	mv	a1,s1
ffffffffc02015c0:	e03e                	sd	a5,0(sp)
ffffffffc02015c2:	9902                	jalr	s2
            putch('x', putdat);
ffffffffc02015c4:	85a6                	mv	a1,s1
ffffffffc02015c6:	07800513          	li	a0,120
ffffffffc02015ca:	9902                	jalr	s2
            num = (unsigned long long)(uintptr_t)va_arg(ap, void *);
ffffffffc02015cc:	0a21                	addi	s4,s4,8
            goto number;
ffffffffc02015ce:	6782                	ld	a5,0(sp)
ffffffffc02015d0:	46c1                	li	a3,16
            num = (unsigned long long)(uintptr_t)va_arg(ap, void *);
ffffffffc02015d2:	ff8a3603          	ld	a2,-8(s4)
            goto number;
ffffffffc02015d6:	bfb5                	j	ffffffffc0201552 <vprintfmt+0x156>
            if ((p = va_arg(ap, char *)) == NULL) {
ffffffffc02015d8:	000a3403          	ld	s0,0(s4)
ffffffffc02015dc:	008a0713          	addi	a4,s4,8
ffffffffc02015e0:	e03a                	sd	a4,0(sp)
ffffffffc02015e2:	14040263          	beqz	s0,ffffffffc0201726 <vprintfmt+0x32a>
            if (width > 0 && padc != '-') {
ffffffffc02015e6:	0fb05763          	blez	s11,ffffffffc02016d4 <vprintfmt+0x2d8>
ffffffffc02015ea:	02d00693          	li	a3,45
ffffffffc02015ee:	0cd79163          	bne	a5,a3,ffffffffc02016b0 <vprintfmt+0x2b4>
            for (; (ch = *p ++) != '\0' && (precision < 0 || -- precision >= 0); width --) {
ffffffffc02015f2:	00044783          	lbu	a5,0(s0)
ffffffffc02015f6:	0007851b          	sext.w	a0,a5
ffffffffc02015fa:	cf85                	beqz	a5,ffffffffc0201632 <vprintfmt+0x236>
ffffffffc02015fc:	00140a13          	addi	s4,s0,1
                if (altflag && (ch < ' ' || ch > '~')) {
ffffffffc0201600:	05e00413          	li	s0,94
            for (; (ch = *p ++) != '\0' && (precision < 0 || -- precision >= 0); width --) {
ffffffffc0201604:	000c4563          	bltz	s8,ffffffffc020160e <vprintfmt+0x212>
ffffffffc0201608:	3c7d                	addiw	s8,s8,-1
ffffffffc020160a:	036c0263          	beq	s8,s6,ffffffffc020162e <vprintfmt+0x232>
                    putch('?', putdat);
ffffffffc020160e:	85a6                	mv	a1,s1
                if (altflag && (ch < ' ' || ch > '~')) {
ffffffffc0201610:	0e0c8e63          	beqz	s9,ffffffffc020170c <vprintfmt+0x310>
ffffffffc0201614:	3781                	addiw	a5,a5,-32
ffffffffc0201616:	0ef47b63          	bgeu	s0,a5,ffffffffc020170c <vprintfmt+0x310>
                    putch('?', putdat);
ffffffffc020161a:	03f00513          	li	a0,63
ffffffffc020161e:	9902                	jalr	s2
            for (; (ch = *p ++) != '\0' && (precision < 0 || -- precision >= 0); width --) {
ffffffffc0201620:	000a4783          	lbu	a5,0(s4)
ffffffffc0201624:	3dfd                	addiw	s11,s11,-1
ffffffffc0201626:	0a05                	addi	s4,s4,1
ffffffffc0201628:	0007851b          	sext.w	a0,a5
ffffffffc020162c:	ffe1                	bnez	a5,ffffffffc0201604 <vprintfmt+0x208>
            for (; width > 0; width --) {
ffffffffc020162e:	01b05963          	blez	s11,ffffffffc0201640 <vprintfmt+0x244>
ffffffffc0201632:	3dfd                	addiw	s11,s11,-1
                putch(' ', putdat);
ffffffffc0201634:	85a6                	mv	a1,s1
ffffffffc0201636:	02000513          	li	a0,32
ffffffffc020163a:	9902                	jalr	s2
            for (; width > 0; width --) {
ffffffffc020163c:	fe0d9be3          	bnez	s11,ffffffffc0201632 <vprintfmt+0x236>
            if ((p = va_arg(ap, char *)) == NULL) {
ffffffffc0201640:	6a02                	ld	s4,0(sp)
ffffffffc0201642:	bbd5                	j	ffffffffc0201436 <vprintfmt+0x3a>
    if (lflag >= 2) {
ffffffffc0201644:	4705                	li	a4,1
            precision = va_arg(ap, int);
ffffffffc0201646:	008a0c93          	addi	s9,s4,8
    if (lflag >= 2) {
ffffffffc020164a:	01174463          	blt	a4,a7,ffffffffc0201652 <vprintfmt+0x256>
    else if (lflag) {
ffffffffc020164e:	08088d63          	beqz	a7,ffffffffc02016e8 <vprintfmt+0x2ec>
        return va_arg(*ap, long);
ffffffffc0201652:	000a3403          	ld	s0,0(s4)
            if ((long long)num < 0) {
ffffffffc0201656:	0a044d63          	bltz	s0,ffffffffc0201710 <vprintfmt+0x314>
            num = getint(&ap, lflag);
ffffffffc020165a:	8622                	mv	a2,s0
ffffffffc020165c:	8a66                	mv	s4,s9
ffffffffc020165e:	46a9                	li	a3,10
ffffffffc0201660:	bdcd                	j	ffffffffc0201552 <vprintfmt+0x156>
            err = va_arg(ap, int);
ffffffffc0201662:	000a2783          	lw	a5,0(s4)
            if (err > MAXERROR || (p = error_string[err]) == NULL) {
ffffffffc0201666:	4719                	li	a4,6
            err = va_arg(ap, int);
ffffffffc0201668:	0a21                	addi	s4,s4,8
            if (err < 0) {
ffffffffc020166a:	41f7d69b          	sraiw	a3,a5,0x1f
ffffffffc020166e:	8fb5                	xor	a5,a5,a3
ffffffffc0201670:	40d786bb          	subw	a3,a5,a3
            if (err > MAXERROR || (p = error_string[err]) == NULL) {
ffffffffc0201674:	02d74163          	blt	a4,a3,ffffffffc0201696 <vprintfmt+0x29a>
ffffffffc0201678:	00369793          	slli	a5,a3,0x3
ffffffffc020167c:	97de                	add	a5,a5,s7
ffffffffc020167e:	639c                	ld	a5,0(a5)
ffffffffc0201680:	cb99                	beqz	a5,ffffffffc0201696 <vprintfmt+0x29a>
                printfmt(putch, putdat, "%s", p);
ffffffffc0201682:	86be                	mv	a3,a5
ffffffffc0201684:	00001617          	auipc	a2,0x1
ffffffffc0201688:	c5c60613          	addi	a2,a2,-932 # ffffffffc02022e0 <slub_sizes+0xd0>
ffffffffc020168c:	85a6                	mv	a1,s1
ffffffffc020168e:	854a                	mv	a0,s2
ffffffffc0201690:	0ce000ef          	jal	ra,ffffffffc020175e <printfmt>
ffffffffc0201694:	b34d                	j	ffffffffc0201436 <vprintfmt+0x3a>
                printfmt(putch, putdat, "error %d", err);
ffffffffc0201696:	00001617          	auipc	a2,0x1
ffffffffc020169a:	c3a60613          	addi	a2,a2,-966 # ffffffffc02022d0 <slub_sizes+0xc0>
ffffffffc020169e:	85a6                	mv	a1,s1
ffffffffc02016a0:	854a                	mv	a0,s2
ffffffffc02016a2:	0bc000ef          	jal	ra,ffffffffc020175e <printfmt>
ffffffffc02016a6:	bb41                	j	ffffffffc0201436 <vprintfmt+0x3a>
                p = "(null)";
ffffffffc02016a8:	00001417          	auipc	s0,0x1
ffffffffc02016ac:	c2040413          	addi	s0,s0,-992 # ffffffffc02022c8 <slub_sizes+0xb8>
                for (width -= strnlen(p, precision); width > 0; width --) {
ffffffffc02016b0:	85e2                	mv	a1,s8
ffffffffc02016b2:	8522                	mv	a0,s0
ffffffffc02016b4:	e43e                	sd	a5,8(sp)
ffffffffc02016b6:	0fc000ef          	jal	ra,ffffffffc02017b2 <strnlen>
ffffffffc02016ba:	40ad8dbb          	subw	s11,s11,a0
ffffffffc02016be:	01b05b63          	blez	s11,ffffffffc02016d4 <vprintfmt+0x2d8>
                    putch(padc, putdat);
ffffffffc02016c2:	67a2                	ld	a5,8(sp)
ffffffffc02016c4:	00078a1b          	sext.w	s4,a5
                for (width -= strnlen(p, precision); width > 0; width --) {
ffffffffc02016c8:	3dfd                	addiw	s11,s11,-1
                    putch(padc, putdat);
ffffffffc02016ca:	85a6                	mv	a1,s1
ffffffffc02016cc:	8552                	mv	a0,s4
ffffffffc02016ce:	9902                	jalr	s2
                for (width -= strnlen(p, precision); width > 0; width --) {
ffffffffc02016d0:	fe0d9ce3          	bnez	s11,ffffffffc02016c8 <vprintfmt+0x2cc>
            for (; (ch = *p ++) != '\0' && (precision < 0 || -- precision >= 0); width --) {
ffffffffc02016d4:	00044783          	lbu	a5,0(s0)
ffffffffc02016d8:	00140a13          	addi	s4,s0,1
ffffffffc02016dc:	0007851b          	sext.w	a0,a5
ffffffffc02016e0:	d3a5                	beqz	a5,ffffffffc0201640 <vprintfmt+0x244>
                if (altflag && (ch < ' ' || ch > '~')) {
ffffffffc02016e2:	05e00413          	li	s0,94
ffffffffc02016e6:	bf39                	j	ffffffffc0201604 <vprintfmt+0x208>
        return va_arg(*ap, int);
ffffffffc02016e8:	000a2403          	lw	s0,0(s4)
ffffffffc02016ec:	b7ad                	j	ffffffffc0201656 <vprintfmt+0x25a>
        return va_arg(*ap, unsigned int);
ffffffffc02016ee:	000a6603          	lwu	a2,0(s4)
ffffffffc02016f2:	46a1                	li	a3,8
ffffffffc02016f4:	8a2e                	mv	s4,a1
ffffffffc02016f6:	bdb1                	j	ffffffffc0201552 <vprintfmt+0x156>
ffffffffc02016f8:	000a6603          	lwu	a2,0(s4)
ffffffffc02016fc:	46a9                	li	a3,10
ffffffffc02016fe:	8a2e                	mv	s4,a1
ffffffffc0201700:	bd89                	j	ffffffffc0201552 <vprintfmt+0x156>
ffffffffc0201702:	000a6603          	lwu	a2,0(s4)
ffffffffc0201706:	46c1                	li	a3,16
ffffffffc0201708:	8a2e                	mv	s4,a1
ffffffffc020170a:	b5a1                	j	ffffffffc0201552 <vprintfmt+0x156>
                    putch(ch, putdat);
ffffffffc020170c:	9902                	jalr	s2
ffffffffc020170e:	bf09                	j	ffffffffc0201620 <vprintfmt+0x224>
                putch('-', putdat);
ffffffffc0201710:	85a6                	mv	a1,s1
ffffffffc0201712:	02d00513          	li	a0,45
ffffffffc0201716:	e03e                	sd	a5,0(sp)
ffffffffc0201718:	9902                	jalr	s2
                num = -(long long)num;
ffffffffc020171a:	6782                	ld	a5,0(sp)
ffffffffc020171c:	8a66                	mv	s4,s9
ffffffffc020171e:	40800633          	neg	a2,s0
ffffffffc0201722:	46a9                	li	a3,10
ffffffffc0201724:	b53d                	j	ffffffffc0201552 <vprintfmt+0x156>
            if (width > 0 && padc != '-') {
ffffffffc0201726:	03b05163          	blez	s11,ffffffffc0201748 <vprintfmt+0x34c>
ffffffffc020172a:	02d00693          	li	a3,45
ffffffffc020172e:	f6d79de3          	bne	a5,a3,ffffffffc02016a8 <vprintfmt+0x2ac>
                p = "(null)";
ffffffffc0201732:	00001417          	auipc	s0,0x1
ffffffffc0201736:	b9640413          	addi	s0,s0,-1130 # ffffffffc02022c8 <slub_sizes+0xb8>
            for (; (ch = *p ++) != '\0' && (precision < 0 || -- precision >= 0); width --) {
ffffffffc020173a:	02800793          	li	a5,40
ffffffffc020173e:	02800513          	li	a0,40
ffffffffc0201742:	00140a13          	addi	s4,s0,1
ffffffffc0201746:	bd6d                	j	ffffffffc0201600 <vprintfmt+0x204>
ffffffffc0201748:	00001a17          	auipc	s4,0x1
ffffffffc020174c:	b81a0a13          	addi	s4,s4,-1151 # ffffffffc02022c9 <slub_sizes+0xb9>
ffffffffc0201750:	02800513          	li	a0,40
ffffffffc0201754:	02800793          	li	a5,40
                if (altflag && (ch < ' ' || ch > '~')) {
ffffffffc0201758:	05e00413          	li	s0,94
ffffffffc020175c:	b565                	j	ffffffffc0201604 <vprintfmt+0x208>

ffffffffc020175e <printfmt>:
printfmt(void (*putch)(int, void*), void *putdat, const char *fmt, ...) {
ffffffffc020175e:	715d                	addi	sp,sp,-80
    va_start(ap, fmt);
ffffffffc0201760:	02810313          	addi	t1,sp,40
printfmt(void (*putch)(int, void*), void *putdat, const char *fmt, ...) {
ffffffffc0201764:	f436                	sd	a3,40(sp)
    vprintfmt(putch, putdat, fmt, ap);
ffffffffc0201766:	869a                	mv	a3,t1
printfmt(void (*putch)(int, void*), void *putdat, const char *fmt, ...) {
ffffffffc0201768:	ec06                	sd	ra,24(sp)
ffffffffc020176a:	f83a                	sd	a4,48(sp)
ffffffffc020176c:	fc3e                	sd	a5,56(sp)
ffffffffc020176e:	e0c2                	sd	a6,64(sp)
ffffffffc0201770:	e4c6                	sd	a7,72(sp)
    va_start(ap, fmt);
ffffffffc0201772:	e41a                	sd	t1,8(sp)
    vprintfmt(putch, putdat, fmt, ap);
ffffffffc0201774:	c89ff0ef          	jal	ra,ffffffffc02013fc <vprintfmt>
}
ffffffffc0201778:	60e2                	ld	ra,24(sp)
ffffffffc020177a:	6161                	addi	sp,sp,80
ffffffffc020177c:	8082                	ret

ffffffffc020177e <sbi_console_putchar>:
uint64_t SBI_REMOTE_SFENCE_VMA_ASID = 7;
uint64_t SBI_SHUTDOWN = 8;

uint64_t sbi_call(uint64_t sbi_type, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    uint64_t ret_val;
    __asm__ volatile (
ffffffffc020177e:	4781                	li	a5,0
ffffffffc0201780:	00005717          	auipc	a4,0x5
ffffffffc0201784:	89073703          	ld	a4,-1904(a4) # ffffffffc0206010 <SBI_CONSOLE_PUTCHAR>
ffffffffc0201788:	88ba                	mv	a7,a4
ffffffffc020178a:	852a                	mv	a0,a0
ffffffffc020178c:	85be                	mv	a1,a5
ffffffffc020178e:	863e                	mv	a2,a5
ffffffffc0201790:	00000073          	ecall
ffffffffc0201794:	87aa                	mv	a5,a0
    return ret_val;
}

void sbi_console_putchar(unsigned char ch) {
    sbi_call(SBI_CONSOLE_PUTCHAR, ch, 0, 0);
}
ffffffffc0201796:	8082                	ret

ffffffffc0201798 <strlen>:
 * The strlen() function returns the length of string @s.
 * */
size_t
strlen(const char *s) {
    size_t cnt = 0;
    while (*s ++ != '\0') {
ffffffffc0201798:	00054783          	lbu	a5,0(a0)
strlen(const char *s) {
ffffffffc020179c:	872a                	mv	a4,a0
    size_t cnt = 0;
ffffffffc020179e:	4501                	li	a0,0
    while (*s ++ != '\0') {
ffffffffc02017a0:	cb81                	beqz	a5,ffffffffc02017b0 <strlen+0x18>
        cnt ++;
ffffffffc02017a2:	0505                	addi	a0,a0,1
    while (*s ++ != '\0') {
ffffffffc02017a4:	00a707b3          	add	a5,a4,a0
ffffffffc02017a8:	0007c783          	lbu	a5,0(a5)
ffffffffc02017ac:	fbfd                	bnez	a5,ffffffffc02017a2 <strlen+0xa>
ffffffffc02017ae:	8082                	ret
    }
    return cnt;
}
ffffffffc02017b0:	8082                	ret

ffffffffc02017b2 <strnlen>:
 * @len if there is no '\0' character among the first @len characters
 * pointed by @s.
 * */
size_t
strnlen(const char *s, size_t len) {
    size_t cnt = 0;
ffffffffc02017b2:	4781                	li	a5,0
    while (cnt < len && *s ++ != '\0') {
ffffffffc02017b4:	e589                	bnez	a1,ffffffffc02017be <strnlen+0xc>
ffffffffc02017b6:	a811                	j	ffffffffc02017ca <strnlen+0x18>
        cnt ++;
ffffffffc02017b8:	0785                	addi	a5,a5,1
    while (cnt < len && *s ++ != '\0') {
ffffffffc02017ba:	00f58863          	beq	a1,a5,ffffffffc02017ca <strnlen+0x18>
ffffffffc02017be:	00f50733          	add	a4,a0,a5
ffffffffc02017c2:	00074703          	lbu	a4,0(a4)
ffffffffc02017c6:	fb6d                	bnez	a4,ffffffffc02017b8 <strnlen+0x6>
ffffffffc02017c8:	85be                	mv	a1,a5
    }
    return cnt;
}
ffffffffc02017ca:	852e                	mv	a0,a1
ffffffffc02017cc:	8082                	ret

ffffffffc02017ce <strcmp>:
int
strcmp(const char *s1, const char *s2) {
#ifdef __HAVE_ARCH_STRCMP
    return __strcmp(s1, s2);
#else
    while (*s1 != '\0' && *s1 == *s2) {
ffffffffc02017ce:	00054783          	lbu	a5,0(a0)
        s1 ++, s2 ++;
    }
    return (int)((unsigned char)*s1 - (unsigned char)*s2);
ffffffffc02017d2:	0005c703          	lbu	a4,0(a1)
    while (*s1 != '\0' && *s1 == *s2) {
ffffffffc02017d6:	cb89                	beqz	a5,ffffffffc02017e8 <strcmp+0x1a>
        s1 ++, s2 ++;
ffffffffc02017d8:	0505                	addi	a0,a0,1
ffffffffc02017da:	0585                	addi	a1,a1,1
    while (*s1 != '\0' && *s1 == *s2) {
ffffffffc02017dc:	fee789e3          	beq	a5,a4,ffffffffc02017ce <strcmp>
    return (int)((unsigned char)*s1 - (unsigned char)*s2);
ffffffffc02017e0:	0007851b          	sext.w	a0,a5
#endif /* __HAVE_ARCH_STRCMP */
}
ffffffffc02017e4:	9d19                	subw	a0,a0,a4
ffffffffc02017e6:	8082                	ret
ffffffffc02017e8:	4501                	li	a0,0
ffffffffc02017ea:	bfed                	j	ffffffffc02017e4 <strcmp+0x16>

ffffffffc02017ec <strncmp>:
 * the characters differ, until a terminating null-character is reached, or
 * until @n characters match in both strings, whichever happens first.
 * */
int
strncmp(const char *s1, const char *s2, size_t n) {
    while (n > 0 && *s1 != '\0' && *s1 == *s2) {
ffffffffc02017ec:	c20d                	beqz	a2,ffffffffc020180e <strncmp+0x22>
ffffffffc02017ee:	962e                	add	a2,a2,a1
ffffffffc02017f0:	a031                	j	ffffffffc02017fc <strncmp+0x10>
        n --, s1 ++, s2 ++;
ffffffffc02017f2:	0505                	addi	a0,a0,1
    while (n > 0 && *s1 != '\0' && *s1 == *s2) {
ffffffffc02017f4:	00e79a63          	bne	a5,a4,ffffffffc0201808 <strncmp+0x1c>
ffffffffc02017f8:	00b60b63          	beq	a2,a1,ffffffffc020180e <strncmp+0x22>
ffffffffc02017fc:	00054783          	lbu	a5,0(a0)
        n --, s1 ++, s2 ++;
ffffffffc0201800:	0585                	addi	a1,a1,1
    while (n > 0 && *s1 != '\0' && *s1 == *s2) {
ffffffffc0201802:	fff5c703          	lbu	a4,-1(a1)
ffffffffc0201806:	f7f5                	bnez	a5,ffffffffc02017f2 <strncmp+0x6>
    }
    return (n == 0) ? 0 : (int)((unsigned char)*s1 - (unsigned char)*s2);
ffffffffc0201808:	40e7853b          	subw	a0,a5,a4
}
ffffffffc020180c:	8082                	ret
    return (n == 0) ? 0 : (int)((unsigned char)*s1 - (unsigned char)*s2);
ffffffffc020180e:	4501                	li	a0,0
ffffffffc0201810:	8082                	ret

ffffffffc0201812 <memset>:
memset(void *s, char c, size_t n) {
#ifdef __HAVE_ARCH_MEMSET
    return __memset(s, c, n);
#else
    char *p = s;
    while (n -- > 0) {
ffffffffc0201812:	ca01                	beqz	a2,ffffffffc0201822 <memset+0x10>
ffffffffc0201814:	962a                	add	a2,a2,a0
    char *p = s;
ffffffffc0201816:	87aa                	mv	a5,a0
        *p ++ = c;
ffffffffc0201818:	0785                	addi	a5,a5,1
ffffffffc020181a:	feb78fa3          	sb	a1,-1(a5)
    while (n -- > 0) {
ffffffffc020181e:	fec79de3          	bne	a5,a2,ffffffffc0201818 <memset+0x6>
    }
    return s;
#endif /* __HAVE_ARCH_MEMSET */
}
ffffffffc0201822:	8082                	ret
