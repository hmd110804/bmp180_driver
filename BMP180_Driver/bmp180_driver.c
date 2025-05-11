#include <linux/module.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/ioctl.h>
#include <linux/delay.h>  // msleep

#define DEVICE_NAME "bmp180"
#define CLASS_NAME "bmp"
#define BMP180_ADDR 0x77

static struct i2c_client *bmp180_client;
static dev_t dev_num;
static struct class *bmp180_class;
static struct cdev bmp180_cdev;

struct bmp180_calib {
    s16 ac1, ac2, ac3;
    u16 ac4, ac5, ac6;
    s16 b1, b2, mb, mc, md;
} calib;

static int read_word(u8 reg)
{
    int msb = i2c_smbus_read_byte_data(bmp180_client, reg);
    int lsb = i2c_smbus_read_byte_data(bmp180_client, reg + 1);
    return (msb << 8) | lsb;
}

static void read_calibration_data(void)
{
    calib.ac1 = read_word(0xAA);
    calib.ac2 = read_word(0xAC);
    calib.ac3 = read_word(0xAE);
    calib.ac4 = read_word(0xB0);
    calib.ac5 = read_word(0xB2);
    calib.ac6 = read_word(0xB4);
    calib.b1  = read_word(0xB6);
    calib.b2  = read_word(0xB8);
    calib.mb  = read_word(0xBA);
    calib.mc  = read_word(0xBC);
    calib.md  = read_word(0xBE);
}

static long bmp180_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int ut, up, x1, x2, b5, b6, x3, b3, p;
    unsigned int b4, b7;
    int temp, pres;
    int result[2];  // Đưa lên đầu theo C90

    // Đọc nhiệt độ chưa bù
    i2c_smbus_write_byte_data(bmp180_client, 0xF4, 0x2E);
    msleep(5);
    ut = read_word(0xF6);

    // Tính nhiệt độ
    x1 = ((ut - calib.ac6) * calib.ac5) >> 15;
    x2 = (calib.mc << 11) / (x1 + calib.md);
    b5 = x1 + x2;
    temp = (b5 + 8) >> 4; // Đơn vị 0.1°C

    // Đọc áp suất chưa bù (OSS = 0)
    i2c_smbus_write_byte_data(bmp180_client, 0xF4, 0x34);
    msleep(8);
    up = (read_word(0xF6) << 8 | i2c_smbus_read_byte_data(bmp180_client, 0xF8)) >> 8;

    // Tính áp suất
    b6 = b5 - 4000;
    x1 = (calib.b2 * (b6 * b6 >> 12)) >> 11;
    x2 = (calib.ac2 * b6) >> 11;
    x3 = x1 + x2;
    b3 = (((calib.ac1 * 4 + x3) << 0) + 2) >> 2;
    x1 = (calib.ac3 * b6) >> 13;
    x2 = (calib.b1 * (b6 * b6 >> 12)) >> 16;
    x3 = ((x1 + x2) + 2) >> 2;
    b4 = (calib.ac4 * (unsigned long)(x3 + 32768)) >> 15;
    b7 = ((unsigned long)up - b3) * 50000;

    if (b7 < 0x80000000)
        p = (b7 * 2) / b4;
    else
        p = (b7 / b4) * 2;

    x1 = (p >> 8) * (p >> 8);
    x1 = (x1 * 3038) >> 16;
    x2 = (-7357 * p) >> 16;
    pres = p + ((x1 + x2 + 3791) >> 4);

    result[0] = temp;
    result[1] = pres;

    if (copy_to_user((int *)arg, result, sizeof(result)))
        return -EFAULT;
    return 0;
}

static int bmp180_open(struct inode *inode, struct file *file) { return 0; }
static int bmp180_release(struct inode *inode, struct file *file) { return 0; }

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = bmp180_open,
    .release = bmp180_release,
    .unlocked_ioctl = bmp180_ioctl,
};

static int bmp180_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    bmp180_client = client;
    read_calibration_data();

    alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    cdev_init(&bmp180_cdev, &fops);
    cdev_add(&bmp180_cdev, dev_num, 1);
    bmp180_class = class_create(THIS_MODULE, CLASS_NAME);
    device_create(bmp180_class, NULL, dev_num, NULL, DEVICE_NAME);

    return 0;
}

static void bmp180_remove(struct i2c_client *client)
{
    device_destroy(bmp180_class, dev_num);
    class_destroy(bmp180_class);
    cdev_del(&bmp180_cdev);
    unregister_chrdev_region(dev_num, 1);
}

static const struct i2c_device_id bmp180_id[] = {
    { DEVICE_NAME, 0 },
    { }
};

MODULE_DEVICE_TABLE(i2c, bmp180_id);

static struct i2c_driver bmp180_driver = {
    .driver = {
        .name = DEVICE_NAME,
    },
    .probe = bmp180_probe,
    .remove = bmp180_remove,
    .id_table = bmp180_id,
};

static struct i2c_board_info bmp180_info = {
    I2C_BOARD_INFO(DEVICE_NAME, BMP180_ADDR)
};

static int __init bmp180_init(void)
{
    struct i2c_adapter *adapter = i2c_get_adapter(1);
    bmp180_client = i2c_new_client_device(adapter, &bmp180_info);
    i2c_put_adapter(adapter);
    return i2c_add_driver(&bmp180_driver);
}

static void __exit bmp180_exit(void)
{
    i2c_unregister_device(bmp180_client);
    i2c_del_driver(&bmp180_driver);
}

module_init(bmp180_init);
module_exit(bmp180_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dung Cuong Duc");
MODULE_DESCRIPTION("BMP180 Temperature and Pressure Driver");
