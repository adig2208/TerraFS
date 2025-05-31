/*
 * unittest-2.c
 * Description: Unit tests (libcheck) for write operations of the file system.
 */

 #define _FILE_OFFSET_BITS 64
 #define FUSE_USE_VERSION 26
 
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <check.h>
 #include <errno.h>
 #include <sys/stat.h>
 #include <utime.h>
 #include <fuse.h>
 #include <zlib.h>
 #include "fs5600.h"
 

 /* Mock fuse context to set uid and gid. */
 struct fuse_context ctx = { .uid = 500, .gid = 500 };
 struct fuse_context *fuse_get_context(void) {
     return &ctx;
 }
 
 extern struct fuse_operations fs_ops;
 extern void block_init(char *file);

void test_setup(void) {
   system("python gen-disk.py -q disk2.in test2.img");
   block_init("test2.img");
   fs_ops.init(NULL);
}

void test_teardown(void) {}

//Helper to generate pattern into buffer
void generate_pattern(char *buf, size_t len, int start)
{
    char *ptr = buf;
    int i = start;
    while ((ptr - buf) + 10 < len)
        ptr += sprintf(ptr, "%d ", i++);
}

/* Helper to compute ceiling */
int compute_block_size(int size)
{
    return (size + 4095) / 4096;
}
 
 /* Test for fs_create - create new file "/newfile" */
 START_TEST(test_create_file) {
     int ret = fs_ops.create("/newfile", 0100666, NULL);
     ck_assert_int_eq(ret, 0);
     
     struct stat st;
     
     ret = fs_ops.getattr("/newfile", &st);
     ck_assert_int_eq(ret, 0);
     ck_assert(S_ISREG(st.st_mode));
     ck_assert_int_eq(st.st_size, 0);
 }
 END_TEST


/* Test for fs_create, create multiple files in root */
START_TEST(test_fs_create_root)
{
    const char *paths[] = {"/newfile1", "/newfile2", "/newfile3"};
    const int NFILES  = sizeof(paths)/sizeof(paths[0]);
    int ret;
    for (int i = 0; i < NFILES; i++) {
        ret = fs_ops.create(paths[i], 0100777, NULL);
        ck_assert_int_eq(ret, 0);
    }
    char *expected[] = {"newfile1", "newfile2", "newfile3"};

    int *seen = calloc(NFILES, sizeof(int));
    ck_assert_ptr_ne(seen, NULL);

    typedef struct { char **exp; int *flag; int n; } rd_ctx_t;

    int filler(void *ptr, const char *name, const struct stat *st, off_t off)
    {
        rd_ctx_t *c = (rd_ctx_t *)ptr;
        for (int i = 0; i < c->n; i++) {
            if (strcmp(c->exp[i], name) == 0) { 
                c->flag[i] = 1; 
                break; 
            }
        }  
        return 0;
    }

    rd_ctx_t ctx = { expected, seen, NFILES };
    ret = fs_ops.readdir("/", &ctx, filler, 0, NULL);
    ck_assert_int_eq(ret, 0);
    for (int i = 0; i < NFILES; i++) {        
        ck_assert_int_eq(seen[i], 1);
    }
    free(seen);
}
END_TEST

 /* Test for fs_create, create files in a subdirectory */
START_TEST(test_fs_create_subdir)
{
    ck_assert_int_eq(fs_ops.mkdir("/dir1", 0777), 0);

    const char *paths[] = {"/dir1/fileA", "/dir1/fileB"};
    const int   NFILES  = sizeof(paths)/sizeof(paths[0]);
    int ret;
    for (int i = 0; i < NFILES; i++){
        ret = fs_ops.create(paths[i], 0100777, NULL);
        ck_assert_int_eq(ret, 0);
    }

    char *expected[] = {"fileA", "fileB"};
    int seen[NFILES]; memset(seen, 0, sizeof(seen));

    typedef struct { char **exp; int *flag; int n; } ctx_t;
    int filler(void *ptr, const char *name, const struct stat *st, off_t off)
    {
        ctx_t *c = (ctx_t *)ptr;
        for (int i = 0; i < c->n; i++) {
            if (strcmp(c->exp[i], name) == 0) { 
                c->flag[i] = 1; 
                break; 
            }
        }
        return 0;
    }

    ctx_t ctx = { expected, seen, NFILES };
    ck_assert_int_eq(fs_ops.readdir("/dir1", &ctx, filler, 0, NULL), 0);
    for (int i = 0; i < NFILES; i++)
        ck_assert_int_eq(seen[i], 1);
}
END_TEST

/* Test for fs_create, create files in a sub‑subdirectory */
START_TEST(test_fs_create_subsubdir)
{
    int ret = fs_ops.mkdir("/dir1", 0777);
    ck_assert((ret == 0) || (ret == -EEXIST));

    ret = fs_ops.mkdir("/dir1/dir2", 0777);
    ck_assert_int_eq(ret, 0);

    const char *paths[] = {"/dir1/dir2/fileA", "/dir1/dir2/fileB"};
    const int   NFILES  = sizeof(paths)/sizeof(paths[0]);

    for (int i = 0; i < NFILES; i++) {
        ret = fs_ops.create(paths[i], 0100777, NULL);
        ck_assert_int_eq(ret, 0);
    }

    char *expected[] = {"fileA", "fileB"};
    int   seen[NFILES]; memset(seen, 0, sizeof(seen));

    typedef struct { char **exp; int *flag; int n; } ctx_t;
    int filler(void *ptr, const char *name, const struct stat *st, off_t off)
    {
        ctx_t *c = (ctx_t *)ptr;
        for (int i = 0; i < c->n; i++) {
            if (strcmp(c->exp[i], name) == 0) { 
                c->flag[i] = 1; 
                break; 
            }
        }
        return 0;
    }

    ctx_t ctx = { expected, seen, NFILES };
    ret = fs_ops.readdir("/dir1/dir2", &ctx, filler, 0, NULL);
    ck_assert_int_eq(ret, 0);
    for (int i = 0; i < NFILES; i++) {
        ck_assert_int_eq(seen[i], 1);
    }
}
END_TEST

