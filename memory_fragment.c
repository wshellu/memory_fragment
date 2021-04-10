#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <net/genetlink.h>

#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/list.h>

#define ENTRY_BUF_SIZE_MAX   4096


typedef struct _FRAG_MEM
{
    uint16_t Id;
    uint16_t PageNum;
    uint32_t BlockNum;

    uint32_t AllocatedNum;
    void** FragMemTbl;
    struct list_head List;
}
FRAG_MEM;

static LIST_HEAD(sg_MemFragList);

static int
_MemFragAdd(
    FRAG_MEM *FragMemConf
    )
{
    int ret = 0;
    int i = 0;
    uint32_t allocatedNum;
    FRAG_MEM *fragMem;

    list_for_each_entry(fragMem, &sg_MemFragList, List)
    {
        if (fragMem->Id == FragMemConf->Id)
        {
            ret = -EEXIST;
           goto CommonReturn;
        }
    }

    fragMem = kzalloc(sizeof(FRAG_MEM), GFP_KERNEL);
    if (!fragMem)
    {
        ret = -EINVAL;
        goto CommonReturn;
    }

    *fragMem = *FragMemConf;

    list_add_tail(&fragMem->List, &sg_MemFragList);

    allocatedNum = 2 * fragMem->BlockNum;
    fragMem->FragMemTbl = kzalloc(allocatedNum * sizeof(void*), GFP_KERNEL);
    if (!fragMem->FragMemTbl)
    {
        ret = -ENOMEM;
        printk(KERN_ERR "alloc FragMemTbl falied.\n");
        goto CommonReturn;
    }

    for (i = 0; i < allocatedNum; i++)
    {
        fragMem->FragMemTbl[i] = kzalloc(fragMem->PageNum * PAGE_SIZE, GFP_KERNEL);
        if (!fragMem->FragMemTbl[i])
        {
            ret = -ENOMEM;
            printk(KERN_ERR "alloc FragMemTbl(%d) falied.\n", i);
            break;
        }

        fragMem->AllocatedNum++;
    }

    for (i = 0; i < allocatedNum; i += 2)
    {
        if (fragMem->FragMemTbl[i])
        {
            kfree(fragMem->FragMemTbl[i]);
            fragMem->FragMemTbl[i] = NULL;
            fragMem->AllocatedNum--;
        }
    }

CommonReturn:
    if (fragMem)
    {
        printk(KERN_INFO"add memfrag(%u pagenum = %u bloclnum = %u AllocatedNum = %u PAGE_SIZE = %lu).\n",
            fragMem->Id, fragMem->PageNum, fragMem->BlockNum, fragMem->AllocatedNum, PAGE_SIZE);
    }
    return ret;
}

static void
_MemFragDel(
    uint8_t Id
    )
{
    uint32_t i = 0;
    FRAG_MEM *fragMem, *nextFragMem;

    list_for_each_entry_safe(fragMem, nextFragMem, &sg_MemFragList, List)
    {
        if (fragMem->Id == Id)
        {
            printk(KERN_INFO"delete memfrag(%u PageNum = %u BlockNum = %u AllocatedNum = %u).\n",
                fragMem->Id, fragMem->PageNum, fragMem->BlockNum, fragMem->AllocatedNum);

            if (fragMem->FragMemTbl)
            {
                for (i = 0; i < fragMem->BlockNum * 2; i++)
                {
                    if (fragMem->FragMemTbl[i])
                    {
                        kfree(fragMem->FragMemTbl[i]);
                        fragMem->FragMemTbl[i] = NULL;
                        fragMem->AllocatedNum--;
                    }
                }
                
                kfree(fragMem->FragMemTbl);
            }

            kfree(fragMem);
            list_del(&fragMem->List);
        }
    }
}

static void
_MemFragDelAll(
    void
    )
{
    uint32_t i = 0;
    FRAG_MEM *fragMem, *nextFragMem;

    list_for_each_entry_safe(fragMem, nextFragMem, &sg_MemFragList, List)
    {
        printk(KERN_INFO"delete memfrag(%u PageNum = %u BlockNum = %u AllocatedNum = %u).\n",
                fragMem->Id, fragMem->PageNum, fragMem->BlockNum, fragMem->AllocatedNum);

        if (fragMem->FragMemTbl)
        {
            for (i = 0; i < fragMem->BlockNum * 2; i++)
            {
                if (fragMem->FragMemTbl[i])
                {
                    kfree(fragMem->FragMemTbl[i]);
                    fragMem->FragMemTbl[i] = NULL;
                    fragMem->AllocatedNum--;
                }
            }
            
            kfree(fragMem->FragMemTbl);
        }

        kfree(fragMem);
        list_del(&fragMem->List);
    }
}

