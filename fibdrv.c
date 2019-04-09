#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

typedef unsigned long long ulint;
#define MOD 1000000007;

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 100

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);

static ulint fib_sequence(long long k)
{
    /* FIXME: use clz/ctz and fast algorithms to speed up */
    unsigned long long f[k + 2];

    f[0] = 0;
    f[1] = 1;

    for (int i = 2; i <= k; i++) {
        f[i] = f[i - 1] + f[i - 2];
    }

    return f[k];
}

void matrix_multiply(ulint F[2][2], ulint M[2][2]);
void matrix_power(ulint F[2][2], long long n);

void multiply(ulint F[2][2], ulint M[2][2])
{
    int x = F[0][0] * M[0][0] + F[0][1] * M[1][0];
    int y = F[0][0] * M[0][1] + F[0][1] * M[1][1];
    int z = F[1][0] * M[0][0] + F[1][1] * M[1][0];
    int w = F[1][0] * M[0][1] + F[1][1] * M[1][1];

    F[0][0] = x;
    F[0][1] = y;
    F[1][0] = z;
    F[1][1] = w;
}

void power(ulint F[2][2], int n)
{
    if (n == 0 || n == 1)
        return;
    ulint M[2][2] = {{1, 1}, {1, 0}};

    power(F, n / 2);
    multiply(F, F);

    if (n % 2 != 0)
        multiply(F, M);
}

static ulint fib_seq_matrix_exponentation(long long k)
{
    ulint F[2][2] = {{1, 1}, {1, 0}};
    if (k == 0)
        return 0;
    power(F, k - 1);

    return F[0][0];
}

static ulint fast_fib(long long k, long long int ans[2])
{
    long long int a, b, c, d;
    if (k == 0) {
        ans[0] = 0;
        ans[1] = 1;
        return;
    }
    fast_fib((k / 2), ans);
    a = ans[0]; /* F(n) */
    b = ans[1]; /* F(n+1) */
    c = 2 * b - a;
    if (c < 0)
        c += MOD;
    c = (a * c) % MOD;         /* F(2n) */
    d = (a * a + b * b) % MOD; /* F(2n + 1) */
    if (k % 2 == 0) {
        ans[0] = c;
        ans[1] = d;
    } else {
        ans[0] = d;
        ans[1] = c + d;
    }
}

static ulint fib_double_fib_identities(long long k)
{
    if (k <= 0)
        return 0;

    long long t = 1 << (63 - __builtin_clz(k)) >>
                  1;  // the first index of leading 1 minus one
    long long a = 1;  // f(i)
    long long b = 1;  // f(i+1)

    for (; t; t >>= 1) {
        long long _a = a * (2 * b - a);  // f(2i) = f(i) * (2f(i+1) - f(i))
        long long _b =
            a * a + b * b;  // f(2i+1) = f(i) * f(i) + f(i+1) * f(i+1)
        a = (k & t) ? _b : _a;
        b = (k & t) ? _b + _a : _b;
    }

    return a;
}

static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    char buffer[100];
    ktime_t t;
    long long int ans[2] = {0};
    t = ktime_get();
    unsigned long long result = fast_fib(*offset, ans);
    t = ktime_sub(ktime_get(), t);
    unsigned long long duration = (unsigned long long) ktime_to_ns(t);
    sprintf(buffer, "%llu %llu", result, duration);
    printk("%llu ns", duration);
    copy_to_user(buf, buffer, 100 * sizeof(char));
    return 0;
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return 1;
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    cdev_init(fib_cdev, &fib_fops);
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }
    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
