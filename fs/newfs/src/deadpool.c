#define _XOPEN_SOURCE 700

#include "deadpool.h"

/******************************************************************************
* SECTION: 宏定义
*******************************************************************************/
#define OPTION(t, p)        { t, offsetof(struct custom_options, p), 1 }

/******************************************************************************
* SECTION: global region
*******************************************************************************/
struct deadpool_super      deadpool_super; 
struct custom_options 	   deadpool_options;

/******************************************************************************
* SECTION: 全局变量
*******************************************************************************/
static const struct fuse_opt option_spec[] = {		/* 用于FUSE文件系统解析参数 */
	OPTION("--device=%s", device),
	FUSE_OPT_END
};

/******************************************************************************
* SECTION: FUSE操作定义
*******************************************************************************/
static struct fuse_operations operations = {
	.init = deadpool_init,						 /* mount文件系统 */		
	.destroy = deadpool_destroy,				 /* umount文件系统 */
	.mkdir = deadpool_mkdir,					 /* 建目录，mkdir */
	.getattr = deadpool_getattr,				 /* 获取文件属性，类似stat，必须完成 */
	.readdir = deadpool_readdir,				 /* 填充dentrys */
	.mknod = deadpool_mknod,					 /* 创建文件，touch相关 */
	.write = deadpool_write,								  	 /* 写入文件 */
	.read = deadpool_read,								  	 /* 读文件 */
	.utimens = deadpool_utimens,				 /* 修改时间，忽略，避免touch报错 */
	.truncate = deadpool_truncate,						  		 /* 改变文件大小 */
	.unlink = deadpool_unlink,							  		 /* 删除文件 */
	.rmdir	= deadpool_rmdir,							  		 /* 删除目录， rm -r */
	.rename = deadpool_rename,							  		 /* 重命名，mv */

	.open = deadpool_open,							
	.opendir = deadpool_opendir,
	.access = deadpool_access
};
/******************************************************************************
* SECTION: 必做函数实现
*******************************************************************************/
/**
 * @brief 挂载（mount）文件系统
 * 
 * @param conn_info 可忽略，一些建立连接相关的信息 
 * @return void*
 */
void* deadpool_init(struct fuse_conn_info * conn_info) {
	/* TODO: 在这里进行挂载 */

	if (deadpool_mount(deadpool_options) != DEADPOOL_ERROR_NONE) {
        DEADPOOL_DBG("[%s] mount error\n", __func__);
		fuse_exit(fuse_get_context()->fuse);
		return NULL;
	} 
	return NULL;
	
}

/**
 * @brief 卸载（umount）文件系统
 * 
 * @param p 可忽略
 * @return void
 */
void deadpool_destroy(void* p) {
	/* TODO: 在这里进行卸载 */
	if (deadpool_umount() != DEADPOOL_ERROR_NONE) {
		DEADPOOL_DBG("[%s] unmount error\n", __func__);
		fuse_exit(fuse_get_context()->fuse);
		return;
	}
	return;
}

/**
 * @brief 创建目录
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建模式（只读？只写？），可忽略
 * @return int 0成功，否则返回对应错误号
 */
int deadpool_mkdir(const char* path, mode_t mode) {
	/* TODO: 解析路径，创建目录 */
	printf("mkdir\n");
	(void)mode;
	boolean is_find, is_root;
	char* fname;
	struct deadpool_dentry* last_dentry = deadpool_lookup(path, &is_find, &is_root);
	struct deadpool_dentry* dentry;
	struct deadpool_inode*  inode;

	if (is_find) {
		return -DEADPOOL_ERROR_EXISTS;
	}

	if (DEADPOOL_IS_REG(last_dentry->inode)) {
		return -DEADPOOL_ERROR_UNSUPPORTED;
	}

	fname  = deadpool_get_fname(path);
	dentry = new_dentry(fname, DEADPOOL_DIR); 
	dentry->parent = last_dentry;
	inode  = deadpool_alloc_inode(dentry);
	deadpool_alloc_dentry(last_dentry->inode, dentry);
	
	return DEADPOOL_ERROR_NONE;
}

/**
 * @brief 获取文件或目录的属性，该函数非常重要
 * 
 * @param path 相对于挂载点的路径
 * @param deadpool_stat 返回状态
 * @return int 0成功，否则返回对应错误号
 */
