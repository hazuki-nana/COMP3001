#include "deadpool.h"

extern struct deadpool_super      deadpool_super; 
extern struct custom_options      deadpool_options;
/**
 * @brief 获取文件名
 * 
 * @param path 
 * @return char* 
 */
char* deadpool_get_fname(const char* path) {
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
int deadpool_calc_lvl(const char * path) {
    // char* path_cpy = (char *)malloc(strlen(path));
    // strcpy(path_cpy, path);
    const char* str = path;
    int   lvl = 0;
    if (strcmp(path, "/") == 0) {
        return lvl;
    }
    while (*str != '\0') {
        if (*str == '/') {
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
int deadpool_driver_read(int offset, uint8_t *out_content, int size) {
    int      offset_aligned = DEADPOOL_ROUND_DOWN(offset, DEADPOOL_BLK_SZ());
    // int      offset_aligned = DEADPOOL_ROUND_DOWN(offset, deadpool_super.sz_io);
    int      bias           = offset - offset_aligned;
    int      size_aligned   = DEADPOOL_ROUND_UP((size + bias), DEADPOOL_BLK_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    // lseek(DEADPOOL_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(DEADPOOL_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // read(DEADPOOL_DRIVER(), cur, DEADPOOL_IO_SZ());
        ddriver_read(DEADPOOL_DRIVER(), (char *)cur, DEADPOOL_IO_SZ());
        cur          += DEADPOOL_IO_SZ();
        size_aligned -= DEADPOOL_IO_SZ();   
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return DEADPOOL_ERROR_NONE;
}

/**
 * @brief 驱动写
 * 
 * @param offset 
 * @param in_content 
 * @param size 
 * @return int 
 */
int deadpool_driver_write(int offset, uint8_t *in_content, int size) {
    int      offset_aligned = DEADPOOL_ROUND_DOWN(offset, DEADPOOL_BLK_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = DEADPOOL_ROUND_UP((size + bias), DEADPOOL_BLK_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    deadpool_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);
    
    // lseek(DEADPOOL_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(DEADPOOL_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // write(DEADPOOL_DRIVER(), cur, DEADPOOL_IO_SZ());
        ddriver_write(DEADPOOL_DRIVER(), (char *)cur, DEADPOOL_IO_SZ());
        cur          += DEADPOOL_IO_SZ();
        size_aligned -= DEADPOOL_IO_SZ();   
    }

    free(temp_content);
    return DEADPOOL_ERROR_NONE;
}

/**
 * @brief 为一个inode分配dentry，采用头插法
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int deadpool_alloc_dentry(struct deadpool_inode* inode, struct deadpool_dentry* dentry) {
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }
    else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;

    inode->size += sizeof(struct deadpool_dentry_d);
    if(DEADPOOL_ROUND_UP(inode->dir_cnt * sizeof(struct deadpool_dentry_d), DEADPOOL_BLK_SZ())
                            > DEADPOOL_BLKS_SZ(inode->block_alloc)){
        inode->block_pointer[inode->block_alloc] = deadpool_alloc_data();
        inode->block_alloc++;
    }

    return inode->dir_cnt;
}

/**
 * @brief 将dentry从inode的dentrys中取出
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int deadpool_drop_dentry(struct deadpool_inode * inode, struct deadpool_dentry * dentry) {
    boolean is_find = FALSE;
    struct deadpool_dentry* dentry_cursor;
    dentry_cursor = inode->dentrys;
    
    if (dentry_cursor == dentry) {
        inode->dentrys = dentry->brother;
        is_find = TRUE;
    }
    else {
        while (dentry_cursor)
        {
            if (dentry_cursor->brother == dentry) {
                dentry_cursor->brother = dentry->brother;
                is_find = TRUE;
                break;
            }
            dentry_cursor = dentry_cursor->brother;
        }
    }
    if (!is_find) {
        return -DEADPOOL_ERROR_NOTFOUND;
    }
    inode->dir_cnt--;
    inode->size -= sizeof(struct deadpool_dentry_d);
    if(DEADPOOL_ROUND_UP(inode->size, (DEADPOOL_BLK_SZ())) != DEADPOOL_ROUND_UP(inode->size, DEADPOOL_BLK_SZ())){
        inode->block_pointer[inode->block_alloc] = -1;
        inode->block_alloc--;
    }
    return inode->dir_cnt;
}

/**
 * @brief 分配一个data，占用位图
 * 
 * 
 * @return 块号
 */
int deadpool_alloc_data() {
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    boolean is_find_free_data = FALSE;

    for (byte_cursor = 0; byte_cursor < DEADPOOL_BLKS_SZ(deadpool_super.map_data_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((deadpool_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                deadpool_super.map_data[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_data = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_data) {
            break;
        }
    }

    if (!is_find_free_data || ino_cursor == deadpool_super.file_max)
        return -DEADPOOL_ERROR_NOSPACE;

    return ino_cursor;
}

/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return deadpool_inode
 */
struct deadpool_inode* deadpool_alloc_inode(struct deadpool_dentry * dentry) {
    struct deadpool_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    boolean is_find_free_entry = FALSE;

    for (byte_cursor = 0; byte_cursor < DEADPOOL_BLKS_SZ(deadpool_super.map_inode_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((deadpool_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                deadpool_super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor == deadpool_super.max_ino)
        return (struct deadpool_inode*)-DEADPOOL_ERROR_NOSPACE;

    inode = (struct deadpool_inode*)malloc(sizeof(struct deadpool_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;

    inode->link = 1;
    inode->ftype = dentry->ftype;
    inode->block_alloc = 0;
                                                      /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
                                                      /* inode指回dentry */
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;
    inode->dentrys = NULL;

    for(int i = 0; i < DEADPOOL_DATA_PER_FILE; i++){
        inode->block_pointer[i] = -1;
    }
    
    if (DEADPOOL_IS_REG(inode)) {
        inode->data = (uint8_t *)malloc(DEADPOOL_BLKS_SZ(DEADPOOL_DATA_PER_FILE));
       
    }

    return inode;
}

/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 * 
 * @param inode 
 * @return int 
 */
int deadpool_sync_inode(struct deadpool_inode * inode) {
    struct deadpool_inode_d  inode_d;
    struct deadpool_dentry*  dentry_cursor;
    struct deadpool_dentry_d dentry_d;
    int ino             = inode->ino;
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    // memcpy(inode_d.target_path, inode->target_path, DEADPOOL_MAX_FILE_NAME);
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;

    inode_d.block_alloc   = inode->block_alloc;
    inode_d.link          = inode->link;
    for(int i = 0; i < DEADPOOL_DATA_PER_FILE; i++){
        if(i < inode->block_alloc)
            inode_d.block_pointer[i] = inode->block_pointer[i];
        else
            inode_d.block_pointer[i] = -1;
    }

    int offset;
                                                      /* Cycle 1: 写 INODE */
                                                      /* Cycle 2: 写 数据 */
    if (DEADPOOL_IS_DIR(inode)) {
        int blk_no = 0;                          
        dentry_cursor = inode->dentrys;
        
        while(dentry_cursor != NULL && blk_no < inode->block_alloc){
            offset = DEADPOOL_DATA_OFS(inode->block_pointer[blk_no]);
            while (dentry_cursor != NULL){
                memcpy(dentry_d.fname, dentry_cursor->fname, DEADPOOL_MAX_FILE_NAME);
                dentry_d.ftype = dentry_cursor->ftype;
                dentry_d.ino = dentry_cursor->ino;
                if (deadpool_driver_write(offset, (uint8_t *)&dentry_d, 
                                    sizeof(struct deadpool_dentry_d)) != DEADPOOL_ERROR_NONE) {
                    DEADPOOL_DBG("[%s] io error\n", __func__);
                    return -DEADPOOL_ERROR_IO;                     
                }
                
                if (dentry_cursor->inode != NULL) {
                    deadpool_sync_inode(dentry_cursor->inode);
                }

                dentry_cursor = dentry_cursor->brother;
                offset += sizeof(struct deadpool_dentry_d);
                if(offset + sizeof(struct deadpool_dentry_d) > DEADPOOL_DATA_OFS(inode->block_pointer[blk_no]) + DEADPOOL_BLK_SZ()){
                    break;
                }
            }
            blk_no++;
        }
        
    }
    else if (DEADPOOL_IS_REG(inode)) {
        uint8_t* temp_data = inode->data;
        int temp_size;
        for(int i = 0; i < DEADPOOL_DATA_PER_FILE; i++){
            if(temp_size >= inode->size) break;
            if(i == inode->block_alloc){
                inode->block_pointer[i] = deadpool_alloc_data();
                inode->block_alloc++;
            }
            if (deadpool_driver_write(DEADPOOL_DATA_OFS(inode->block_pointer[i]), temp_data, 
                                DEADPOOL_BLK_SZ()) != DEADPOOL_ERROR_NONE) {
                DEADPOOL_DBG("[%s] io error\n", __func__);
                return -DEADPOOL_ERROR_IO;
            }
            temp_data += DEADPOOL_BLK_SZ();
            temp_size += DEADPOOL_BLK_SZ();
        }
        inode_d.block_alloc   = inode->block_alloc;
        for(int i = 0; i < inode->block_alloc; i++){
            inode_d.block_pointer[i] = inode->block_pointer[i];
        }
    }

    if (deadpool_driver_write(DEADPOOL_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct deadpool_inode_d)) != DEADPOOL_ERROR_NONE) {
        DEADPOOL_DBG("[%s] io error\n", __func__);
        return -DEADPOOL_ERROR_IO;
    }

    return DEADPOOL_ERROR_NONE;
}

/**
 * @brief 删除内存中的一个inode， 暂时不释放
 * Case 1: Reg File
 * 
 *                  Inode
 *                /      \
 *            Dentry -> Dentry (Reg Dentry)
 *                       |
 *                      Inode  (Reg File)
 * 
 *  1) Step 1. Erase Bitmap     
 *  2) Step 2. Free Inode                      (Function of deadpool_drop_inode)
 * ------------------------------------------------------------------------
 *  3) *Setp 3. Free Dentry belonging to Inode (Outsider)
 * ========================================================================
 * Case 2: Dir
 *                  Inode
 *                /      \
 *            Dentry -> Dentry (Dir Dentry)
 *                       |
 *                      Inode  (Dir)
 *                    /     \
 *                Dentry -> Dentry
 * 
 *   Recursive
 * @param inode 
 * @return int 
 */
int deadpool_drop_inode(struct deadpool_inode * inode) {
    struct deadpool_dentry*  dentry_cursor;
    struct deadpool_dentry*  dentry_to_free;
    struct deadpool_inode*   inode_cursor;

    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    boolean is_find = FALSE;

    if (inode == deadpool_super.root_dentry->inode) {
        return DEADPOOL_ERROR_INVAL;
    }

    deadpool_drop_data(inode);
    
    if (DEADPOOL_IS_DIR(inode)) {
        dentry_cursor = inode->dentrys;
                                                      /* 递归向下drop */
        while (dentry_cursor)
        {   
            inode_cursor = dentry_cursor->inode;
            deadpool_drop_inode(inode_cursor);
            deadpool_drop_dentry(inode, dentry_cursor);
            dentry_to_free = dentry_cursor;
            dentry_cursor = dentry_cursor->brother;
            free(dentry_to_free);
        }
    }
    else if (DEADPOOL_IS_REG(inode) || DEADPOOL_IS_SYM_LINK(inode)) {
        for (byte_cursor = 0; byte_cursor < DEADPOOL_BLKS_SZ(deadpool_super.map_inode_blks); 
            byte_cursor++)                            /* 调整inodemap */
        {
            for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
                if (ino_cursor == inode->ino) {
                     deadpool_super.map_inode[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
                     is_find = TRUE;
                     break;
                }
                ino_cursor++;
            }
            if (is_find == TRUE) {
                break;
            }
        }
        if (inode->data)
            free(inode->data);
        free(inode);
    }
    return DEADPOOL_ERROR_NONE;
}

int deadpool_drop_data(struct deadpool_inode * inode){
    for(int i = 0; i < inode->block_alloc; i++){
        int byte_cursor = 0; 
        int bit_cursor  = 0; 
        int data_cursor  = 0;
        boolean is_find = FALSE;
        for (byte_cursor = 0; byte_cursor < DEADPOOL_BLKS_SZ(deadpool_super.map_data_blks); 
                byte_cursor++)                            /* 调整inodemap */
            {
                for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
                    if (data_cursor == inode->block_pointer[i]) {
                        deadpool_super.map_data[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
                        is_find = TRUE;
                        break;
                    }
                    data_cursor++;
                }
                if (is_find == TRUE) {
                    break;
                }
            }
    }
    return DEADPOOL_ERROR_NONE;
}

/**
 * @brief 
 * 
 * @param dentry dentry指向ino，读取该inode入内存
 * @param ino inode唯一编号
 * @return struct deadpool_inode* 
 */
struct deadpool_inode* deadpool_read_inode(struct deadpool_dentry * dentry, int ino) {
    printf("~ hello ~\n");
    struct deadpool_inode* inode = (struct deadpool_inode*)malloc(sizeof(struct deadpool_inode));
    struct deadpool_inode_d inode_d;
    struct deadpool_dentry* sub_dentry;
    struct deadpool_dentry_d dentry_d;
    int    dir_cnt = 0, i;
    if (deadpool_driver_read(DEADPOOL_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct deadpool_inode_d)) != DEADPOOL_ERROR_NONE) {
        DEADPOOL_DBG("[%s] io error\n", __func__);
        return NULL;                    
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    // memcpy(inode->target_path, inode_d.target_path, DEADPOOL_MAX_FILE_NAME);
    inode->dentry = dentry;

    inode->dentrys = NULL;

    inode->link = inode_d.link;
    inode->ftype = inode_d.ftype;
    inode->block_alloc = inode_d.block_alloc;
    for(int i = 0; i < DEADPOOL_DATA_PER_FILE; i++){
        inode->block_pointer[i] = inode_d.block_pointer[i];
    }

    // printf("read_inode ftype:%d\n", dentry->ftype);
    printf("read_inode ftype:%d\n", inode->dentry->ftype);
    if (DEADPOOL_IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;
        // inode->dir_cnt = dir_cnt;
        int blk_no = 0;
        int size = 0;
        int temp_i = 0;
        for (i = 0; i < dir_cnt && blk_no < inode_d.block_alloc; i++)
        {
            if (deadpool_driver_read(DEADPOOL_DATA_OFS(inode_d.block_pointer[blk_no]) + (i - temp_i) * sizeof(struct deadpool_dentry_d), 
                                (uint8_t *)&dentry_d, 
                                sizeof(struct deadpool_dentry_d)) != DEADPOOL_ERROR_NONE) {
                DEADPOOL_DBG("[%s] io error\n", __func__);
                return NULL;                    
            }
            size += sizeof(struct deadpool_dentry_d);
            if(size + sizeof(struct deadpool_dentry_d) > DEADPOOL_BLK_SZ()){
                blk_no++;
                size = 0;
                temp_i = i;
            }
            sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
            sub_dentry->parent = inode->dentry;
            sub_dentry->ino    = dentry_d.ino; 
            deadpool_alloc_dentry(inode, sub_dentry);
        }
    }
    else if (DEADPOOL_IS_REG(inode)) {
        inode->data = (uint8_t *)malloc(DEADPOOL_BLKS_SZ(DEADPOOL_DATA_PER_FILE));
        // if (deadpool_driver_read(DEADPOOL_DATA_OFS(ino), (uint8_t *)inode->data, 
        //                     DEADPOOL_BLKS_SZ(DEADPOOL_DATA_PER_FILE)) != DEADPOOL_ERROR_NONE) {
        //     DEADPOOL_DBG("[%s] io error\n", __func__);
        //     return NULL;                    
        // }
        uint8_t *temp_data = inode->data;
        for(int i = 0; i < inode->block_alloc; i++){
            if (deadpool_driver_read(DEADPOOL_DATA_OFS(inode->block_pointer[i]), temp_data, 
                            DEADPOOL_BLK_SZ()) != DEADPOOL_ERROR_NONE) {
                DEADPOOL_DBG("[%s] io error\n", __func__);
                return NULL;                    
            }
            temp_data += DEADPOOL_BLK_SZ();
        }
        
    }
    return inode;
}

/**
 * @brief 
 * 
 * @param inode 
 * @param dir [0...]
 * @return struct deadpool_dentry* 
 */
struct deadpool_dentry* deadpool_get_dentry(struct deadpool_inode * inode, int dir) {
    struct deadpool_dentry* dentry_cursor = inode->dentrys;
    int    cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt) {
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
 * 
 * @param path 
 * @return struct deadpool_inode* 
 */
struct deadpool_dentry* deadpool_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct deadpool_dentry* dentry_cursor = deadpool_super.root_dentry;
    struct deadpool_dentry* dentry_ret = NULL;
    struct deadpool_inode*  inode; 
    int   total_lvl = deadpool_calc_lvl(path);
    int   lvl = 0;
    boolean is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
    *is_root = FALSE;
    strcpy(path_cpy, path);

    if (total_lvl == 0) {                           /* 根目录 */
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = deadpool_super.root_dentry;
    }
    fname = strtok(path_cpy, "/");       
    while (fname)
    {   
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制 */
            deadpool_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode;

        if (DEADPOOL_IS_REG(inode) && lvl < total_lvl) {
            DEADPOOL_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
        if (DEADPOOL_IS_DIR(inode)) {
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;

            while (dentry_cursor)
            {
                if (memcmp(dentry_cursor->fname, fname, strlen(fname)) == 0) {
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;
            }
            
            if (!is_hit) {
                *is_find = FALSE;
                DEADPOOL_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }

            if (is_hit && lvl == total_lvl) {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/"); 
    }

    if (dentry_ret->inode == NULL) {
        dentry_ret->inode = deadpool_read_inode(dentry_ret, dentry_ret->ino);
    }
    
    return dentry_ret;
}

int deadpool_mount(struct custom_options options){
    int                         ret = DEADPOOL_ERROR_NONE;
    int                         driver_fd;
    struct deadpool_super_d     deadpool_super_d; 
    struct deadpool_dentry*     root_dentry;
    struct deadpool_inode*      root_inode;

    int                         inode_num;
    int                         map_inode_blks;
    
    int                         super_blks;
    int                         map_data_blks;
    int                         data_max;
    boolean                     is_init = FALSE;

    deadpool_super.is_mounted = FALSE;

    driver_fd = ddriver_open(options.device);

    if (driver_fd < 0) {
        return driver_fd;
    }

    deadpool_super.fd = driver_fd;
    ddriver_ioctl(DEADPOOL_DRIVER(), IOC_REQ_DEVICE_SIZE,  &deadpool_super.sz_disk);
    ddriver_ioctl(DEADPOOL_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &deadpool_super.sz_io);
    deadpool_super.sz_blk = 2 * deadpool_super.sz_io;

    root_dentry = new_dentry("/", DEADPOOL_DIR);

    if (deadpool_driver_read(DEADPOOL_SUPER_OFS, (uint8_t *)(&deadpool_super_d), 
                        sizeof(struct deadpool_super_d)) != DEADPOOL_ERROR_NONE) {
        return -DEADPOOL_ERROR_IO;
    }
                                                                /* 读取super */
    if (deadpool_super_d.magic != DEADPOOL_MAGIC_NUM) {     /* 幻数无 */
                                                                /* 估算各部分大小 */
        super_blks = 1;

        inode_num  =  (deadpool_super.sz_disk) / (sizeof(struct deadpool_inode_d) + 6 * DEADPOOL_BLK_SZ());

        int inode_blks = (inode_num * sizeof(struct deadpool_inode_d)) % DEADPOOL_BLK_SZ() == 0 ? \
        ((inode_num * sizeof(struct deadpool_inode_d)) / DEADPOOL_BLK_SZ()) : ((inode_num * sizeof(struct deadpool_inode_d)) / DEADPOOL_BLK_SZ() + 1); 

        map_inode_blks = 1;       

        map_data_blks = 1;
        data_max = (deadpool_super.sz_disk / DEADPOOL_BLK_SZ() - map_inode_blks - map_data_blks - super_blks - inode_blks); 
                                                      /* 布局layout */
        deadpool_super_d.max_ino = inode_num; 
        deadpool_super_d.file_max = data_max;

        deadpool_super_d.map_inode_offset = DEADPOOL_SUPER_OFS + DEADPOOL_BLKS_SZ(super_blks);
        deadpool_super_d.map_data_offset = deadpool_super_d.map_inode_offset + DEADPOOL_BLKS_SZ(map_inode_blks);
        deadpool_super_d.inode_offset = deadpool_super_d.map_data_offset + DEADPOOL_BLKS_SZ(map_data_blks);
        deadpool_super_d.data_offset = deadpool_super_d.inode_offset + DEADPOOL_BLKS_SZ(inode_blks);

        deadpool_super_d.map_inode_blks  = map_inode_blks;
        deadpool_super_d.map_data_blks = map_data_blks;
    
        
        deadpool_super_d.sz_usage    = 0;
        DEADPOOL_DBG("inode map blocks: %d\n", map_inode_blks);
        is_init = TRUE;
    }
    deadpool_super.sz_usage   = deadpool_super_d.sz_usage;      /* 建立 in-memory 结构 */
    
    deadpool_super.map_inode = (uint8_t *)malloc(DEADPOOL_BLKS_SZ(deadpool_super_d.map_inode_blks));
    deadpool_super.map_data = (uint8_t *)malloc(DEADPOOL_BLKS_SZ(deadpool_super_d.map_data_blks));

    deadpool_super.map_inode_blks = deadpool_super_d.map_inode_blks;
    deadpool_super.map_data_blks = deadpool_super_d.map_data_blks;

    deadpool_super.map_inode_offset = deadpool_super_d.map_inode_offset;
    deadpool_super.map_data_offset = deadpool_super_d.map_data_offset;
    deadpool_super.inode_offset = deadpool_super_d.inode_offset;
    deadpool_super.data_offset = deadpool_super_d.data_offset;

    deadpool_super.max_ino = deadpool_super_d.max_ino;
    deadpool_super.file_max = deadpool_super_d.file_max;

    if (deadpool_driver_read(deadpool_super_d.map_inode_offset, (uint8_t *)(deadpool_super.map_inode), 
                        DEADPOOL_BLKS_SZ(deadpool_super_d.map_inode_blks)) != DEADPOOL_ERROR_NONE) {
        return -DEADPOOL_ERROR_IO;
    }

    if (deadpool_driver_read(deadpool_super_d.map_data_offset, (uint8_t *)(deadpool_super.map_data), 
                        DEADPOOL_BLKS_SZ(deadpool_super_d.map_data_blks)) != DEADPOOL_ERROR_NONE) {
        return -DEADPOOL_ERROR_IO;
    }

    if (is_init) {                                    /* 分配根节点 */
        root_inode = deadpool_alloc_inode(root_dentry);//第一次挂载分配0号inode
        deadpool_sync_inode(root_inode);
    }
    
    root_inode            = deadpool_read_inode(root_dentry, DEADPOOL_ROOT_INO);

    root_dentry->inode    = root_inode;
    deadpool_super.root_dentry = root_dentry;
    deadpool_super.is_mounted  = TRUE;

    deadpool_dump_map();
    return ret;
}

/**
 * @brief 
 * 
 * @return int 
 */
int deadpool_umount() {
    struct deadpool_super_d  deadpool_super_d; 

    if (!deadpool_super.is_mounted) {
        return DEADPOOL_ERROR_NONE;
    }

    deadpool_sync_inode(deadpool_super.root_dentry->inode);     /* 从根节点向下刷写节点 */
                                                    
    deadpool_super_d.magic               = DEADPOOL_MAGIC_NUM;
    deadpool_super_d.map_inode_blks      = deadpool_super.map_inode_blks;
    deadpool_super_d.map_inode_offset    = deadpool_super.map_inode_offset;

    deadpool_super_d.sz_usage            = deadpool_super.sz_usage;

    deadpool_super_d.map_data_offset     = deadpool_super.map_data_offset;
    deadpool_super_d.map_data_blks       = deadpool_super.map_data_blks;
    deadpool_super_d.inode_offset        = deadpool_super.inode_offset;
    deadpool_super_d.data_offset         = deadpool_super.data_offset;
    deadpool_super_d.max_ino             = deadpool_super.max_ino;
    deadpool_super_d.file_max            = deadpool_super.file_max;

    if (deadpool_driver_write(DEADPOOL_SUPER_OFS, (uint8_t *)&deadpool_super_d, 
                     sizeof(struct deadpool_super_d)) != DEADPOOL_ERROR_NONE) {
        return -DEADPOOL_ERROR_IO;
    }

    if (deadpool_driver_write(deadpool_super_d.map_inode_offset, (uint8_t *)(deadpool_super.map_inode), 
                         DEADPOOL_BLKS_SZ(deadpool_super_d.map_inode_blks)) != DEADPOOL_ERROR_NONE) {
        return -DEADPOOL_ERROR_IO;
    }

    if (deadpool_driver_write(deadpool_super_d.map_data_offset, (uint8_t *)(deadpool_super.map_data), 
                         DEADPOOL_BLKS_SZ(deadpool_super_d.map_data_blks)) != DEADPOOL_ERROR_NONE) {
        return -DEADPOOL_ERROR_IO;
    }

    // if (deadpool_driver_write(deadpool_super_d.inode_offset, (uint8_t *)(deadpool_super.map_data), 
    //                      DEADPOOL_BLKS_SZ(deadpool_super_d.map_data_blks)) != DEADPOOL_ERROR_NONE) {
    //     return -DEADPOOL_ERROR_IO;
    // }

    // deadpool_drop_inode(deadpool_super.root_dentry->inode);
    free(deadpool_super.map_inode);
    free(deadpool_super.map_data);
    ddriver_close(DEADPOOL_DRIVER());

    printf("umount magic:%x\n", deadpool_super_d.magic);
    return DEADPOOL_ERROR_NONE;
}


void deadpool_dump_map() {
    int byte_cursor = 0;
    int bit_cursor = 0;

    
    printf("maginc:%d\nmap_inode_offset:%d\n", deadpool_super.magic, deadpool_super.map_inode_offset);
    printf("map_data_offset:%d\ninode_offset:%d\n", deadpool_super.map_data_offset, deadpool_super.inode_offset);
    printf("data_offset:%d\n", deadpool_super.data_offset);
    for (byte_cursor = 0; byte_cursor < DEADPOOL_BLKS_SZ(deadpool_super.map_inode_blks); 
         byte_cursor+=4)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            printf("%d ", (deadpool_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) >> bit_cursor);   
        }
        printf("\t");

        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            printf("%d ", (deadpool_super.map_inode[byte_cursor + 1] & (0x1 << bit_cursor)) >> bit_cursor);   
        }
        printf("\t");
        
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            printf("%d ", (deadpool_super.map_inode[byte_cursor + 2] & (0x1 << bit_cursor)) >> bit_cursor);   
        }
        printf("\t");
        
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            printf("%d ", (deadpool_super.map_inode[byte_cursor + 3] & (0x1 << bit_cursor)) >> bit_cursor);   
        }
        printf("\n");
    }
}