/* Test for fs_create parent does not exist */
START_TEST(test_fs_create_parent_does_not_exist)
{
    int ret = fs_ops.create("/does-not-exist/file", 0100777, NULL);
    ck_assert_int_eq(ret, -ENOENT);
}
END_TEST

/* Test for fs_create parent is not a directory */
START_TEST(test_fs_create_parent_not_dir)
{
    int ret = fs_ops.create("/fileA", 0100777, NULL);
    ck_assert_int_eq(ret, 0);
    
    ret = fs_ops.create("/fileA/file", 0100777, NULL);
    ck_assert_int_eq(ret, -ENOTDIR);
}
END_TEST

//Test for fs_create file already exists */
START_TEST(test_fs_create_file_already_exists)
{
    int ret = fs_ops.create("/fileX", 0100777, NULL);
    ck_assert_int_eq(ret, 0);
    
    ret = fs_ops.create("/fileX", 0100777, NULL);
    ck_assert_int_eq(ret, -EEXIST);
}
END_TEST

//Test for fs_create with a long Name */
START_TEST(test_fs_create_long_name)
{
    int ret = fs_ops.mkdir("/dir1", 0777);
    ck_assert((ret == 0) || (ret == -EEXIST));

    const char *input_path =
        "/dir1/veryylongggfileeenameeeetesttttpurpose";   /* 37 chars */

    ret = fs_ops.create(input_path, 0100777, NULL);
    ck_assert_int_eq(ret, 0);

    const char *expect_leaf = "veryylongggfileeenameeeetes"; /* 27 */

    typedef struct { const char *leaf; int seen; } ctx_t;

    int filler(void *ptr, const char *name, const struct stat *st, off_t off)
    {
        ctx_t *c = (ctx_t *)ptr;
        if (strcmp(name, c->leaf) == 0) {
            c->seen = 1;
        }
        return 0;
    }

    ctx_t ctx = { expect_leaf, 0 };
    ret = fs_ops.readdir("/dir1", &ctx, filler, 0, NULL);
    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(ctx.seen, 1);
}
END_TEST
 
 /* Test for fs_mkdir to create new directory "/newdir" */
 START_TEST(test_mkdir) {
     int ret = fs_ops.mkdir("/newdir", 0777);
     ck_assert_int_eq(ret, 0);

     struct stat st;
     ret = fs_ops.getattr("/newdir", &st);
     ck_assert_int_eq(ret, 0);
     ck_assert(S_ISDIR(st.st_mode));
 }
 END_TEST

 /* Test for fs_mkdir, multiple directories */
START_TEST(test_fs_mkdir_multiple)
{
    system("python gen-disk.py -q disk2.in test2.img");
    block_init("test2.img");
    fs_ops.init(NULL);

    const char *dir_paths[] = {"/dir1", "/dir2", "/dir3"};
    const int NDIRS = sizeof(dir_paths)/sizeof(dir_paths[0]);
    int ret;
    for (int i = 0; i < NDIRS; i++) {
        ret = fs_ops.mkdir(dir_paths[i], 0777);
        ck_assert_int_eq(ret, 0);
    }
    
    char *expected[] = {"dir1", "dir2", "dir3"};

    typedef struct { char **exp; int *seen; int n; } ctx_t;

    int filler(void *ptr, const char *name, const struct stat *st, off_t off)
    {
        ctx_t *c = (ctx_t *)ptr;
        for (int i = 0; i < c->n; i++) {
            if (strcmp(c->exp[i], name) == 0) { 
                c->seen[i] = 1; 
                break; 
            }
        }
        return 0;
    }
    int seen[NDIRS]; memset(seen, 0, sizeof(seen));
    ctx_t ctx = { expected, seen, NDIRS };

    ret = fs_ops.readdir("/", &ctx, filler, 0, NULL);
    ck_assert_int_eq(ret, 0);
    for (int i = 0; i < NDIRS; i++) {
        ck_assert_int_eq(seen[i], 1);
    }
}
END_TEST

/* Test for fs_mkdir subdirectory creation */
START_TEST(test_fs_mkdir_subdirectory)
{
    fs_ops.rmdir("/dirX/subd1");
    fs_ops.rmdir("/dirX/subd2");
    fs_ops.rmdir("/dirX");

    int ret = fs_ops.mkdir("/dirX", 0777);
    ck_assert_int_eq(ret, 0);

    const char *subdir_paths[] = {"/dirX/subd1", "/dirX/subd2"};
    const int NSDIRS = sizeof(subdir_paths)/sizeof(subdir_paths[0]);

    for (int i = 0; i < NSDIRS; i++) {
        int ret = fs_ops.mkdir(subdir_paths[i], 0777);
        ck_assert_int_eq(ret, 0);
    }
    
    char *expected[] = {"subd1", "subd2"};

    typedef struct { char **exp; int *seen; int n; } ctx_t;

    int filler(void *ptr, const char *name, const struct stat *st, off_t off)
    {
        ctx_t *c = (ctx_t *)ptr;
        for (int i = 0; i < c->n; i++) {
            if (strcmp(c->exp[i], name) == 0) { 
                c->seen[i] = 1; 
                break; 
            }
        }
        return 0;
    }
    int seen[NSDIRS]; memset(seen, 0, sizeof(seen));
    ctx_t ctx = {expected, seen, NSDIRS};

    ret = fs_ops.readdir("/dirX", &ctx, filler, 0, NULL);
    ck_assert_int_eq(ret, 0);
    for (int i = 0; i < NSDIRS; i++) {
        ck_assert_int_eq(seen[i], 1);
    }
}
END_TEST

