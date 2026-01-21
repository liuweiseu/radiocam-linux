#include <linux/init.h>
#include <linux/module.h>

static __init int hello_init(void)
{
  printk("Hello World!\n");
  return 0;
}

static __exit void hello_exit(void)
{
  printk("Byebye!\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPLv2");
MODULE_AUTHOR("Wei Liu");
MODULE_DESCRIPTION("This is for test.");