int deadpool_getattr(const char* path, struct stat * deadpool_stat) {
	/* TODO: 解析路径，获取Inode，填充deadpool_stat，可参考/fs/simplefs/sfs.c的sfs_getattr()函数实现 */
	boolean	is_find, is_root;
	struct deadpool_dentry* dentry = deadpool_lookup(path, &is_find, &is_root);
	if (is_find == FALSE) {
		return -DEADPOOL_ERROR_NOTFOUND;
	}

	if (DEADPOOL_IS_DIR(dentry->inode)) {
		deadpool_stat->st_mode = S_IFDIR | DEADPOOL_DEFAULT_PERM;
		deadpool_stat->st_size = dentry->inode->dir_cnt * sizeof(struct deadpool_dentry_d);
	}
	else if (DEADPOOL_IS_REG(dentry->inode)) {
		deadpool_stat->st_mode = S_IFREG | DEADPOOL_DEFAULT_PERM;
		deadpool_stat->st_size = dentry->inode->size;
	}
	else if (DEADPOOL_IS_SYM_LINK(dentry->inode)) {
		deadpool_stat->st_mode = S_IFLNK | DEADPOOL_DEFAULT_PERM;
		deadpool_stat->st_size = dentry->inode->size;
	}

	deadpool_stat->st_nlink = 1;
	deadpool_stat->st_uid 	 = getuid();
	deadpool_stat->st_gid 	 = getgid();
	deadpool_stat->st_atime   = time(NULL);
	deadpool_stat->st_mtime   = time(NULL);
	deadpool_stat->st_blksize = DEADPOOL_BLK_SZ();

	if (is_root) {
		deadpool_stat->st_size	= deadpool_super.sz_usage; 
		deadpool_stat->st_blocks = DEADPOOL_DISK_SZ() / DEADPOOL_BLK_SZ();
		deadpool_stat->st_nlink  = 2;		/* !特殊，根目录link数为2 */
	}
	return DEADPOOL_ERROR_NONE;
}

/**
 * @brief 遍历目录项，填充至buf，并交给FUSE输出
 * 
 * @param path 相对于挂载点的路径
 * @param buf 输出buffer
 * @param filler 参数讲解:
 * 
 * typedef int (*fuse_fill_dir_t) (void *buf, const char *name,
 *				const struct stat *stbuf, off_t off)
 * buf: name会被复制到buf中
 * name: dentry名字
 * stbuf: 文件状态，可忽略
 * off: 下一次offset从哪里开始，这里可以理解为第几个dentry
 * 
 * @param offset 第几个目录项？
 * @param fi 可忽略
 * @return int 0成功，否则返回对应错误号
 */
int deadpool_readdir(const char * path, void * buf, fuse_fill_dir_t filler, off_t offset,
			    		 struct fuse_file_info * fi) {
    /* TODO: 解析路径，获取目录的Inode，并读取目录项，利用filler填充到buf，可参考/fs/simplefs/sfs.c的sfs_readdir()函数实现 */
    boolean	is_find, is_root;
	int		cur_dir = offset;

	struct deadpool_dentry* dentry = deadpool_lookup(path, &is_find, &is_root);
	struct deadpool_dentry* sub_dentry;
	struct deadpool_inode* inode;
	if (is_find) {
		inode = dentry->inode;
		sub_dentry = deadpool_get_dentry(inode, cur_dir);
		if (sub_dentry) {
			filler(buf, sub_dentry->fname, NULL, ++offset);
		}
		return DEADPOOL_ERROR_NONE;
	}
	return -DEADPOOL_ERROR_NOTFOUND;
}

/**
 * @brief 创建文件
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建文件的模式，可忽略
 * @param dev 设备类型，可忽略
 * @return int 0成功，否则返回对应错误号
 */
