#ifndef _DEADPOOL_H_
#define _DEADPOOL_H_

#define FUSE_USE_VERSION 26
#include "stdio.h"
#include "stdlib.h"
#include <unistd.h>
#include "fcntl.h"
#include "string.h"
#include "fuse.h"
#include <stddef.h>
#include "ddriver.h"
#include "errno.h"
#include "types.h"
#include "stdint.h"

#define DEADPOOL_MAGIC           0x52415453      /* TODO: Define by yourself */
#define DEADPOOL_DEFAULT_PERM    0777   /* 全权限打开 */

/******************************************************************************
* SECTION: macro debug
*******************************************************************************/
#define DEADPOOL_DBG(fmt, ...) do { printf("DEADPOOL_DBG: " fmt, ##__VA_ARGS__); } while(0) 
/******************************************************************************
* SECTION: deadpool.c
*******************************************************************************/
void* 			   deadpool_init(struct fuse_conn_info *);
void  			   deadpool_destroy(void *);
int   			   deadpool_mkdir(const char *, mode_t);
int   			   deadpool_getattr(const char *, struct stat *);
int   			   deadpool_readdir(const char *, void *, fuse_fill_dir_t, off_t,
						                struct fuse_file_info *);
int   			   deadpool_mknod(const char *, mode_t, dev_t);
int   			   deadpool_write(const char *, const char *, size_t, off_t,
					                  struct fuse_file_info *);
int   			   deadpool_read(const char *, char *, size_t, off_t,
					                 struct fuse_file_info *);
int   			   deadpool_access(const char *, int);
int   			   deadpool_unlink(const char *);
int   			   deadpool_rmdir(const char *);
int   			   deadpool_rename(const char *, const char *);
int   			   deadpool_utimens(const char *, const struct timespec tv[2]);
int   			   deadpool_truncate(const char *, off_t);
			
int   			   deadpool_open(const char *, struct fuse_file_info *);
int   			   deadpool_opendir(const char *, struct fuse_file_info *);

/******************************************************************************
* SECTION: deadpool_utils.c
*******************************************************************************/
char* 			   		deadpool_get_fname(const char* path);
int 			   		deadpool_calc_lvl(const char * path);
int 			   		deadpool_driver_read(int offset, uint8_t *out_content, int size);
int 			   		deadpool_driver_write(int offset, uint8_t *in_content, int size);


int 			   		deadpool_mount(struct custom_options options);
int 			   		deadpool_umount();

int 			   		deadpool_alloc_dentry(struct deadpool_inode * inode, struct deadpool_dentry * dentry);
int 			   		deadpool_drop_dentry(struct deadpool_inode * inode, struct deadpool_dentry * dentry);
int						deadpool_alloc_data();
struct deadpool_inode*  deadpool_alloc_inode(struct deadpool_dentry * dentry);
int 			   		deadpool_sync_inode(struct deadpool_inode * inode);
int 			   		deadpool_drop_inode(struct deadpool_inode * inode);
int 					deadpool_drop_data(struct deadpool_inode * inode);
struct deadpool_inode*  deadpool_read_inode(struct deadpool_dentry * dentry, int ino);
struct deadpool_dentry* deadpool_get_dentry(struct deadpool_inode * inode, int dir);

struct deadpool_dentry* deadpool_lookup(const char * path, int * is_find, int* is_root);

/******************************************************************************
* SECTION: deadpool_debug.c
*******************************************************************************/
void 			   		deadpool_dump_map();

#endif  /* _deadpool_H_ */