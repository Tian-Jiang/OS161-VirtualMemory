/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * File-related system call implementations.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>

/*
 * ==================================================
 * FILE TABLE IMPLEMENTATION
 * ==================================================
 */



/*** openfile functions ***/

/*
 * file_open
 * opens a file, places it in the filetable, sets RETFD to the file
 * descriptor. the pointer arguments must be kernel pointers.
 * NOTE -- the passed in filename must be a mutable string.
 */
int
file_open(char *filename, int flags, int mode, int *retfd)
{
	struct vnode *vn;
	struct openfile *file;
	int result;
	
	result = vfs_open(filename, flags, mode, &vn);
	if (result) {
		return result;
	}

	file = kmalloc(sizeof(struct openfile));
	if (file == NULL) {
		vfs_close(vn);
		return ENOMEM;
	}

	/* initialize the file struct */
	file->of_lock = lock_create("file lock");
	if (file->of_lock == NULL) {
		vfs_close(vn);
		kfree(file);
		return ENOMEM;
	}
	file->of_vnode = vn;
	file->of_offset = 0;
	file->of_accmode = flags & O_ACCMODE;
	file->of_refcount = 1;

	/* vfs_open checks for invalid access modes */
	KASSERT(file->of_accmode==O_RDONLY ||
		file->of_accmode==O_WRONLY ||
		file->of_accmode==O_RDWR);

	/* place the file in the filetable, getting the file descriptor */
	result = filetable_placefile(file, retfd);
	if (result) {
		lock_destroy(file->of_lock);
		kfree(file);
		vfs_close(vn);
		return result;
	}

	return 0;
}

/*
 * file_doclose
 * shared code for file_close and filetable_destroy
 */
static
int
file_doclose(struct openfile *file)
{
	lock_acquire(file->of_lock);

	/* if this is the last close of this file, free it up */
	if (file->of_refcount == 1) {
		vfs_close(file->of_vnode);
		lock_release(file->of_lock);
		lock_destroy(file->of_lock);
		kfree(file);
	}
	else {
		KASSERT(file->of_refcount > 1);
		file->of_refcount--;
		lock_release(file->of_lock);
	}

	return 0;
}

/* 
 * file_close
 * knock off the refcount, freeing the memory if it goes to 0.
 */
int
file_close(int fd)
{
	struct openfile *file;
	int result;

	/* find the file in the filetable */
	result = filetable_findfile(fd, &file);
	if (result) {
		return result;
	}

	result = file_doclose(file);
	if (result) {
		/* leave file open for possible retry */
		return result;
	}
	curthread->t_filetable->ft_openfiles[fd] = NULL;

	return 0;
}

/*** filetable functions ***/

/* 
 * filetable_init
 * pretty straightforward -- allocate the space, initialize to NULL.
 * note that the one careful thing is to open the std i/o in order to
 * get
 * stdin  == 0
 * stdout == 1
 * stderr == 2
 */
int
filetable_init(const char *inpath, const char *outpath, const char *errpath)
{
	/* the filenames come from the kernel; assume reasonable length */
	char path[32];
	int result;
	int fd;

	/* make sure we can fit these */
	KASSERT(strlen(inpath) < sizeof(path));
	KASSERT(strlen(outpath) < sizeof(path));
	KASSERT(strlen(errpath) < sizeof(path));
	
	/* catch memory leaks, repeated calls */
	KASSERT(curthread->t_filetable == NULL);

	curthread->t_filetable = kmalloc(sizeof(struct filetable));
	if (curthread->t_filetable == NULL) {
		return ENOMEM;
	}
	
	/* NULL-out the table */
	for (fd = 0; fd < OPEN_MAX; fd++) {
		curthread->t_filetable->ft_openfiles[fd] = NULL;
	}

	/*
	 * open the std fds.  note that the names must be copied into
	 * the path buffer so that they're mutable.
	 */
	strcpy(path, inpath);
	result = file_open(path, O_RDONLY, 0, &fd);
	if (result) {
		return result;
	}

	strcpy(path, outpath);
	result = file_open(path, O_WRONLY, 0, &fd);
	if (result) {
		return result;
	}

	strcpy(path, errpath);
	result = file_open(path, O_WRONLY, 0, &fd);
	if (result) {
		return result;
	}

	return 0;
}

/*
 * filetable_copy
 * again, pretty straightforward.  the subtle business here is that instead of
 * copying the openfile structure, we just increment the refcount.  this means
 * that openfile structs will, in fact, be shared between processes, as in
 * Unix.
 */
