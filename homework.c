/*
 * file: homework.c
 * description: skeleton file for CS 5600 system
 *
 * CS 5600, Computer Systems, Northeastern
 */

 #define FUSE_USE_VERSION 27
 #define _FILE_OFFSET_BITS 64
 #define MAX_PATH_COMPONENTS 10
 #define MAX_NAME_LEN 27
 
 #include <stdlib.h>
 #include <stddef.h>
 #include <unistd.h>
 #include <fuse.h>
 #include <fcntl.h>
 #include <string.h>
 #include <stdio.h>
 #include <errno.h>
 #include <assert.h>
 #include <time.h>
 #include <libgen.h>
 #include <sys/stat.h>
 #include <sys/statvfs.h>
 #include <utime.h>
 
 #include "fs5600.h"

 extern int block_read(void *buf, int lba, int nblks);
 extern int block_write(void *buf, int lba, int nblks);
 
 static char superblock[FS_BLOCK_SIZE];
 static unsigned char bitmap[FS_BLOCK_SIZE];  
 
 static int translate(const char *path);
 static int lookup_parent(const char *path, int *parent_inum, char **leaf);
 static int allocate_block(void);
 static int free_block(int block_num);
 
 /* A helper for caching the superblock and bitmap */ 
 static int load_fs_metadata() {
     if (block_read(superblock, 0, 1) < 0)
         return -EIO;
     if (block_read((char*)bitmap, 1, 1) < 0)
         return -EIO;
     return 0;
 }
 
 /* Helper function to read an inode given its block number (inode number) */ 
 static int read_inode(int inum, struct fs_inode *inode) {
     if (inum < 0)
         return -ENOENT;
     if (block_read((char *)inode, inum, 1) < 0)
         return -EIO;
     return 0;
 }
 
 /* Helper to write back a modified inode to disk */
 static int write_inode(int inum, struct fs_inode *inode) {
     if (inum < 0)
         return -ENOENT;
     if (block_write((char *)inode, inum, 1) < 0)
         return -EIO;
     return 0;
 }
 
 /* if you don't understand why you can't use these system calls here, 
  * you need to read the assignment description another time
  */
 #define stat(a,b) error do not use stat()
 #define open(a,b) error do not use open()
 #define read(a,b,c) error do not use read()
 #define write(a,b,c) error do not use write()
 
 /* disk access. All access is in terms of 4KB blocks; read and
  * write functions return 0 (success) or -EIO.
  */
 
 /* bitmap functions*/
 void bit_set(unsigned char *map, int i)
 {
     map[i/8] |= (1 << (i%8));
 }
 void bit_clear(unsigned char *map, int i)
 {
     map[i/8] &= ~(1 << (i%8));
 }
 int bit_test(unsigned char *map, int i)
 {
     return map[i/8] & (1 << (i%8));
 }
 
 
 /* init - this is called once by the FUSE framework at startup. Ignore
  * the 'conn' argument.
  * recommended actions:
  *   - read superblock
  *   - allocate memory, read bitmaps and inodes
  */
 void* fs_init(struct fuse_conn_info *conn)
 {
     if (load_fs_metadata() < 0) {
         fprintf(stderr, "Failed to load file system metadata\n");
         exit(1);
     }
     return NULL;
 }
 
 /* Note on path translation errors:
  * In addition to the method-specific errors listed below, almost
  * every method can return one of the following errors if it fails to
  * locate a file or directory corresponding to a specified path.
  *
  * ENOENT - a component of the path doesn't exist.
  * ENOTDIR - an intermediate component of the path (e.g. 'b' in
  *           /a/b/c) is not a directory
  */
 
 /* note on splitting the 'path' variable:
  * the value passed in by the FUSE framework is declared as 'const',
  * which means you can't modify it. The standard mechanisms for
  * splitting strings in C (strtok, strsep) modify the string in place,
  * so you have to copy the string and then free the copy when you're
  * done. One way of doing this:
  *
  *    char *_path = strdup(path);
  *    int inum = translate(_path);
  *    free(_path);
  */
 
 
 
 /* getattr - get file or directory attributes. For a description of
  *  the fields in 'struct stat', see 'man lstat'.
  *
  * Note - for several fields in 'struct stat' there is no corresponding
  *  information in our file system:
  *    st_nlink - always set it to 1
  *    st_atime, st_ctime - set to same value as st_mtime
  *
  * success - return 0
  * errors - path translation, ENOENT
  * hint - factor out inode-to-struct stat conversion - you'll use it
  *        again in readdir
  */
 int fs_getattr(const char *path, struct stat *sb)
 {
     int inum = translate(path);
     if (inum < 0)
         return inum;
     
     struct fs_inode inode;
     if (read_inode(inum, &inode) < 0)
         return -EIO;
     
     memset(sb, 0, sizeof(struct stat));
     sb->st_mode = inode.mode;
     sb->st_uid = inode.uid;
     sb->st_gid = inode.gid;
     sb->st_size = inode.size;
     sb->st_nlink = 1;
     sb->st_mtime = inode.mtime;
     sb->st_atime = inode.mtime;
     sb->st_ctime = inode.mtime;
     sb->st_blocks = (inode.size + FS_BLOCK_SIZE) / FS_BLOCK_SIZE;
     return 0;
 }
 
 /* readdir - get directory contents.
  *
  * call the 'filler' function once for each valid entry in the 
  * directory, as follows:
  *     filler(buf, <name>, <statbuf>, 0)
  * where <statbuf> is a pointer to a struct stat
  * success - return 0
  * errors - path resolution, ENOTDIR, ENOENT
  * 
  * hint - check the testing instructions if you don't understand how
  *        to call the filler function
  */
 int fs_readdir(const char *path, void *ptr, fuse_fill_dir_t filler,
                off_t offset, struct fuse_file_info *fi)
 {
     int inum = translate(path);
     if (inum < 0)
         return inum;
     
     struct fs_inode inode;
     if (read_inode(inum, &inode) < 0)
         return -EIO;
     
     if ((inode.mode & S_IFMT) != S_IFDIR)
         return -ENOTDIR;
     
     char dir_block[FS_BLOCK_SIZE];
     if (block_read(dir_block, inode.ptrs[0], 1) < 0)
         return -EIO;
     
     for (int i = 0; i < 128; i++) {
         struct fs_dirent *d = (struct fs_dirent *)(dir_block + i * 32);
         if (d->valid) {
             struct stat st;
             memset(&st, 0, sizeof(st));
             struct fs_inode child_inode;
             if (read_inode(d->inode, &child_inode) < 0)
                 continue;
             st.st_mode = child_inode.mode;
             st.st_size = child_inode.size;
             st.st_uid = child_inode.uid;
             st.st_gid = child_inode.gid;
             st.st_mtime = child_inode.mtime;
             st.st_atime = child_inode.mtime;
             st.st_ctime = child_inode.mtime;
             st.st_nlink = 1;
             if (filler(ptr, d->name, &st, 0) != 0)
                 break;
         }
     }
     return 0;
 }
 
 /* create - create a new file with specified permissions
  *
  * success - return 0
  * errors - path resolution, EEXIST
  *          in particular, for create("/a/b/c") to succeed,
  *          "/a/b" must exist, and "/a/b/c" must not.
  *
  * Note that 'mode' will already have the S_IFREG bit set, so you can
  * just use it directly. Ignore the third parameter.
  *
  * If a file or directory of this name already exists, return -EEXIST.
  * If there are already 128 entries in the directory (i.e. it's filled an
  * entire block), you are free to return -ENOSPC instead of expanding it.
  */
 int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
 {
     int parent_inum;
     char *leaf;
     int ret = lookup_parent(path, &parent_inum, &leaf);
     if (ret < 0)
         return ret;
     
     struct fs_inode parent_inode;
     if (read_inode(parent_inum, &parent_inode) < 0) {
         free(leaf);
         return -EIO;
     }
     if ((parent_inode.mode & S_IFMT) != S_IFDIR) {
         free(leaf);
         return -ENOTDIR;
     }
     
     char dir_block[FS_BLOCK_SIZE];
     if (block_read(dir_block, parent_inode.ptrs[0], 1) < 0) {
         free(leaf);
         return -EIO;
     }
     
     for (int i = 0; i < 128; i++) {
         struct fs_dirent *d = (struct fs_dirent *)(dir_block + i*32);
         if (d->valid && strcmp(d->name, leaf) == 0) {
             free(leaf);
             return -EEXIST;
         }
     }
     
     int new_inum = allocate_block();
     if (new_inum < 0) {
         free(leaf);
         return new_inum;
     }
     
     struct fs_inode new_inode;
     new_inode.uid = fuse_get_context()->uid;
     new_inode.gid = fuse_get_context()->gid;
     new_inode.mode = mode;  
     new_inode.ctime = time(NULL);
     new_inode.mtime = new_inode.ctime;
     new_inode.size = 0;
     memset(new_inode.ptrs, 0, sizeof(new_inode.ptrs));
     
     if (write_inode(new_inum, &new_inode) < 0) {
         free(leaf);
         return -EIO;
     }
     
     int added = 0;
     for (int i = 0; i < 128; i++) {
         struct fs_dirent *d = (struct fs_dirent *)(dir_block + i*32);
         if (!d->valid) {
             d->valid = 1;
             d->inode = new_inum;
             strncpy(d->name, leaf, MAX_NAME_LEN);
             d->name[MAX_NAME_LEN] = '\0';
             added = 1;
             break;
         }
     }
     free(leaf);
     if (!added)
         return -ENOSPC;
     
     if (block_write(dir_block, parent_inode.ptrs[0], 1) < 0)
         return -EIO;
     
     return 0;
 }
 
 /* mkdir - create a directory with the given mode.
  *
  * WARNING: unlike fs_create, @mode only has the permission bits. You
  * have to OR it with S_IFDIR before setting the inode 'mode' field.
  *
  * success - return 0
  * Errors - path resolution, EEXIST
  * Conditions for EEXIST are the same as for create. 
  */ 
 int fs_mkdir(const char *path, mode_t mode)
 {
     int parent_inum;
     char *leaf;
     int ret = lookup_parent(path, &parent_inum, &leaf);
     if (ret < 0)
         return ret;
     
     struct fs_inode parent_inode;
     if (read_inode(parent_inum, &parent_inode) < 0) {
         free(leaf);
         return -EIO;
     }
     if ((parent_inode.mode & S_IFMT) != S_IFDIR) {
         free(leaf);
         return -ENOTDIR;
     }
     
     char dir_block[FS_BLOCK_SIZE];
     if (block_read(dir_block, parent_inode.ptrs[0], 1) < 0) {
         free(leaf);
         return -EIO;
     }
     
     for (int i = 0; i < 128; i++) {
         struct fs_dirent *d = (struct fs_dirent *)(dir_block + i*32);
         if (d->valid && strcmp(d->name, leaf) == 0) {
             free(leaf);
             return -EEXIST;
         }
     }
     
     int new_inum = allocate_block();
     if (new_inum < 0) {
         free(leaf);
         return new_inum;
     }
     struct fs_inode new_inode;
     new_inode.uid = fuse_get_context()->uid;
     new_inode.gid = fuse_get_context()->gid;
     new_inode.mode = S_IFDIR | mode;  
     new_inode.ctime = time(NULL);
     new_inode.mtime = new_inode.ctime;
     new_inode.size = FS_BLOCK_SIZE;
     memset(new_inode.ptrs, 0, sizeof(new_inode.ptrs));
     
     int dblk = allocate_block();
     if (dblk < 0) {
         free(leaf);
         return dblk;
     }
     new_inode.ptrs[0] = dblk;
     
     char empty_block[FS_BLOCK_SIZE];
     memset(empty_block, 0, FS_BLOCK_SIZE);
     if (block_write(empty_block, dblk, 1) < 0)
         return -EIO;
     
     if (write_inode(new_inum, &new_inode) < 0)
         return -EIO;
     
     int added = 0;
     for (int i = 0; i < 128; i++) {
         struct fs_dirent *d = (struct fs_dirent *)(dir_block + i*32);
         if (!d->valid) {
             d->valid = 1;
             d->inode = new_inum;
             strncpy(d->name, leaf, MAX_NAME_LEN);
             d->name[MAX_NAME_LEN] = '\0';
             added = 1;
             break;
         }
     }
     free(leaf);
     if (!added)
         return -ENOSPC;
     
     if (block_write(dir_block, parent_inode.ptrs[0], 1) < 0)
         return -EIO;
     
     return 0;
 }
 
 
 /* unlink - delete a file
  *  success - return 0
  *  errors - path resolution, ENOENT, EISDIR
  */
 int fs_unlink(const char *path)
 {
     int parent_inum;
     char *leaf;
     int ret = lookup_parent(path, &parent_inum, &leaf);
     if (ret < 0)
         return ret;
     
     struct fs_inode parent_inode;
     if (read_inode(parent_inum, &parent_inode) < 0) {
         free(leaf);
         return -EIO;
     }

     if ((parent_inode.mode & S_IFMT) != S_IFDIR) {
        free(leaf);
        return -ENOTDIR;
    }
     
     char dir_block[FS_BLOCK_SIZE];
     if (block_read(dir_block, parent_inode.ptrs[0], 1) < 0) {
         free(leaf);
         return -EIO;
     }
     
     int entry_index = -1;
     for (int i = 0; i < 128; i++) {
         struct fs_dirent *d = (struct fs_dirent *)(dir_block + i*32);
         if (d->valid && strcmp(d->name, leaf) == 0) {
             entry_index = i;
             break;
         }
     }
     if (entry_index == -1) {
         free(leaf);
         return -ENOENT;
     }
     
     struct fs_inode file_inode;
     struct fs_dirent *entry = (struct fs_dirent *)(dir_block + entry_index*32);
     if (read_inode(entry->inode, &file_inode) < 0) {
         free(leaf);
         return -EIO;
     }
     if ((file_inode.mode & S_IFMT) == S_IFDIR) {
         free(leaf);
         return -EISDIR;
     }
     
     ((struct fs_dirent *)(dir_block + entry_index*32))->valid = 0;
     if (block_write(dir_block, parent_inode.ptrs[0], 1) < 0) {
         free(leaf);
         return -EIO;
     }
     
     free_block(entry->inode);
     int nblocks = (file_inode.size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
     for (int i = 0; i < nblocks; i++) {
         free_block(file_inode.ptrs[i]);
     }
     free(leaf);
     return 0;
 }
 
 /* rmdir - remove a directory
  *  success - return 0
  *  Errors - path resolution, ENOENT, ENOTDIR, ENOTEMPTY
  */
 int fs_rmdir(const char *path)
 {
     int parent_inum;
     char *leaf;
     int ret = lookup_parent(path, &parent_inum, &leaf);
     if (ret < 0)
         return ret;
     
     struct fs_inode parent_inode;
     if (read_inode(parent_inum, &parent_inode) < 0) {
         free(leaf);
         return -EIO;
     }
     
     if ((parent_inode.mode & S_IFMT) != S_IFDIR) {
        free(leaf);
        return -ENOTDIR;
    }

     char dir_block[FS_BLOCK_SIZE];
     if (block_read(dir_block, parent_inode.ptrs[0], 1) < 0) {
         free(leaf);
         return -EIO;
     }
     
     int entry_index = -1;
     for (int i = 0; i < 128; i++) {
         struct fs_dirent *d = (struct fs_dirent *)(dir_block + i*32);
         if (d->valid && strcmp(d->name, leaf) == 0) {
             entry_index = i;
             break;
         }
     }
     if (entry_index == -1) {
         free(leaf);
         return -ENOENT;
     }
     
     struct fs_inode dir_inode;
     struct fs_dirent *entry = (struct fs_dirent *)(dir_block + entry_index*32);
     if (read_inode(entry->inode, &dir_inode) < 0) {
         free(leaf);
         return -EIO;
     }
     if ((dir_inode.mode & S_IFMT) != S_IFDIR) {
         free(leaf);
         return -ENOTDIR;
     }
     
     char dblock[FS_BLOCK_SIZE];
     if (block_read(dblock, dir_inode.ptrs[0], 1) < 0) {
         free(leaf);
         return -EIO;
     }
     for (int i = 0; i < 128; i++) {
         struct fs_dirent *d = (struct fs_dirent *)(dblock + i*32);
         if (d->valid) {
             free(leaf);
             return -ENOTEMPTY;
         }
     }
     
     ((struct fs_dirent *)(dir_block + entry_index*32))->valid = 0;
     if (block_write(dir_block, parent_inode.ptrs[0], 1) < 0) {
         free(leaf);
         return -EIO;
     }
     
     free_block(entry->inode);
     free_block(dir_inode.ptrs[0]);
     free(leaf);
     return 0;
 }
 
 /* rename - rename a file or directory
  * success - return 0
  * Errors - path resolution, ENOENT, EINVAL, EEXIST
  *
  * ENOENT - source does not exist
  * EEXIST - destination already exists
  * EINVAL - source and destination are not in the same directory
  *
  * Note that this is a simplified version of the UNIX rename
  * functionality - see 'man 2 rename' for full semantics. In
  * particular, the full version can move across directories, replace a
  * destination file, and replace an empty directory with a full one.
  */
 int fs_rename(const char *src_path, const char *dst_path)
 {
     int src_parent;
     char *src_leaf;
     int ret = lookup_parent(src_path, &src_parent, &src_leaf);
     if (ret < 0)
         return ret;
     
     int dst_parent;
     char *dst_leaf;
     ret = lookup_parent(dst_path, &dst_parent, &dst_leaf);
     if (ret < 0) {
         free(src_leaf);
         return ret;
     }
     if (src_parent != dst_parent) {
         free(src_leaf);
         free(dst_leaf);
         return -EINVAL;
     }
     
     struct fs_inode parent_inode;
     if (read_inode(src_parent, &parent_inode) < 0) {
         free(src_leaf);
         free(dst_leaf);
         return -EIO;
     }
     
     char dir_block[FS_BLOCK_SIZE];
     if (block_read(dir_block, parent_inode.ptrs[0], 1) < 0) {
         free(src_leaf);
         free(dst_leaf);
         return -EIO;
     }
     
     int found_src = 0;
     for (int i = 0; i < 128; i++) {
         struct fs_dirent *d = (struct fs_dirent *)(dir_block + i*32);
         if (d->valid) {
             if (strcmp(d->name, src_leaf) == 0) {
                 found_src = 1;
             }
             if (strcmp(d->name, dst_leaf) == 0) {
                 free(src_leaf);
                 free(dst_leaf);
                 return -EEXIST;
             }
         }
     }
     if (!found_src) {
         free(src_leaf);
         free(dst_leaf);
         return -ENOENT;
     }
     
     for (int i = 0; i < 128; i++) {
         struct fs_dirent *d = (struct fs_dirent *)(dir_block + i*32);
         if (d->valid && strcmp(d->name, src_leaf) == 0) {
             strncpy(d->name, dst_leaf, MAX_NAME_LEN);
             d->name[MAX_NAME_LEN] = '\0';
             break;
         }
     }
     if (block_write(dir_block, parent_inode.ptrs[0], 1) < 0) {
         free(src_leaf);
         free(dst_leaf);
         return -EIO;
     }
     free(src_leaf);
     free(dst_leaf);
     return 0;
 }
 
 /* chmod - change file permissions
  * utime - change access and modification times
  *         (for definition of 'struct utimebuf', see 'man utime')
  *
  * success - return 0
  * Errors - path resolution, ENOENT.
  */
 int fs_chmod(const char *path, mode_t mode)
 {
     int inum = translate(path);
     if (inum < 0)
         return inum;
     
     struct fs_inode inode;
     if (read_inode(inum, &inode) < 0)
         return -EIO;
     
     inode.mode = (inode.mode & S_IFMT) | (mode & ~S_IFMT);
     
     if (write_inode(inum, &inode) < 0)
         return -EIO;
     
     return 0;
 }
 
 int fs_utime(const char *path, struct utimbuf *ut)
 {
     int inum = translate(path);
     if (inum < 0)
         return inum;
     
     struct fs_inode inode;
     if (read_inode(inum, &inode) < 0)
         return -EIO;
     
     inode.mtime = ut->modtime;
     inode.ctime = ut->modtime;
     
     if (write_inode(inum, &inode) < 0)
         return -EIO;
     return 0;
 }
 
 /* truncate - truncate file to exactly 'len' bytes
  * success - return 0
  * Errors - path resolution, ENOENT, EISDIR, EINVAL
  *    return EINVAL if len > 0.
  */
 int fs_truncate(const char *path, off_t len)
 {
     if (len != 0)
         return -EINVAL;
     int inum = translate(path);
     if (inum < 0)
         return inum;
     
     struct fs_inode inode;
     if (read_inode(inum, &inode) < 0)
         return -EIO;
     
     if ((inode.mode & S_IFMT) != S_IFREG)
         return -EISDIR;
     
     int nblocks = (inode.size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
     for (int i = 0; i < nblocks; i++) {
         free_block(inode.ptrs[i]);
         inode.ptrs[i] = 0;
     }
     inode.size = 0;
     inode.mtime = time(NULL);
     if (write_inode(inum, &inode) < 0)
         return -EIO;
     return 0;
 }
 
 
 /* read - read data from an open file.
  * success: should return exactly the number of bytes requested, except:
  *   - if offset >= file len, return 0
  *   - if offset+len > file len, return #bytes from offset to end
  *   - on error, return <0
  * Errors - path resolution, ENOENT, EISDIR
  */
 int fs_read(const char *path, char *buf, size_t len, off_t offset, struct fuse_file_info *fi)
 {
     int inum = translate(path);
     if (inum < 0)
         return inum;
     
     struct fs_inode inode;
     if (read_inode(inum, &inode) < 0)
         return -EIO;
     
     if ((inode.mode & S_IFMT) != S_IFREG)
         return -EISDIR;
     
     if (offset >= inode.size)
         return 0;
     
     if (offset + len > inode.size)
         len = inode.size - offset;
     
     int bytes_read = 0;
     int start_block = offset / FS_BLOCK_SIZE;
     int block_offset = offset % FS_BLOCK_SIZE;
     
     while (len > 0) {
         char block_data[FS_BLOCK_SIZE];
         int block_num = inode.ptrs[start_block];
         if (block_read(block_data, block_num, 1) < 0)
             return -EIO;
         int bytes_to_copy = FS_BLOCK_SIZE - block_offset;
         if (bytes_to_copy > len)
             bytes_to_copy = len;
         memcpy(buf + bytes_read, block_data + block_offset, bytes_to_copy);
         bytes_read += bytes_to_copy;
         len -= bytes_to_copy;
         block_offset = 0;
         start_block++;
     }
     return bytes_read;
 }
 
 /* write - write data to a file
  * success - return number of bytes written. (this will be the same as
  *           the number requested, or else it's an error)
  * Errors - path resolution, ENOENT, EISDIR
  *  return EINVAL if 'offset' is greater than current file length.
  *  (POSIX semantics support the creation of files with "holes" in them, 
  *   but we don't)
  */
 int fs_write(const char *path, const char *buf, size_t len, off_t offset, struct fuse_file_info *fi)
 {
     int inum = translate(path);
     if (inum < 0)
         return inum;
     
     struct fs_inode inode;
     if (read_inode(inum, &inode) < 0)
         return -EIO;
     
     if ((inode.mode & S_IFMT) != S_IFREG)
         return -EISDIR;
     
     if (offset > inode.size)
         return -EINVAL;  
     
     size_t end_offset = offset + len;
     int cur_blocks = (inode.size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
     int required_blocks = (end_offset + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
     for (int i = cur_blocks; i < required_blocks; i++) {
         int new_blk = allocate_block();
         if (new_blk < 0)
             return new_blk;
         inode.ptrs[i] = new_blk;
     }
     
     int bytes_written = 0;
     int start_block = offset / FS_BLOCK_SIZE;
     int block_offset = offset % FS_BLOCK_SIZE;
     size_t remaining = len;
     
     while (remaining > 0) {
         char block_data[FS_BLOCK_SIZE];
         int blk = inode.ptrs[start_block];
         if (block_read(block_data, blk, 1) < 0)
             return -EIO;
         
         int can_write = FS_BLOCK_SIZE - block_offset;
         if (can_write > remaining)
             can_write = remaining;
         memcpy(block_data + block_offset, buf + bytes_written, can_write);
         
         if (block_write(block_data, blk, 1) < 0)
             return -EIO;
         
         bytes_written += can_write;
         remaining -= can_write;
         block_offset = 0;
         start_block++;
     }
     if (end_offset > inode.size)
         inode.size = end_offset;
     inode.mtime = time(NULL);
     if (write_inode(inum, &inode) < 0)
         return -EIO;
     return bytes_written;
 }
 
 /* statfs - get file system statistics
  * see 'man 2 statfs' for description of 'struct statvfs'.
  * Errors - none. Needs to work.
  */
 int fs_statfs(const char *path, struct statvfs *st)
 {
     struct fs_super *sb_ptr = (struct fs_super *)superblock;
     int total = sb_ptr->disk_size; 
     int used = 0;
     for (int i = 0; i < sb_ptr->disk_size; i++) {
         if (bit_test(bitmap, i))
             used++;
     }
     int free_blocks = sb_ptr->disk_size - used;
     st->f_bsize = FS_BLOCK_SIZE;
     st->f_blocks = total;
     st->f_bfree = free_blocks;
     st->f_bavail = free_blocks;
     st->f_namemax = MAX_NAME_LEN;
     st->f_frsize = 0;
     st->f_files = 0;
     st->f_ffree = 0;
     st->f_favail = 0;
     return 0;
 }
 
 /* operations vector. Please don't rename it, or else you'll break things
  */
 struct fuse_operations fs_ops = {
     .init = fs_init,            /* read-mostly operations */
     .getattr = fs_getattr,
     .readdir = fs_readdir,
     .rename = fs_rename,
     .chmod = fs_chmod,
     .read = fs_read,
     .statfs = fs_statfs,
 
     .create = fs_create,        /* write operations */
     .mkdir = fs_mkdir,
     .unlink = fs_unlink,
     .rmdir = fs_rmdir,
     .utime = fs_utime,
     .truncate = fs_truncate,
     .write = fs_write,
 };
 
 /* translate splits a path into components from the root to get the inode number. Returns inode number or a negative error. */
 static int translate(const char *path) {
     if (strcmp(path, "/") == 0)
         return 2;  
 
     char *path_copy = strdup(path);
     if (!path_copy)
         return -ENOMEM;
 
     char *components[MAX_PATH_COMPONENTS];
     int count = 0;
     char *token = strtok(path_copy, "/");
     while (token != NULL && count < MAX_PATH_COMPONENTS) {
         if (strlen(token) > MAX_NAME_LEN)
             token[MAX_NAME_LEN] = '\0';
         components[count++] = token;
         token = strtok(NULL, "/");
     }
     
     int cur_inum = 2;  
     struct fs_inode inode;
     int ret;
     for (int i = 0; i < count; i++) {
         if ((ret = read_inode(cur_inum, &inode)) < 0) {
             free(path_copy);
             return ret;
         }
         if ((inode.mode & S_IFMT) != S_IFDIR) {
             free(path_copy);
             return -ENOTDIR;
         }
         char dir_block[FS_BLOCK_SIZE];
         if (block_read(dir_block, inode.ptrs[0], 1) < 0) {
             free(path_copy);
             return -EIO;
         }
         int found = 0;
         for (int j = 0; j < 128; j++) {
             struct fs_dirent *d = (struct fs_dirent *)(dir_block + j * 32);
             if (d->valid && strcmp(d->name, components[i]) == 0) {
                 cur_inum = d->inode;
                 found = 1;
                 break;
             }
         }
         if (!found) {
             free(path_copy);
             return -ENOENT;
         }
     }
     free(path_copy);
     return cur_inum;
 }
 
 /* lookup_parent return the inode number of "/a/b" in *parent_inum and a newly allocated string for the leaf name ("c") in *leaf */
 static int lookup_parent(const char *path, int *parent_inum, char **leaf) {
     if (strcmp(path, "/") == 0)
         return -EINVAL; 
     
     char *dup = strdup(path);
     if (!dup)
         return -ENOMEM;
     
     int len = strlen(dup);
     if (dup[len - 1] == '/')
         dup[len - 1] = '\0';
     
     char *dir_copy = strdup(dup);
     if (!dir_copy) {
         free(dup);
         return -ENOMEM;
     }
     char *base = basename(dup);
     if (strlen(base) > MAX_NAME_LEN)
         base[MAX_NAME_LEN] = '\0';
     *leaf = strdup(base);
     char *parent_path = dirname(dir_copy);
     int ret = translate(parent_path);
     *parent_inum = ret;
     
     free(dup);
     free(dir_copy);
     return (ret < 0) ? ret : 0;
 }
 
 /* allocate_block allocates a free block. Blocks 0 and 1 are reserved. */
 static int allocate_block(void) {
     struct fs_super *sb_ptr = (struct fs_super *)superblock;
     for (int i = 2; i < sb_ptr->disk_size; i++) {
         if (!bit_test(bitmap, i)) {
             bit_set(bitmap, i);
             if (block_write((char*)bitmap, 1, 1) < 0)
                 return -EIO;
             return i;
         }
     }
     return -ENOSPC;
 }
 
 /* free_block frees a block and update the bitmap */
 static int free_block(int block_num) {
     bit_clear(bitmap, block_num);
     if (block_write((char*)bitmap, 1, 1) < 0)
         return -EIO;
     return 0;
 }