static void *
_ProcMemFragSeqStart(
    struct seq_file *Seq,
    loff_t *Pos
    )
{
    return seq_list_start_head(&sg_MemFragList, *Pos);
}

static void *
_ProcMemFragSeqNext(
    struct seq_file *Seq,
    void *V,
    loff_t *Pos
    )
{
    return seq_list_next(V, &sg_MemFragList, Pos);
}

static void
_ProcMemFragSeqStop(
    struct seq_file *Seq,
    void *V
    )
{
}

static int
_ProcMemFragSeqShow(
    struct seq_file *Seq,
    void *V
    )
{
    FRAG_MEM *fragMem;

    if (V == &sg_MemFragList)
    {
        return 0;
    }

    fragMem = list_entry(V, FRAG_MEM, List);
    seq_printf(Seq, "id=%u pagenum=%u blocknum=%u allocated=%u\n",
        fragMem->Id, fragMem->PageNum, fragMem->BlockNum, fragMem->AllocatedNum);

    return 0;
}

static struct seq_operations g_ProcMemFragSeqOps =
{
    .start = _ProcMemFragSeqStart,
    .next = _ProcMemFragSeqNext,
    .stop = _ProcMemFragSeqStop,
    .show = _ProcMemFragSeqShow,
};

static int
_ProcMemFragOpen(
    struct inode *Inode,
    struct file *File
)
{
    return seq_open(File, &g_ProcMemFragSeqOps);
}
static ssize_t
_ProcMemFragWrite(
    struct file *File,
    const char __user *Buffer,
    size_t Count,
    loff_t *Pos
    )
{
    int ret = 0;
    char *buf = NULL, *str, opStr;
    char *p, *end;
    FRAG_MEM fragMem;
 
    buf = vzalloc(Count + 1);
    if (!buf)
    {
        ret = -ENOMEM;
        goto CommonReturn;
    }

    if (copy_from_user(buf, Buffer, Count))
    {
        ret = -EFAULT;
        goto CommonReturn;
    }

    memset(&fragMem, 0, sizeof(fragMem));
    str = buf;
    while (*str == ' ')
    {
        str++;
    }

    opStr = *str;
    if (opStr != '+' && opStr != '-')
    {
        ret = -EINVAL;
        goto CommonReturn;
    }
    str++;

    end = buf + Count;
    while(str && str < end)
    {
        if (*str == ' ')
        {
            str++;
            continue;
        }

        p = strchr(str, '=');
        if (!p)
        {
            break;
        }

        if (strncmp(str, "id", p - str) == 0)
        {
            p++;
            fragMem.Id = simple_strtol(p, NULL, 0);
        }
        else if (strncmp(str, "pagenum", p - str) == 0)
        {
            p++;
            fragMem.PageNum = simple_strtol(p, NULL, 0);
        }
        else if (strncmp(str, "blocknum", p - str) == 0)
        {
            p++;
            fragMem.BlockNum = simple_strtol(p, NULL, 0);
        }
        else
        {
            ret = -EINVAL;
            goto CommonReturn;
        }

        str = strchr(p, ' ');
    }

    if (opStr == '+')
    {
        ret = _MemFragAdd(&fragMem);
        if (ret < 0)
        {
            printk(KERN_ERR"_MemFragDel failed(ret = %d).\n", ret);
            goto CommonReturn;
        }
    }
    else if (opStr == '-')
    {
        _MemFragDel(fragMem.Id);
    }
    
    ret = Count;

CommonReturn:
    if (buf)
    {
        vfree(buf);
    }
    return ret;
}

static const struct file_operations sg_MemFragOps =
{
    .owner = THIS_MODULE,
    .open = _ProcMemFragOpen,
    .read = seq_read,
    .write  = _ProcMemFragWrite,
    .llseek = seq_lseek,
    .release = seq_release,
};


static int
MemFragProcInit(
    void
    )
{
    int ret = 0;
    struct proc_dir_entry *entry;

    entry = proc_create("fragmem", S_IALLUGO, NULL, &sg_MemFragOps);
    if (!entry)
    {
        ret = -ENOMEM;
        goto CommonReturn;
    }
    
CommonReturn:
    return ret;
}

static void
MemFragProcExit(
    void
    )
{
    remove_proc_entry ("fragmem", NULL);
}

static int
MemFragInit(
    void
    )
{
    int ret = 0;

    ret = MemFragProcInit();
    if (ret < 0)
    {
        printk(KERN_ERR"MemFragProcInit failed(ret = %d).\n", ret);
        goto CommonReturn;
    }

CommonReturn:
    return ret;
}

static void
MemFragExit(
    void
    )
{
    MemFragProcExit();
    _MemFragDelAll();
}

module_init(MemFragInit);
module_exit(MemFragExit);