int deadpool_mknod(const char* path, mode_t mode, dev_t dev) {
	/* TODO: 解析路径，并创建相应的文件 */
	boolean	is_find, is_root;
	
	struct deadpool_dentry* last_dentry = deadpool_lookup(path, &is_find, &is_root);
	struct deadpool_dentry* dentry;
	struct deadpool_inode* inode;
	char* fname;
	
	if (is_find == TRUE) {
		return -DEADPOOL_ERROR_EXISTS;
	}

	fname = deadpool_get_fname(path);
	
	if (S_ISREG(mode)) {
		dentry = new_dentry(fname, DEADPOOL_REG_FILE);
	}
	else if (S_ISDIR(mode)) {
		dentry = new_dentry(fname, DEADPOOL_DIR);
	}
	else {
		dentry = new_dentry(fname, DEADPOOL_REG_FILE);
	}
	dentry->parent = last_dentry;
	inode = deadpool_alloc_inode(dentry);
	deadpool_alloc_dentry(last_dentry->inode, dentry);

	return DEADPOOL_ERROR_NONE;
}

/**
 * @brief 修改时间，为了不让touch报错 
 * 
 * @param path 相对于挂载点的路径
 * @param tv 实践
 * @return int 0成功，否则返回对应错误号
 */
int deadpool_utimens(const char* path, const struct timespec tv[2]) {
	(void)path;
	return 0;
}
/******************************************************************************
* SECTION: 选做函数实现
*******************************************************************************/
/**
 * @brief 写入文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 写入的内容
 * @param size 写入的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 写入大小
 */
int deadpool_write(const char* path, const char* buf, size_t size, off_t offset,
		        struct fuse_file_info* fi) {
	/* 选做 */
	boolean	is_find, is_root;
	struct deadpool_dentry* dentry = deadpool_lookup(path, &is_find, &is_root);
	struct deadpool_inode*  inode;
	
	if (is_find == FALSE) {
		return -DEADPOOL_ERROR_NOTFOUND;
	}

	inode = dentry->inode;
	
	if (DEADPOOL_IS_DIR(inode)) {
		return -DEADPOOL_ERROR_ISDIR;	
	}

	if (inode->size < offset) {
		return -DEADPOOL_ERROR_SEEK;
	}

	memcpy(inode->data + offset, buf, size);
	inode->size = offset + size > inode->size ? offset + size : inode->size;
	
	return size;
}

/**
 * @brief 读取文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 读取的内容
 * @param size 读取的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 读取大小
 */
int deadpool_read(const char* path, char* buf, size_t size, off_t offset,
		       struct fuse_file_info* fi) {
	/* 选做 */
	boolean	is_find, is_root;
	struct deadpool_dentry* dentry = deadpool_lookup(path, &is_find, &is_root);
	struct deadpool_inode*  inode;

	if (is_find == FALSE) {
		return -DEADPOOL_ERROR_NOTFOUND;
	}

	inode = dentry->inode;
	
	if (DEADPOOL_IS_DIR(inode)) {
		return -DEADPOOL_ERROR_ISDIR;	
	}

	if (inode->size < offset) {
		return -DEADPOOL_ERROR_SEEK;
	}

	memcpy(buf, inode->data + offset, size);

	return size;		   
}

/**
 * @brief 删除文件
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则返回对应错误号
 */
int deadpool_unlink(const char* path) {
	/* 选做 */
	boolean	is_find, is_root;
	struct deadpool_dentry* dentry = deadpool_lookup(path, &is_find, &is_root);
	struct deadpool_inode*  inode;

	if (is_find == FALSE) {
		return -DEADPOOL_ERROR_NOTFOUND;
	}

	inode = dentry->inode;

	deadpool_drop_inode(inode);
	deadpool_drop_dentry(dentry->parent->inode, dentry);
	return DEADPOOL_ERROR_NONE;
}

/**
 * @brief 删除目录
 * 
 * 一个可能的删除目录操作如下：
 * rm ./tests/mnt/j/ -r
 *  1) Step 1. rm ./tests/mnt/j/j
 *  2) Step 2. rm ./tests/mnt/j
 * 即，先删除最深层的文件，再删除目录文件本身
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则返回对应错误号
 */
int deadpool_rmdir(const char* path) {
	/* 选做 */
	return deadpool_unlink(path);
}

/**
 * @brief 重命名文件 
 * 
 * @param from 源文件路径
 * @param to 目标文件路径
 * @return int 0成功，否则返回对应错误号
 */
