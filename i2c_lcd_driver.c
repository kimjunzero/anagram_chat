// i2c_lcd_driver.c
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>

// --- LCD 설정 ---
#define LCD_I2C_ADDR 0x27
#define MAX_MSG_LEN  32
#define LCD_MAJOR    240
#define DEVICE_NAME  "i2c_lcd_display"

// PCF8574 핀 매핑
#define RS_BIT       (1 << 0)
#define RW_BIT       (1 << 1)
#define EN_BIT       (1 << 2)
#define BL_BIT       (1 << 3)

// LCD 명령어
#define LCD_CMD_CLEARDISPLAY   0x01
#define LCD_CMD_RETURNHOME     0x02
#define LCD_CMD_ENTRYMODESET   0x06
#define LCD_CMD_DISPLAYCONTROL 0x0C
#define LCD_CMD_FUNCTIONSET    0x28

static struct i2c_client *lcd_i2c_client;
static struct workqueue_struct *lcd_wq;
static struct delayed_work lcd_work;
static char current_lcd_message[MAX_MSG_LEN + 1];
static struct cdev lcd_cdev;
static dev_t lcd_dev_num;

static int i2c_lcd_write_byte(u8 data, u8 mode)
{
    int ret;
    u8 tx = data | mode | BL_BIT;

    ret = i2c_smbus_write_byte(lcd_i2c_client, tx | EN_BIT);
    if (ret < 0) {
        pr_err("LCD write EN-high failed: %d\n", ret);
        return ret;
    }
    udelay(5);

    ret = i2c_smbus_write_byte(lcd_i2c_client, tx & ~EN_BIT);
    if (ret < 0) {
        pr_err("LCD write EN-low failed: %d\n", ret);
        return ret;
    }
    udelay(200);
    return 0;
}

static void lcd_send_nibbles(u8 val, u8 mode)
{
    i2c_lcd_write_byte(val & 0xF0, mode);
    i2c_lcd_write_byte((val << 4) & 0xF0, mode);
}

static void lcd_send_cmd(u8 cmd)
{
    lcd_send_nibbles(cmd, 0);
    if (cmd == LCD_CMD_CLEARDISPLAY || cmd == LCD_CMD_RETURNHOME)
        mdelay(3);
}

static void lcd_send_data(u8 data)
{
    lcd_send_nibbles(data, RS_BIT);
}

static void lcd_set_cursor(u8 col, u8 row)
{
    u8 addr = (row == 0 ? 0x80 : 0xC0) + col;
    lcd_send_cmd(addr);
}

static void lcd_print_string(const char *s)
{
    while (*s) {
        lcd_send_data(*s++);
    }
}

static void lcd_display_user_message_fn(struct work_struct *work)
{
    size_t len = strlen(current_lcd_message);
    size_t i;

    pr_info("I2C LCD: Displaying user message: '%s'\n", current_lcd_message);

    lcd_send_cmd(LCD_CMD_CLEARDISPLAY);
    mdelay(5);
    lcd_send_cmd(LCD_CMD_RETURNHOME);
    mdelay(5);

    for (i = 0; i < len; ++i) {
        if (i == 16) {
            lcd_set_cursor(0, 1);
        }
        if (i >= MAX_MSG_LEN) {
            break;
        }
        lcd_send_data(current_lcd_message[i]);
    }
}

static void lcd_init_sequence(void)
{
    int i;
    mdelay(100);

    for (i = 0; i < 3; ++i) {
        i2c_lcd_write_byte(0x30, 0);
        udelay(6000);
    }
    i2c_lcd_write_byte(0x20, 0);
    udelay(300);

    lcd_send_cmd(LCD_CMD_FUNCTIONSET);
    lcd_send_cmd(LCD_CMD_DISPLAYCONTROL);
    lcd_send_cmd(LCD_CMD_CLEARDISPLAY);
    mdelay(3);
    lcd_send_cmd(LCD_CMD_ENTRYMODESET);
}

// --- 문자 장치 파일 오퍼레이션 ---
static int lcd_open(struct inode *inode, struct file *file)
{
    pr_info("I2C LCD: Device opened.\n");
    return 0;
}

static int lcd_release(struct inode *inode, struct file *file)
{
    pr_info("I2C LCD: Device closed.\n");
    return 0;
}