/* Test for fs_mkdir sub‑subdirectory creation */
START_TEST(test_fs_mkdir_subsubdir)
{
    int ret = fs_ops.mkdir("/dir1", 0777);
    ck_assert((ret == 0) || (ret == -EEXIST));
    ret = fs_ops.mkdir("/dir1/dir2", 0777);
    ck_assert_int_eq(ret, 0);

    const char *subsubdir_paths[] = {"/dir1/dir2/subdA", "/dir1/dir2/subdB"};
    const int NSDIRS = sizeof(subsubdir_paths)/sizeof(subsubdir_paths[0]);

    for (int i = 0; i < NSDIRS; i++) {
        int ret = fs_ops.mkdir(subsubdir_paths[i], 0777);
        ck_assert_int_eq(ret, 0);
    }
    
    char *expected[] = {"subdA", "subdB"};

    typedef struct { char **exp; int *seen; int n; } ctx_t;

    int filler(void *ptr, const char *name, const struct stat *st, off_t off)
    {
        ctx_t *c = (ctx_t *)ptr;
        for (int i = 0; i < c->n; i++) {
            if (strcmp(c->exp[i], name) == 0) { 
                c->seen[i] = 1; 
                break; 
            }
        }
        return 0;
    }
    int seen[NSDIRS]; memset(seen, 0, sizeof(seen));
    ctx_t ctx = { expected, seen, NSDIRS };

    ret = fs_ops.readdir("/dir1/dir2", &ctx, filler, 0, NULL);
    ck_assert_int_eq(ret, 0);
    for (int i = 0; i < NSDIRS; i++) {
        ck_assert_int_eq(seen[i], 1);
    }
}
END_TEST

/* Test for fs_mkdir errors */ 
START_TEST(test_fs_mkdir_errors)
{
    int ret = fs_ops.mkdir("/does-not-exist/dir", 0777);
    ck_assert_int_eq(ret, -ENOENT);

    ret = fs_ops.create("/fileA", 0100777, NULL);
    ck_assert_int_eq(ret, 0);
    
    ret = fs_ops.mkdir("/fileA/dir", 0777);
    ck_assert_int_eq(ret, -ENOTDIR);

    ret = fs_ops.create("/writedata", 0100666, NULL);
    ck_assert_int_eq(ret, 0);
    const char *data = "Hello, Test!";
    
    ret = fs_ops.write("/writedata", data, strlen(data), 0, NULL);
    ck_assert_int_eq(ret, (int)strlen(data));
    char buf[50] = {0};
    
    ret = fs_ops.read("/writedata", buf, 50, 0, NULL);
    ck_assert_int_eq(ret, (int)strlen(data));
    ck_assert_str_eq(buf, data);
}
END_TEST

/* Test for fs_mkdir target already exists */
START_TEST(test_fs_mkdir_target_exists)
{
    int ret = fs_ops.mkdir("/dirA", 0777);
    ck_assert_int_eq(ret, 0);
    
    ret = fs_ops.mkdir("/dirA", 0777);
    ck_assert_int_eq(ret, -EEXIST);
    
    ret = fs_ops.create("/filename", 0100777, NULL);
    ck_assert_int_eq(ret, 0);

    ret = fs_ops.mkdir("/filename", 0777);
    ck_assert_int_eq(ret, -EEXIST);
}
END_TEST
   
/* Test for fs_mkdir long name */
START_TEST(test_fs_mkdir_long_name)
{
    int ret = fs_ops.mkdir("/testdir", 0777);
    ck_assert((ret == 0) || (ret == -EEXIST));

    const char *input_path = "/testdir/verryyylonggggdirnameeeetesttttpurpose";
    ret = fs_ops.mkdir(input_path, 0777);
    ck_assert_int_eq(ret, 0);

    const char *expect_lf = "verryyylonggggdirnameeeetes";

    typedef struct { const char *leaf; int seen; } ctx_t;

    int filler(void *ptr, const char *name, const struct stat *st, off_t off)
    {
        ctx_t *c = (ctx_t *)ptr;
        if (strcmp(name, c->leaf) == 0) {
            c->seen = 1;
        }
        return 0;
    }

    ctx_t ctx = { expect_lf, 0 };
    ret = fs_ops.readdir("/testdir", &ctx, filler, 0, NULL);
    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(ctx.seen, 1);
}
END_TEST
 
/* Test: fs_truncate - truncate a file to zero length */
START_TEST(test_truncate) {
    int ret = fs_ops.create("/truncfile", 0100666, NULL);
    ck_assert_int_eq(ret, 0);
    const char *data = "Data to be removed.";
    
    ret = fs_ops.write("/truncfile", data, strlen(data), 0, NULL);
    ck_assert_int_eq(ret, (int)strlen(data));
    
    ret = fs_ops.truncate("/truncfile", 0);
    ck_assert_int_eq(ret, 0);
    
    struct stat st;
    ret = fs_ops.getattr("/truncfile", &st);
    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(st.st_size, 0);
}
END_TEST

/* Truncate test common function */
void test_truncate_values(const char *path, size_t initial_size)
{
    struct statvfs before_trunc, after_trunc;
    int ret = fs_ops.create(path, 0100777, NULL);
    ck_assert_int_eq(ret, 0);

    char *buf = malloc(initial_size);
    ck_assert_ptr_ne(buf, NULL);
    
    generate_pattern(buf, initial_size, 0);

    ret = fs_ops.write(path, buf, initial_size, 0, NULL);
    ck_assert_int_eq(ret, (int)initial_size);

    ret = fs_ops.statfs("/", &before_trunc);
    ck_assert_int_eq(ret, 0);

    struct stat st;
    ret = fs_ops.getattr(path, &st);
    ck_assert_int_eq(ret, 0);

    ret = fs_ops.truncate(path, 0);
    ck_assert_int_eq(ret, 0);

    ret = fs_ops.statfs("/", &after_trunc);
    ck_assert_int_eq(ret, 0);

    int blocks_before = compute_block_size(initial_size);
    int e_free = blocks_before; 
    
    ck_assert_int_eq(after_trunc.f_bfree, before_trunc.f_bfree + e_free);

    ret = fs_ops.unlink(path);
    ck_assert_int_eq(ret, 0);

    free(buf);
}

