#ifndef _TYPES_H_
#define _TYPES_H_

#define MAX_NAME_LEN    128     
/******************************************************************************
 * SECTION: Macro
 *******************************************************************************/
#define NEWFS_MAGIC_NUM 880818 // 卸载时的magic数
#define NEWFS_ERROR_NONE 0
#define NEWFS_SUPER_OFS 0     // 超级快的offset
#define NEWFS_ERROR_IO EIO    /* Error Input/Output */
#define NEWFS_DATA_PER_FILE 6 // 数据最多占6块
#define NEWFS_ROOT_INO 0      // 根节点ino
#define NEWFS_ERROR_NOSPACE ENOSPC
#define UINT8_BITS 8
#define NEWFS_ERROR_EXISTS EEXIST
#define NEWFS_ERROR_UNSUPPORTED ENXIO
#define NEWFS_ERROR_NOTFOUND ENOENT
#define NEWFS_ERROR_SEEK ESPIPE
#define NEWFS_ERROR_ISDIR EISDIR
/******************************************************************************
 * SECTION: Macro Function
 *******************************************************************************/
#define NEWFS_DRIVER() (newfs_super.fd)
#define NEWFS_IO_SZ() (newfs_super.sz_io)
#define NEWFS_DISK_SZ() (newfs_super.sz_disk)
#define NEWFS_ROUND_DOWN(value, round) (value % round == 0 ? value : (value / round) * round)   // 向下取整
#define NEWFS_ROUND_UP(value, round) (value % round == 0 ? value : (value / round + 1) * round) // 向上取整
#define NEWFS_BLKS_SZ(blk) (newfs_super.blks_size * blk)
#define NEWFS_IS_DIR(pinode) (pinode->ftype == NEWFS_DIR)
#define NEWFS_IS_REG(pinode) (pinode->ftype == NEWFS_REG_FILE)
#define NEWFS_ASSIGN_FNAME(psfs_dentry, _fname) memcpy(psfs_dentry->name, _fname, strlen(_fname))
#define NEWFS_DATA_OFS(data_blk) (newfs_super.data_offset + NEWFS_BLKS_SZ(data_blk))
#define NEWFS_INO_OFS(ino) (newfs_super.ino_offset + NEWFS_BLKS_SZ(ino / 20) + 50 * (ino % 20))

typedef enum newfs_file_type
{
    NEWFS_REG_FILE,
    NEWFS_DIR
} NEW_FILE_TYPE;

struct custom_options {
	const char*        device;
};

struct newfs_super {
    uint32_t magic;
    int      fd;// driver的文件描述符
    /* TODO: Define yourself */
    // 逻辑块信息 
    int blks_size; // 逻辑块大小
    int blks_nums; // 逻辑块数

    // 索引节点位图 
    int ino_map_offset; // 索引节点位图于磁盘中的偏移
    int ino_map_blks;   // 索引节点位图于磁盘中的块数

    // 数据块位图
    int data_map_offset;
    int data_map_blks;

    // 索引节点区
    int ino_offset;
    int ino_blks;

    // 数据块区
    int data_offset;
    int data_blks;

    // 根目录索引 
    struct newfs_dentry *root_dentry;// 根目录 dentry 方便快速访问

    // 文件系统参数 
    int ino_max; // 最大支持inode数
    int sz_usage; // 磁盘已经使用的大小
    int sz_disk;  // 磁盘容量
    int sz_io;
    int is_mounted;
    uint8_t *ino_map;
    uint8_t *data_map;
};

struct newfs_super_d
{
    uint32_t magic;
    int fd;
    /* TODO: Define yourself */
    // 逻辑块信息 
    int blks_size; // 逻辑块大小
    int blks_nums; // 逻辑块数

    int sz_usage; // 使用的大小

    // 索引节点位图 
    int ino_map_offset; // 索引节点位图于磁盘中的偏移
    int ino_map_blks;   // 索引节点位图于磁盘中的块数

    // 数据块位图
    int data_map_offset;
    int data_map_blks;

    // 索引节点区同理
    int ino_offset;
    int ino_blks;

    // 数据块区同理
    int data_offset;
    int data_blks;

    // 支持的限制 
    int ino_max; // 最大支持inode数
};

struct newfs_inode {
    uint32_t ino;
    /* TODO: Define yourself */
    // 文件的属性 
    int size;            // 文件已占用空间
    int link;            // 链接数，默认为1
    NEW_FILE_TYPE ftype; // 文件类型

    // 数据块的索引 
    int block_pointer[6]; // 数据块指针

    // 其他字段 
    int dir_cnt;                  // 如果是目录类型文件，下面有几个目录项
    struct newfs_dentry *dentry;  // 指向该inode的dentry
    struct newfs_dentry *dentrys; // 所有目录项
    uint8_t *data;
};

struct newfs_inode_d
{
    uint32_t ino;
    /* TODO: Define yourself */
    // 文件的属性 
    int size;            // 文件已占用空间
    int link;            // 链接数，默认为1
    NEW_FILE_TYPE ftype; // 文件类型（目录类型、普通文件类型）

    // 数据块的索引 
    int block_pointer[6]; // 数据块指针（可固定分配）

    // 其他字段 
    int dir_cnt; // 如果是目录类型文件，下面有几个目录项
};

struct newfs_dentry {
    char     name[MAX_NAME_LEN];
    uint32_t ino;
    /* TODO: Define yourself */
    NEW_FILE_TYPE ftype;
    struct newfs_inode *inode;    // 指向inode
    struct newfs_dentry *parent;  // 父亲inode的dentry
    struct newfs_dentry *brother; // 兄弟dentry
};

struct newfs_dentry_d
{
    char name[MAX_NAME_LEN];
    uint32_t ino;
    /* TODO: Define yourself */
    NEW_FILE_TYPE ftype;
};

static inline struct newfs_dentry *new_dentry(char *fname, NEW_FILE_TYPE ftype)
{
    struct newfs_dentry *dentry = (struct newfs_dentry *)malloc(sizeof(struct newfs_dentry));
    memset(dentry, 0, sizeof(struct newfs_dentry));
    NEWFS_ASSIGN_FNAME(dentry, fname);
    dentry->ftype = ftype;
    dentry->ino = -1;
    dentry->inode = NULL;
    dentry->parent = NULL;
    dentry->brother = NULL;
}

#endif /* _TYPES_H_ */