#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>

/*
 * Add your file-related functions here ...
 */

// global table array in heap
oft_struct *oft;


int init_oft() {
    oft = (oft_struct *)kmalloc(sizeof(oft_struct) * OPEN_MAX);
    if (oft == NULL)
    {
        return ENOMEM;
    }
    for (int i = 0; i < OPEN_MAX; ++i)
    {
        oft[i].ref_counter = 0;
        oft[i].node = NULL;
        oft[i].curr_offset = 0;
        oft[i].flags = -1;
    }

    char con1[] = "con:";
    char con2[] = "con:";
    // stdout
    oft[1].flags = O_WRONLY;
    oft[1].ref_counter = 1;
    oft[1].curr_offset = 0;
    vfs_open(con1, O_WRONLY, 0, &oft[1].node);
    //stderr
    oft[2].flags = O_WRONLY;
    oft[2].ref_counter = 1;
    oft[2].curr_offset = 0;
    vfs_open(con2, O_WRONLY, 0, &oft[2].node);
    return 0;
}

int sys_open(userptr_t filename, int flags, mode_t mode, int32_t *retval) {
    // find the first free global index
    int oft_index;
    for (oft_index = 0; oft_index < OPEN_MAX; ++oft_index)
    {
        if (oft[oft_index].node == NULL)
        {
            break;
        }
    }
    if (oft_index == OPEN_MAX)
    {
        return ENFILE;
    }
    // find the first free current process index
    int curr_fd;
    for (curr_fd = 0; curr_fd < OPEN_MAX; ++curr_fd)
    {
        if (curproc->fd_table[curr_fd] == -1)
        {
            break;
        }
    }
    if (curr_fd == OPEN_MAX)
    {
        return EMFILE;
    }

    // invoke vfs_open
    int err = 0;
    char kfilename[__NAME_MAX];
    size_t kfilename_len;
    err = copyinstr(filename, kfilename, __NAME_MAX, &kfilename_len);
    if (err) {
        return err;
    }
    err = vfs_open(kfilename, flags, mode, &oft[oft_index].node);
    if (err) {
        // vfs_open returned an error
        return err;
    }

    // setup the global table data
    oft[oft_index].ref_counter = 1;
    oft[oft_index].curr_offset = 0;
    oft[oft_index].flags = flags;

    // setup the per process table
    curproc->fd_table[curr_fd] = oft_index;

    // assign retval
    *retval = curr_fd;

    return err;
}

int sys_close(int fd)
{
    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF;
    }
    int oft_index = curproc->fd_table[fd];
    if (oft_index < 0 || oft_index >= OPEN_MAX)
    {
        return EBADF;
    }

    curproc->fd_table[fd] = -1;
    if (oft[oft_index].ref_counter == 1)
    {
        oft[oft_index].ref_counter = 0;
        oft[oft_index].curr_offset = 0;
        oft[oft_index].flags = -1;
        vfs_close(oft[oft_index].node);
        oft[oft_index].node = NULL;
    }
    else if (oft[oft_index].ref_counter > 1)
    {
        oft[oft_index].ref_counter--;
    }

    return 0;
}

//TODO
int sys_read(int fd, void *buf, size_t bufflen, int32_t *retval)
{
     // validate fd and fp
    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF;
    }
    int oft_index = curproc->fd_table[fd];
    if (oft_index < 0 || oft_index >= OPEN_MAX || oft[oft_index].ref_counter <= 0)
    {
        return EBADF;
    }

    if (oft[oft_index].flags == O_WRONLY) {
        return EBADF;
    }
    struct iovec iov;
    struct uio u;
    uio_uinit(&iov, &u, (userptr_t) buf, bufflen, oft[oft_index].curr_offset, UIO_READ);

    int err = VOP_READ(oft[oft_index].node, &u);

    if (err)
    {
        return err;
    }

    // update the offset
    oft[oft_index].curr_offset = u.uio_offset;
    *retval = bufflen - u.uio_resid;
    return 0;
}

//TODO
int sys_write(int fd, void *buf, size_t nbytes, int32_t *retval)
{
    // validate fd and fp
    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF;
    }
    int oft_index = curproc->fd_table[fd];
    if (oft_index < 0 || oft_index >= OPEN_MAX || oft[oft_index].ref_counter <= 0)
    {
        return EBADF;
    }

    if (oft[oft_index].flags == O_RDONLY) {
        return EBADF;
    }
    struct iovec iov;
    struct uio u;

    uio_uinit(&iov, &u, buf, nbytes, oft[oft_index].curr_offset, UIO_WRITE);

    int err = VOP_WRITE(oft[oft_index].node, &u);

    if (err)
    {
        return err;
    }

    *retval = u.uio_offset - oft[oft_index].curr_offset;
    oft[oft_index].curr_offset = u.uio_offset;
    return 0;
}

int sys_dup2(int oldfd, int newfd, int32_t *retval)
{
    *retval = -1;
    if (oldfd < 0 || oldfd >= OPEN_MAX)
    {
        return EBADF;
    }
    if (newfd < 0 || newfd >= OPEN_MAX)
    {
        return EBADF;
    }

    int oldfd_oft = curproc->fd_table[oldfd];
    int newfd_oft = curproc->fd_table[newfd];

    if (oldfd_oft < 0 || oldfd_oft >= OPEN_MAX)
    {
        return EBADF;
    }
    if (oft[oldfd_oft].node == NULL)
    {
        return EBADF;
    }

    // take no action when dup2 clones a file handle onto itself
    if (oldfd == newfd){
        return 0;
    }
    
    // if the newfd is an open fd, close it first
    if (newfd_oft >= 0 && newfd_oft < OPEN_MAX)
    {
        if (oft[newfd_oft].node != NULL)
        {
            if (sys_close(newfd))
            {
                return EBADF;
            }
        }
    }
    curproc->fd_table[newfd] = oldfd_oft;
    oft[oldfd_oft].ref_counter++;
    *retval = newfd;
    return 0;
}

//TODO
int sys_lseek(int fd, off_t pos, int whence, int64_t *retval)
{
    // validate fd and fp
    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF;
    }
    int oft_index = curproc->fd_table[fd];
    if (oft_index < 0 || oft_index >= OPEN_MAX || oft[oft_index].ref_counter <= 0)
    {
        return EBADF;
    }
    
    struct stat statbuf;
    // check if the file is seekable
    bool seekable = oft[oft_index].node->vn_ops->vop_isseekable(oft[oft_index].node);
    if (!seekable)
    {
        return ESPIPE;
    }
    switch (whence)
    {
    case SEEK_CUR:
        oft[oft_index].curr_offset += pos;
        break;
    case SEEK_SET:
        oft[oft_index].curr_offset = pos;
        break;
    case SEEK_END:
        VOP_STAT(oft[oft_index].node, &statbuf);
        off_t file_size = statbuf.st_size;

        oft[oft_index].curr_offset = file_size + pos;
        break;
    default:
        return EINVAL;
        break;
    }

    *retval = oft[oft_index].curr_offset;
    return 0;
}