/* Tests for fs_truncate with specific sizes */
START_TEST(test_trunc_lt_1blk) { test_truncate_values("/tA",  1000);  } END_TEST
START_TEST(test_trunc_eq_1blk) { test_truncate_values("/tB",  4096);  } END_TEST
START_TEST(test_trunc_lt_2blk) { test_truncate_values("/tC",  7000);  } END_TEST
START_TEST(test_trunc_eq_2blk) { test_truncate_values("/tD",  8192);  } END_TEST
START_TEST(test_trunc_lt_3blk) { test_truncate_values("/tE", 10000);  } END_TEST
START_TEST(test_trunc_eq_3blk) { test_truncate_values("/tF", 12288);  } END_TEST

/* Test for fs_truncate errors (non-zero length, non existant parent, parent not a dir, file non existant, target is a dir) */
START_TEST(test_truncate_errors)
{
    int ret = fs_ops.create("/invalid_truncate", 0100777, NULL);
    ck_assert_int_eq(ret, 0);

    ret = fs_ops.truncate("/invalid_truncate", 1000);
    ck_assert_int_eq(ret, -EINVAL);

    ret = fs_ops.unlink("/invalid_truncate");
    ck_assert_int_eq(ret, 0);

    ret = fs_ops.truncate("/does-not-exist/filename", 0);
    ck_assert_int_eq(ret, -ENOENT);

    ret = fs_ops.create("/fileX", 0100777, NULL);
    ck_assert_int_eq(ret, 0);

    ret = fs_ops.truncate("/fileX/file", 0);
    ck_assert_int_eq(ret, -ENOTDIR);
    fs_ops.unlink("/fileX");

    ret = fs_ops.mkdir("/dA", 0777);
    ck_assert((ret == 0) || (ret == -EEXIST));

    ret = fs_ops.truncate("/dA/missing-file", 0);
    ck_assert_int_eq(ret, -ENOENT);

    ret= fs_ops.mkdir("/dZ", 0777);
    ck_assert_int_eq(ret, 0);

    ret = fs_ops.truncate("/dZ", 0);
    ck_assert_int_eq(ret, -EISDIR);
}
END_TEST

/* Test for fs_truncate to length greater than current size */
START_TEST(test_fs_truncate_invalid)
{
    int ret = fs_ops.create("/trunc-file", 0100777, NULL);
    ck_assert_int_eq(ret, 0);

    char *buf = malloc(1000);
    ck_assert_ptr_ne(buf, NULL);
    generate_pattern(buf, 1000, 0);
    
    ret = fs_ops.write("/trunc-file", buf, 1000, 0, NULL);
    ck_assert_int_eq(ret, 1000);
    free(buf);

    ret = fs_ops.truncate("/trunc-file", 3000);
    ck_assert_int_eq(ret, -EINVAL);
}
END_TEST
 
 /* Test for fs_unlink */
 START_TEST(test_unlink) {
     int ret = fs_ops.create("/unlinkfile", 0100666, NULL);
     ck_assert_int_eq(ret, 0);
     struct stat st;
     
     ret = fs_ops.getattr("/unlinkfile", &st);
     ck_assert_int_eq(ret, 0);
     
     ret = fs_ops.unlink("/unlinkfile");
     ck_assert_int_eq(ret, 0);
     
     ret = fs_ops.getattr("/unlinkfile", &st);
     ck_assert_int_eq(ret, -ENOENT);
 }
 END_TEST

/* Test for fs_unlink delete files root*/
START_TEST(test_fs_unlink_root)
{
    int ret;
    const char *files[] = {"/ufile1", "/ufile2"};
    int count = sizeof(files) / sizeof(files[0]);

    for (int i = 0; i < count; i++) {
        ret = fs_ops.create(files[i], 0100777, NULL);
        ck_assert_int_eq(ret, 0);
    }

    for (int i = 0; i < count; i++) {
        ret = fs_ops.unlink(files[i]);
        ck_assert_int_eq(ret, 0);
    }

    const char *expected[] = {"ufile1", "ufile2"};
    int seen[count];
    memset(seen, 0, sizeof(seen));

    typedef struct { const char **exp; int *seen; int n; } ctx_t;
    int filler(void *ptr, const char *name, const struct stat *st, off_t off) {
        ctx_t *c = (ctx_t *)ptr;
        for (int i = 0; i < c->n; i++) {
            if (strcmp(name, c->exp[i]) == 0) { c->seen[i] = 1; break; }
        }
        return 0;
    }

    ctx_t ctx = {expected, seen, count};
    ret = fs_ops.readdir("/", &ctx, filler, 0, NULL);
    ck_assert_int_eq(ret, 0);
    for (int i = 0; i < count; i++) {
        ck_assert_int_eq(seen[i], 0);
    }
}
END_TEST

/* Test for fs_unlink delete files subdirectory */
START_TEST(test_fs_unlink_subdir)
{
    int ret = fs_ops.mkdir("/ul_sub", 0777);
    ck_assert((ret == 0) || (ret == -EEXIST));

    const char *files[] = {"/ul_sub/f1", "/ul_sub/f2"};
    int count = sizeof(files) / sizeof(files[0]);

    for (int i = 0; i < count; i++) {
        ret = fs_ops.create(files[i], 0100777, NULL);
        ck_assert_int_eq(ret, 0);
    }

    for (int i = 0; i < count; i++) {
        ret = fs_ops.unlink(files[i]);
        ck_assert_int_eq(ret, 0);
    }

    const char *expected[] = {"f1", "f2"};
    int seen[count]; memset(seen, 0, sizeof(seen));

    typedef struct { const char **exp; int *seen; int n; } ctx_t;
    int filler(void *ptr, const char *name, const struct stat *st, off_t off) {
        ctx_t *c = (ctx_t *)ptr;
        for (int i = 0; i < c->n; i++) {
            if (strcmp(name, c->exp[i]) == 0) { c->seen[i] = 1; break; }
        }
        return 0;
    }

    ctx_t ctx = {expected, seen, count};
    ret = fs_ops.readdir("/ul_sub", &ctx, filler, 0, NULL);
    ck_assert_int_eq(ret, 0);
    for (int i = 0; i < count; i++) {
        ck_assert_int_eq(seen[i], 0);
    }
}
END_TEST