int deadpool_rename(const char* from, const char* to) {
	/* 选做 */
	int ret = DEADPOOL_ERROR_NONE;
	boolean	is_find, is_root;
	struct deadpool_dentry* from_dentry = deadpool_lookup(from, &is_find, &is_root);
	struct deadpool_inode*  from_inode;
	struct deadpool_dentry* to_dentry;
	mode_t mode = 0;
	if (is_find == FALSE) {
		return -DEADPOOL_ERROR_NOTFOUND;
	}

	if (strcmp(from, to) == 0) {
		return DEADPOOL_ERROR_NONE;
	}

	from_inode = from_dentry->inode;
	
	if (DEADPOOL_IS_DIR(from_inode)) {
		mode = S_IFDIR;
	}
	else if (DEADPOOL_IS_REG(from_inode)) {
		mode = S_IFREG;
	}
	
	ret = deadpool_mknod(to, mode, NULL);
	if (ret != DEADPOOL_ERROR_NONE) {					  /* 保证目的文件不存在 */
		return ret;
	}
	
	to_dentry = deadpool_lookup(to, &is_find, &is_root);	  
	deadpool_drop_inode(to_dentry->inode);				  /* 保证生成的inode被释放 */	
	to_dentry->ino = from_inode->ino;				  /* 指向新的inode */
	to_dentry->inode = from_inode;
	
	deadpool_drop_dentry(from_dentry->parent->inode, from_dentry);
	return ret;
}

/**
 * @brief 打开文件，可以在这里维护fi的信息，例如，fi->fh可以理解为一个64位指针，可以把自己想保存的数据结构
 * 保存在fh中
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则返回对应错误号
 */
int deadpool_open(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return 0;
}

/**
 * @brief 打开目录文件
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则返回对应错误号
 */
int deadpool_opendir(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return DEADPOOL_ERROR_NONE;
}

/**
 * @brief 改变文件大小
 * 
 * @param path 相对于挂载点的路径
 * @param offset 改变后文件大小
 * @return int 0成功，否则返回对应错误号
 */
int deadpool_truncate(const char* path, off_t offset) {
	/* 选做 */
	boolean	is_find, is_root;
	struct deadpool_dentry* dentry = deadpool_lookup(path, &is_find, &is_root);
	struct deadpool_inode*  inode;
	
	if (is_find == FALSE) {
		return -DEADPOOL_ERROR_NOTFOUND;
	}
	
	inode = dentry->inode;

	if (DEADPOOL_IS_DIR(inode)) {
		return -DEADPOOL_ERROR_ISDIR;
	}

	inode->size = offset;

	return DEADPOOL_ERROR_NONE;
}


/**
 * @brief 访问文件，因为读写文件时需要查看权限
 * 
 * @param path 相对于挂载点的路径
 * @param type 访问类别
 * R_OK: Test for read permission. 
 * W_OK: Test for write permission.
 * X_OK: Test for execute permission.
 * F_OK: Test for existence. 
 * 
 * @return int 0成功，否则返回对应错误号
 */
int deadpool_access(const char* path, int type) {
	/* 选做: 解析路径，判断是否存在 */
	boolean	is_find, is_root;
	boolean is_access_ok = FALSE;
	struct deadpool_dentry* dentry = deadpool_lookup(path, &is_find, &is_root);
	struct deadpool_inode*  inode;

	switch (type)
	{
	case R_OK:
		is_access_ok = TRUE;
		break;
	case F_OK:
		if (is_find) {
			is_access_ok = TRUE;
		}
		break;
	case W_OK:
		is_access_ok = TRUE;
		break;
	case X_OK:
		is_access_ok = TRUE;
		break;
	default:
		break;
	}
	return is_access_ok ? DEADPOOL_ERROR_NONE : -DEADPOOL_ERROR_ACCESS;
}	
/******************************************************************************
* SECTION: FUSE入口
*******************************************************************************/
int main(int argc, char **argv)
{
    int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	deadpool_options.device = strdup("/home/students/220110221/ddriver");

	if (fuse_opt_parse(&args, &deadpool_options, option_spec, NULL) == -1)
		return -1;
	
	ret = fuse_main(args.argc, args.argv, &operations, NULL);
	fuse_opt_free_args(&args);
	return ret;
}