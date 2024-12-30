#include "newfs.h"

extern struct newfs_super newfs_super; // 内存超级块

/**
 * @brief 获取文件名
 *
 * @param path
 * @return char*
 */
char *newfs_get_fname(const char *path)
{
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}
/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path
 * @return int
 */
int newfs_calc_lvl(const char *path)
{
    char *str = path;
    int lvl = 0;
    if (strcmp(path, "/") == 0)
    {
        return lvl;
    }
    while (*str != NULL)
    {
        if (*str == '/')
        {
            lvl++;
        }
        str++;
    }
    return lvl;
}
/**
 * @brief 驱动读
 *
 * @param offset
 * @param out_content
 * @param size
 * @return int
 */
int newfs_driver_read(int offset, uint8_t *out_content, int size)
{
    int offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_IO_SZ());
    int bias = offset - offset_aligned;
    int size_aligned = NEWFS_ROUND_UP((size + bias), NEWFS_IO_SZ());
    uint8_t *temp_content = (uint8_t *)malloc(size_aligned);
    uint8_t *cur = temp_content;
    
    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        ddriver_read(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        cur += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NEWFS_ERROR_NONE;
}
/**
 * @brief 驱动写
 *
 * @param offset
 * @param in_content
 * @param size
 * @return int
 */
int newfs_driver_write(int offset, uint8_t *in_content, int size)
{
    int offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_IO_SZ());
    int bias = offset - offset_aligned;
    int size_aligned = NEWFS_ROUND_UP((size + bias), NEWFS_IO_SZ());
    uint8_t *temp_content = (uint8_t *)malloc(size_aligned);
    uint8_t *cur = temp_content;
    newfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);

    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        ddriver_write(NEWFS_DRIVER(), (char *)cur, NEWFS_IO_SZ());
        cur += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();
    }

    free(temp_content);
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 分配一个inode，占用位图
 *
 * @param dentry 该dentry指向分配的inode
 * @return newfs_inode
 */
struct newfs_inode *newfs_alloc_inode(struct newfs_dentry *dentry)
{
    struct newfs_inode *inode;
    int byte_cursor = 0;
    int bit_cursor = 0;
    int ino_cursor = 0;
    int is_find_free_entry = 0;

    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.ino_map_blks); byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++)
        {
            if ((newfs_super.ino_map[byte_cursor] & (0x1 << bit_cursor)) == 0)
            {
                /* 当前ino_cursor位置空闲 */
                newfs_super.ino_map[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = 1; 
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry)
        {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor == newfs_super.ino_max)
        return -NEWFS_ERROR_NOSPACE;

    // 填充信息
    inode = (struct newfs_inode *)malloc(sizeof(struct newfs_inode));
    inode->ino = ino_cursor;
    inode->size = 0;
    /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino = inode->ino;
    /* inode指回dentry */
    inode->dentry = dentry;

    inode->dir_cnt = 0;
    inode->dentrys = NULL;
    inode->ftype = dentry->ftype; // 一个inode对应一个文件，需要确定文件类型

    if (NEWFS_IS_REG(inode))
    {
        inode->data = (uint8_t *)malloc(NEWFS_BLKS_SZ(NEWFS_DATA_PER_FILE));
        // 等待真正存储存储数据时再分配块指针
    }

    return inode;
}
/**
 * @brief 为data分配一个数据块并返回数据块编号
 * @return int
 * 仿照alloc_inode
 */
int newfs_alloc_data()
{
    int byte_cursor = 0;// 字节游标，用于遍历数据位图的字节
    int bit_cursor = 0;// 位游标，用于遍历数据位图的位
    int blk_cursor = 0;
    int is_find_free_blk = 0;// 数据块游标，记录找到的空闲数据块的编号

    // 遍历数据位图的字节
    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.data_map_blks); byte_cursor++)
    {
         // 遍历数据位图的位
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++)
        {
            // 检查当前位是否为0，表示该数据块为空闲
            if ((newfs_super.data_map[byte_cursor] & (0x1 << bit_cursor)) == 0)
            {
                newfs_super.data_map[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_blk = 1;
                break;
            }
            blk_cursor++;
        }
        if (is_find_free_blk)
        {
            break;
        }
    }
    if (!is_find_free_blk || blk_cursor == newfs_super.ino_max)
        return -NEWFS_ERROR_NOSPACE;
    return blk_cursor;
}
/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 *
 * @param inode
 * @return int
 */
