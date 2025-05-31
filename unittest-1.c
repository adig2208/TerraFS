/*
 * unittest-1.c
 * Description: Unit tests (libcheck) for read-only file system functions.
*/

#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 26
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <check.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fuse.h>
#include "fs5600.h"
#include <zlib.h>

 extern struct fuse_operations fs_ops;
 extern void block_init(char *file);

 typedef struct {
    char *path;
    int size;       
    int mode;       
    int uid;
    int gid;
    int ctime;
    int mtime;
    unsigned checksum;  
} ro_test_t;

/* Test table */
ro_test_t ro_files[] = {
    {"/",                         4096, 040777, 0,   0,   1565283152, 1565283167, 0},
    {"/file.1k",                  1000, 0100666, 500, 500, 1565283152, 1565283152, 1726121896},
    {"/file.10",                   10, 0100666, 500, 500, 1565283152, 1565283167, 3766980606},
    {"/dir-with-long-name",       4096, 040777, 0,   0,   1565283152, 1565283167, 0},
    {"/dir-with-long-name/file.12k+",12289,0100666, 0, 500, 1565283152, 1565283167, 2781093465},
    {"/dir2",                    8192, 040777, 500, 500, 1565283152, 1565283167, 0},
    {"/dir2/twenty-seven-byte-file-name", 1000, 0100666, 500, 500, 1565283152, 1565283167, 2902524398},
    {"/dir2/file.4k+",           4098, 0100777, 500, 500, 1565283152, 1565283167, 1626046637},
    {"/dir3",                    4096, 040777, 0,   500, 1565283152, 1565283167, 0},
    {"/dir3/subdir",             4096, 040777, 0,   500, 1565283152, 1565283167, 0},
    {"/dir3/subdir/file.4k-",    4095, 0100666, 500, 500, 1565283152, 1565283167, 2991486384},
    {"/dir3/subdir/file.8k-",    8190, 0100666, 500, 500, 1565283152, 1565283167, 724101859},
    {"/dir3/subdir/file.12k",   12288, 0100666, 500, 500, 1565283152, 1565283167, 1483119748},
    {"/dir3/file.12k-",         12287, 0100777, 0,   500, 1565283152, 1565283167, 1203178000},
    {"/file.8k+",               8195, 0100666, 500, 500, 1565283152, 1565283167, 1217760297},
    {NULL, 0, 0, 0, 0, 0, 0, 0}
};

void test_setup(void) {
    system("python gen-disk.py -q disk1.in test.img");
    block_init("test.img");
    fs_ops.init(NULL);
 }
 
 void test_teardown(void) {}
 
 /* Test for fs_getattr for root, all filles and directories in the table */
 START_TEST(test_getattr_root) {
    struct stat st;
    for (int i = 0; ro_files[i].path != NULL; i++) {
        int ret = fs_ops.getattr(ro_files[i].path, &st);
        ck_assert_int_eq(ret, 0);
        ck_assert_int_eq(st.st_size, ro_files[i].size);
        ck_assert_int_eq(st.st_mode, ro_files[i].mode);
        ck_assert_int_eq(st.st_gid, ro_files[i].gid);
        ck_assert_int_eq(st.st_uid, ro_files[i].uid);
        ck_assert_int_eq(st.st_ctime, ro_files[i].mtime);
        ck_assert_int_eq(st.st_mtime, ro_files[i].mtime);
        
    }
 }
 END_TEST
 
/* Test for fs_getattr errors */
START_TEST(test_getattr_errors)
{
    struct stat st;
    int ret;
    ret = fs_ops.getattr("/invalid", &st);
    ck_assert_int_eq(ret, -ENOENT);
    ret = fs_ops.getattr("/file.1k/file.0", &st);
    ck_assert_int_eq(ret, -ENOTDIR);
    ret = fs_ops.getattr("/not-a-dir/file.0", &st);
    ck_assert_int_eq(ret, -ENOENT);
    ret = fs_ops.getattr("/dir2/invalid", &st);
    ck_assert_int_eq(ret, -ENOENT);
}
END_TEST
 
 /* Test for fs_readdir for the root and all directories */
 START_TEST(test_readdir_alldir) {
    char *root_entries[]               = { "dir2","dir3","dir-with-long-name","file.10","file.1k","file.8k+", NULL };
    char *dir2_entries[]               = { "twenty-seven-byte-file-name", "file.4k+", NULL };
    char *dir3_entries[]               = { "subdir","file.12k-", NULL };
    char *dir3_subdir_entries[]        = { "file.4k-","file.8k-","file.12k", NULL };
    char *dir_long_name_entries[]      = { "file.12k+", NULL }; 
    
    struct {
        const char *path;
        char      **expected;
    } dir_map[] = {
        { "/",                   root_entries          },
        { "/dir2",               dir2_entries          },
        { "/dir3",               dir3_entries          },
        { "/dir3/subdir",        dir3_subdir_entries   },
        { "/dir-with-long-name", dir_long_name_entries }
    };
 
    const int NDIRS = sizeof(dir_map)/sizeof(dir_map[0]);
    for (int d = 0; d < NDIRS; d++) {
        int cnt;
        for (cnt=0; dir_map[d].expected[cnt]; cnt++);   
        int seen[cnt]; memset(seen, 0, sizeof(seen));
        /* Filler callback: mark an entry as seen if it matches one in root_entries */
        int filler(void *ptr, const char *name, const struct stat *stbuf, off_t off) {
            char **exp = (char **)ptr;
            for (int i=0; exp[i]; i++) {
                if (strcmp(exp[i], name) == 0) { 
                    seen[i] = 1; break; 
                }
            }
            return 0;
        }
 
        int ret = fs_ops.readdir("/", root_entries, filler, 0, NULL);
        ck_assert_int_eq(ret, 0);
        for (int i = 0; root_entries[i] != NULL; i++) {
             ck_assert_int_eq(seen[i], 1);
        }
    }
}
 END_TEST

 /* Test for fs_readdir errors */
