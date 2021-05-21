/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>

// extra includes
#include <types.h>
/*
 * Put your function declarations and data types here ...
 */

typedef struct open_file_table_struct {
    int ref_counter;
    int flags;
    off_t curr_offset;
    struct vnode *node;
} oft_struct;


int init_oft(void);

int sys_open(userptr_t filename, int flags, mode_t mode, int32_t *retval);

int sys_close(int fd);

int sys_read(int fd, void *buf, size_t bufflen, int32_t *retval);

int sys_write(int fd, void *buf, size_t nbytes, int32_t *retval);

int sys_dup2(int oldfd, int newfd, int32_t *retval);

int sys_lseek(int fd, off_t pos, int whence, int64_t *retval);

#define OPEN_MAX __OPEN_MAX
#endif /* _FILE_H_ */