/* Test for fs_unlink delete files sub subdirectory*/
START_TEST(test_fs_unlink_subsubdir)
{
    int ret;
    ret = fs_ops.mkdir("/ul_sub2", 0777);
    ck_assert((ret == 0) || (ret == -EEXIST));
    ret = fs_ops.mkdir("/ul_sub2/inner", 0777);
    ck_assert((ret == 0) || (ret == -EEXIST));

    const char *files[] = {"/ul_sub2/inner/a", "/ul_sub2/inner/b"};
    int count = sizeof(files) / sizeof(files[0]);

    for (int i = 0; i < count; i++) {
        ret = fs_ops.create(files[i], 0100777, NULL);
        ck_assert_int_eq(ret, 0);
    }

    for (int i = 0; i < count; i++) {
        ret = fs_ops.unlink(files[i]);
        ck_assert_int_eq(ret, 0);
    }

    const char *expected[] = {"a", "b"};
    int seen[count]; memset(seen, 0, sizeof(seen));

    typedef struct { const char **exp; int *seen; int n; } ctx_t;
    int filler(void *ptr, const char *name, const struct stat *st, off_t off) {
        ctx_t *c = (ctx_t *)ptr;
        for (int i = 0; i < c->n; i++) {
            if (strcmp(name, c->exp[i]) == 0) { c->seen[i] = 1; break; }
        }
        return 0;
    }

    ctx_t ctx = {expected, seen, count};
    ret = fs_ops.readdir("/ul_sub2/inner", &ctx, filler, 0, NULL);
    ck_assert_int_eq(ret, 0);
    for (int i = 0; i < count; i++) {
        ck_assert_int_eq(seen[i], 0);
    }
}
END_TEST


/* Test for fs_unlink errors (parent non-existent, parent is not a dir, file does not exist, dir is a target) */
START_TEST(test_fs_unlink_errors)
{
    int ret = fs_ops.unlink("/not-exist/file");
    ck_assert_int_eq(ret, -ENOENT);

    ret = fs_ops.create("/fA", 0100666, NULL);
    ck_assert_int_eq(ret, 0);

    ret = fs_ops.unlink("/fA/fileB");
    ck_assert_int_eq(ret, -ENOTDIR);

    ret = fs_ops.unlink("/dir1/missing-file");
    ck_assert_int_eq(ret, -ENOENT);

    ret = fs_ops.mkdir("/dB", 0777);
    ck_assert((ret == 0) || (ret == -EEXIST));

    ret = fs_ops.unlink("/dB");
    ck_assert_int_eq(ret, -EISDIR);
}
END_TEST

/* Test for fs_unlink after write from sub-subdirectory covering all levels*/
START_TEST(test_fs_unlink_subsubdir_free_blocks)
{
    int ret;
    struct statvfs before_write, after_write, after_unlink;

    ret = fs_ops.mkdir("/data", 0777);
    ret = fs_ops.mkdir("/data/logs", 0777);
    ck_assert_int_eq(ret, 0);

    ret = fs_ops.statfs("/", &before_write);
    ck_assert_int_eq(ret, 0);

    const char *path = "/data/logs/file";
    ret = fs_ops.create(path, 0100777, NULL);
    ck_assert_int_eq(ret, 0);

    char *buf = malloc(4000);
    ck_assert_ptr_ne(buf, NULL);
    char *ptr = buf;
    for (int i = 0; i < 4000 && ptr < buf + 4000; i++) {
        ptr += sprintf(ptr, "%d ", i);
    }
    ret = fs_ops.write(path, buf, 4000, 0, NULL);
    ck_assert_int_eq(ret, 4000);
    free(buf);

    ret = fs_ops.statfs("/", &after_write);
    ck_assert_int_eq(ret, 0);
    ck_assert(before_write.f_bfree > after_write.f_bfree);

    ret = fs_ops.unlink(path);
    ck_assert_int_eq(ret, 0);

    ret = fs_ops.statfs("/", &after_unlink);
    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(after_unlink.f_bfree, before_write.f_bfree);
}
END_TEST

 /* Test for fs_rmdir - remove a directory */
 START_TEST(test_rmdir) {
     int ret = fs_ops.mkdir("/rmdir_dir", 0777);
     ck_assert_int_eq(ret, 0);
     
     ret = fs_ops.rmdir("/rmdir_dir");
     ck_assert_int_eq(ret, 0);
     
     struct stat st;
     ret = fs_ops.getattr("/rmdir_dir", &st);
     ck_assert_int_eq(ret, -ENOENT);
 }
 END_TEST

 /* Test for fs_rmdir - remove root directory */
START_TEST(test_fs_rmdir_root)
{
    int ret;
    const char *dir[] = {"/rdir1", "/rdir2", "/rdir3"};
    int count = sizeof(dir) / sizeof(dir[0]);

    for (int i = 0; i < count; i++) {
        ret = fs_ops.mkdir(dir[i], 0777);
        ck_assert_int_eq(ret, 0);
    }

    for (int i = 0; i < count; i++) {
        ret = fs_ops.rmdir(dir[i]);
        ck_assert_int_eq(ret, 0);
    }
    char *expected[] = {"rdir1", "rdir2", "rdir3"};
    int seen[3] = {0};

    typedef struct { char **exp; int *seen; int n; } ctx_t;
    int filler(void *ptr, const char *name, const struct stat *st, off_t off) {
        ctx_t *c = (ctx_t *)ptr;
        for (int i = 0; i < c->n; i++) {
            if (strcmp(name, c->exp[i]) == 0) { c->seen[i] = 1; break; }
        }
        return 0;
    }

    ctx_t ctx = {expected, seen, count};
    ret = fs_ops.readdir("/", &ctx, filler, 0, NULL);
    ck_assert_int_eq(ret, 0);
    for (int i = 0; i < count; i++) {
        ck_assert_int_eq(seen[i], 0);
    }
}
END_TEST