START_TEST(test_readdir_errors)
{
    int ret;
    int filler(void *ptr, const char *name, const struct stat *stbuf, off_t off) {
        char **expected = (char **)ptr;
        for (int i = 0; expected[i]; i++) {
            if (strcmp(expected[i], name) == 0) {
                return 0;
            }
        }
        return 1;
    }
    ret = fs_ops.readdir("/file.1k", NULL, filler, 0, NULL);
    ck_assert_int_eq(ret, -ENOTDIR);
    ret = fs_ops.readdir("/does-not-exist", NULL, filler, 0, NULL);
    ck_assert_int_eq(ret, -ENOENT);
}
END_TEST
 
 /* Test for fs_read for "/file.1k" */
 START_TEST(test_read_file_1k) {
     char buf[2000];
     int ret = fs_ops.read("/file.1k", buf, 2000, 0, NULL);
     ck_assert_int_eq(ret, 1000);  
 }
 END_TEST
 
 /* Test for fs_read */
 START_TEST(test_fs_read)
 {
     for (int i = 0; ro_files[i].path != NULL; i++) {
 
         if (ro_files[i].checksum == 0 || ro_files[i].size <= 0)
             continue;
 
         size_t ask   = ro_files[i].size + 100;
         char  *buf   = malloc(ask);
         ck_assert_ptr_ne(buf, NULL);
 
         int got = fs_ops.read(ro_files[i].path, buf, ask, 0, NULL);
         ck_assert_int_eq(got, ro_files[i].size);
 
         unsigned crc = crc32(0, (const Bytef *)buf, ro_files[i].size);
         ck_assert_uint_eq(crc, ro_files[i].checksum);
 
         free(buf);
     }
 }
 END_TEST

 /* Test for fs_read, multiple */
 START_TEST(test_fs_read_multiple)
 {
    const int chunks[] = { 17, 100, 1000, 1024, 1970, 3000 };
    const int NCHUNKS = sizeof(chunks)/sizeof(chunks[0]);
 
    for (int f = 0; ro_files[f].path != NULL; f++) {
 
        if (ro_files[f].checksum == 0 || ro_files[f].size <= 0) {
            continue;
        } 
        int fsize = ro_files[f].size;
        char *scratch = malloc(fsize);
        ck_assert_ptr_ne(scratch, NULL);

        for (int c = 0; c < NCHUNKS; c++) {

            int chunk   = chunks[c];
            int offset  = 0;
            int ret;

            memset(scratch, 0, fsize);

            while (offset < fsize) {
               ret = fs_ops.read(ro_files[f].path, scratch + offset, chunk, offset, NULL);
               ck_assert_int_gt(ret, 0);          
               offset += ret;
            }
            ck_assert_int_eq(offset, fsize);

            unsigned crc = crc32(0, (const Bytef *)scratch, fsize);
            ck_assert_uint_eq(crc, ro_files[f].checksum);
        }
        free(scratch);
    }
 }
 END_TEST

 /* Test for fs_read errors */