static ssize_t lcd_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
    if (count > MAX_MSG_LEN) {
        pr_warn("I2C LCD: Message too long, truncating to %d characters.\n", MAX_MSG_LEN);
        count = MAX_MSG_LEN;
    }

    if (copy_from_user(current_lcd_message, buf, count)) {
        pr_err("I2C LCD: Failed to copy data from user space.\n");
        return -EFAULT;
    }
    current_lcd_message[count] = '\0';

    cancel_delayed_work_sync(&lcd_work);
    INIT_DELAYED_WORK(&lcd_work, lcd_display_user_message_fn);
    queue_delayed_work(lcd_wq, &lcd_work, msecs_to_jiffies(100));

    pr_info("I2C LCD: Received message from user: '%s'\n", current_lcd_message);

    return count;
}

static const struct file_operations lcd_fops = {
    .owner   = THIS_MODULE,
    .open    = lcd_open,
    .release = lcd_release,
    .write   = lcd_write,
};

// --- I2C 드라이버 부분 ---
static int lcd_probe(struct i2c_client *client)
{
    int ret;
    lcd_i2c_client = client;
    pr_info("I2C LCD: Probing LCD at address 0x%x on I2C bus %d\n",
            client->addr, i2c_adapter_id(client->adapter));

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
        pr_err("I2C LCD: I2C SMBus byte data functionality not supported.\n");
        return -EIO;
    }

    lcd_init_sequence();

    lcd_wq = create_singlethread_workqueue("lcd_wq");
    if (!lcd_wq) {
        pr_err("I2C LCD: Failed to create workqueue.\n");
        return -ENOMEM;
    }

    strncpy(current_lcd_message, "Hello from Kernel!", MAX_MSG_LEN);
    current_lcd_message[MAX_MSG_LEN] = '\0';
    INIT_DELAYED_WORK(&lcd_work, lcd_display_user_message_fn);
    queue_delayed_work(lcd_wq, &lcd_work, msecs_to_jiffies(500));

    ret = alloc_chrdev_region(&lcd_dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("I2C LCD: Failed to allocate char device region.\n");
        destroy_workqueue(lcd_wq);
        return ret;
    }

    cdev_init(&lcd_cdev, &lcd_fops);
    lcd_cdev.owner = THIS_MODULE;
    ret = cdev_add(&lcd_cdev, lcd_dev_num, 1);
    if (ret < 0) {
        pr_err("I2C LCD: Failed to add char device.\n");
        unregister_chrdev_region(lcd_dev_num, 1);
        destroy_workqueue(lcd_wq);
        return ret;
    }

    pr_info("I2C LCD: Driver loaded and device /dev/%s created (Major: %d, Minor: %d).\n",
            DEVICE_NAME, MAJOR(lcd_dev_num), MINOR(lcd_dev_num));

    return 0;
}

static void lcd_remove(struct i2c_client *client)
{
    cdev_del(&lcd_cdev);
    unregister_chrdev_region(lcd_dev_num, 1);

    cancel_delayed_work_sync(&lcd_work);
    if (lcd_wq)
        destroy_workqueue(lcd_wq);

    lcd_send_cmd(LCD_CMD_CLEARDISPLAY);
    mdelay(3);
    i2c_smbus_write_byte(client, 0x00);
    pr_info("I2C LCD: Driver unloaded and device /dev/%s removed.\n", DEVICE_NAME);
}

static const struct i2c_device_id lcd_id[] = {
    { "i2c_lcd", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, lcd_id);

static struct i2c_driver lcd_driver = {
    .driver = {
        .name = "i2c_lcd",
        .owner = THIS_MODULE,
    },
    .probe  = lcd_probe,
    .remove = lcd_remove,
    .id_table = lcd_id,
};

static int __init i2c_lcd_module_init(void)
{
    pr_info("I2C LCD: Initializing I2C LCD driver module.\n");
    return i2c_add_driver(&lcd_driver);
}

static void __exit i2c_lcd_module_exit(void)
{
    i2c_del_driver(&lcd_driver);
    pr_info("I2C LCD: Exiting I2C LCD driver module.\n");
}

module_init(i2c_lcd_module_init);
module_exit(i2c_lcd_module_exit);

MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("I2C LCD driver with user space interface");
MODULE_LICENSE("GPL");