int
filetable_copy(struct filetable **copy)
{
	struct filetable *ft = curthread->t_filetable;
	int fd;

	/* waste of a call, really */
	if (ft == NULL) {
		*copy = NULL;
		return 0;
	}
	
	*copy = kmalloc(sizeof(struct filetable));
	
	if (*copy == NULL) {
		return ENOMEM;
	}

	/* copy over the entries */
	for (fd = 0; fd < OPEN_MAX; fd++) {
		if (ft->ft_openfiles[fd] != NULL) {
			lock_acquire(ft->ft_openfiles[fd]->of_lock);
			ft->ft_openfiles[fd]->of_refcount++;
			lock_release(ft->ft_openfiles[fd]->of_lock);
			(*copy)->ft_openfiles[fd] = ft->ft_openfiles[fd];
		} 
		else {
			(*copy)->ft_openfiles[fd] = NULL;
		}
	}

	return 0;
}

/*
 * filetable_destroy
 * closes the files in the file table, frees the table.
 */
void
filetable_destroy(struct filetable *ft)
{
	int fd, result;

	KASSERT(ft != NULL);

	for (fd = 0; fd < OPEN_MAX; fd++) {
		if (ft->ft_openfiles[fd]) {
			result = file_doclose(ft->ft_openfiles[fd]);
			KASSERT(result == 0);
		}
	}
	
	kfree(ft);
}	

/* 
 * filetable_placefile
 * finds the smallest available file descriptor, places the file at the point,
 * sets FD to it.
 */
int
filetable_placefile(struct openfile *file, int *fd)
{
	struct filetable *ft = curthread->t_filetable;
	int i;
	
	for (i = 0; i < OPEN_MAX; i++) {
		if (ft->ft_openfiles[i] == NULL) {
			ft->ft_openfiles[i] = file;
			*fd = i;
			return 0;
		}
	}

	return EMFILE;
}

/*
 * filetable_findfile
 * verifies that the file descriptor is valid and actually references an
 * open file, setting the FILE to the file at that index if it's there.
 */
int
filetable_findfile(int fd, struct openfile **file)
{
	struct filetable *ft = curthread->t_filetable;

	if (fd < 0 || fd >= OPEN_MAX) {
		return EBADF;
	}
	
	*file = ft->ft_openfiles[fd];
	if (*file == NULL) {
		return EBADF;
	}

	return 0;
}

/*
 * filetable_dup2file
 * verifies that both file descriptors are valid, and that the OLDFD is
 * actually an open file.  then, if the NEWFD is open, it closes it.
 * finally, it sets the filetable entry at newfd, and ups its refcount.
 */
int
filetable_dup2file(int oldfd, int newfd)
{
	struct filetable *ft = curthread->t_filetable;
	struct openfile *file;
	int result;

	if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX) {
		return EBADF;
	}

	file = ft->ft_openfiles[oldfd];
	if (file == NULL) {
		return EBADF;
	}

	/* dup2'ing an fd to itself automatically succeeds (BSD semantics) */
	if (oldfd == newfd) {
		return 0;
	}

	/* closes the newfd if it's open */
	if (ft->ft_openfiles[newfd] != NULL) {
		result = file_close(newfd);
		if (result) {
			return result;
		}
	}

	/* up the refcount */
	lock_acquire(file->of_lock);
	file->of_refcount++;
	lock_release(file->of_lock);

	/* doesn't need to be synchronized because it's just changing the ft */
	ft->ft_openfiles[newfd] = file;

	return 0;
}

/*
 * ==================================================
 * SYSCALL IMPLEMENTATION
 * ==================================================
 */

/*
 * mk_useruio
 * sets up the uio for a USERSPACE transfer. 
 */
static
void
mk_useruio(struct iovec *iov, struct uio *u,
	   userptr_t buf, size_t len, off_t offset, enum uio_rw rw)
{
	DEBUGASSERT(iov);
	DEBUGASSERT(u);

	iov->iov_ubase = buf;
	iov->iov_len = len;
	u->uio_iov = iov;
	u->uio_iovcnt = 1;
	u->uio_offset = offset;
	u->uio_resid = len;
	u->uio_segflg = UIO_USERSPACE;
	u->uio_rw = rw;
	u->uio_space = curthread->t_addrspace;
}

/*
 * sys_open
 * just copies in the filename, then passes work to file_open.
 */
int
sys_open(userptr_t filename, int flags, int mode, int *retval)
{
	char fname[PATH_MAX];
	int result;

	result = copyinstr(filename, fname, sizeof(fname), NULL);
	if (result) {
		return result;
	}

	return file_open(fname, flags, mode, retval);
}

/*
 * sys_read
 * translates the fd into its openfile, then calls VOP_READ.
 */