/* Test for fs_rmdir - remove subdirectory */
START_TEST(test_fs_rmdir_subdir)
{
    int ret = fs_ops.mkdir("/rparent", 0777);
    ck_assert((ret == 0) || (ret == -EEXIST));

    const char *subdirs[] = {"/rparent/s1", "/rparent/s2"};
    for (int i = 0; i < 2; i++) {
        fs_ops.rmdir(subdirs[i]);
        ret = fs_ops.mkdir(subdirs[i], 0777);
        ck_assert_int_eq(ret, 0);
    }

    for (int i = 0; i < 2; i++) {
        ret = fs_ops.rmdir(subdirs[i]);
        ck_assert_int_eq(ret, 0);
    }

    char *expected[] = {"s1", "s2"};
    int seen[2] = {0};
    typedef struct { char **exp; int *flag; int n; } ctx_t;

    int filler(void *ptr, const char *name, const struct stat *st, off_t off) {
        ctx_t *c = (ctx_t *)ptr;
        for (int i = 0; i < c->n; i++) {
            if (strcmp(name, c->exp[i]) == 0) {
                c->flag[i] = 1;
                break;
            }
        }
        return 0;
    }

    ctx_t ctx = {expected, seen, 2};
    ret = fs_ops.readdir("/rparent", &ctx, filler, 0, NULL);
    ck_assert_int_eq(ret, 0);
    for (int i = 0; i < 2; i++)
        ck_assert_int_eq(seen[i], 0);
}
END_TEST

/* Test for fs_rmdir - remove sub subdirectory */
START_TEST(test_fs_rmdir_subsubdir)
{
    fs_ops.rmdir("/rlevel1/rlevel2/x");
    fs_ops.rmdir("/rlevel1/rlevel2/y");
    fs_ops.rmdir("/rlevel1/rlevel2");
    fs_ops.rmdir("/rlevel1");

    int ret = fs_ops.mkdir("/rlevel1", 0777);
    ck_assert_int_eq(ret, 0);

    ret = fs_ops.mkdir("/rlevel1/rlevel2", 0777);
    ck_assert_int_eq(ret, 0);

    const char *subsubdirs[] = {"/rlevel1/rlevel2/x", "/rlevel1/rlevel2/y"};
    for (int i = 0; i < 2; i++) {
        fs_ops.rmdir(subsubdirs[i]);
        ret = fs_ops.mkdir(subsubdirs[i], 0777);
        ck_assert_int_eq(ret, 0);
    }

    for (int i = 0; i < 2; i++) {
        ret = fs_ops.rmdir(subsubdirs[i]);
        ck_assert_int_eq(ret, 0);
    }

    char *expected[] = {"x", "y"};
    int seen[2] = {0};

    typedef struct { char **exp; int *flag; int n; } ctx_t;

    int filler(void *ptr, const char *name, const struct stat *st, off_t off) {
        ctx_t *c = (ctx_t *)ptr;
        for (int i = 0; i < c->n; i++) {
            if (strcmp(name, c->exp[i]) == 0) {
                c->flag[i] = 1;
                break;
            }
        }
        return 0;
    }

    ctx_t ctx = {expected, seen, 2};
    ret = fs_ops.readdir("/rlevel1/rlevel2", &ctx, filler, 0, NULL);
    ck_assert_int_eq(ret, 0);
    for (int i = 0; i < 2; i++)
        ck_assert_int_eq(seen[i], 0);
}
END_TEST

 
/* Test for fs_rmdir errors (parent does not exist, parent is not a directory, target does not exist, target is a file, directory not empty) */
START_TEST(test_fs_rmdir_errors)
{
    int ret = fs_ops.rmdir("/noexist/child");
    ck_assert_int_eq(ret, -ENOENT);

    ret = fs_ops.create("/fX", 0100777, NULL);
    ck_assert_int_eq(ret, 0);
    
    ret = fs_ops.rmdir("/fX/subdir");
    ck_assert_int_eq(ret, -ENOTDIR);

    ret = fs_ops.mkdir("/dir5", 0777);
    ck_assert((ret == 0) || (ret == -EEXIST));
    
    ret = fs_ops.rmdir("/dir5/does-not-exist");
    ck_assert_int_eq(ret, -ENOENT);

    ret = fs_ops.create("/fileY", 0100777, NULL);
    ck_assert_int_eq(ret, 0);
    
    ret = fs_ops.rmdir("/fileY");
    ck_assert_int_eq(ret, -ENOTDIR);

    ret = fs_ops.mkdir("/d6", 0777);
    ck_assert((ret == 0) || (ret == -EEXIST));
    
    ret = fs_ops.mkdir("/d6/child", 0777);
    ck_assert_int_eq(ret, 0);
    
    ret = fs_ops.rmdir("/d6");
    ck_assert_int_eq(ret, -ENOTEMPTY);
}
END_TEST

