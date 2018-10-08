#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0xd12509e8, "module_layout" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0xb5247982, "cdev_del" },
	{ 0x7ccacbf6, "cdev_add" },
	{ 0x643e0add, "cdev_init" },
	{ 0x1c524352, "cdev_alloc" },
	{ 0x3fd78f3b, "register_chrdev_region" },
	{ 0xdb7305a1, "__stack_chk_fail" },
	{ 0xb44ad4b3, "_copy_to_user" },
	{ 0x2ea2c95c, "__x86_indirect_thunk_rax" },
	{ 0x362ef408, "_copy_from_user" },
	{ 0xbdfb6dbb, "__fentry__" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "23EF3A7F94AFF2E4367EC30");