int
sys_read(int fd, userptr_t buf, size_t size, int *retval)
{
	struct iovec iov;
	struct uio useruio;
	struct openfile *file;
	int result;

	/* better be a valid file descriptor */
	result = filetable_findfile(fd, &file);
	if (result) {
		return result;
	}

	lock_acquire(file->of_lock);

	if (file->of_accmode == O_WRONLY) {
		lock_release(file->of_lock);
		return EBADF;
	}

	/* set up a uio with the buffer, its size, and the current offset */
	mk_useruio(&iov, &useruio, buf, size, file->of_offset, UIO_READ);

	/* does the read */
	result = VOP_READ(file->of_vnode, &useruio);
	if (result) {
		lock_release(file->of_lock);
		return result;
	}

	/* set the offset to the updated offset in the uio */
	file->of_offset = useruio.uio_offset;

	lock_release(file->of_lock);
	
	/*
	 * The amount read is the size of the buffer originally, minus
	 * how much is left in it.
	 */
	*retval = size - useruio.uio_resid;

	return 0;
}

/*
 * sys_write
 * translates the fd into its openfile, then calls VOP_WRITE.
 */
int
sys_write(int fd, userptr_t buf, size_t size, int *retval)
{
	struct iovec iov;
	struct uio useruio;
	struct openfile *file;
	int result;

	result = filetable_findfile(fd, &file);
	if (result) {
		return result;
	}

	lock_acquire(file->of_lock);

	if (file->of_accmode == O_RDONLY) {
		lock_release(file->of_lock);
		return EBADF;
	}

	/* set up a uio with the buffer, its size, and the current offset */
	mk_useruio(&iov, &useruio, buf, size, file->of_offset, UIO_WRITE);

	/* does the write */
	result = VOP_WRITE(file->of_vnode, &useruio);
	if (result) {
		lock_release(file->of_lock);
		return result;
	}

	/* set the offset to the updated offset in the uio */
	file->of_offset = useruio.uio_offset;

	lock_release(file->of_lock);

	/*
	 * the amount written is the size of the buffer originally,
	 * minus how much is left in it.
	 */
	*retval = size - useruio.uio_resid;

	return 0;
}

/* 
 * sys_close
 * just pass off the work to file_close.
 */
int
sys_close(int fd)
{
	return file_close(fd);
}

/*
 * sys_lseek
 * translates the fd into its openfile, then based on the type of seek,
 * figure out the new offset, try the seek, if that succeeds, update the
 * openfile.
 */
int
sys_lseek(int fd, off_t offset, int whence, off_t *retval)
{
	struct stat info;
	struct openfile *file;
	int result;

	result = filetable_findfile(fd, &file);
	if (result) {
		return result;
	}

	lock_acquire(file->of_lock);
	
	/* based on the type of seek, set the retval */ 
	switch (whence) {
	    case SEEK_SET:
		*retval = offset;
		break;
	    case SEEK_CUR:
		*retval = file->of_offset + offset;
		break;
	    case SEEK_END:
		result = VOP_STAT(file->of_vnode, &info);
		if (result) {
			lock_release(file->of_lock);
			return result;
		}
		*retval = info.st_size + offset;
		break;
	    default:
		lock_release(file->of_lock);
		return EINVAL;
	}

	/* try the seek -- if it fails, return */
	result = VOP_TRYSEEK(file->of_vnode, *retval);
	if (result) {
		lock_release(file->of_lock);
		return result;
	}
	
	/* success -- update the file structure */
	file->of_offset = *retval;

	lock_release(file->of_lock);

	return 0;
}

/* 
 * sys_dup2
 * just pass the work off to the filetable
 */
int
sys_dup2(int oldfd, int newfd, int *retval)
{
	int result;

	result = filetable_dup2file(oldfd, newfd);
	if (result) {
		return result;
	}

	*retval = newfd;
	return 0;
}

/* really not "file" calls, per se, but might as well put it here */

/*
 * sys_chdir
 * copyin the path and pass it off to vfs.
 */
int
sys_chdir(userptr_t path)
{
	char pathbuf[PATH_MAX];
	int result;
	
	result = copyinstr(path, pathbuf, PATH_MAX, NULL);
	if (result) {
		return result;
	}

	return vfs_chdir(pathbuf);
}

/*
 * sys___getcwd
 * just use vfs_getcwd.
 */
int
sys___getcwd(userptr_t buf, size_t buflen, int *retval)
{
	struct iovec iov;
	struct uio useruio;
	int result;

	mk_useruio(&iov, &useruio, buf, buflen, 0, UIO_READ);

	result = vfs_getcwd(&useruio);
	if (result) {
		return result;
	}

	*retval = buflen - useruio.uio_resid;

	return 0;
}