/* Write append test common function */
void test_write_append_values(const char *path_prefix, size_t total_len)
{
    int ret;
    struct statvfs before_write, after_write;
    int chunks[]    = { 17, 100, 1000, 1024, 1970, 3000 };
    int num_chunks  = sizeof(chunks) / sizeof(chunks[0]);

    for (int ci = 0; ci < num_chunks; ci++) {

        int chunk = chunks[ci];

        char path[128];
        snprintf(path, sizeof(path), "%s_chunk%d", path_prefix, chunk);

        ret = fs_ops.statfs("/", &before_write);
        ck_assert_int_eq(ret, 0);

        ret = fs_ops.create(path, 0100777, NULL);
        ck_assert_int_eq(ret, 0);

        char *buffer = malloc(total_len);
        ck_assert_ptr_ne(buffer, NULL);
        generate_pattern(buffer, total_len, 0);

        size_t offset = 0;
        while (offset < total_len) {
            size_t write_len =
                (offset + chunk > total_len) ? (total_len - offset) : chunk;
            ret = fs_ops.write(path, buffer + offset, write_len, offset, NULL);
            ck_assert_int_eq(ret, (int)write_len);
            offset += write_len;
        }
        ck_assert_int_eq((int)offset, (int)total_len);

        char *read_buffer = malloc(total_len);
        ck_assert_ptr_ne(read_buffer, NULL);
        ret = fs_ops.read(path, read_buffer, total_len, 0, NULL);
        ck_assert_int_eq(ret, (int)total_len);

        ck_assert(memcmp(buffer, read_buffer, total_len) == 0);
        unsigned crc1 = crc32(0, (const Bytef *)buffer,      total_len);
        unsigned crc2 = crc32(0, (const Bytef *)read_buffer, total_len);
        ck_assert_uint_eq(crc1, crc2);

        free(buffer);
        free(read_buffer);

        ret = fs_ops.unlink(path);
        ck_assert_int_eq(ret, 0);

        ret = fs_ops.statfs("/", &after_write);
        ck_assert_int_eq(ret, 0);
        ck_assert_uint_eq(after_write.f_bfree,  before_write.f_bfree);
        ck_assert_uint_eq(after_write.f_bavail, before_write.f_bavail);
    }
}

/* Test for write append with specific sizes */
START_TEST(test_write_append_lt_1blk) { test_write_append_values("/wA1", 1000); } END_TEST
START_TEST(test_write_append_eq_1blk) { test_write_append_values("/wA2", 4096); } END_TEST
START_TEST(test_write_append_lt_2blk) { test_write_append_values("/wA3", 7000); } END_TEST
START_TEST(test_write_append_eq_2blk) { test_write_append_values("/wA4", 8192); } END_TEST
START_TEST(test_write_append_lt_3blk) { test_write_append_values("/wA5", 10000); } END_TEST
START_TEST(test_write_append_eq_3blk) { test_write_append_values("/wA6", 12288); } END_TEST

/* Write overwrite test common function */
void test_write_overwrite_values(const char *path, size_t len)
{
    struct statvfs before_write, after_write;

    int ret = fs_ops.statfs("/", &before_write);
    ck_assert_int_eq(ret, 0);

    ret = fs_ops.create(path, 0100777, NULL);
    ck_assert_int_eq(ret, 0);

    char *bufferA = malloc(len);
    ck_assert_ptr_ne(bufferA, NULL);
    
    generate_pattern(bufferA, len, 0);
    
    ret = fs_ops.write(path, bufferA, len, 0, NULL);
    ck_assert_int_eq(ret, (int)len);

    char *bufferB = malloc(len);
    ck_assert_ptr_ne(bufferB, NULL);

    generate_pattern(bufferB, len, 1000);  

    ret = fs_ops.write(path, bufferB, len, 0, NULL);
    ck_assert_int_eq(ret, (int)len);

    char *read_buffer = malloc(len);
    ck_assert_ptr_ne(read_buffer, NULL);
    
    ret = fs_ops.read(path, read_buffer, len, 0, NULL);
    ck_assert_int_eq(ret, (int)len);

    ck_assert(memcmp(bufferB, read_buffer, len) == 0);

    unsigned crcB = crc32(0, (const Bytef *)bufferB, len);
    unsigned crcRead = crc32(0, (const Bytef *)read_buffer, len);
    ck_assert_uint_eq(crcB, crcRead);

    free(bufferA);
    free(bufferB);
    free(read_buffer);

    ret = fs_ops.unlink(path);
    ck_assert_int_eq(ret, 0);
    
    ret = fs_ops.statfs("/", &after_write);
    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(after_write.f_bfree, before_write.f_bfree);
    ck_assert_uint_eq(after_write.f_bavail, before_write.f_bavail);
}

/* Test for write overwrite with specific values */
START_TEST(test_write_overwrite_lt_1blk) { test_write_overwrite_values("/o1", 1000); } END_TEST
START_TEST(test_write_overwrite_eq_1blk) { test_write_overwrite_values("/o2", 4096); } END_TEST
START_TEST(test_write_overwrite_lt_2blk) { test_write_overwrite_values("/o3", 7000); } END_TEST
START_TEST(test_write_overwrite_eq_2blk) { test_write_overwrite_values("/o4", 8192); } END_TEST
START_TEST(test_write_overwrite_lt_3blk) { test_write_overwrite_values("/o5", 10000); } END_TEST
START_TEST(test_write_overwrite_eq_3blk) { test_write_overwrite_values("/o6", 12288); } END_TEST

 /* Test for fs_utime to update access and modification times - file*/
 START_TEST(test_utime_file) {
     int ret = fs_ops.create("/utimefile", 0100666, NULL);
     ck_assert_int_eq(ret, 0);
     
     struct stat st;
     ret = fs_ops.getattr("/utimefile", &st);
     ck_assert_int_eq(ret, 0);
     
     struct utimbuf new_time;
     new_time.actime = st.st_mtime - 100;  
     new_time.modtime = st.st_mtime - 100;
     
     ret = fs_ops.utime("/utimefile", &new_time);
     ck_assert_int_eq(ret, 0);
     
     ret = fs_ops.getattr("/utimefile", &st);
     ck_assert_int_eq(ret, 0);
     ck_assert_int_eq(st.st_mtime, new_time.modtime);
 }
 END_TEST

 /* Test for fs_utime to update access and modification times - dir */
 START_TEST(test_utime_dir) {
    int ret = fs_ops.mkdir("/utimedir", 0755);
    ck_assert_int_eq(ret, 0);
    
    time_t fixed = 1700000000;               
    struct utimbuf ut = {.modtime = fixed};

    ret = fs_ops.utime("/utimedir", &ut);
    ck_assert_int_eq(ret, 0);

    struct stat st;
    ret = fs_ops.getattr("/utimedir", &st);
    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(st.st_mtime, fixed);
    ck_assert_int_eq(st.st_ctime, fixed);
}
END_TEST
 
