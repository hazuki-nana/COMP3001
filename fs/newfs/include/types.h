#ifndef _TYPES_H_
#define _TYPES_H_

#define MAX_NAME_LEN    128     

/******************************************************************************
* SECTION: Type def
*******************************************************************************/
typedef int          boolean;
typedef uint16_t     flag16;

typedef enum deadpool_file_type {
    DEADPOOL_REG_FILE,
    DEADPOOL_DIR,
    DEADPOOL_SYM_LINK
} DEADPOOL_FILE_TYPE;
/******************************************************************************
* SECTION: Macro
*******************************************************************************/
#define TRUE                    1
#define FALSE                   0
#define UINT32_BITS             32
#define UINT8_BITS              8

#define DEADPOOL_MAGIC_NUM           0x52415453  
#define DEADPOOL_SUPER_OFS           0
#define DEADPOOL_ROOT_INO            0



#define DEADPOOL_ERROR_NONE          0
#define DEADPOOL_ERROR_ACCESS        EACCES
#define DEADPOOL_ERROR_SEEK          ESPIPE     
#define DEADPOOL_ERROR_ISDIR         EISDIR
#define DEADPOOL_ERROR_NOSPACE       ENOSPC
#define DEADPOOL_ERROR_EXISTS        EEXIST
#define DEADPOOL_ERROR_NOTFOUND      ENOENT
#define DEADPOOL_ERROR_UNSUPPORTED   ENXIO
#define DEADPOOL_ERROR_IO            EIO     /* Error Input/Output */
#define DEADPOOL_ERROR_INVAL         EINVAL  /* Invalid Args */
#define DEADPOOL_MAX_FILE_NAME       128
#define DEADPOOL_INODE_PER_FILE      1
#define DEADPOOL_DATA_PER_FILE       6
#define DEADPOOL_DEFAULT_PERM        0777

#define DEADPOOL_IOC_MAGIC           'S'
#define DEADPOOL_IOC_SEEK            _IO(DEADPOOL_IOC_MAGIC, 0)

#define DEADPOOL_FLAG_BUF_DIRTY      0x1
#define DEADPOOL_FLAG_BUF_OCCUPY     0x2
/******************************************************************************
* SECTION: Macro Function
*******************************************************************************/
#define DEADPOOL_IO_SZ()                     (deadpool_super.sz_io)
#define DEADPOOL_DISK_SZ()                   (deadpool_super.sz_disk)
#define DEADPOOL_DRIVER()                    (deadpool_super.fd)
#define DEADPOOL_BLK_SZ()                    (deadpool_super.sz_blk)

#define DEADPOOL_ROUND_DOWN(value, round)    ((value) % (round) == 0 ? (value) : ((value) / (round)) * (round))
#define DEADPOOL_ROUND_UP(value, round)      ((value) % (round) == 0 ? (value) : ((value) / (round) + 1) * (round))

#define DEADPOOL_BLKS_SZ(blks)               ((blks) * DEADPOOL_BLK_SZ())
#define DEADPOOL_ASSIGN_FNAME(pdeadpool_dentry, _fname)\
                                        memcpy(pdeadpool_dentry->fname, _fname, strlen(_fname))
#define DEADPOOL_INO_OFS(ino)                (deadpool_super.inode_offset + (ino) * sizeof(struct deadpool_inode_d))
#define DEADPOOL_DATA_OFS(bno)               (deadpool_super.data_offset + (bno) * DEADPOOL_BLKS_SZ(DEADPOOL_INODE_PER_FILE))

#define DEADPOOL_IS_DIR(pinode)              (pinode->dentry->ftype == DEADPOOL_DIR)
#define DEADPOOL_IS_REG(pinode)              (pinode->dentry->ftype == DEADPOOL_REG_FILE)
#define DEADPOOL_IS_SYM_LINK(pinode)         (pinode->dentry->ftype == DEADPOOL_SYM_LINK)

#endif /* _TYPES_H_ */

/******************************************************************************
* SECTION: FS Specific Structure - In memory structure
*******************************************************************************/

struct deadpool_dentry;
struct deadpool_inode;
struct deadpool_inode_d;
struct deadpool_super;