START_TEST(test_fs_read_errors)
{
    char buffer[100];
    int ret;

    ret = fs_ops.read("/does-not-exist", buffer, 100, 0, NULL);
    ck_assert_int_eq(ret, -ENOENT);

    ret = fs_ops.read("/dir3", buffer, 100, 0, NULL);
    ck_assert_int_eq(ret, -EISDIR);

    ret = fs_ops.read("/file.10", buffer, 100, 10, NULL);
    ck_assert_int_eq(ret, 0);
}
END_TEST

 /* Test for fs_statfs */
 START_TEST(test_statfs) {
     struct statvfs sv;
     int ret = fs_ops.statfs("/", &sv);
     ck_assert_int_eq(ret, 0);
     ck_assert_int_eq(sv.f_blocks, 400);
     ck_assert_int_eq(sv.f_bsize, 4096);
     ck_assert_int_eq(sv.f_bavail, 355);
     ck_assert_int_eq(sv.f_bfree, 355);
     ck_assert_int_eq(sv.f_namemax, 27);
 }
 END_TEST
 
 /* Test for fs_rename - rename "/file.10" to "/file.new" and verify */
 START_TEST(test_rename) {
     int ret = fs_ops.rename("/file.10", "/file.new");
     ck_assert_int_eq(ret, 0);
     
     struct stat st;
     ret = fs_ops.getattr("/file.10", &st);
     ck_assert_int_eq(ret, -ENOENT);
     
     ret = fs_ops.getattr("/file.new", &st);
     ck_assert_int_eq(ret, 0);
     ck_assert_int_eq(st.st_size, 10);
     ck_assert_int_eq(st.st_mode, 0100666);
     
     char *buf = malloc(10);
     ck_assert_ptr_ne(buf, NULL);
     ret = fs_ops.read("/file.new", buf, 10, 0, NULL);
     ck_assert_int_eq(ret, 10);
     unsigned ck = crc32(0, (const Bytef *)buf, 10);
     ck_assert_uint_eq(ck, 3766980606);
     free(buf);
     
     ret = fs_ops.rename("/file.new", "/file.10");
     ck_assert_int_eq(ret, 0);
 }
 END_TEST

 /* Test for fs_rename - rename "/dir3/subdir" to "/dir3/subdir_new" and then verify */
 START_TEST(test_rename_directory) {
    int ret = fs_ops.rename("/dir3/subdir", "/dir3/subdir_new");
    ck_assert_int_eq(ret, 0);
    
    struct stat st;
    ret = fs_ops.getattr("/dir3/subdir", &st);
    ck_assert_int_eq(ret, -ENOENT);
    
    ret = fs_ops.getattr("/dir3/subdir_new", &st);
    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(st.st_size, 4096);
    ck_assert_int_eq(st.st_mode, 040777);
    
    ret = fs_ops.getattr("/dir3/subdir_new/file.4k-", &st);
    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(st.st_size, 4095);
    ck_assert_int_eq(st.st_mode, 0100666);
    
    char *buf = malloc(4095);
    ck_assert_ptr_ne(buf, NULL);
    ret = fs_ops.read("/dir3/subdir_new/file.4k-", buf, 4095, 0, NULL);
    ck_assert_int_eq(ret, 4095);
    unsigned ck = crc32(0, (const Bytef *)buf, 4095);
    ck_assert_uint_eq(ck, 2991486384);
    free(buf);
}
END_TEST

/* Test for fs_rename errors */
START_TEST(test_fs_rename_errors)
{
    int ret = fs_ops.rename("/does-not-exist", "/does-not-exist_new");
    ck_assert_int_eq(ret, -ENOENT);

    ret = fs_ops.rename("/file.1k", "/file.8k+");
    ck_assert_int_eq(ret, -EEXIST);

    ret = fs_ops.rename("/file.10", "/dir2/file.10");
    ck_assert_int_eq(ret, -EINVAL);
}
END_TEST
 
 /* Test for fs_chmod - change permissions of "/file.1k" */
 START_TEST(test_chmod) {
     int ret = fs_ops.chmod("/file.1k", 0600);
     ck_assert_int_eq(ret, 0);
     struct stat st;
     ret = fs_ops.getattr("/file.1k", &st);
     ck_assert_int_eq(ret, 0);
     ck_assert_int_eq(st.st_mode & 0777, 0600);
 }
 END_TEST

 /* Test for fs_chmod for directory, change permissions to 0755 and verify */
START_TEST(test_fs_chmod_directory)
{
    int ret;
    ret = fs_ops.chmod("/dir3", 0755);
    ck_assert_int_eq(ret, 0);

    struct stat st;
    ret = fs_ops.getattr("/dir3", &st);
    ck_assert_int_eq(ret, 0);

    int expected = S_IFDIR | 0755;
    ck_assert_int_eq(st.st_mode, expected);
}
END_TEST

/* Test for fs_chmod errors */
START_TEST(test_fs_chmod_errors)
{
    int ret;
    ret = fs_ops.chmod("/does-not-exist", 0600);
    ck_assert_int_eq(ret, -ENOENT);
    
    ret = fs_ops.chmod("/file.1k/something", 0600);
    ck_assert_int_eq(ret, -ENOTDIR);
}
END_TEST
 
int main(int argc, char **argv) {
    Suite *s = suite_create("fs5600-ReadOnly");
    TCase *tc = tcase_create("read_mostly");
    tcase_add_checked_fixture(tc, test_setup, test_teardown);
    
    tcase_add_test(tc, test_getattr_root);
    tcase_add_test(tc, test_getattr_errors);
    tcase_add_test(tc, test_readdir_alldir);
    tcase_add_test(tc, test_readdir_errors);
    tcase_add_test(tc, test_read_file_1k);
    tcase_add_test(tc, test_fs_read);
    tcase_add_test(tc, test_fs_read_multiple);
    tcase_add_test(tc, test_fs_read_errors);
    tcase_add_test(tc, test_statfs);
    tcase_add_test(tc, test_rename);
    tcase_add_test(tc, test_rename_directory);
    tcase_add_test(tc, test_fs_rename_errors);
    tcase_add_test(tc, test_chmod);
    tcase_add_test(tc, test_fs_chmod_directory);
    tcase_add_test(tc, test_fs_chmod_errors);
    suite_add_tcase(s, tc);

    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_VERBOSE);
    int n_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (n_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
 