/* Test for fs_utime errors (nonexistent file) */
START_TEST(test_fs_utime_errors_noexist_file)
{
    struct utimbuf ut = {.actime = 0, .modtime = 0};
    int ret = fs_ops.utime("/does-not-exist", &ut);
    ck_assert_int_eq(ret, -ENOENT);
}
END_TEST

/* Test for fs_utime errors (component not a dir) */
START_TEST(test_fs_utime_errors_no_dir)
{
    int ret = fs_ops.create("/utime-no-dir", 0644 | S_IFREG, NULL);
    ck_assert_int_eq(ret, 0);

    struct utimbuf ut = { .modtime = 1710000000 };
    ret = fs_ops.utime("/utime-no-dir/something", &ut);
    ck_assert_int_eq(ret, -ENOTDIR);

}
END_TEST
    
/* Test for fs_utime to check it doesn't modify mode or size */
START_TEST(test_fs_utime_metadata)
{
    const char *path = "/utime_check_file";

    int ret = fs_ops.create(path, 0644 | S_IFREG, NULL);
    ck_assert_int_eq(ret, 0);

    char buf[1024] = "x";
    ret = fs_ops.write(path, buf, sizeof(buf), 0, NULL);
    ck_assert_int_eq(ret, sizeof(buf));

    struct stat before;
    ret = fs_ops.getattr(path, &before);
    ck_assert_int_eq(ret, 0);

    struct utimbuf ut = { .modtime = 1700000000 };
    ret = fs_ops.utime(path, &ut);
    ck_assert_int_eq(ret, 0);

    struct stat after;
    ret = fs_ops.getattr(path, &after);
    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(after.st_mtime, ut.modtime);
    ck_assert_int_eq(after.st_ctime, ut.modtime);
    ck_assert_int_eq(before.st_mode, after.st_mode);
    ck_assert_int_eq(before.st_uid, after.st_uid);
    ck_assert_int_eq(before.st_gid, after.st_gid);
    ck_assert_int_eq(before.st_size, after.st_size);
}
END_TEST

/* Main: add tests to the suite */
int main(int argc, char **argv) {
    Suite *s = suite_create("fs5600-Write");
    TCase *tc = tcase_create("write_mostly");
    
    tcase_add_checked_fixture(tc, test_setup, test_teardown);

    /* Create tests */
    tcase_add_test(tc, test_create_file);
    tcase_add_test(tc, test_fs_create_root);
    tcase_add_test(tc, test_fs_create_subdir);
    tcase_add_test(tc, test_fs_create_subsubdir);
    tcase_add_test(tc, test_fs_create_parent_does_not_exist);
    tcase_add_test(tc, test_fs_create_parent_not_dir);
    tcase_add_test(tc, test_fs_create_file_already_exists);
    tcase_add_test(tc, test_fs_create_long_name);
    
    /* mkdir tests */
    tcase_add_test(tc, test_mkdir);
    tcase_add_test(tc, test_fs_mkdir_multiple);
    tcase_add_test(tc, test_fs_mkdir_subdirectory);
    tcase_add_test(tc, test_fs_mkdir_subsubdir);
    tcase_add_test(tc, test_fs_mkdir_errors);
    tcase_add_test(tc, test_fs_mkdir_target_exists);
    tcase_add_test(tc, test_fs_mkdir_long_name);
    
    /* truncate tests */
    tcase_add_test(tc, test_truncate);
    tcase_add_test(tc, test_trunc_lt_1blk);
    tcase_add_test(tc, test_trunc_eq_1blk);
    tcase_add_test(tc, test_trunc_lt_2blk);
    tcase_add_test(tc, test_trunc_eq_2blk);
    tcase_add_test(tc, test_trunc_lt_3blk);
    tcase_add_test(tc, test_trunc_eq_3blk);
    tcase_add_test(tc, test_truncate_errors);
    tcase_add_test(tc, test_fs_truncate_invalid);
    
    /* unlink tests */
    tcase_add_test(tc, test_unlink);
    tcase_add_test(tc, test_fs_unlink_root);
    tcase_add_test(tc, test_fs_unlink_subdir);
    tcase_add_test(tc, test_fs_unlink_subsubdir);
    tcase_add_test(tc, test_fs_unlink_errors);
    tcase_add_test(tc, test_fs_unlink_subsubdir_free_blocks);
    
    /* rmdir tests */
    tcase_add_test(tc, test_rmdir);
    tcase_add_test(tc, test_fs_rmdir_root);
    tcase_add_test(tc, test_fs_rmdir_subdir);
    tcase_add_test(tc, test_fs_rmdir_subsubdir);
    tcase_add_test(tc, test_fs_rmdir_errors);
    
    /* Write-append tests */
    tcase_add_test(tc, test_write_append_lt_1blk);
    tcase_add_test(tc, test_write_append_eq_1blk);
    tcase_add_test(tc, test_write_append_lt_2blk);
    tcase_add_test(tc, test_write_append_eq_2blk);
    tcase_add_test(tc, test_write_append_lt_3blk);
    tcase_add_test(tc, test_write_append_eq_3blk);
    
    /* Write-overwrite tests */
    tcase_add_test(tc, test_write_overwrite_lt_1blk);
    tcase_add_test(tc, test_write_overwrite_eq_1blk);
    tcase_add_test(tc, test_write_overwrite_lt_2blk);
    tcase_add_test(tc, test_write_overwrite_eq_2blk);
    tcase_add_test(tc, test_write_overwrite_lt_3blk);
    tcase_add_test(tc, test_write_overwrite_eq_3blk);
    
    /* utime tests */
    tcase_add_test(tc, test_utime_file);
    tcase_add_test(tc, test_utime_dir);
    tcase_add_test(tc, test_fs_utime_errors_noexist_file);
    tcase_add_test(tc, test_fs_utime_errors_no_dir);
    tcase_add_test(tc, test_fs_utime_metadata);
    
    suite_add_tcase(s, tc);
 
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_VERBOSE);
    int n_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (n_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
 }
 