struct custom_options {
	char*        device;//设备
};

struct deadpool_super {
    uint32_t magic;//幻数
    int      fd;//文件描述符
    /* TODO: Define yourself */

    int           sz_io;/*io大小*/
    int           sz_disk;/*磁盘大小*/
    int           sz_blk;/*逻辑块大小*/
    int           sz_usage;
    
    uint8_t*      map_inode;/*inode位图*/
    int           map_inode_blks;/*位图逻辑块数量*/
    int           map_inode_offset;/*inode位图偏移*/

    uint8_t*      map_data;/*数据块位图*/
    int           map_data_offset;/*数据块偏移*/
    int           map_data_blks;/*位图逻辑块数量*/

    int           inode_offset;/*位图块起始地址偏移*/
    // int           inode_blks;

    int           data_offset;/*数据块起始地址偏移*/
    // int           data_blks;

    int           max_ino;/*最大inode数量*/
    int           file_max;/*最大数据块数量*/

    int           root_ino;/*根节点ino*/

    boolean       is_mounted;/*是否挂载*/

    struct deadpool_dentry* root_dentry;/*根节点dentry*/
};

struct deadpool_inode {
    uint32_t ino;
    /* TODO: Define yourself */
    int                     size;                          /* 文件已占用空间 */
    // char               target_path[DEADPOOL_MAX_FILE_NAME];/* store traget path when it is a symlink */
    int                     link;
    DEADPOOL_FILE_TYPE      ftype;

    int                     block_pointer[DEADPOOL_DATA_PER_FILE];// 数据块指针（可固定分配）

    struct deadpool_dentry* dentry;                        /* 指向inode的父目录项中对应的dentry */
    struct deadpool_dentry* dentrys;                       /* 所有目录项 */
    uint8_t*                data;      //内存数据缓冲快，卸载时再写会

    int                     dir_cnt; // 如果是目录类型文件，下面有几个目录项
    int                     block_alloc;//已分配block快数

};

struct deadpool_dentry {
    char     fname[MAX_NAME_LEN];//文件名
    uint32_t ino;
    /* TODO: Define yourself */
    struct deadpool_dentry* parent; //指向父目录的dentry
    struct deadpool_dentry* brother;//该目录下的兄弟dentry
    struct deadpool_inode*  inode;//指向该dentry对应的inode
    DEADPOOL_FILE_TYPE      ftype;
};

static inline struct deadpool_dentry* new_dentry(char * fname, DEADPOOL_FILE_TYPE ftype) {
    struct deadpool_dentry * dentry = (struct deadpool_dentry *)malloc(sizeof(struct deadpool_dentry));
    memset(dentry, 0, sizeof(struct deadpool_dentry));
    DEADPOOL_ASSIGN_FNAME(dentry, fname);
    dentry->ftype   = ftype;
    dentry->ino     = -1;
    dentry->inode   = NULL;
    dentry->parent  = NULL;
    dentry->brother = NULL;    
    return dentry;                                        
}

/******************************************************************************
* SECTION: FS Specific Structure - Disk structure
*******************************************************************************/
struct deadpool_super_d
{
    uint32_t           magic;
    uint32_t           sz_usage;
    
    uint32_t           max_ino;
    uint32_t           file_max;

    uint32_t           map_inode_blks;
    uint32_t           map_inode_offset;

    uint32_t           map_data_offset;
    uint32_t           map_data_blks;

    uint32_t           inode_offset;

    uint32_t           data_offset;

};

struct deadpool_inode_d
{
    uint32_t                ino;                           /* 在inode位图中的下标 */
    uint32_t                size;                          /* 文件已占用空间 */
    // char               target_path[DEADPOOL_MAX_FILE_NAME];/* store traget path when it is a symlink */
    uint32_t                dir_cnt;
    DEADPOOL_FILE_TYPE      ftype;   

    int                     block_pointer[DEADPOOL_DATA_PER_FILE];
    int                     block_alloc;
    int                     link;
    
};  

struct deadpool_dentry_d
{
    char                    fname[MAX_NAME_LEN];
    DEADPOOL_FILE_TYPE      ftype;
    uint32_t                ino;                           /* 指向的ino号 */
};  