int newfs_sync_inode(struct newfs_inode *inode)
{
    struct newfs_inode_d inode_d;
    struct newfs_dentry *dentry_cursor;
    struct newfs_dentry_d dentry_d;
    int ino = inode->ino;
    inode_d.ino = ino;
    inode_d.size = inode->size;
    inode_d.ftype = inode->ftype;
    inode_d.dir_cnt = inode->dir_cnt;
    int offset;

    memcpy(inode_d.block_pointer, inode->block_pointer, sizeof(inode_d.block_pointer)); // 将块指针写回

    if (newfs_driver_write(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE)
    {
        NEWFS_DBG("[%s] io error\n", __func__);
        return -NEWFS_ERROR_IO;
    }

    /* Cycle 1: 写 INODE */
    /* Cycle 2: 写 数据 */
    if (NEWFS_IS_DIR(inode)) // 文件夹写回
    {
        dentry_cursor = inode->dentrys;
        offset = NEWFS_DATA_OFS(inode->block_pointer[0]); // 计算文件夹内容在磁盘内的偏移量，用第一个块指针寻找，和SFS不一样
        while (dentry_cursor != NULL)
        {
            memcpy(dentry_d.name, dentry_cursor->name, MAX_NAME_LEN);
            dentry_d.ftype = dentry_cursor->ftype;
            dentry_d.ino = dentry_cursor->ino;
            if (newfs_driver_write(offset, (uint8_t *)&dentry_d, sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE)
            {
                NEWFS_DBG("[%s] io error\n", __func__);
                return -NEWFS_ERROR_IO;
            }

            if (dentry_cursor->inode != NULL)
            {
                newfs_sync_inode(dentry_cursor->inode);
            }

            dentry_cursor = dentry_cursor->brother;
            offset += sizeof(struct newfs_dentry_d);
        }
    }
    else if (NEWFS_IS_REG(inode))
    {
        for (int i = 0; i < 6; i++)
        {
            if (inode_d.block_pointer[i] < 0)
                continue;
            offset = inode_d.block_pointer[i] * newfs_super.blks_size;
            if (newfs_driver_write(offset, inode->data, NEWFS_BLKS_SZ(6)) != NEWFS_ERROR_NONE) // 调用封装好的函数直接将文件占用的逻辑块全部写回
            {
                NEWFS_DBG("[%s] io error\n", __func__);
                return -NEWFS_ERROR_IO;
            }
        }
    }

    return NEWFS_ERROR_NONE;
}

/**
 * @brief 为一个inode分配dentry，采用头插法
 *
 * @param inode
 * @param dentry
 * @return int
 */
int newfs_alloc_dentry(struct newfs_inode *inode, struct newfs_dentry *dentry)
{
    // 如果inode中没有任何目录项，直接将新目录项赋给inode的dentrys指针
    if (inode->dentrys == NULL)
    {
        inode->dentrys = dentry;
    }
    else
    {
        // 如果已经存在目录项，则采用头插法，将新目录项插入到链表的开头
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    if (inode->dir_cnt == 1)// 如果是第一个目录项，分配块来存储dentry
    {
        inode->block_pointer[0] = newfs_alloc_data(); // 分配块指针存储dentry
    }
    else
    {
        int old_blk_used;
        old_blk_used = inode->size / NEWFS_BLKS_SZ(1); // 完整使用的块数
        int incomplete_old_blk_used = old_blk_used * NEWFS_BLKS_SZ(1) < inode->size ? old_blk_used + 1 : old_blk_used;
        // 增加inode的size，表示目录项的总大小
        inode->size += sizeof(struct newfs_dentry_d);
        // 如果超过文件系统支持的最大大小，则报错
        if (inode->size > NEWFS_BLKS_SZ(NEWFS_DATA_PER_FILE))
        {
            printf("overflows\n");
            return inode->dir_cnt;
        }
        // 如果所有块都完整使用，则分配新块
        if (old_blk_used < (inode->size / NEWFS_BLKS_SZ(1)) && incomplete_old_blk_used == old_blk_used) // 如果所有块都完整使用，则分配新块
        {
            inode->block_pointer[old_blk_used] = newfs_alloc_data();
        }
    }
    return inode->dir_cnt;
}

/**
 * @brief
 *
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct newfs_inode*
 */
struct newfs_inode *newfs_read_inode(struct newfs_dentry *dentry, int ino)
{
    struct newfs_inode *inode = (struct newfs_inode *)malloc(sizeof(struct newfs_inode));
    struct newfs_inode_d inode_d;
    struct newfs_dentry *sub_dentry;
    struct newfs_dentry_d dentry_d;
    int dir_cnt = 0, i;
    if (newfs_driver_read(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE)
    {
        NEWFS_DBG("[%s] io error\n", __func__);
        return NULL;
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    inode->ftype = inode_d.ftype;
    memcpy(inode->block_pointer, inode_d.block_pointer, sizeof(inode->block_pointer));

    inode->dentry = dentry;
    inode->dentrys = NULL;
    if (NEWFS_IS_DIR(inode))
    {
        dir_cnt = inode_d.dir_cnt;
        for (i = 0; i < dir_cnt; i++)
        {
            if (newfs_driver_read(NEWFS_DATA_OFS(inode_d.block_pointer[0]) + i * sizeof(struct newfs_dentry_d), (uint8_t *)&dentry_d,
                                  sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE)
            {
                NEWFS_DBG("[%s] io error\n", __func__);
                return NULL;
            }
            sub_dentry = new_dentry(dentry_d.name, dentry_d.ftype);
            sub_dentry->parent = inode->dentry;
            sub_dentry->ino = dentry_d.ino;
            newfs_alloc_dentry(inode, sub_dentry);
        }
    }
    else if (NEWFS_IS_REG(inode))
    {
        inode->data = (uint8_t *)malloc(NEWFS_BLKS_SZ(NEWFS_DATA_PER_FILE));
        for (i = 0; i < 6; i++)
        {
            if (inode->block_pointer[i] < 0)
                continue;
            if (newfs_driver_read(inode->block_pointer[i], (uint8_t *)(inode->data + NEWFS_BLKS_SZ(i)), NEWFS_BLKS_SZ(1)) != NEWFS_ERROR_NONE)
            {
                NEWFS_DBG("[%s] io error\n", __func__);
                return NULL;
            }
        }
    }
    return inode;
}
/**
 * @brief
 *
 * @param inode
 * @param dir [0...]
 * @return struct newfs_dentry*
 */
struct newfs_dentry *newfs_get_dentry(struct newfs_inode *inode, int dir)
{
    struct newfs_dentry *dentry_cursor = inode->dentrys;
    int cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt)
        {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}

/**
 * @brief
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 * 返回输入的最底下的那个文件夹
 * @param path
 * @return struct newfs_inode*
 */
struct newfs_dentry *newfs_lookup(const char *path, int *is_find, int *is_root)
{
    struct newfs_dentry *dentry_cursor = newfs_super.root_dentry;
    struct newfs_dentry *dentry_ret = NULL;
    struct newfs_inode *inode;
    int total_lvl = newfs_calc_lvl(path);
    int lvl = 0;
    int is_hit;
    char *fname = NULL;
    char *path_cpy = (char *)malloc(sizeof(path));
    *is_root = 0;
    strcpy(path_cpy, path);

    if (total_lvl == 0) // 根目录
    {
        *is_find = 1;
        *is_root = 1;
        dentry_ret = newfs_super.root_dentry;
    }
    fname = strtok(path_cpy, "/");
    while (fname)
    {
        lvl++;
        if (dentry_cursor->inode == NULL) // 内存是磁盘的cache
        {
            newfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode;

        if (NEWFS_IS_REG(inode) && lvl < total_lvl)
        {
            NEWFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
        if (NEWFS_IS_DIR(inode))
        {
            dentry_cursor = inode->dentrys;
            is_hit = 0;

            while (dentry_cursor)
            {
                if (memcmp(dentry_cursor->name, fname, strlen(fname)) == 0)
                {
                    is_hit = 1;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;
            }

            if (!is_hit)
            {
                *is_find = 0;
                NEWFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }

            if (is_hit && lvl == total_lvl)
            {
                *is_find = 1;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/");
    }

    if (dentry_ret->inode == NULL)
    {
        dentry_ret->inode = newfs_read_inode(dentry_ret, dentry_ret->ino);
    }

    return dentry_ret;
}

/**
 * @brief 挂载newfs, Layout 如下
 *
 * Layout
 * | Super | Inode Map | Data Map | Inode | Data |
 *
 * 2 * IO_SZ = BLK_SZ
 *
 * 一个blk多个inode
 * @param options
 * @return int
 */
int newfs_mount(struct custom_options options)
{
    int ret = NEWFS_ERROR_NONE;         // 返回值
    int driver_fd;                      // 磁盘文件描述符
    struct newfs_super_d newfs_super_d; // 磁盘超级块
    struct newfs_dentry *root_dentry;   // 根目录的dentry
    struct newfs_inode *root_inode;     // 根目录的inode

    int inode_num;      // inode数目
    int map_inode_blks; // ino_map的磁盘块个数
    int map_data_blks;  // data_map占的磁盘块个数

    int super_blks;  // 超级块数目
    int is_init = 0; // 是否初始化

    newfs_super.is_mounted = 0;

    driver_fd = ddriver_open(options.device);

    if (driver_fd < 0)
    {
        return driver_fd;
    }

    newfs_super.fd = driver_fd;
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_SIZE, &newfs_super.sz_disk);
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &newfs_super.sz_io);
    newfs_super.blks_size = newfs_super.sz_io * 2;

    // 根目录无父目录，需要新建dentry
    root_dentry = new_dentry("/", NEWFS_DIR);

    if (newfs_driver_read(NEWFS_SUPER_OFS, (uint8_t *)(&newfs_super_d), sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }
    // 读取super 
    if (newfs_super_d.magic != NEWFS_MAGIC_NUM)
    { // 第一次挂载 
        // 估算各部分大小 
        super_blks = 1;     // 超级块占1个逻辑块
        inode_num = 585;    // 索引节点占585逻辑块，一个逻辑块放一个节点
        map_inode_blks = 1; // 索引节点位图占1个逻辑块
        map_data_blks = 1;  // 数据块位图占1个逻辑块
        newfs_super_d.ino_max = inode_num;
        newfs_super_d.ino_map_offset = NEWFS_BLKS_SZ(super_blks); // 索引节点位图的位置在超级块之后
        newfs_super_d.ino_map_blks = map_inode_blks;
        newfs_super_d.data_map_offset = NEWFS_BLKS_SZ((super_blks + map_inode_blks)); // 数据块位图在索引节点位图之后
        newfs_super_d.data_map_blks = map_data_blks;

        newfs_super_d.ino_offset = NEWFS_BLKS_SZ((super_blks + map_inode_blks + map_data_blks)); // 第一个inode在磁盘中的位置
        newfs_super_d.ino_blks = inode_num;

        newfs_super_d.data_offset = NEWFS_BLKS_SZ((super_blks + map_inode_blks + map_data_blks + newfs_super_d.ino_blks)); // 第一个数据块在磁盘中的位置
        newfs_super_d.sz_usage = 0;

        is_init = 1;
    }

    newfs_super.sz_usage = newfs_super_d.sz_usage; // 在内存中构建超级块
    newfs_super.ino_max = newfs_super_d.ino_max;

    // 初始化inode map
    newfs_super.ino_map = (uint8_t *)malloc(NEWFS_BLKS_SZ(newfs_super_d.ino_map_blks));
    newfs_super.ino_map_blks = newfs_super_d.ino_map_blks;
    newfs_super.ino_map_offset = newfs_super_d.ino_map_offset;

    // 初始化data_map
    newfs_super.data_map = (uint8_t *)malloc(NEWFS_BLKS_SZ(newfs_super_d.data_map_blks));
    newfs_super.data_map_blks = newfs_super_d.data_map_blks;
    newfs_super.data_map_offset = newfs_super_d.data_map_offset;

    // 初始化数据块偏移量
    newfs_super.data_offset = newfs_super_d.data_offset;
    newfs_super.ino_offset = newfs_super_d.ino_offset;
    newfs_super.ino_blks = newfs_super_d.ino_blks;

    if (newfs_driver_read(newfs_super_d.ino_map_offset, (uint8_t *)(newfs_super.ino_map), NEWFS_BLKS_SZ(newfs_super_d.ino_map_blks)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }

    if (newfs_driver_read(newfs_super_d.data_map_offset, (uint8_t *)(newfs_super.data_map), NEWFS_BLKS_SZ(newfs_super_d.data_map_blks)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }

    // 构建根目录的inode
    if (is_init)
    { // 分配根节点
        root_inode = newfs_alloc_inode(root_dentry);
        newfs_sync_inode(root_inode);
    }
    root_inode = newfs_read_inode(root_dentry, NEWFS_ROOT_INO);
    root_dentry->inode = root_inode;
    newfs_super.root_dentry = root_dentry;
    newfs_super.is_mounted = 1;

    return ret;
}

void newfs_sync_super_blk(struct newfs_super_d *newfs_super_d)
{
    newfs_super_d->magic = NEWFS_MAGIC_NUM;
    newfs_super_d->ino_map_blks = newfs_super.ino_map_blks;
    newfs_super_d->ino_map_offset = newfs_super.ino_map_offset;
    newfs_super_d->data_offset = newfs_super.data_offset;
    newfs_super_d->sz_usage = newfs_super.sz_usage;
    newfs_super_d->blks_size = newfs_super.blks_size;
    newfs_super_d->data_map_blks = newfs_super.data_map_blks;
    newfs_super_d->data_map_offset = newfs_super.data_map_offset;
    newfs_super_d->ino_max = newfs_super.ino_max;
    newfs_super_d->ino_offset = newfs_super.ino_offset;
    newfs_super_d->data_offset = newfs_super.data_offset;
    newfs_super_d->ino_blks = newfs_super.ino_blks;
}

/**
 * @brief
 *
 * @return int
 */
int newfs_umount()
{
    struct newfs_super_d newfs_super_d;

    if (!newfs_super.is_mounted)
    {
        return NEWFS_ERROR_NONE;
    }

    newfs_sync_inode(newfs_super.root_dentry->inode); /* 递归刷写节点 */

    newfs_sync_super_blk(&newfs_super_d);

    if (newfs_driver_write(NEWFS_SUPER_OFS, (uint8_t *)&newfs_super_d, sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }

    // 清空inode位图
    if (newfs_driver_write(newfs_super_d.ino_map_offset, (uint8_t *)(newfs_super.ino_map), NEWFS_BLKS_SZ(newfs_super.ino_map_blks)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }
    free(newfs_super.ino_map);

    // 清空数据块位图
    if (newfs_driver_write(newfs_super_d.data_map_offset, (uint8_t *)(newfs_super.data_map), NEWFS_BLKS_SZ(newfs_super.data_map_blks)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }
    free(newfs_super.data_map);

    ddriver_close(NEWFS_DRIVER());

    return NEWFS_ERROR_NONE;
}